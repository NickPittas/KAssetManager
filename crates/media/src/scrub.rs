//! Hover scrubbing: frames of a video or image sequence at fractions of its length, produced by the
//! thumbnailer's pool at the lowest priority. A small LRU of opened sources keeps big containers
//! (MOV/MXF) from being reopened for every slot of one scrub gesture.

use std::collections::HashSet;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Mutex, PoisonError};

use gallerybrowser_core::{Entry, MediaKind};
use lightcraft_preview::{Hash128, Lru, PreviewCache};

use crate::thumbs::{ScrubSlot, ThumbResult, decode_scale, finish_frame, from_rgba8, to_rgba8};
use crate::{FrameRate, FrameRequest, MediaError, RgbaImage, SharedSource, Tick};

/// Opened sources kept for scrubbing (each may hold a demuxer index and decoder state).
const MAX_OPEN_SOURCES: usize = 3;
/// Most slots one scrub gesture may divide a clip into (bounds keys and caches).
pub(crate) const MAX_SLOTS: u32 = 1024;
/// Failed (entry, slot) keys remembered so a hovered, undecodable frame is not retried every frame.
const MAX_FAILED: usize = 4096;
/// Frame rate sequences are opened at: only frame indices matter, never real time.
const SEQUENCE_RATE: FrameRate = FrameRate::FPS_24;

/// Clamp a caller's (slot, slots) to a valid pair: `1 <= slots <= MAX_SLOTS`, `slot < slots`.
pub(crate) fn normalize(slot: u32, slots: u32) -> (u32, u32) {
    let slots = slots.clamp(1, MAX_SLOTS);
    (slot.min(slots.saturating_sub(1)), slots)
}

/// Frame index shown for `slot` of `slots` in a clip of `frames` frames: the centre of the slot's
/// span, so the first slot is near the start and the last never lands past the final frame.
pub(crate) fn scrub_index(slot: u32, slots: u32, frames: i64) -> i64 {
    let (slot, slots) = normalize(slot, slots);
    let frames = frames.max(1);
    let centre = i128::from(slot) * 2 + 1;
    let idx = centre.saturating_mul(i128::from(frames)) / (i128::from(slots) * 2);
    i64::try_from(idx).unwrap_or(i64::MAX).min(frames - 1)
}

/// Scrub-side state shared with the worker jobs: decoded-frame memory tier, open sources, failures.
pub(crate) struct Scrub {
    memo: Mutex<Lru<u128, Arc<RgbaImage>>>,
    sources: Mutex<Vec<(u128, SharedSource)>>,
    failed: Mutex<HashSet<u128>>,
}

impl Scrub {
    pub(crate) fn new(budget: usize) -> Self {
        Self {
            memo: Mutex::new(Lru::new(budget)),
            sources: Mutex::new(Vec::new()),
            failed: Mutex::new(HashSet::new()),
        }
    }

    pub(crate) fn get(&self, key: u128) -> Option<Arc<RgbaImage>> {
        self.memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .get(&key)
            .cloned()
    }

    fn insert(&self, key: u128, img: Arc<RgbaImage>) {
        let cost = img.pixels.len().saturating_add(64);
        self.memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .insert(key, img, cost);
    }

    pub(crate) fn set_budget(&self, budget: usize) {
        self.memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .set_budget(budget);
    }

    pub(crate) fn clear(&self) {
        self.memo
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clear();
        self.sources
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clear();
        self.failed
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clear();
    }

    pub(crate) fn is_failed(&self, key: u128) -> bool {
        self.failed
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .contains(&key)
    }

    fn mark_failed(&self, key: u128) {
        let mut failed = self.failed.lock().unwrap_or_else(PoisonError::into_inner);
        if failed.len() >= MAX_FAILED {
            failed.clear();
        }
        failed.insert(key);
    }

    /// The opened source for `key` (most recently used kept), opening it outside the lock.
    fn source(&self, key: u128, entry: &Entry) -> crate::Result<SharedSource> {
        {
            let mut sources = self.sources.lock().unwrap_or_else(PoisonError::into_inner);
            if let Some(pos) = sources.iter().position(|(k, _)| *k == key) {
                let hit = sources.remove(pos);
                let src = hit.1.clone();
                sources.push(hit);
                return Ok(src);
            }
        }
        let src = open_source(entry)?;
        let mut sources = self.sources.lock().unwrap_or_else(PoisonError::into_inner);
        sources.retain(|(k, _)| *k != key);
        sources.push((key, src.clone()));
        if sources.len() > MAX_OPEN_SOURCES {
            sources.remove(0);
        }
        Ok(src)
    }
}

