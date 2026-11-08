use std::io::Read;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, PoisonError};
use std::time::SystemTime;

use filmcraft_time::format_timecode_frames;
use gallerybrowser_core::{Entry, MediaKind};

use crate::{MediaInfo, Result, SourceKind};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct AudioStreamMeta {
    pub codec: String,
    pub sample_rate: u32,
    pub channels: u32,
    pub bits_per_sample: Option<u32>,
}

/// ComfyUI generation parameters parsed from PNG text chunks `prompt` / `workflow` (JSON).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ComfyMetadata {
    pub positive: Option<String>,
    pub negative: Option<String>,
    pub seed: Option<i64>,
    pub steps: Option<i64>,
    pub cfg: Option<f64>,
    pub sampler: Option<String>,
    pub scheduler: Option<String>,
    pub model: Option<String>,
    pub prompt_json: Option<String>,
    pub workflow_json: Option<String>,
}

/// Everything the info pane shows. Every field optional: a failed probe still yields path/kind and the
/// error text (never panics on missing size etc.).
#[derive(Clone, Debug, Default)]
pub struct MediaMetadata {
    pub path: PathBuf,
    pub kind: Option<MediaKind>,
    pub file_size: Option<u64>,
    pub modified: Option<SystemTime>,
    pub format: Option<String>,
    pub container: Option<String>,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub duration_secs: Option<f64>,
    pub frame_rate: Option<f64>,
    pub frame_count: Option<u64>,
    pub video_codec: Option<String>,
    pub pixel_format: Option<String>,
    pub bit_rate: Option<u64>,
    pub color: Option<String>,
    pub has_alpha: Option<bool>,
    pub timecode: Option<String>,
    pub audio: Vec<AudioStreamMeta>,
    /// Sequence: (first, last, count, missing count).
    pub sequence_range: Option<(i64, i64, usize, usize)>,
    /// Human-readable EXIF (label, value) pairs.
    pub exif: Vec<(String, String)>,
    /// Raw PNG tEXt/iTXt/zTXt pairs (excluding huge JSON shown via `comfy`).
    pub text_chunks: Vec<(String, String)>,
    pub comfy: Option<ComfyMetadata>,
    pub error: Option<String>,
}

/// Extensions whose files can carry EXIF/XMP that `lightcraft-meta` reads.
const EXIF_EXTENSIONS: &[&str] = &[
    "jpg", "jpeg", "jpe", "png", "webp", "tif", "tiff", "dng", "heic", "heif", "avif", "cr2",
    "cr3", "nef", "arw", "orf", "rw2", "raf",
];
/// Most bytes of an image read to look for EXIF.
const MAX_EXIF_BYTES: u64 = 64 << 20;

/// Blocking probe (call from a worker). Filesystem facts first, then the container/codec facts from
/// the decoder (`MediaInfo`), EXIF, PNG text and ComfyUI parameters. Never fails for lack of a size,
/// a decoder or a readable file: whatever could not be read is reported in `error` and the rest is
/// still returned.
pub fn probe(entry: &Entry) -> Result<MediaMetadata> {
    let mut m = MediaMetadata {
        path: entry.path.clone(),
        kind: Some(entry.kind),
        modified: entry.modified,
        ..Default::default()
    };
    if let Err(p) = catch_unwind(AssertUnwindSafe(|| fill(entry, &mut m))) {
        m.error.get_or_insert_with(|| {
            format!(
                "metadata probe failed: {}",
                crate::guard::panic_message(&*p)
            )
        });
    }
    Ok(m)
}

