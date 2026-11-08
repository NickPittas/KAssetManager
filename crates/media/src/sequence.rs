//! Image sequences as a movie source whose frames go through the same decoder chain as
//! [`crate::open`]: the filmcraft still decoders first (OpenEXR/HDR/DNG as linear floats, everything
//! else as RGBA8), then photocraft-codecs for what filmcraft refuses (multi-part / multi-layer
//! OpenEXR from Blender, exotic TIFF, …).
//!
//! Frame decoding mirrors `filmcraft_media::sequence::ImageSequenceSource` (lazy decoding, bounded
//! frame cache, size/alpha/timing from the first frame); the difference is the per-frame fallback.

use std::path::{Path, PathBuf};
use std::sync::Arc;

use filmcraft_frame::{AudioBuffer, VideoFrame};
use filmcraft_media::still;
use filmcraft_media::{
    FrameCache, FrameRequest, MediaError as FError, MediaInfo, MediaKind, MediaSource,
    VideoStreamInfo,
};
use filmcraft_time::{FrameRate, Tick};

use crate::guard::{self, Guarded};
use crate::open::{ext_lower, file_name, heif_rgba, photocraft_rgba, read_capped};
use crate::{MediaError, Result, SharedSource};

/// Decoded-frame cache budget (same as filmcraft's `ImageSequenceSource`).
const CACHE_BYTES: usize = 256 << 20;

/// One decoded frame plus what the media info needs from the first one.
struct Decoded {
    frame: VideoFrame,
    has_alpha: bool,
    pixel_format: String,
    /// DNG sequences carry their own frame rate / start timecode.
    timing: Option<(FrameRate, Option<i64>)>,
}

/// Decode `bytes` with the filmcraft still decoders (same dispatch as filmcraft's sequence source).
fn decode_filmcraft(name: &str, bytes: &[u8], rate: FrameRate) -> filmcraft_media::Result<Decoded> {
    if still::exr::sniff(bytes) {
        let d = still::exr::decode(name, bytes)?;
        return Ok(Decoded {
            frame: d.frame,
            has_alpha: d.has_alpha,
            pixel_format: d.pixel_format,
            timing: None,
        });
    }
    if still::hdr::sniff(bytes) {
        let frame = still::hdr::decode(name, bytes)?;
        return Ok(Decoded {
            frame,
            has_alpha: false,
            pixel_format: "RGB 32-bit float (RGBE)".to_string(),
            timing: None,
        });
    }
    if still::dng::sniff(bytes) {
        let d = still::dng::decode(name, bytes)?;
        return Ok(Decoded {
            frame: d.frame,
            has_alpha: false,
            pixel_format: d.pixel_format,
            timing: Some(still::dng::sequence_timing(&d.info, rate)),
        });
    }
    let img = still::decode_raster(name, bytes)?;
    let pixel_format = if img.has_alpha {
        "RGBA 8-bit"
    } else {
        "RGB 8-bit"
    };
    Ok(Decoded {
        frame: VideoFrame::rgba8(img.width, img.height, img.rgba),
        has_alpha: img.has_alpha,
        pixel_format: pixel_format.to_string(),
        timing: None,
    })
}

/// Decode one frame file: filmcraft first, then photocraft-codecs (8-bit sRGB RGBA).
fn decode_file(path: &Path, rate: FrameRate) -> Result<Decoded> {
    let name = file_name(path);
    let bytes = read_capped(path)?;
    match guard::catch(|| decode_filmcraft(&name, &bytes, rate).map_err(guard::convert)) {
        Ok(d) => Ok(d),
        Err(first) => {
            let decoded = if crate::heif::sniff(&bytes) {
                heif_rgba(&name, &bytes)
            } else {
                photocraft_rgba(&name, &bytes)
            };
            let (w, h, rgba, has_alpha) = decoded.map_err(|_| first)?;
            Ok(Decoded {
                frame: VideoFrame::rgba8(w, h, rgba),
                has_alpha,
                pixel_format: if has_alpha { "RGBA 8-bit" } else { "RGB 8-bit" }.to_string(),
                timing: None,
            })
        }
    }
}

pub(crate) struct SequenceSource {
    info: MediaInfo,
    frames: Vec<PathBuf>,
    cache: FrameCache<usize>,
}