fn open_source(entry: &Entry) -> crate::Result<SharedSource> {
    match (&entry.kind, &entry.sequence) {
        (MediaKind::Sequence, Some(seq)) => crate::open_sequence(seq, SEQUENCE_RATE),
        (MediaKind::Video, _) => crate::open(&entry.path),
        _ => Err(MediaError::Unsupported(
            "only videos and image sequences can be scrubbed".into(),
        )),
    }
}

/// One frame of `entry` for `slot`/`slots`, scaled to fit `max_edge`.
fn render_scrub(
    scrub: &Scrub,
    source_key: u128,
    entry: &Entry,
    max_edge: u32,
    slot: u32,
    slots: u32,
) -> crate::Result<RgbaImage> {
    let max_edge = max_edge.clamp(16, 4096);
    let src = scrub.source(source_key, entry)?;
    let info = src.info();
    let video = info
        .video
        .as_ref()
        .ok_or_else(|| MediaError::Unsupported("no video stream to scrub".into()))?;
    let rate = video.frame_rate.sane();
    let frames = rate.frame_at(info.duration).max(1);
    let time = rate.tick_of(scrub_index(slot, slots, frames));
    let scale = decode_scale(video.width, video.height, max_edge);
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

pub(crate) struct ScrubJob {
    pub(crate) entry: Entry,
    pub(crate) max_edge: u32,
    pub(crate) slot: u32,
    pub(crate) slots: u32,
    pub(crate) hash: Hash128,
    pub(crate) source_key: u128,
    pub(crate) generation: u64,
    pub(crate) cache_generation: u64,
    pub(crate) cache: Arc<PreviewCache>,
    pub(crate) scrub: Arc<Scrub>,
    pub(crate) batch: Arc<AtomicU64>,
}

impl ScrubJob {
    pub(crate) fn run(self) -> ThumbResult {
        let image = if self.batch.load(Ordering::SeqCst) != self.generation {
            Err("cancelled".to_string())
        } else {
            match catch_unwind(AssertUnwindSafe(|| self.produce())) {
                Ok(r) => r.map_err(|e| e.to_string()),
                Err(p) => Err(format!(
                    "scrub frame generation panicked: {}",
                    crate::guard::panic_message(&*p)
                )),
            }
        };
        match &image {
            Ok(img) => self.scrub.insert(self.hash.0, img.clone()),
            Err(e) if e != "cancelled" => self.scrub.mark_failed(self.hash.0),
            Err(_) => {}
        }
        ThumbResult {
            path: self.entry.path.clone(),
            max_edge: self.max_edge,
            scrub: Some(ScrubSlot {
                slot: self.slot,
                slots: self.slots,
            }),
            image,
        }
    }

    fn produce(&self) -> crate::Result<Arc<RgbaImage>> {
        if let Some(img) = self.cache.get_at(self.cache_generation, self.hash) {
            return from_rgba8(&img).map(Arc::new);
        }
        let img = render_scrub(
            &self.scrub,
            self.source_key,
            &self.entry,
            self.max_edge,
            self.slot,
            self.slots,
        )?;
        if let Some(staged) = to_rgba8(&img) {
            self.cache
                .put_at(self.cache_generation, self.hash, Arc::new(staged));
        }
        Ok(Arc::new(img))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn index_spreads_slots_over_the_clip() {
        assert_eq!(scrub_index(0, 24, 240), 5);
        assert_eq!(scrub_index(23, 24, 240), 235);
        assert_eq!(scrub_index(99, 24, 240), 235, "slot is clamped");
        assert_eq!(scrub_index(0, 1, 1), 0);
        assert_eq!(scrub_index(5, 0, 0), 0, "hostile arguments stay in range");
        assert!((0..i64::MAX).contains(&scrub_index(1, 2, i64::MAX)));
        assert_eq!(scrub_index(0, 200, 3), 0);
        assert_eq!(scrub_index(199, 200, 3), 2);
    }

    #[test]
    fn normalize_bounds_slots() {
        assert_eq!(normalize(7, 0), (0, 1));
        assert_eq!(normalize(5000, 5000), (MAX_SLOTS - 1, MAX_SLOTS));
    }
}