fn fill(entry: &Entry, m: &mut MediaMetadata) {
    let probe_path: PathBuf = match entry.sequence.as_deref() {
        Some(seq) => {
            m.file_size = Some(entry.size);
            let count = seq.frames.len();
            if let (Some(first), Some(last)) = (seq.first(), seq.last()) {
                let span = last.number.saturating_sub(first.number).saturating_add(1);
                let missing = usize::try_from(span)
                    .unwrap_or(usize::MAX)
                    .saturating_sub(count);
                m.sequence_range = Some((first.number, last.number, count, missing));
            }
            m.frame_count = Some(count as u64);
            m.container = Some("Image Sequence".into());
            match seq.first() {
                Some(f) => f.path.clone(),
                None => {
                    m.error = Some("image sequence has no frames".into());
                    return;
                }
            }
        }
        None => {
            match std::fs::metadata(&entry.path) {
                Ok(md) => {
                    m.file_size = Some(md.len());
                    m.modified = md.modified().ok().or(entry.modified);
                }
                Err(e) => {
                    m.error = Some(format!("cannot read file information: {e}"));
                    return;
                }
            }
            entry.path.clone()
        }
    };
    let ext = crate::open::ext_lower(&probe_path);
    if !ext.is_empty() {
        m.format = Some(ext.to_ascii_uppercase());
    }
    if entry.kind == MediaKind::Other {
        return;
    }

    match crate::open(&probe_path) {
        Ok(src) => fill_from_info(m, src.info()),
        Err(e) => {
            m.error.get_or_insert_with(|| e.to_string());
        }
    }

    if matches!(entry.kind, MediaKind::Image | MediaKind::Sequence) {
        if EXIF_EXTENSIONS.contains(&ext.as_str()) {
            match read_head(&probe_path, MAX_EXIF_BYTES) {
                Ok(bytes) => {
                    m.exif = lightcraft_meta::tags::file_tag_rows(&bytes)
                        .into_iter()
                        .map(|r| {
                            let label = if matches!(r.group, "TIFF" | "EXIF") {
                                r.name
                            } else {
                                format!("{} {}", r.group, r.name)
                            };
                            (label, r.value)
                        })
                        .collect();
                }
                Err(e) => log::debug!("exif read of {}: {e}", probe_path.display()),
            }
        }
        if ext == "png" {
            match crate::png_text::read_text_chunks(&probe_path) {
                Ok(chunks) => {
                    let find = |key: &str| {
                        chunks
                            .iter()
                            .find(|(k, _)| k == key)
                            .map(|(_, v)| v.as_str())
                    };
                    m.comfy = crate::comfy::parse(find("prompt"), find("workflow"));
                    m.text_chunks = chunks
                        .iter()
                        .filter(|(k, _)| m.comfy.is_none() || (k != "prompt" && k != "workflow"))
                        .cloned()
                        .collect();
                }
                Err(e) => log::debug!("png text read of {}: {e}", probe_path.display()),
            }
        }
    }
}

fn read_head(path: &Path, max: u64) -> std::io::Result<Vec<u8>> {
    let mut buf = Vec::new();
    std::fs::File::open(path)?.take(max).read_to_end(&mut buf)?;
    Ok(buf)
}

fn fill_from_info(m: &mut MediaMetadata, info: &MediaInfo) {
    if m.container.is_none() && !info.container.is_empty() {
        m.container = Some(info.container.clone());
    }
    let movie = matches!(info.kind, SourceKind::Movie);
    if let Some(v) = &info.video {
        m.width = Some(v.width);
        m.height = Some(v.height);
        m.video_codec = Some(v.codec.clone());
        m.pixel_format = (!v.pixel_format.is_empty()).then(|| v.pixel_format.clone());
        m.color = Some(format!(
            "{:?} / {:?} / {:?} / {:?} range",
            v.color.matrix, v.color.transfer, v.color.primaries, v.color.range
        ));
        m.has_alpha = Some(v.has_alpha);
        m.bit_rate = v.bitrate;
        if movie {
            let rate = v.frame_rate.sane();
            m.frame_rate = Some(rate.as_f64());
            if let Some(tc) = info.start_timecode {
                m.timecode = Some(format_timecode_frames(tc, rate, false));
            }
            let secs = info.duration.seconds();
            if secs.is_finite() && secs > 0.0 {
                let frames = (secs * rate.as_f64()).round();
                if frames.is_finite() && (1.0..9.0e15).contains(&frames) {
                    m.frame_count = Some(frames as u64);
                }
            }
        }
    }
    if matches!(info.kind, SourceKind::Movie | SourceKind::AudioOnly) {
        let secs = info.duration.seconds();
        if secs.is_finite() && secs > 0.0 {
            m.duration_secs = Some(secs);
            // overall bit rate when the container reports no stream rate
            if m.bit_rate.is_none()
                && secs >= 0.5
                && let Some(size) = m.file_size
            {
                m.bit_rate = Some(((size as f64) * 8.0 / secs) as u64);
            }
        }
    }
    m.audio = info
        .audio_streams
        .iter()
        .map(|a| AudioStreamMeta {
            codec: a.codec.clone(),
            sample_rate: a.sample_rate,
            channels: a.channels,
            bits_per_sample: a.bits_per_sample,
        })
        .collect();
}

