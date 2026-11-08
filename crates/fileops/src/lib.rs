//! File operations run on a dedicated worker thread (never the UI thread). Destinations are NEVER
//! silently overwritten: collisions follow [`ConflictPolicy`]. Every finished op reports exactly which
//! paths moved/were created/removed so the library can relink metadata.

mod fsops;
pub mod rename;
pub mod worker;

pub use rename::{
    CaseMode, InsertRemoveMode, NumberMode, NumberOrder, RenameItem, RenamePlan, RenameRule,
    RenameScope, RenameStatus, plan_renames,
};
pub use worker::{FileOpsWorker, OpEvent, OpOutcome};

use std::path::PathBuf;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum ConflictPolicy {
    /// Leave the existing destination alone and skip this source (reported in `errors`).
    Skip,
    /// Pick a free name: `name (1).ext`, `name (2).ext`, …
    #[default]
    KeepBoth,
}

#[derive(Clone, Debug)]
pub enum FileOp {
    /// Each source may be a file or directory (recursive).
    Copy {
        sources: Vec<PathBuf>,
        dest_dir: PathBuf,
        conflict: ConflictPolicy,
    },
    Move {
        sources: Vec<PathBuf>,
        dest_dir: PathBuf,
        conflict: ConflictPolicy,
    },
    /// Freedesktop trash (`trash` crate). Undo restores from trash when possible.
    Trash {
        paths: Vec<PathBuf>,
    },
    /// Permanent; not undoable.
    Delete {
        paths: Vec<PathBuf>,
    },
    /// Fails (no overwrite) if `to` exists.
    Rename {
        from: PathBuf,
        to: PathBuf,
    },
    /// Executed collision-safely (two-phase via temp names) so cyclic renames (1→2, 2→3) work.
    BatchRename {
        plan: RenamePlan,
    },
    NewFolder {
        parent: PathBuf,
        name: String,
    },
}

#[derive(Debug, Clone, thiserror::Error)]
pub enum OpError {
    #[error("{path}: {message}")]
    Path { path: PathBuf, message: String },
    #[error("{0}")]
    Other(String),
}
