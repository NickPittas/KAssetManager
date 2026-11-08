//! Filesystem primitives: no-clobber copy/move (with cross-device fallback), conflict naming, two-phase
//! rename. Nothing here ever overwrites an existing destination.

use std::collections::HashSet;
use std::ffi::OsString;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::ConflictPolicy;

/// Hostile directory trees must not overflow the stack.
const MAX_DEPTH: usize = 128;
/// Upper bound on `name (N).ext` probing.
const MAX_KEEP_BOTH: u32 = 10_000;

#[derive(Debug)]
pub(crate) enum FsError {
    /// The destination already exists (nothing was written).
    Exists,
    Io(io::Error),
}

impl From<io::Error> for FsError {
    fn from(e: io::Error) -> Self {
        FsError::Io(e)
    }
}

impl FsError {
    pub(crate) fn message(&self, dest: &Path) -> String {
        match self {
            FsError::Exists => format!("destination already exists: {}", dest.display()),
            FsError::Io(e) => e.to_string(),
        }
    }
}

pub(crate) fn exists_nofollow(p: &Path) -> bool {
    fs::symlink_metadata(p).is_ok()
}

fn is_cross_device(e: &io::Error) -> bool {
    e.kind() == io::ErrorKind::CrossesDevices
}

/// `name`, `name (1).ext`, `name (2).ext`, … (`n == 0` is the plain name).
pub(crate) fn numbered_name(name: &std::ffi::OsStr, n: u32) -> OsString {
    if n == 0 {
        return name.to_os_string();
    }
    let p = Path::new(name);
    let stem = p.file_stem().map(|s| s.to_os_string()).unwrap_or_default();
    let mut out = stem;
    out.push(format!(" ({n})"));
    if let Some(ext) = p.extension() {
        out.push(".");
        out.push(ext);
    }
    out
}

/// Try candidate destinations in `dest_dir` until `create` claims one atomically. `create` must return
/// `FsError::Exists` without side effects when the candidate is taken. `Ok(None)` = skipped by policy.
pub(crate) fn claim_dest(
    dest_dir: &Path,
    name: &std::ffi::OsStr,
    policy: ConflictPolicy,
    mut create: impl FnMut(&Path) -> Result<(), FsError>,
) -> Result<Option<PathBuf>, FsError> {
    for n in 0..=MAX_KEEP_BOTH {
        let candidate = dest_dir.join(numbered_name(name, n));
        match create(&candidate) {
            Ok(()) => return Ok(Some(candidate)),
            Err(FsError::Exists) => {
                if policy == ConflictPolicy::Skip {
                    return Ok(None);
                }
            }
            Err(e) => return Err(e),
        }
    }
    Err(FsError::Io(io::Error::other("no free file name found")))
}

/// Creates `dst` as a symlink to `target`, the link read from `src`.
#[cfg(unix)]
fn symlink(_src: &Path, target: &Path, dst: &Path) -> io::Result<()> {
    std::os::unix::fs::symlink(target, dst)
}

/// Creates `dst` as a symlink to `target`, the link read from `src`. Windows distinguishes file and
/// directory links, so the kind is taken from what `src` resolves to (creating one needs the
/// symlink privilege or developer mode; without it this is a plain error).
#[cfg(windows)]
fn symlink(src: &Path, target: &Path, dst: &Path) -> io::Result<()> {
    if fs::metadata(src).is_ok_and(|m| m.is_dir()) {
        std::os::windows::fs::symlink_dir(target, dst)
    } else {
        std::os::windows::fs::symlink_file(target, dst)
    }
}

/// Copy a file, directory tree or symlink to `dst` (which must not exist). On failure everything this
/// call created is removed again.
pub(crate) fn copy_node(src: &Path, dst: &Path) -> Result<(), FsError> {
    copy_depth(src, dst, 0)
}