impl SequenceSource {
    /// Decodes the first frame now (size, alpha, timing); the rest on demand.
    pub(crate) fn open(name: &str, frames: Vec<PathBuf>, rate: FrameRate) -> Result<Self> {
        let first = frames
            .first()
            .ok_or_else(|| MediaError::Unsupported("image sequence has no frames".into()))?;
        let d = decode_file(first, rate)?;
        let (rate, start_timecode) = d.timing.unwrap_or((rate, None));
        let frame = d.frame;
        let ext = ext_lower(first).to_ascii_uppercase();
        let ext = if ext.is_empty() {
            "IMAGE".to_string()
        } else {
            ext
        };
        let info = MediaInfo {
            name: name.to_string(),
            kind: MediaKind::ImageSequence,
            duration: rate.tick_of(i64::try_from(frames.len()).unwrap_or(i64::MAX)),
            video: Some(VideoStreamInfo {
                width: frame.width,
                height: frame.height,
                frame_rate: rate,
                par: (1, 1),
                codec: format!("{ext} sequence"),
                pixel_format: d.pixel_format,
                color: frame.color,
                has_alpha: d.has_alpha,
                bitrate: None,
                hdr: None,
            }),
            audio_streams: Vec::new(),
            container: "Image Sequence".into(),
            start_timecode,
            file_size: None,
        };
        let cache = FrameCache::new(CACHE_BYTES);
        cache.insert(0, Arc::new(frame));
        Ok(Self {
            info,
            frames,
            cache,
        })
    }

    fn decode(&self, i: usize) -> filmcraft_media::Result<Arc<VideoFrame>> {
        if let Some(f) = self.cache.get(&i) {
            return Ok(f);
        }
        let path = self.frames.get(i).ok_or(FError::NoStream("video"))?;
        let d = decode_file(path, self.info.frame_rate()).map_err(|e| match e {
            MediaError::Unsupported(s) => FError::Unsupported(s),
            MediaError::Io(s) => FError::Io(s),
            MediaError::Decode(s) => FError::Decode(s),
        })?;
        let f = Arc::new(d.frame);
        self.cache.insert(i, f.clone());
        Ok(f)
    }
}

impl MediaSource for SequenceSource {
    fn info(&self) -> &MediaInfo {
        &self.info
    }

    fn video_frame(&self, req: FrameRequest) -> filmcraft_media::Result<Arc<VideoFrame>> {
        let last = self.frames.len().saturating_sub(1);
        let want = self.info.frame_rate().frame_at(req.time.max(Tick::ZERO));
        let want = usize::try_from(want.max(0)).unwrap_or(usize::MAX).min(last);
        self.decode(want)
    }

    fn audio(
        &self,
        _start: i64,
        _frames: usize,
        _sample_rate: u32,
    ) -> filmcraft_media::Result<AudioBuffer> {
        Err(FError::NoStream("audio"))
    }
}

/// Open `frames` as a guarded movie source named `name`.
pub(crate) fn open_frames(
    name: &str,
    frames: Vec<PathBuf>,
    rate: FrameRate,
) -> Result<SharedSource> {
    guard::catch(|| {
        let src = SequenceSource::open(name, frames, rate)?;
        Ok(Guarded::wrap(Arc::new(src)))
    })
}

#[cfg(test)]
mod tests {
    // Test-only module: assertions may panic by design.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

    use std::path::PathBuf;

    use filmcraft_media::MediaSource;
    use filmcraft_time::FrameRate;

    use super::SequenceSource;

    /// A real 1x1 PNG, written to disk and decoded back: the sequence source must open a run of ordinary image
    /// files the way the app asks it to (a listing folds them into one item, so this is what selecting that item
    /// opens). Written rather than generated here so the check does not depend on an encoder.
    const PNG_1X1: &[u8] = &[
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 1, 0, 0, 0, 1, 8, 2,
        0, 0, 0, 144, 119, 83, 222, 0, 0, 0, 12, 73, 68, 65, 84, 120, 218, 99, 248, 32, 39, 7, 0,
        3, 46, 1, 45, 0, 63, 131, 7, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130,
    ];

    fn fixture(dir: &std::path::Path, n: usize) -> Vec<PathBuf> {
        (1..=n)
            .map(|i| {
                let path = dir.join(format!("walk.{i:04}.png"));
                std::fs::write(&path, PNG_1X1).unwrap();
                path
            })
            .collect()
    }

    #[test]
    fn a_png_sequence_opens_as_a_source_at_the_requested_rate() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../build-rust-tmp/media-seq-open");
        std::fs::create_dir_all(&dir).unwrap();
        let frames = fixture(&dir, 3);

        let src = SequenceSource::open("walk_[0001-0003].png", frames.clone(), FrameRate::FPS_24)
            .expect("a PNG sequence should open");
        let info = src.info();
        assert_eq!(info.video.as_ref().unwrap().width, 1);
        assert_eq!(info.video.as_ref().unwrap().frame_rate, FrameRate::FPS_24);
        // Three frames at 24 fps is one eighth of a second - the run plays as one clip, not three stills.
        assert!(
            (info.duration.seconds() - 3.0 / 24.0).abs() < 1e-6,
            "duration {} s",
            info.duration.seconds()
        );

        for path in &frames {
            let _ = std::fs::remove_file(path);
        }
    }
}
