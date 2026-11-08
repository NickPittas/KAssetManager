use std::collections::HashMap;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, PoisonError};
use std::time::UNIX_EPOCH;

use gallerybrowser_core::Entry;
use lightcraft_preview::{Hash128, Hasher128, JobPool, Lru, PreviewCache};
use lightcraft_raster::Rgba8;

use crate::image::{box_downscale, fit_size};
use crate::scrub::{Scrub, ScrubJob, normalize};
use crate::{FrameRequest, MediaError, RgbaImage, Tick, VideoFrame, frame_to_rgba};

/// Bytes of the `PreviewCache`'s own memory tier. Decoded thumbnails live in the thumbnailer's
/// `RgbaImage` LRU (what the UI consumes), so this tier only needs to stage disk reads and writes.
const STAGING_BYTES: usize = 4 << 20;
/// Bump when the rendering recipe changes, so old disk entries are not reused.
const RECIPE_VERSION: u64 = 1;
const MAX_THREADS: usize = 32;
/// Neutral background transparent thumbnails are flattened onto (the disk cache stores JPEG).
const FLATTEN_RGB: [u8; 3] = [0x30, 0x30, 0x30];

#[derive(Clone, Debug)]
pub struct ThumbnailerConfig {
    /// `AppDirs::thumbnails_dir()`
    pub cache_dir: PathBuf,
    pub memory_bytes: u64,
    /// From `Settings::max_cache_mb`.
    pub disk_bytes: u64,
    /// From `Settings::thumbnail_threads`.
    pub threads: usize,
}

/// Which frame of a clip a scrub result is: `slot` of `slots` equal spans of its length.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ScrubSlot {
    pub slot: u32,
    pub slots: u32,
}

#[derive(Clone, Debug)]
pub struct ThumbResult {
    /// `Entry::path` the request was made for.
    pub path: PathBuf,
    /// Requested max edge.
    pub max_edge: u32,
    /// `Some` for a hover-scrub frame (`Thumbnailer::request_scrub`), `None` for a thumbnail.
    pub scrub: Option<ScrubSlot>,
    pub image: std::result::Result<Arc<RgbaImage>, String>,
}

type Memo = Arc<Mutex<Lru<u128, Arc<RgbaImage>>>>;

struct Inner {
    cfg: ThumbnailerConfig,
    cache: Arc<PreviewCache>,
    pool: JobPool<u128, ThumbResult>,
    /// Slot -> batch generation of the job queued or running for it.
    active: HashMap<u128, u64>,
    /// Results known at request time (memory hits), delivered by the next `poll`.
    ready: Vec<ThumbResult>,
    /// Scrub lane (entry + size) -> (slot, batch generation) last asked of the pool: a repeated
    /// request for it is a no-op, a different slot replaces the queued job of the lane.
    scrub_want: HashMap<u128, (u32, u64)>,
}

/// Background thumbnail service: memory LRU + disk cache keyed by (path, size, mtime, max_edge).
/// Videos: frame at 10% of duration (clamped to [0, 5s]); sequences: representative frame.
/// Cancellation is generation-based: `begin_batch` invalidates queued work (no stop/restart races).
/// Transparent images are flattened onto a neutral grey: the disk cache is JPEG, and the memory and
/// disk tiers must show the same picture.
pub struct Thumbnailer {
    inner: Mutex<Inner>,
    memo: Memo,
    batch: Arc<AtomicU64>,
    scrub: Arc<Scrub>,
}

fn clamp_threads(n: usize) -> usize {
    n.clamp(1, MAX_THREADS)
}

fn memo_budget(cfg: &ThumbnailerConfig) -> usize {
    usize::try_from(cfg.memory_bytes).unwrap_or(usize::MAX)
}

/// Scrub frames get their own memory tier so a long scrub cannot evict regular thumbnails.
fn scrub_budget(cfg: &ThumbnailerConfig) -> usize {
    memo_budget(cfg) / 4
}

fn make_cache(cfg: &ThumbnailerConfig) -> Arc<PreviewCache> {
    Arc::new(PreviewCache::with_disk(
        STAGING_BYTES,
        &cfg.cache_dir,
        cfg.disk_bytes,
    ))
}

