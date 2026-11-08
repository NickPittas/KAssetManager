//! Media access built on the ArtCraft pure-Rust decoders (no ffmpeg/GStreamer):
//! video (MP4/MOV, MXF, MKV/WebM, AVI, MPEG-TS/PS; H.264, HEVC, ProRes, DNxHD/HR, AV1, VP8/9, MPEG-2…)
//! through `filmcraft-codecs` with streaming readers (never read a whole video into RAM),
//! stills through `filmcraft-media`/`photocraft-codecs`, PSD/PSB through `photocraft-psd`,
//! EXIF through `lightcraft-meta`, thumbnails through `lightcraft-preview`.

mod comfy;
mod guard;
mod heif;
pub mod image;
pub mod metadata;
pub mod open;
mod png_text;
mod scrub;
mod sequence;
pub mod thumbs;

pub use filmcraft_frame::{AudioBuffer, VideoFrame};
pub use filmcraft_media::{
    FrameRequest, MediaInfo, MediaKind as SourceKind, MediaSource, SharedSource,
};
pub use filmcraft_time::{FrameRate, Tick};

pub use image::{RgbaImage, frame_to_rgba};
pub use metadata::{AudioStreamMeta, ComfyMetadata, MediaMetadata, MetadataWorker, probe};
pub use open::{open, open_sequence};
pub use thumbs::{ScrubSlot, ThumbResult, Thumbnailer, ThumbnailerConfig};

#[derive(Debug, Clone, thiserror::Error)]
pub enum MediaError {
    #[error("unsupported media: {0}")]
    Unsupported(String),
    #[error("I/O error: {0}")]
    Io(String),
    #[error("decode error: {0}")]
    Decode(String),
}

pub type Result<T> = std::result::Result<T, MediaError>;
