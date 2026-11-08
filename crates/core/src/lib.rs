//! GalleryBrowser filesystem domain. UI-free (layer L0): nothing here touches egui.
//!
//! Identity rule: every browsable item is an [`Entry`] whose `path` is its stable identity.
//! For image sequences the identity is the *pattern path* (`dir/shot_####.exr`), never a frame;
//! operations on an entry act on [`Entry::member_paths`] (every frame of a sequence).

pub mod dirs;
pub mod entry;
pub mod kind;
pub mod natural;
pub mod scan;
pub mod sequence;
pub mod settings;

pub use dirs::AppDirs;
pub use entry::Entry;
pub use kind::{AUDIO_EXTENSIONS, IMAGE_EXTENSIONS, MediaKind, VIDEO_EXTENSIONS, classify};
pub use scan::{ScanOptions, SortKey, is_hidden, list_subfolders, scan_folder, sort_entries};
pub use sequence::{Sequence, SequenceFrame, detect_sequences};
pub use settings::{FilterScope, Settings, Theme, ViewMode};

#[derive(Debug, thiserror::Error)]
pub enum CoreError {
    #[error("I/O error at {path}: {source}")]
    Io {
        path: std::path::PathBuf,
        #[source]
        source: std::io::Error,
    },
    #[error("settings error: {0}")]
    Settings(String),
    #[error("no writable directory for {0}")]
    NoWritableDir(&'static str),
}

pub type Result<T> = std::result::Result<T, CoreError>;