impl Thumbnailer {
    pub fn new(cfg: ThumbnailerConfig) -> Self {
        let memo: Memo = Arc::new(Mutex::new(Lru::new(memo_budget(&cfg))));
        let scrub = Arc::new(Scrub::new(scrub_budget(&cfg)));
        let inner = Inner {
            cache: make_cache(&cfg),
            pool: JobPool::new(clamp_threads(cfg.threads)),
            active: HashMap::new(),
            ready: Vec::new(),
            scrub_want: HashMap::new(),
            cfg,
        };
        Self {
            inner: Mutex::new(inner),
            memo,
            batch: Arc::new(AtomicU64::new(0)),
            scrub,
        }
    }

    fn lock(&self) -> std::sync::MutexGuard<'_, Inner> {
        self.inner.lock().unwrap_or_else(PoisonError::into_inner)
    }

    /// Apply new budgets/threads at runtime (settings dialog).
    pub fn reconfigure(&self, cfg: ThumbnailerConfig) {
        let mut inner = self.lock();
        self.memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .set_budget(memo_budget(&cfg));
        self.scrub.set_budget(scrub_budget(&cfg));
        if cfg.cache_dir != inner.cfg.cache_dir {
            let old = std::mem::replace(&mut inner.cache, make_cache(&cfg));
            old.retire();
        } else if let Some(disk) = inner.cache.disk() {
            disk.set_budget(cfg.disk_bytes);
        }
        if clamp_threads(cfg.threads) != clamp_threads(inner.cfg.threads) {
            // Queued work of the old pool is discarded with it; running jobs finish into the void.
            self.batch.fetch_add(1, Ordering::SeqCst);
            inner.active.clear();
            inner.ready.clear();
            inner.scrub_want.clear();
            let old = std::mem::replace(&mut inner.pool, JobPool::new(clamp_threads(cfg.threads)));
            // Dropping a pool waits (bounded) for running jobs: never on the caller's thread.
            let spawn = std::thread::Builder::new()
                .name("gb-thumb-retire".into())
                .spawn(move || drop(old));
            if let Err(e) = spawn {
                log::warn!("could not retire the old thumbnail pool in the background: {e}");
            }
        }
        inner.cfg = cfg;
    }

    /// Drop all queued (not yet started) requests; results of running work for earlier batches are
    /// discarded (their pictures still reach the cache).
    pub fn begin_batch(&self) {
        let mut inner = self.lock();
        self.batch.fetch_add(1, Ordering::SeqCst);
        for slot in inner.pool.reprioritize(|_, _| None) {
            inner.active.remove(&slot);
        }
        inner.ready.clear();
        inner.scrub_want.clear();
    }

    /// Memory-cache hit, synchronous and cheap.
    pub fn cached(&self, entry: &Entry, max_edge: u32) -> Option<Arc<RgbaImage>> {
        if entry.kind == gallerybrowser_core::MediaKind::Other {
            return None;
        }
        let key = cache_key(entry, max_edge).0;
        self.memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .get(&key)
            .cloned()
    }

    /// Queue generation (deduplicated). Higher priority first (visible items).
    pub fn request(&self, entry: &Entry, max_edge: u32, priority: i32) {
        if entry.kind == gallerybrowser_core::MediaKind::Other {
            self.lock().ready.push(ThumbResult {
                path: entry.path.clone(),
                max_edge,
                scrub: None,
                image: Err("no thumbnail".to_string()),
            });
            return;
        }
        let hash = cache_key(entry, max_edge);
        let slot = hash.0;
        let hit = self
            .memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .get(&slot)
            .cloned();
        let mut inner = self.lock();
        if let Some(img) = hit {
            inner.ready.push(ThumbResult {
                path: entry.path.clone(),
                max_edge,
                scrub: None,
                image: Ok(img),
            });
            return;
        }
        let generation = self.batch.load(Ordering::SeqCst);
        let prio = u32::try_from(i64::from(priority) - i64::from(i32::MIN)).unwrap_or(0);
        if inner.active.get(&slot) == Some(&generation) {
            // already queued or running in this batch: only raise/lower its priority
            inner
                .pool
                .reprioritize(|s, p| Some(if *s == slot { prio } else { p }));
            return;
        }
        inner.active.insert(slot, generation);
        let job = Job {
            entry: entry.clone(),
            max_edge,
            hash,
            generation,
            cache_generation: inner.cache.generation(),
            cache: inner.cache.clone(),
            memo: self.memo.clone(),
            batch: self.batch.clone(),
        };
        inner
            .pool
            .submit(slot, generation, prio, Box::new(move || job.run()));
    }

    /// Memory-cache hit for a scrub frame (`slot` of `slots` of the entry's length), synchronous.
    pub fn cached_scrub(
        &self,
        entry: &Entry,
        max_edge: u32,
        slot: u32,
        slots: u32,
    ) -> Option<Arc<RgbaImage>> {
        if !scrubbable(entry) {
            return None;
        }
        let (slot, slots) = normalize(slot, slots);
        self.scrub.get(scrub_key(entry, max_edge, slot, slots).0)
    }

    /// Queue the frame at the centre of `slot` out of `slots` equal spans of a video or image
    /// sequence, at the lowest priority (below every thumbnail). Only the newest not-yet-started
    /// request per (entry, size) is kept, so dragging across a card never builds a backlog; frames
    /// come back through `poll` with `ThumbResult::slot` set. Other kinds, and frames that failed to
    /// decode before, are ignored.
    pub fn request_scrub(&self, entry: &Entry, max_edge: u32, slot: u32, slots: u32) {
        if !scrubbable(entry) {
            return;
        }
        let (slot, slots) = normalize(slot, slots);
        let hash = scrub_key(entry, max_edge, slot, slots);
        if let Some(img) = self.scrub.get(hash.0) {
            self.lock().ready.push(ThumbResult {
                path: entry.path.clone(),
                max_edge,
                scrub: Some(ScrubSlot { slot, slots }),
                image: Ok(img),
            });
            return;
        }
        if self.scrub.is_failed(hash.0) {
            return;
        }
        let lane = scrub_lane(entry, max_edge).0;
        let mut inner = self.lock();
        let generation = self.batch.load(Ordering::SeqCst);
        if inner.scrub_want.get(&lane) == Some(&(slot, generation)) {
            return;
        }
        inner.scrub_want.insert(lane, (slot, generation));
        let job = ScrubJob {
            entry: entry.clone(),
            max_edge,
            slot,
            slots,
            hash,
            source_key: source_key(entry),
            generation,
            cache_generation: inner.cache.generation(),
            cache: inner.cache.clone(),
            scrub: self.scrub.clone(),
            batch: self.batch.clone(),
        };
        // Same pool slot for every frame of the lane: serialises decoding on its open source and
        // lets the newest request replace a queued older one. Priority 0 = below all thumbnails.
        inner
            .pool
            .submit(lane, generation, 0, Box::new(move || job.run()));
    }

    pub fn poll(&self) -> Vec<ThumbResult> {
        let mut inner = self.lock();
        let mut out = std::mem::take(&mut inner.ready);
        let current = self.batch.load(Ordering::SeqCst);
        while let Some(done) = inner.pool.try_recv() {
            if inner.active.get(&done.slot) == Some(&done.key) {
                inner.active.remove(&done.slot);
            }
            if let Some(ScrubSlot { slot, .. }) = done.result.scrub
                && inner.scrub_want.get(&done.slot) == Some(&(slot, done.key))
            {
                inner.scrub_want.remove(&done.slot);
            }
            if done.key == current {
                out.push(done.result);
            }
        }
        out
    }

    /// Delete the disk cache.
    pub fn clear_disk_cache(&self) -> crate::Result<()> {
        let inner = self.lock();
        inner.cache.clear();
        self.memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clear();
        self.scrub.clear();
        Ok(())
    }
}

