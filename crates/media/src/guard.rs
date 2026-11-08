//! Panic containment: decoders parse hostile files, and a bug in one must surface as an error the
//! UI can show, never as a dead thread (AGENTS.md rule 0.5).

use std::any::Any;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;

use filmcraft_frame::{AudioBuffer, VideoFrame};
use filmcraft_media::{FrameRequest, MediaInfo, MediaSource, SharedSource};

use crate::{MediaError, Result};

/// Message of a caught panic payload.
pub(crate) fn panic_message(payload: &(dyn Any + Send)) -> String {
    if let Some(s) = payload.downcast_ref::<&str>() {
        (*s).to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "unknown panic".to_string()
    }
}

/// Run `f`, turning a panic into [`MediaError::Decode`].
pub(crate) fn catch<T>(f: impl FnOnce() -> Result<T>) -> Result<T> {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => r,
        Err(p) => Err(MediaError::Decode(format!(
            "decoder panicked: {}",
            panic_message(&*p)
        ))),
    }
}

pub(crate) fn convert(e: filmcraft_media::MediaError) -> MediaError {
    use filmcraft_media::MediaError as F;
    match e {
        F::Unsupported(s) => MediaError::Unsupported(s),
        F::Io(s) | F::Offline(s) => MediaError::Io(s),
        other => MediaError::Decode(other.to_string()),
    }
}

/// A source whose decode calls cannot unwind into the caller.
pub(crate) struct Guarded(pub SharedSource);

impl Guarded {
    pub(crate) fn wrap(inner: SharedSource) -> SharedSource {
        Arc::new(Guarded(inner))
    }
}

fn caught<T>(f: impl FnOnce() -> filmcraft_media::Result<T>) -> filmcraft_media::Result<T> {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) => r,
        Err(p) => Err(filmcraft_media::MediaError::Decode(format!(
            "decoder panicked: {}",
            panic_message(&*p)
        ))),
    }
}

impl MediaSource for Guarded {
    fn info(&self) -> &MediaInfo {
        self.0.info()
    }
    fn video_frame(&self, req: FrameRequest) -> filmcraft_media::Result<Arc<VideoFrame>> {
        caught(|| self.0.video_frame(req))
    }
    fn audio(
        &self,
        start: i64,
        frames: usize,
        sample_rate: u32,
    ) -> filmcraft_media::Result<AudioBuffer> {
        caught(|| self.0.audio(start, frames, sample_rate))
    }
    fn audio_stream(
        &self,
        stream: usize,
        start: i64,
        frames: usize,
        sample_rate: u32,
    ) -> filmcraft_media::Result<AudioBuffer> {
        caught(|| self.0.audio_stream(stream, start, frames, sample_rate))
    }
}