fn copy_depth(src: &Path, dst: &Path, depth: usize) -> Result<(), FsError> {
    if depth > MAX_DEPTH {
        return Err(FsError::Io(io::Error::other("directory tree too deep")));
    }
    let meta = fs::symlink_metadata(src)?;
    let ft = meta.file_type();
    if ft.is_symlink() {
        let target = fs::read_link(src)?;
        return symlink(src, &target, dst).map_err(map_exists);
    }
    if ft.is_dir() {
        fs::create_dir(dst).map_err(map_exists)?;
        let result = copy_children(src, dst, depth);
        match result {
            Ok(()) => {
                let _ = fs::set_permissions(dst, meta.permissions());
                Ok(())
            }
            Err(e) => {
                let _ = fs::remove_dir_all(dst);
                Err(match e {
                    FsError::Exists => {
                        FsError::Io(io::Error::other("destination changed during copy"))
                    }
                    other => other,
                })
            }
        }
    } else if ft.is_file() {
        let mut input = fs::File::open(src)?;
        let mut output = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(dst)
            .map_err(map_exists)?;
        if let Err(e) = io::copy(&mut input, &mut output) {
            drop(output);
            let _ = fs::remove_file(dst);
            return Err(FsError::Io(e));
        }
        drop(output);
        let _ = fs::set_permissions(dst, meta.permissions());
        Ok(())
    } else {
        Err(FsError::Io(io::Error::other(
            "unsupported file type (device, socket or pipe)",
        )))
    }
}

fn copy_children(src: &Path, dst: &Path, depth: usize) -> Result<(), FsError> {
    for entry in fs::read_dir(src)? {
        let entry = entry?;
        copy_depth(&entry.path(), &dst.join(entry.file_name()), depth + 1)?;
    }
    Ok(())
}

fn map_exists(e: io::Error) -> FsError {
    if e.kind() == io::ErrorKind::AlreadyExists {
        FsError::Exists
    } else {
        FsError::Io(e)
    }
}

pub(crate) fn remove_node(p: &Path) -> io::Result<()> {
    let meta = fs::symlink_metadata(p)?;
    if meta.file_type().is_dir() {
        fs::remove_dir_all(p)
    } else {
        fs::remove_file(p)
    }
}

/// True when `to` names the file `from` already is, spelled with different letter case — the same
/// directory entry on a case-insensitive filesystem (Windows, default macOS). A distinct file whose
/// name merely differs in case, or a hard link, is not.
pub(crate) fn is_case_variant_of_same_file(from: &Path, to: &Path) -> bool {
    let (Some(a), Some(b)) = (from.file_name(), to.file_name()) else {
        return false;
    };
    from.parent() == to.parent()
        && a != b
        && a.to_string_lossy().to_lowercase() == b.to_string_lossy().to_lowercase()
        && same_file::is_same_file(from, to).unwrap_or(false)
}

/// Move `src` to the exact path `dst`, refusing to overwrite. Falls back to copy + remove across
/// filesystems. A case-only rename on a case-insensitive filesystem (where `dst` "exists" because it
/// is `src`) goes through a temporary name, since renaming a file onto its own case variant can be a
/// no-op.
pub(crate) fn move_exact(src: &Path, dst: &Path) -> Result<(), FsError> {
    if exists_nofollow(dst) {
        if is_case_variant_of_same_file(src, dst) {
            return case_only_rename(src, dst);
        }
        return Err(FsError::Exists);
    }
    match fs::rename(src, dst) {
        Ok(()) => Ok(()),
        Err(e) if is_cross_device(&e) => {
            let is_dir = fs::symlink_metadata(src)
                .map(|m| m.file_type().is_dir())
                .unwrap_or(false);
            copy_node(src, dst)?;
            match remove_node(src) {
                Ok(()) => Ok(()),
                Err(rm) => {
                    if !is_dir {
                        // Source is intact: undo the copy so exactly one instance remains.
                        let _ = remove_node(dst);
                        Err(FsError::Io(rm))
                    } else {
                        Err(FsError::Io(io::Error::other(format!(
                            "copied to {} but could not remove the source: {rm}",
                            dst.display()
                        ))))
                    }
                }
            }
        }
        Err(e) => Err(FsError::Io(e)),
    }
}

/// Rename `src` to its own case variant `dst` through a temporary sibling name. If `dst` still exists
/// once `src` has moved away (they were distinct links, not one entry), the original is restored.
fn case_only_rename(src: &Path, dst: &Path) -> Result<(), FsError> {
    let parent = src.parent().unwrap_or_else(|| Path::new("."));
    let pid = std::process::id();
    let tmp = (0u32..=1000)
        .map(|n| parent.join(format!(".gbcase-{pid}-{n}.tmp")))
        .find(|c| !exists_nofollow(c))
        .ok_or_else(|| FsError::Io(io::Error::other("could not find a free temporary name")))?;
    fs::rename(src, &tmp)?;
    if exists_nofollow(dst) {
        let _ = fs::rename(&tmp, src);
        return Err(FsError::Exists);
    }
    match fs::rename(&tmp, dst) {
        Ok(()) => Ok(()),
        Err(e) => {
            let _ = fs::rename(&tmp, src);
            Err(FsError::Io(e))
        }
    }
}