struct Job {
    entry: Entry,
    max_edge: u32,
    hash: Hash128,
    generation: u64,
    cache_generation: u64,
    cache: Arc<PreviewCache>,
    memo: Memo,
    batch: Arc<AtomicU64>,
}

impl Job {
    fn run(self) -> ThumbResult {
        let path = self.entry.path.clone();
        let max_edge = self.max_edge;
        let image = if self.batch.load(Ordering::SeqCst) != self.generation {
            Err("cancelled".to_string())
        } else {
            match catch_unwind(AssertUnwindSafe(|| self.produce())) {
                Ok(r) => r.map_err(|e| e.to_string()),
                Err(p) => Err(format!(
                    "thumbnail generation panicked: {}",
                    crate::guard::panic_message(&*p)
                )),
            }
        };
        if let Ok(img) = &image {
            let cost = img.pixels.len().saturating_add(64);
            self.memo
                .lock()
                .unwrap_or_else(PoisonError::into_inner)
                .insert(self.hash.0, img.clone(), cost);
        }
        ThumbResult {
            path,
            max_edge,
            scrub: None,
            image,
        }
    }

    fn produce(&self) -> crate::Result<Arc<RgbaImage>> {
        if let Some(img) = self.cache.get_at(self.cache_generation, self.hash) {
            return from_rgba8(&img).map(Arc::new);
        }
        let img = render(&self.entry, self.max_edge)?;
        if let Some(staged) = to_rgba8(&img) {
            self.cache
                .put_at(self.cache_generation, self.hash, Arc::new(staged));
        }
        Ok(Arc::new(img))
    }
}

