use std::path::PathBuf;
use std::sync::Arc;
use std::time::SystemTime;

use crate::{MediaKind, Sequence};

/// One browsable item (file or collapsed sequence).
#[derive(Clone, Debug)]
pub struct Entry {
    /// Stable identity (file path, or `Sequence::pattern_path` for sequences).
    pub path: PathBuf,
    /// Display name (file name or `Sequence::display_name`).
    pub name: String,
    pub kind: MediaKind,
    /// Bytes (sum over frames for sequences).
    pub size: u64,
    /// Newest mtime (over frames for sequences).
    pub modified: Option<SystemTime>,
    pub sequence: Option<Arc<Sequence>>,
}

impl Entry {
    /// Every file this entry consists of; file operations MUST act on these.
    pub fn member_paths(&self) -> Vec<PathBuf> {
        match &self.sequence {
            Some(s) => s.paths(),
            None => vec![self.path.clone()],
        }
    }
    /// File whose pixels represent the entry (thumbnail/still preview).
    pub fn representative_path(&self) -> PathBuf {
        self.sequence
            .as_ref()
            .and_then(|s| s.representative().map(|f| f.path.clone()))
            .unwrap_or_else(|| self.path.clone())
    }
}