/// Rename every `(from, to)` pair so that cycles and chains (1→2, 2→3) work: phase 1 moves each source to
/// a unique temporary name next to it, phase 2 moves temporaries to their final names (no-clobber). Any
/// failure rolls everything back, leaving the original names. Pair sources and targets must each be
/// unique.
pub(crate) fn two_phase_rename(pairs: &[(PathBuf, PathBuf)], tag: u64) -> Result<(), String> {
    match pairs {
        [] => return Ok(()),
        [(from, to)] => {
            return move_exact(from, to)
                .map_err(|e| format!("{}: {}", from.display(), e.message(to)));
        }
        _ => {}
    }
    let mut seen_to = HashSet::new();
    let mut seen_from = HashSet::new();
    for (from, to) in pairs {
        if !seen_to.insert(to) {
            return Err(format!("two items would be renamed to {}", to.display()));
        }
        if !seen_from.insert(from) {
            return Err(format!("{} appears twice", from.display()));
        }
    }
    let pid = std::process::id();
    let mut temps: Vec<PathBuf> = Vec::with_capacity(pairs.len());
    // Phase 1: sources → temporaries.
    for (idx, (from, _)) in pairs.iter().enumerate() {
        let parent = from.parent().unwrap_or_else(|| Path::new("."));
        let mut attempt = 0u32;
        let tmp = loop {
            let cand = parent.join(format!(".gbrename-{pid}-{tag}-{idx}-{attempt}.tmp"));
            if !exists_nofollow(&cand) {
                break cand;
            }
            attempt += 1;
            if attempt > 1000 {
                rollback(pairs, &temps, 0);
                return Err("could not find a free temporary name".into());
            }
        };
        if let Err(e) = move_exact(from, &tmp) {
            let msg = format!("{}: {}", from.display(), e.message(&tmp));
            rollback(pairs, &temps, 0);
            return Err(msg);
        }
        temps.push(tmp);
    }
    // Phase 2: temporaries → final names.
    for (idx, (_, to)) in pairs.iter().enumerate() {
        let Some(tmp) = temps.get(idx) else { continue };
        if let Err(e) = move_exact(tmp, to) {
            let msg = format!("{}: {}", to.display(), e.message(to));
            rollback(pairs, &temps, idx);
            return Err(msg);
        }
    }
    Ok(())
}

/// Undo a partially applied [`two_phase_rename`]: the first `finalized` pairs already sit at their final
/// name; all temporaries belong to a source.
fn rollback(pairs: &[(PathBuf, PathBuf)], temps: &[PathBuf], finalized: usize) {
    for (idx, (_, to)) in pairs.iter().enumerate().take(finalized) {
        if let Some(tmp) = temps.get(idx) {
            let _ = move_exact(to, tmp);
        }
    }
    for (idx, (from, _)) in pairs.iter().enumerate() {
        if let Some(tmp) = temps.get(idx) {
            let _ = move_exact(tmp, from);
        }
    }
}

/// True when `candidate` is `root` or lies below it (lexically, and after resolving symlinks if possible).
pub(crate) fn is_inside(candidate: &Path, root: &Path) -> bool {
    if candidate.starts_with(root) {
        return true;
    }
    match (fs::canonicalize(candidate), fs::canonicalize(root)) {
        (Ok(c), Ok(r)) => c.starts_with(r),
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn numbered_names() {
        assert_eq!(numbered_name("a.jpg".as_ref(), 0), OsString::from("a.jpg"));
        assert_eq!(
            numbered_name("a.jpg".as_ref(), 2),
            OsString::from("a (2).jpg")
        );
        assert_eq!(numbered_name("dir".as_ref(), 1), OsString::from("dir (1)"));
        assert_eq!(
            numbered_name(".hidden".as_ref(), 1),
            OsString::from(".hidden (1)")
        );
    }
}