/// Identity of the file the cached pictures were made from.
fn identity(entry: &Entry) -> Hasher128 {
    let mtime = entry
        .modified
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map_or(0, |d| u64::try_from(d.as_nanos()).unwrap_or(u64::MAX));
    let mut h = Hasher128::new();
    h.u64(RECIPE_VERSION)
        .str(&entry.path.to_string_lossy())
        .u64(entry.size)
        .u64(mtime);
    h
}

fn cache_key(entry: &Entry, max_edge: u32) -> Hash128 {
    identity(entry).u64(u64::from(max_edge)).finish()
}

/// Key of one scrub frame (memory and disk tiers).
fn scrub_key(entry: &Entry, max_edge: u32, slot: u32, slots: u32) -> Hash128 {
    identity(entry)
        .str("scrub")
        .u64(u64::from(max_edge))
        .u64(u64::from(slots))
        .u64(u64::from(slot))
        .finish()
}

/// Pool slot shared by every scrub frame of one entry at one size.
fn scrub_lane(entry: &Entry, max_edge: u32) -> Hash128 {
    identity(entry)
        .str("scrub-lane")
        .u64(u64::from(max_edge))
        .finish()
}

/// Key of the opened source an entry's scrub frames share.
fn source_key(entry: &Entry) -> u128 {
    identity(entry).str("scrub-source").finish().0
}

fn scrubbable(entry: &Entry) -> bool {
    match entry.kind {
        gallerybrowser_core::MediaKind::Video => true,
        gallerybrowser_core::MediaKind::Sequence => entry.sequence.is_some(),
        _ => false,
    }
}

pub(crate) fn from_rgba8(img: &Rgba8) -> crate::Result<RgbaImage> {
    let (w, h) = (u32::try_from(img.width), u32::try_from(img.height));
    let (Ok(width), Ok(height)) = (w, h) else {
        return Err(MediaError::Decode("cached thumbnail too large".into()));
    };
    if width == 0 || height == 0 || img.data.len() != img.width.saturating_mul(img.height) {
        return Err(MediaError::Decode(
            "cached thumbnail is inconsistent".into(),
        ));
    }
    let mut pixels = Vec::with_capacity(img.data.len() * 4);
    for p in &img.data {
        pixels.extend_from_slice(p);
    }
    Ok(RgbaImage {
        width,
        height,
        pixels: Arc::new(pixels),
    })
}

pub(crate) fn to_rgba8(img: &RgbaImage) -> Option<Rgba8> {
    let data: Vec<[u8; 4]> = img.pixels.as_chunks::<4>().0.to_vec();
    let expected = (img.width as usize).checked_mul(img.height as usize)?;
    (data.len() == expected).then_some(Rgba8 {
        width: img.width as usize,
        height: img.height as usize,
        data,
    })
}