struct Slot {
    generation: u64,
    entry: Entry,
}

struct Shared {
    /// The newest unstarted request.
    pending: Mutex<Option<Slot>>,
    wake: Condvar,
    /// Newest completed result (with the request generation it answers).
    done: Mutex<Option<(u64, MediaMetadata)>>,
    latest: AtomicU64,
    closed: AtomicBool,
}

/// Background metadata probing; latest request wins (stale results dropped by generation).
pub struct MetadataWorker {
    shared: Arc<Shared>,
    started: bool,
}

impl MetadataWorker {
    pub fn new() -> Self {
        let shared = Arc::new(Shared {
            pending: Mutex::new(None),
            wake: Condvar::new(),
            done: Mutex::new(None),
            latest: AtomicU64::new(0),
            closed: AtomicBool::new(false),
        });
        let worker = shared.clone();
        let started = match std::thread::Builder::new()
            .name("gb-metadata".into())
            .spawn(move || run(&worker))
        {
            Ok(_) => true,
            Err(e) => {
                log::error!("metadata worker could not start: {e}");
                false
            }
        };
        Self { shared, started }
    }

    /// Replace any queued or in-flight request with one for `entry`.
    pub fn request(&self, entry: &Entry) {
        let generation = self
            .shared
            .latest
            .fetch_add(1, Ordering::SeqCst)
            .wrapping_add(1);
        *self
            .shared
            .done
            .lock()
            .unwrap_or_else(PoisonError::into_inner) = None;
        if !self.started {
            let meta = MediaMetadata {
                path: entry.path.clone(),
                kind: Some(entry.kind),
                error: Some("metadata worker is unavailable".into()),
                ..Default::default()
            };
            *self
                .shared
                .done
                .lock()
                .unwrap_or_else(PoisonError::into_inner) = Some((generation, meta));
            return;
        }
        *self
            .shared
            .pending
            .lock()
            .unwrap_or_else(PoisonError::into_inner) = Some(Slot {
            generation,
            entry: entry.clone(),
        });
        self.shared.wake.notify_one();
    }

    /// Latest completed result for the latest request, if any.
    pub fn poll(&self) -> Option<MediaMetadata> {
        let mut done = self
            .shared
            .done
            .lock()
            .unwrap_or_else(PoisonError::into_inner);
        match done.take() {
            Some((generation, meta)) if generation == self.shared.latest.load(Ordering::SeqCst) => {
                Some(meta)
            }
            _ => None,
        }
    }
}

impl Drop for MetadataWorker {
    fn drop(&mut self) {
        // The thread may be inside a slow probe: let it end on its own rather than block the UI.
        self.shared.closed.store(true, Ordering::SeqCst);
        self.shared.wake.notify_all();
    }
}

fn run(shared: &Shared) {
    loop {
        let slot = {
            let mut pending = shared
                .pending
                .lock()
                .unwrap_or_else(PoisonError::into_inner);
            loop {
                if shared.closed.load(Ordering::SeqCst) {
                    return;
                }
                if let Some(s) = pending.take() {
                    break s;
                }
                pending = shared
                    .wake
                    .wait(pending)
                    .unwrap_or_else(PoisonError::into_inner);
            }
        };
        let meta = match catch_unwind(AssertUnwindSafe(|| probe(&slot.entry))) {
            Ok(Ok(m)) => m,
            Ok(Err(e)) => MediaMetadata {
                path: slot.entry.path.clone(),
                kind: Some(slot.entry.kind),
                error: Some(e.to_string()),
                ..Default::default()
            },
            Err(p) => MediaMetadata {
                path: slot.entry.path.clone(),
                kind: Some(slot.entry.kind),
                error: Some(format!(
                    "metadata probe failed: {}",
                    crate::guard::panic_message(&*p)
                )),
                ..Default::default()
            },
        };
        if slot.generation == shared.latest.load(Ordering::SeqCst) {
            *shared.done.lock().unwrap_or_else(PoisonError::into_inner) =
                Some((slot.generation, meta));
        }
    }
}

impl Default for MetadataWorker {
    fn default() -> Self {
        Self::new()
    }
}