/// Composite straight-alpha RGBA over the neutral background, in place.
fn flatten_alpha(px: &mut [u8]) {
    for p in px.as_chunks_mut::<4>().0 {
        let a = u32::from(p[3]);
        if a == 255 {
            continue;
        }
        for (c, bg) in p.iter_mut().zip(FLATTEN_RGB) {
            *c = ((u32::from(*c) * a + u32::from(bg) * (255 - a) + 127) / 255) as u8;
        }
        p[3] = 255;
    }
}

/// The picture a thumbnail shows: still/sequence representative frame, or a video frame at 10% of
/// its duration (at most 5 s in), requested at reduced scale so decoders can skip work.
fn render(entry: &Entry, max_edge: u32) -> crate::Result<RgbaImage> {
    let max_edge = max_edge.clamp(16, 4096);
    let path = match &entry.sequence {
        Some(seq) => seq
            .representative()
            .map(|f| f.path.clone())
            .ok_or_else(|| MediaError::Unsupported("image sequence has no frames".into()))?,
        None => entry.path.clone(),
    };
    let src = crate::open(&path)?;
    let info = src.info();
    let video = info
        .video
        .as_ref()
        .ok_or_else(|| MediaError::Unsupported("no video stream to preview".into()))?;
    let scale = decode_scale(video.width, video.height, max_edge);
    let time = if matches!(info.kind, crate::SourceKind::Movie) {
        let secs = (info.duration.seconds() * 0.1).clamp(0.0, 5.0);
        let secs = if secs.is_finite() { secs } else { 0.0 };
        video.frame_rate.sane().snap(Tick::from_seconds_f64(secs))
    } else {
        Tick::ZERO
    };
    let frame = match src.video_frame(FrameRequest { time, scale }) {
        Ok(f) => f,
        Err(_) if time != Tick::ZERO => src
            .video_frame(FrameRequest {
                time: Tick::ZERO,
                scale,
            })
            .map_err(crate::guard::convert)?,
        Err(e) => return Err(crate::guard::convert(e)),
    };
    finish_frame(&frame, max_edge)
}

/// Decoder scale that brings a `w`x`h` picture near `max_edge` (never upscales).
pub(crate) fn decode_scale(w: u32, h: u32, max_edge: u32) -> f32 {
    (max_edge as f32 / w.max(h).max(1) as f32).clamp(0.02, 1.0)
}

/// Decoded frame -> RGBA picture fitting `max_edge`, flattened onto the neutral background.
pub(crate) fn finish_frame(frame: &VideoFrame, max_edge: u32) -> crate::Result<RgbaImage> {
    let full = frame_to_rgba(frame)?;
    let (dw, dh) = fit_size(full.width, full.height, max_edge);
    let mut pixels = if (dw, dh) == (full.width, full.height) {
        full.pixels.as_ref().clone()
    } else {
        box_downscale(&full.pixels, full.width, full.height, dw, dh)?
    };
    flatten_alpha(&mut pixels);
    Ok(RgbaImage {
        width: dw,
        height: dh,
        pixels: Arc::new(pixels),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn flatten_blends_over_grey() {
        let mut px = vec![255, 0, 0, 255, 255, 0, 0, 0, 255, 0, 0, 128];
        flatten_alpha(&mut px);
        assert_eq!(&px[0..4], &[255, 0, 0, 255]);
        assert_eq!(&px[4..8], &[0x30, 0x30, 0x30, 255]);
        assert_eq!(px[11], 255);
        assert!(px[8] > 0x30 && px[8] < 255);
    }

    #[test]
    fn key_changes_with_size_mtime_and_edge() {
        let mut e = Entry {
            path: "/a/b.png".into(),
            name: "b.png".into(),
            kind: gallerybrowser_core::MediaKind::Image,
            size: 10,
            modified: None,
            sequence: None,
        };
        let k = cache_key(&e, 128);
        assert_eq!(k, cache_key(&e, 128));
        assert_ne!(k, cache_key(&e, 256));
        e.size = 11;
        assert_ne!(k, cache_key(&e, 128));
        e.modified = Some(UNIX_EPOCH + std::time::Duration::from_secs(5));
        assert_ne!(cache_key(&e, 128), k);
    }
}
