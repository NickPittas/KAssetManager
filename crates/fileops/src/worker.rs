use std::ffi::OsStr;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, MutexGuard, PoisonError};

use crossbeam_channel::{Receiver, Sender, unbounded};

use crate::fsops;
use crate::rename::{RenamePlan, RenameStatus};
use crate::{ConflictPolicy, FileOp, OpError};

#[derive(Clone, Debug, Default)]
pub struct OpOutcome {
    pub id: u64,
    /// Human label for status bar / undo menu, e.g. "Move 3 items".
    pub label: String,
    /// (old, new) for every file/dir that changed path (move, rename, batch rename, undo of those).
    pub moved: Vec<(PathBuf, PathBuf)>,
    pub created: Vec<PathBuf>,
    pub removed: Vec<PathBuf>,
    pub errors: Vec<OpError>,
}

#[derive(Clone, Debug)]
pub enum OpEvent {
    Progress {
        id: u64,
        done: u64,
        total: u64,
        current: Option<PathBuf>,
        /// Bytes accounted per source: what the sources weighed before anything moved, and how much of that has
        /// landed. A single large file reports its size when it finishes - copying inside one file is not chunked,
        /// so there is nothing to report in between. Ops with no weight worth reporting send `0 / 0`.
        bytes_done: u64,
        bytes_total: u64,
    },
    Finished(OpOutcome),
}

/// How much has to land between two words about a large copy. The copy itself reports every chunk; this decides
/// how often that is worth an event and a status-line update.
const PROGRESS_REPORT_BYTES: u64 = 8 << 20;

/// Maximum number of remembered undoable operations.
const HISTORY_LIMIT: usize = 100;

/// A reversible step stored in the history.
#[derive(Clone, Debug)]
enum Action {
    /// Move each `(from, to)` to its exact target (no overwrite). `atomic`: two-phase, all-or-nothing
    /// (renames, where targets may be other sources' old names).
    Moves {
        pairs: Vec<(PathBuf, PathBuf)>,
        atomic: bool,
    },
    /// Copy each `(source, exact destination)`.
    CopyExact(Vec<(PathBuf, PathBuf)>),
    /// Send to the freedesktop trash.
    Trash(Vec<PathBuf>),
    /// Restore these original paths from the freedesktop trash.
    Restore(Vec<PathBuf>),
    /// Remove an (empty) directory.
    RemoveDir(PathBuf),
    /// Create a directory.
    MakeDir(PathBuf),
}

#[derive(Clone, Debug)]
struct Entry {
    label: String,
    undo: Action,
    redo: Action,
}

#[derive(Default)]
struct History {
    undo: Vec<Entry>,
    redo: Vec<Entry>,
}

enum Job {
    Op { id: u64, op: FileOp },
    Undo { id: u64 },
    Redo { id: u64 },
}

#[derive(Default)]
struct Effect {
    moved: Vec<(PathBuf, PathBuf)>,
    created: Vec<PathBuf>,
    removed: Vec<PathBuf>,
    errors: Vec<OpError>,
}

impl Effect {
    fn did_anything(&self) -> bool {
        !(self.moved.is_empty() && self.created.is_empty() && self.removed.is_empty())
    }
    fn err(&mut self, path: &Path, message: impl Into<String>) {
        self.errors.push(OpError::Path {
            path: path.to_path_buf(),
            message: message.into(),
        });
    }
    fn other(&mut self, message: impl Into<String>) {
        self.errors.push(OpError::Other(message.into()));
    }
}

/// A request to stop work: the operation running now, and everything still queued behind it - the two buttons
/// Qt's operations dialog offers. The worker reads the same flags before each queued job, between sources, and
/// inside a large copy, so cancelling a 2 GB file stops within a chunk of it rather than after all of it.
#[derive(Clone, Default)]
struct Cancel {
    current: Arc<AtomicBool>,
    all: Arc<AtomicBool>,
}

impl Cancel {
    fn requested(&self) -> bool {
        self.current.load(Ordering::SeqCst) || self.all.load(Ordering::SeqCst)
    }

    /// Called as each queued operation starts: a cancel aimed at the previous one does not hold back this one.
    fn begin_job(&self) {
        self.current.store(false, Ordering::SeqCst);
    }

    /// Forget both requests once nothing is queued; the next operation starts clean.
    fn reset(&self) {
        self.current.store(false, Ordering::SeqCst);
        self.all.store(false, Ordering::SeqCst);
    }
}

struct Ctx<'a> {
    id: u64,
    events: &'a Sender<OpEvent>,
    bytes_total: u64,
    bytes_done: std::cell::Cell<u64>,
    cancel: Cancel,
}

impl Ctx<'_> {
    fn progress(&self, done: usize, total: usize, current: Option<&Path>) {
        let _ = self.events.send(OpEvent::Progress {
            id: self.id,
            done: done as u64,
            total: total as u64,
            current: current.map(Path::to_path_buf),
            bytes_done: self.bytes_done.get(),
            bytes_total: self.bytes_total,
        });
    }

    /// Account a source that has just landed, so the progress line can say how much of the weight is across.
    fn add_bytes(&self, n: u64) {
        self.bytes_done.set(self.bytes_done.get() + n);
    }
}

/// Bytes a source is worth to the progress line: its size, or the sum of a tree's files. Best effort - a file that
/// disappears while being measured counts as nothing rather than failing the operation.
fn tree_bytes(path: &Path) -> u64 {
    let Ok(meta) = std::fs::symlink_metadata(path) else {
        return 0;
    };
    if !meta.file_type().is_dir() {
        return meta.len();
    }
    let mut total = meta.len();
    if let Ok(entries) = std::fs::read_dir(path) {
        for entry in entries.flatten() {
            total += tree_bytes(&entry.path());
        }
    }
    total
}

/// What the sources of an operation weigh before anything moves. Only copies and moves carry weight worth
/// reporting; a rename is instant, and history steps do not re-weigh what they replay.
fn op_bytes(op: &FileOp) -> u64 {
    match op {
        FileOp::Copy { sources, .. } | FileOp::Move { sources, .. } => {
            sources.iter().map(|src| tree_bytes(src)).sum()
        }
        _ => 0,
    }
}

/// Owns the worker thread and the undo/redo history (history mutated only on the worker, serialized).
pub struct FileOpsWorker {
    jobs: Sender<Job>,
    events_tx: Sender<OpEvent>,
    events: Receiver<OpEvent>,
    history: Arc<Mutex<History>>,
    pending: Arc<AtomicUsize>,
    next_id: AtomicU64,
    /// The operation the worker thread is inside now, and the two cancel requests.
    running: Arc<Mutex<Option<u64>>>,
    cancel: Cancel,
}

fn lock<T>(m: &Mutex<T>) -> MutexGuard<'_, T> {
    m.lock().unwrap_or_else(PoisonError::into_inner)
}

impl FileOpsWorker {
    pub fn new() -> Self {
        let (jobs, job_rx) = unbounded::<Job>();
        let (events_tx, events) = unbounded::<OpEvent>();
        let history = Arc::new(Mutex::new(History::default()));
        let pending = Arc::new(AtomicUsize::new(0));
        let worker_events = events_tx.clone();
        let worker_history = Arc::clone(&history);
        let worker_pending = Arc::clone(&pending);
        let running: Arc<Mutex<Option<u64>>> = Arc::new(Mutex::new(None));
        let cancel = Cancel::default();
        let worker_running = Arc::clone(&running);
        let worker_cancel = cancel.clone();
        let spawned = std::thread::Builder::new()
            .name("fileops".into())
            .spawn(move || {
                for job in job_rx.iter() {
                    let id = match &job {
                        Job::Op { id, .. } | Job::Undo { id } | Job::Redo { id } => *id,
                    };
                    *lock(&worker_running) = Some(id);
                    worker_cancel.begin_job();
                    // An operation the user cancelled before its turn does no work at all.
                    let skipped = worker_cancel.requested();
                    if skipped {
                        let _ = worker_events.send(OpEvent::Finished(OpOutcome {
                            id,
                            label: "File operation".into(),
                            errors: vec![OpError::Other("cancelled".into())],
                            ..Default::default()
                        }));
                        drop(job);
                    } else {
                        let cancel_for_job = worker_cancel.clone();
                        let result = catch_unwind(AssertUnwindSafe(|| {
                            run_job(job, &worker_history, &worker_events, cancel_for_job)
                        }));
                        if result.is_err() {
                            let _ = worker_events.send(OpEvent::Finished(OpOutcome {
                                id,
                                label: "File operation".into(),
                                errors: vec![OpError::Other(
                                    "internal error while running file operation".into(),
                                )],
                                ..Default::default()
                            }));
                        }
                    }
                    *lock(&worker_running) = None;
                    // Once nothing is left queued, the cancel requests have spent themselves.
                    if job_rx.is_empty() {
                        worker_cancel.reset();
                    }
                    worker_pending.fetch_sub(1, Ordering::SeqCst);
                }
            });
        if let Err(e) = spawned {
            log::error!("could not start the file operations thread: {e}");
        }
        Self {
            jobs,
            events_tx,
            events,
            history,
            pending,
            next_id: AtomicU64::new(1),
            running,
            cancel,
        }
    }

    fn enqueue(&self, make: impl FnOnce(u64) -> Job) -> u64 {
        let id = self.next_id.fetch_add(1, Ordering::SeqCst);
        self.pending.fetch_add(1, Ordering::SeqCst);
        if self.jobs.send(make(id)).is_err() {
            self.pending.fetch_sub(1, Ordering::SeqCst);
            let _ = self.events_tx.send(OpEvent::Finished(OpOutcome {
                id,
                label: "File operation".into(),
                errors: vec![OpError::Other(
                    "the file operations thread is not running".into(),
                )],
                ..Default::default()
            }));
        }
        id
    }

    /// Stop the operation that is running now. What has already been copied stays on disk - the operation stops,
    /// it does not roll back. Qt's operations dialog calls this "Cancel Current". False when nothing is running.
    pub fn cancel_current(&self) -> bool {
        let running = lock(&self.running).is_some();
        if running {
            self.cancel.current.store(true, Ordering::SeqCst);
        }
        running
    }

    /// Also skip every operation still queued, until the queue drains. Qt's "Cancel All".
    pub fn cancel_all(&self) {
        self.cancel.all.store(true, Ordering::SeqCst);
    }

    /// Whether an operation is running now; what decides if Cancel Current can be used.
    pub fn is_running(&self) -> bool {
        lock(&self.running).is_some()
    }

    /// Queue an operation; returns its id immediately.
    pub fn submit(&self, op: FileOp) -> u64 {
        self.enqueue(|id| Job::Op { id, op })
    }
    /// Undo the most recent undoable finished op (queued like any op). `None` if nothing to undo.
    pub fn undo(&self) -> Option<u64> {
        if !self.can_undo() && !self.busy() {
            return None;
        }
        Some(self.enqueue(|id| Job::Undo { id }))
    }
    pub fn redo(&self) -> Option<u64> {
        if !self.can_redo() && !self.busy() {
            return None;
        }
        Some(self.enqueue(|id| Job::Redo { id }))
    }
    pub fn can_undo(&self) -> bool {
        !lock(&self.history).undo.is_empty()
    }
    pub fn can_redo(&self) -> bool {
        !lock(&self.history).redo.is_empty()
    }
    /// Label of the op `undo` would revert.
    pub fn undo_label(&self) -> Option<String> {
        lock(&self.history).undo.last().map(|e| e.label.clone())
    }
    /// Non-blocking: drain events since last call.
    pub fn poll(&self) -> Vec<OpEvent> {
        self.events.try_iter().collect()
    }
    /// True while any op is queued or running.
    pub fn busy(&self) -> bool {
        self.pending.load(Ordering::SeqCst) > 0
    }
}

impl Default for FileOpsWorker {
    fn default() -> Self {
        Self::new()
    }
}

// ---------------------------------------------------------------------------------------------
// Job execution (worker thread)
// ---------------------------------------------------------------------------------------------

fn run_job(job: Job, history: &Mutex<History>, events: &Sender<OpEvent>, cancel: Cancel) {
    match job {
        Job::Op { id, op } => {
            // Weighed before the work starts: once the sources have moved there is nothing left to measure.
            let bytes_total = op_bytes(&op);
            let ctx = Ctx {
                id,
                events,
                bytes_total,
                bytes_done: std::cell::Cell::new(0),
                cancel,
            };
            let (label, effect, entry) = run_op(&ctx, op);
            if let Some(entry) = entry {
                let mut h = lock(history);
                h.redo.clear();
                h.undo.push(entry);
                if h.undo.len() > HISTORY_LIMIT {
                    h.undo.remove(0);
                }
            }
            finish(events, id, label, effect);
        }
        Job::Undo { id } => run_history(id, history, events, cancel, true),
        Job::Redo { id } => run_history(id, history, events, cancel, false),
    }
}

fn finish(events: &Sender<OpEvent>, id: u64, label: String, effect: Effect) {
    let _ = events.send(OpEvent::Finished(OpOutcome {
        id,
        label,
        moved: effect.moved,
        created: effect.created,
        removed: effect.removed,
        errors: effect.errors,
    }));
}

fn run_history(
    id: u64,
    history: &Mutex<History>,
    events: &Sender<OpEvent>,
    cancel: Cancel,
    undo: bool,
) {
    let verb = if undo { "Undo" } else { "Redo" };
    let entry = {
        let mut h = lock(history);
        if undo { h.undo.pop() } else { h.redo.pop() }
    };
    let Some(entry) = entry else {
        let mut effect = Effect::default();
        effect.other(format!("Nothing to {}", verb.to_lowercase()));
        finish(events, id, verb.to_string(), effect);
        return;
    };
    // History replays a remembered action, so its weight is not re-derived here; the counts still arrive.
    let ctx = Ctx {
        id,
        events,
        bytes_total: 0,
        bytes_done: std::cell::Cell::new(0),
        cancel,
    };
    let action = if undo { &entry.undo } else { &entry.redo };
    let effect = run_action(&ctx, action, id);
    {
        let mut h = lock(history);
        // Nothing happened: keep the entry where it was so the user can retry.
        let target_is_undo = if effect.did_anything() { !undo } else { undo };
        if target_is_undo {
            h.undo.push(entry.clone());
        } else {
            h.redo.push(entry.clone());
        }
    }
    finish(events, id, format!("{verb} {}", entry.label), effect);
}

fn items_label(verb: &str, paths: &[PathBuf]) -> String {
    match paths {
        [one] => match one.file_name() {
            Some(n) => format!("{verb} \"{}\"", n.to_string_lossy()),
            None => format!("{verb} 1 item"),
        },
        many => format!("{verb} {} items", many.len()),
    }
}

fn run_op(ctx: &Ctx<'_>, op: FileOp) -> (String, Effect, Option<Entry>) {
    match op {
        FileOp::Copy {
            sources,
            dest_dir,
            conflict,
        } => {
            let label = items_label("Copy", &sources);
            let (effect, pairs) = copy_sources(ctx, &sources, &dest_dir, conflict);
            let entry = (!pairs.is_empty()).then(|| Entry {
                label: label.clone(),
                undo: Action::Trash(pairs.iter().map(|(_, d)| d.clone()).collect()),
                redo: Action::CopyExact(pairs),
            });
            (label, effect, entry)
        }
        FileOp::Move {
            sources,
            dest_dir,
            conflict,
        } => {
            let label = items_label("Move", &sources);
            let effect = move_sources(ctx, &sources, &dest_dir, conflict);
            let entry = moves_entry(&label, &effect.moved, false);
            (label, effect, entry)
        }
        FileOp::Trash { paths } => {
            let label = items_label("Trash", &paths);
            let effect = trash_paths(ctx, &paths);
            // The `trash` crate cannot restore on macOS, so there trashing is not undoable.
            let entry =
                (cfg!(not(target_os = "macos")) && !effect.removed.is_empty()).then(|| Entry {
                    label: label.clone(),
                    undo: Action::Restore(effect.removed.clone()),
                    redo: Action::Trash(effect.removed.clone()),
                });
            (label, effect, entry)
        }
        FileOp::Delete { paths } => {
            let label = items_label("Delete", &paths);
            let mut effect = Effect::default();
            for (i, p) in paths.iter().enumerate() {
                ctx.progress(i, paths.len(), Some(p));
                match fsops::remove_node(p) {
                    Ok(()) => effect.removed.push(p.clone()),
                    Err(e) => effect.err(p, e.to_string()),
                }
            }
            ctx.progress(paths.len(), paths.len(), None);
            (label, effect, None)
        }
        FileOp::Rename { from, to } => {
            let label = items_label("Rename", std::slice::from_ref(&from));
            let mut effect = Effect::default();
            ctx.progress(0, 1, Some(&from));
            if from == to {
                effect.err(&from, "the new name is the same as the old one");
            } else {
                match fsops::move_exact(&from, &to) {
                    Ok(()) => effect.moved.push((from, to)),
                    Err(e) => effect.err(&from, e.message(&to)),
                }
            }
            ctx.progress(1, 1, None);
            let entry = moves_entry(&label, &effect.moved, true);
            (label, effect, entry)
        }
        FileOp::BatchRename { plan } => {
            let label = format!("Rename {} items", plan.items.len());
            let effect = batch_rename(ctx, &plan);
            let entry = moves_entry(&label, &effect.moved, true);
            (label, effect, entry)
        }
        FileOp::NewFolder { parent, name } => {
            let label = format!("New folder \"{name}\"");
            let mut effect = Effect::default();
            let invalid = name.is_empty()
                || name == "."
                || name == ".."
                || name.contains('/')
                || name.contains('\0')
                || name.len() > 255;
            if invalid {
                effect.err(&parent, format!("invalid folder name \"{name}\""));
                return (label, effect, None);
            }
            let path = parent.join(&name);
            match std::fs::create_dir(&path) {
                Ok(()) => effect.created.push(path.clone()),
                Err(e) => effect.err(&path, e.to_string()),
            }
            let entry = (!effect.created.is_empty()).then(|| Entry {
                label: label.clone(),
                undo: Action::RemoveDir(path.clone()),
                redo: Action::MakeDir(path),
            });
            (label, effect, entry)
        }
    }
}

fn moves_entry(label: &str, moved: &[(PathBuf, PathBuf)], atomic: bool) -> Option<Entry> {
    if moved.is_empty() {
        return None;
    }
    Some(Entry {
        label: label.to_string(),
        undo: Action::Moves {
            pairs: moved
                .iter()
                .rev()
                .map(|(a, b)| (b.clone(), a.clone()))
                .collect(),
            atomic,
        },
        redo: Action::Moves {
            pairs: moved.to_vec(),
            atomic,
        },
    })
}

fn source_name(src: &Path) -> Option<&OsStr> {
    src.file_name()
}

/// Returns the effect and the exact `(source, created destination)` pairs.
fn copy_sources(
    ctx: &Ctx<'_>,
    sources: &[PathBuf],
    dest_dir: &Path,
    policy: ConflictPolicy,
) -> (Effect, Vec<(PathBuf, PathBuf)>) {
    let mut effect = Effect::default();
    let mut pairs = Vec::new();
    for (i, src) in sources.iter().enumerate() {
        if ctx.cancel.requested() {
            effect.err(src, "cancelled");
            break;
        }
        // Measured before the copy: this is what the progress line counts down from.
        let src_bytes = tree_bytes(src);
        ctx.progress(i, sources.len(), Some(src));
        let Some(name) = source_name(src) else {
            effect.err(src, "no file name");
            continue;
        };
        if let Ok(meta) = std::fs::symlink_metadata(src)
            && meta.file_type().is_dir()
            && fsops::is_inside(dest_dir, src)
        {
            effect.err(src, "cannot copy a folder into itself");
            continue;
        }
        // Bytes are reported as they land, so the progress line moves through one large file instead of sitting at
        // zero and jumping to done. `base` carries the running total across sources; the throttle keeps a 2 GB
        // copy to a few hundred events rather than one per megabyte.
        let base = ctx.bytes_done.get();
        let mut within: u64 = 0;
        let mut emitted: u64 = 0;
        match fsops::claim_dest(dest_dir, name, policy, |cand| {
            fsops::copy_node_reported(src, cand, &mut |delta| {
                within += delta;
                if within.saturating_sub(emitted) >= PROGRESS_REPORT_BYTES {
                    emitted = within;
                    ctx.bytes_done.set(base + within);
                    ctx.progress(i, sources.len(), Some(src));
                }
                // The copy asks here every chunk, so a cancel reaches the middle of a large file.
                !ctx.cancel.requested()
            })
        }) {
            Ok(Some(dst)) => {
                ctx.add_bytes(src_bytes);
                effect.created.push(dst.clone());
                pairs.push((src.clone(), dst));
            }
            Ok(None) => effect.err(
                src,
                "skipped: a file with this name already exists in the destination",
            ),
            Err(e) => effect.err(src, e.message(&dest_dir.join(name))),
        }
    }
    ctx.progress(sources.len(), sources.len(), None);
    (effect, pairs)
}

fn move_sources(
    ctx: &Ctx<'_>,
    sources: &[PathBuf],
    dest_dir: &Path,
    policy: ConflictPolicy,
) -> Effect {
    let mut effect = Effect::default();
    for (i, src) in sources.iter().enumerate() {
        if ctx.cancel.requested() {
            effect.err(src, "cancelled");
            break;
        }
        // Measured before the move: after it, the source is gone and there is nothing left to weigh.
        let src_bytes = tree_bytes(src);
        ctx.progress(i, sources.len(), Some(src));
        let Some(name) = source_name(src) else {
            effect.err(src, "no file name");
            continue;
        };
        if src.parent() == Some(dest_dir) {
            effect.err(src, "already in the destination folder");
            continue;
        }
        if let Ok(meta) = std::fs::symlink_metadata(src)
            && meta.file_type().is_dir()
            && fsops::is_inside(dest_dir, src)
        {
            effect.err(src, "cannot move a folder into itself");
            continue;
        }
        match fsops::claim_dest(dest_dir, name, policy, |cand| fsops::move_exact(src, cand)) {
            Ok(Some(dst)) => {
                ctx.add_bytes(src_bytes);
                effect.moved.push((src.clone(), dst));
            }
            Ok(None) => effect.err(
                src,
                "skipped: a file with this name already exists in the destination",
            ),
            Err(e) => effect.err(src, e.message(&dest_dir.join(name))),
        }
    }
    ctx.progress(sources.len(), sources.len(), None);
    effect
}

fn trash_paths(ctx: &Ctx<'_>, paths: &[PathBuf]) -> Effect {
    let mut effect = Effect::default();
    for (i, p) in paths.iter().enumerate() {
        ctx.progress(i, paths.len(), Some(p));
        if !fsops::exists_nofollow(p) {
            effect.err(p, "does not exist");
            continue;
        }
        match trash::delete(p) {
            Ok(()) => effect.removed.push(p.clone()),
            Err(e) => effect.err(p, e.to_string()),
        }
    }
    ctx.progress(paths.len(), paths.len(), None);
    effect
}

fn batch_rename(ctx: &Ctx<'_>, plan: &RenamePlan) -> Effect {
    let mut effect = Effect::default();
    if !plan.is_valid() {
        let first_problem = plan.items.iter().find_map(|i| match &i.status {
            RenameStatus::Invalid(m) | RenameStatus::Collision(m) => {
                Some((i.from.clone(), m.clone()))
            }
            _ => None,
        });
        match first_problem {
            Some((p, m)) => effect.err(&p, m),
            None => effect.other("nothing to rename"),
        }
        return effect;
    }
    let pairs: Vec<(PathBuf, PathBuf)> = plan
        .items
        .iter()
        .filter(|i| i.status == RenameStatus::Ok)
        .map(|i| (i.from.clone(), i.to.clone()))
        .collect();
    ctx.progress(0, pairs.len(), None);
    match fsops::two_phase_rename(&pairs, ctx.id) {
        Ok(()) => effect.moved = pairs,
        Err(msg) => effect.other(format!("rename failed, nothing was changed: {msg}")),
    }
    ctx.progress(effect.moved.len(), effect.moved.len().max(1), None);
    effect
}

fn run_action(ctx: &Ctx<'_>, action: &Action, tag: u64) -> Effect {
    let mut effect = Effect::default();
    match action {
        Action::Moves {
            pairs,
            atomic: true,
        } => {
            ctx.progress(0, pairs.len(), None);
            match fsops::two_phase_rename(pairs, tag) {
                Ok(()) => effect.moved = pairs.clone(),
                Err(msg) => effect.other(msg),
            }
        }
        Action::Moves {
            pairs,
            atomic: false,
        } => {
            for (i, (from, to)) in pairs.iter().enumerate() {
                ctx.progress(i, pairs.len(), Some(from));
                if let Some(parent) = to.parent()
                    && !parent.as_os_str().is_empty()
                    && let Err(e) = std::fs::create_dir_all(parent)
                {
                    effect.err(from, e.to_string());
                    continue;
                }
                match fsops::move_exact(from, to) {
                    Ok(()) => effect.moved.push((from.clone(), to.clone())),
                    Err(e) => effect.err(from, e.message(to)),
                }
            }
        }
        Action::CopyExact(pairs) => {
            for (i, (src, dst)) in pairs.iter().enumerate() {
                ctx.progress(i, pairs.len(), Some(src));
                match fsops::copy_node(src, dst) {
                    Ok(()) => effect.created.push(dst.clone()),
                    Err(e) => effect.err(src, e.message(dst)),
                }
            }
        }
        Action::Trash(paths) => return trash_paths(ctx, paths),
        Action::Restore(paths) => restore_from_trash(&mut effect, paths),
        Action::RemoveDir(p) => match std::fs::remove_dir(p) {
            Ok(()) => effect.removed.push(p.clone()),
            Err(e) => effect.err(p, format!("cannot remove folder (is it empty?): {e}")),
        },
        Action::MakeDir(p) => match std::fs::create_dir(p) {
            Ok(()) => effect.created.push(p.clone()),
            Err(e) => effect.err(p, e.to_string()),
        },
    }
    effect
}

/// Restore the newest trashed item whose original path equals each path (freedesktop / Windows
/// Recycle Bin).
#[cfg(not(target_os = "macos"))]
fn restore_from_trash(effect: &mut Effect, paths: &[PathBuf]) {
    let items = match trash::os_limited::list() {
        Ok(items) => items,
        Err(e) => {
            effect.other(format!("cannot read the trash: {e}"));
            return;
        }
    };
    let mut used: std::collections::HashSet<std::ffi::OsString> = Default::default();
    for p in paths {
        if fsops::exists_nofollow(p) {
            effect.err(p, "cannot restore: a file with this name exists");
            continue;
        }
        let candidate = items
            .iter()
            .filter(|i| i.original_path() == *p && !used.contains(&i.id))
            .max_by_key(|i| i.time_deleted);
        let Some(item) = candidate else {
            effect.err(p, "not found in the trash");
            continue;
        };
        used.insert(item.id.clone());
        match trash::os_limited::restore_all([item.clone()]) {
            Ok(()) => effect.created.push(p.clone()),
            Err(e) => effect.err(p, format!("cannot restore from trash: {e}")),
        }
    }
}

/// The `trash` crate cannot list or restore items on macOS (Finder owns the Trash), so no undo entry is
/// recorded for trashing there; this only runs if a `Restore` is queued anyway.
#[cfg(target_os = "macos")]
fn restore_from_trash(effect: &mut Effect, paths: &[PathBuf]) {
    for p in paths {
        effect.err(
            p,
            "restoring from the Trash is not supported on macOS; use Finder's Put Back",
        );
    }
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

    use super::*;
    use crate::rename::{NumberMode, RenameRule, plan_renames};
    use std::fs;
    use std::time::{Duration, Instant};

    fn wait_finished(w: &FileOpsWorker, id: u64) -> OpOutcome {
        let deadline = Instant::now() + Duration::from_secs(20);
        while Instant::now() < deadline {
            for ev in w.poll() {
                if let OpEvent::Finished(o) = ev
                    && o.id == id
                {
                    return o;
                }
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        panic!("timeout waiting for op {id}");
    }

    fn write(p: &Path, s: &str) {
        fs::write(p, s).expect("write");
    }

    fn read(p: &Path) -> String {
        fs::read_to_string(p).expect("read")
    }

    #[test]
    fn copy_keep_both_and_skip() {
        let d = tempfile::tempdir().expect("tmp");
        let src = d.path().join("a.txt");
        let dest = d.path().join("dest");
        fs::create_dir(&dest).expect("mkdir");
        write(&src, "new");
        write(&dest.join("a.txt"), "old");
        let w = FileOpsWorker::new();

        let id = w.submit(FileOp::Copy {
            sources: vec![src.clone()],
            dest_dir: dest.clone(),
            conflict: ConflictPolicy::KeepBoth,
        });
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        assert_eq!(o.created, vec![dest.join("a (1).txt")]);
        assert_eq!(read(&dest.join("a.txt")), "old");
        assert_eq!(read(&dest.join("a (1).txt")), "new");

        let id = w.submit(FileOp::Copy {
            sources: vec![src.clone()],
            dest_dir: dest.clone(),
            conflict: ConflictPolicy::KeepBoth,
        });
        assert_eq!(wait_finished(&w, id).created, vec![dest.join("a (2).txt")]);

        let id = w.submit(FileOp::Copy {
            sources: vec![src.clone()],
            dest_dir: dest.clone(),
            conflict: ConflictPolicy::Skip,
        });
        let o = wait_finished(&w, id);
        assert!(o.created.is_empty());
        assert_eq!(o.errors.len(), 1);
        assert_eq!(read(&dest.join("a.txt")), "old");
        assert!(src.exists());
    }

    #[test]
    fn copy_directory_recursive_and_into_itself() {
        let d = tempfile::tempdir().expect("tmp");
        let dir = d.path().join("dir");
        fs::create_dir_all(dir.join("sub")).expect("mkdir");
        write(&dir.join("sub/f.txt"), "f");
        let out = d.path().join("out");
        fs::create_dir(&out).expect("mkdir");
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::Copy {
            sources: vec![dir.clone()],
            dest_dir: out.clone(),
            conflict: ConflictPolicy::KeepBoth,
        });
        let o = wait_finished(&w, id);
        assert_eq!(o.created, vec![out.join("dir")]);
        assert_eq!(read(&out.join("dir/sub/f.txt")), "f");

        let id = w.submit(FileOp::Copy {
            sources: vec![dir.clone()],
            dest_dir: dir.join("sub"),
            conflict: ConflictPolicy::KeepBoth,
        });
        let o = wait_finished(&w, id);
        assert_eq!(o.errors.len(), 1);
        assert!(o.created.is_empty());
    }

    #[test]
    fn move_across_dirs_keep_both_skip_and_undo_redo() {
        let d = tempfile::tempdir().expect("tmp");
        let a = d.path().join("a");
        let b = d.path().join("b");
        fs::create_dir(&a).expect("mkdir");
        fs::create_dir(&b).expect("mkdir");
        write(&a.join("x.txt"), "x");
        write(&a.join("y.txt"), "y");
        write(&b.join("x.txt"), "existing");
        let w = FileOpsWorker::new();

        let id = w.submit(FileOp::Move {
            sources: vec![a.join("x.txt"), a.join("y.txt")],
            dest_dir: b.clone(),
            conflict: ConflictPolicy::KeepBoth,
        });
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        assert_eq!(
            o.moved,
            vec![
                (a.join("x.txt"), b.join("x (1).txt")),
                (a.join("y.txt"), b.join("y.txt"))
            ]
        );
        assert_eq!(read(&b.join("x.txt")), "existing");
        assert!(!a.join("x.txt").exists());
        assert!(w.can_undo());

        let id = w.undo().expect("undo");
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        assert_eq!(read(&a.join("x.txt")), "x");
        assert_eq!(read(&a.join("y.txt")), "y");
        assert_eq!(read(&b.join("x.txt")), "existing");
        assert!(!w.can_undo());
        assert!(w.can_redo());

        let id = w.redo().expect("redo");
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        assert!(b.join("x (1).txt").exists());
        assert!(w.can_undo());

        // Skip policy
        write(&a.join("z.txt"), "z");
        write(&b.join("z.txt"), "zz");
        let id = w.submit(FileOp::Move {
            sources: vec![a.join("z.txt")],
            dest_dir: b.clone(),
            conflict: ConflictPolicy::Skip,
        });
        let o = wait_finished(&w, id);
        assert!(o.moved.is_empty());
        assert_eq!(o.errors.len(), 1);
        assert_eq!(read(&a.join("z.txt")), "z");
        assert_eq!(read(&b.join("z.txt")), "zz");
    }

    #[test]
    fn rename_refuses_overwrite_and_undoes() {
        let d = tempfile::tempdir().expect("tmp");
        let a = d.path().join("a.txt");
        let b = d.path().join("b.txt");
        write(&a, "a");
        write(&b, "b");
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::Rename {
            from: a.clone(),
            to: b.clone(),
        });
        let o = wait_finished(&w, id);
        assert_eq!(o.errors.len(), 1);
        assert!(o.moved.is_empty());
        assert_eq!(read(&a), "a");
        assert_eq!(read(&b), "b");
        assert!(!w.can_undo());

        let c = d.path().join("c.txt");
        let id = w.submit(FileOp::Rename {
            from: a.clone(),
            to: c.clone(),
        });
        let o = wait_finished(&w, id);
        assert_eq!(o.moved, vec![(a.clone(), c.clone())]);
        let id = w.undo().expect("undo");
        let o = wait_finished(&w, id);
        assert_eq!(o.moved, vec![(c.clone(), a.clone())]);
        assert_eq!(read(&a), "a");

        // Undo is collision-checked: occupy the old name first.
        let id = w.redo().expect("redo");
        wait_finished(&w, id);
        write(&a, "squatter");
        let id = w.undo().expect("undo");
        let o = wait_finished(&w, id);
        assert_eq!(o.errors.len(), 1);
        assert_eq!(read(&a), "squatter");
        assert_eq!(read(&c), "a");
        assert!(w.can_undo(), "failed undo stays available");
    }

    #[test]
    fn case_only_rename_works_single_and_batch() {
        let d = tempfile::tempdir().expect("tmp");
        let a = d.path().join("image.jpg");
        write(&a, "a");
        let upper = d.path().join("IMAGE.JPG");
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::Rename {
            from: a.clone(),
            to: upper.clone(),
        });
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        assert_eq!(o.moved, vec![(a.clone(), upper.clone())]);
        let names: Vec<_> = fs::read_dir(d.path())
            .expect("dir")
            .map(|e| e.expect("entry").file_name())
            .collect();
        assert_eq!(names, vec![std::ffi::OsString::from("IMAGE.JPG")]);
        assert_eq!(read(&upper), "a");

        // Batch (plan + apply) of the case change back, plus an unrelated second file.
        let b = d.path().join("other.jpg");
        write(&b, "b");
        let rule = RenameRule {
            case_mode: crate::rename::CaseMode::Lower,
            scope: crate::rename::RenameScope::NameAndExtension,
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[upper.clone(), b]);
        assert!(plan.is_valid(), "{plan:?}");
        let id = w.submit(FileOp::BatchRename { plan });
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        let mut names: Vec<String> = fs::read_dir(d.path())
            .expect("dir")
            .map(|e| e.expect("entry").file_name().to_string_lossy().into_owned())
            .collect();
        names.sort();
        assert_eq!(
            names,
            vec!["image.jpg".to_string(), "other.jpg".to_string()]
        );
    }

    #[test]
    fn batch_rename_cycle_and_undo() {
        let d = tempfile::tempdir().expect("tmp");
        let one = d.path().join("1.jpg");
        let two = d.path().join("2.jpg");
        write(&one, "one");
        write(&two, "two");
        let rule = RenameRule {
            number_mode: NumberMode::Replace,
            start: 2,
            step: 1,
            padding: 1,
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[one.clone(), two.clone()]);
        assert!(plan.is_valid());
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::BatchRename { plan });
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        assert_eq!(
            o.moved,
            vec![
                (one.clone(), two.clone()),
                (two.clone(), d.path().join("3.jpg"))
            ]
        );
        assert_eq!(read(&two), "one");
        assert_eq!(read(&d.path().join("3.jpg")), "two");
        assert!(!one.exists());
        let leftovers = fs::read_dir(d.path()).expect("dir").count();
        assert_eq!(leftovers, 2);

        let id = w.undo().expect("undo");
        let o = wait_finished(&w, id);
        assert!(o.errors.is_empty(), "{:?}", o.errors);
        assert_eq!(read(&one), "one");
        assert_eq!(read(&two), "two");
        assert!(!d.path().join("3.jpg").exists());
    }

    #[test]
    fn batch_rename_rolls_back_on_failure() {
        let d = tempfile::tempdir().expect("tmp");
        let a = d.path().join("a.txt");
        let b = d.path().join("b.txt");
        write(&a, "a");
        write(&b, "b");
        let rule = RenameRule {
            template: "{name}x".into(),
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[a.clone(), b.clone()]);
        assert!(plan.is_valid());
        // An unrelated file appears after planning: the second target is taken.
        write(&d.path().join("bx.txt"), "squatter");
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::BatchRename { plan });
        let o = wait_finished(&w, id);
        assert_eq!(o.errors.len(), 1);
        assert!(o.moved.is_empty());
        assert_eq!(read(&a), "a");
        assert_eq!(read(&b), "b");
        assert_eq!(read(&d.path().join("bx.txt")), "squatter");
        assert!(!d.path().join("ax.txt").exists());
        assert_eq!(fs::read_dir(d.path()).expect("dir").count(), 3);
    }

    #[test]
    fn invalid_plan_is_refused() {
        let d = tempfile::tempdir().expect("tmp");
        let a = d.path().join("1.jpg");
        write(&a, "a");
        write(&d.path().join("2.jpg"), "b");
        let rule = RenameRule {
            number_mode: NumberMode::Replace,
            start: 2,
            step: 1,
            padding: 1,
            ..Default::default()
        };
        let plan = plan_renames(&rule, std::slice::from_ref(&a));
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::BatchRename { plan });
        let o = wait_finished(&w, id);
        assert_eq!(o.errors.len(), 1);
        assert_eq!(read(&a), "a");
    }

    #[test]
    fn new_folder_undo_only_when_empty() {
        let d = tempfile::tempdir().expect("tmp");
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::NewFolder {
            parent: d.path().to_path_buf(),
            name: "New".into(),
        });
        let o = wait_finished(&w, id);
        assert_eq!(o.created, vec![d.path().join("New")]);
        let id = w.submit(FileOp::NewFolder {
            parent: d.path().to_path_buf(),
            name: "New".into(),
        });
        assert_eq!(wait_finished(&w, id).errors.len(), 1);
        let id = w.submit(FileOp::NewFolder {
            parent: d.path().to_path_buf(),
            name: "a/b".into(),
        });
        assert_eq!(wait_finished(&w, id).errors.len(), 1);

        write(&d.path().join("New/f"), "f");
        let id = w.undo().expect("undo");
        let o = wait_finished(&w, id);
        assert_eq!(o.errors.len(), 1);
        assert!(d.path().join("New").exists());
        fs::remove_file(d.path().join("New/f")).expect("rm");
        let id = w.undo().expect("undo");
        let o = wait_finished(&w, id);
        assert_eq!(o.removed, vec![d.path().join("New")]);
        assert!(!d.path().join("New").exists());
    }

    #[test]
    fn delete_is_not_undoable_and_nothing_to_undo() {
        let d = tempfile::tempdir().expect("tmp");
        let f = d.path().join("f");
        write(&f, "f");
        let w = FileOpsWorker::new();
        assert!(w.undo().is_none());
        let id = w.submit(FileOp::Delete {
            paths: vec![f.clone()],
        });
        let o = wait_finished(&w, id);
        assert_eq!(o.removed, vec![f.clone()]);
        assert!(!f.exists());
        assert!(!w.can_undo());
        assert!(!w.busy());
    }

    #[test]
    fn progress_events_are_emitted() {
        let d = tempfile::tempdir().expect("tmp");
        let f = d.path().join("f");
        write(&f, "f");
        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::Copy {
            sources: vec![f],
            dest_dir: d.path().to_path_buf(),
            conflict: ConflictPolicy::KeepBoth,
        });
        let deadline = Instant::now() + Duration::from_secs(10);
        let mut progress = 0;
        let mut done = false;
        while !done && Instant::now() < deadline {
            for ev in w.poll() {
                match ev {
                    OpEvent::Progress { id: pid, total, .. } => {
                        assert_eq!(pid, id);
                        assert_eq!(total, 1);
                        progress += 1;
                    }
                    OpEvent::Finished(_) => done = true,
                }
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        assert!(done);
        assert!(progress >= 1);
    }

    /// A large copy has to be stoppable while it is inside the file. That is what Qt's dialog calls Cancel
    /// Current, and what makes a 2 GB mistake recoverable instead of something you watch finish.
    #[test]
    fn cancel_current_stops_a_large_copy() {
        let d = tempfile::tempdir().expect("tmp");
        let src = d.path().join("huge.bin");
        fs::write(&src, vec![3u8; 240 << 20]).expect("huge file");
        let dst = d.path().join("out");
        fs::create_dir(&dst).expect("dst");

        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::Copy {
            sources: vec![src.clone()],
            dest_dir: dst.clone(),
            conflict: ConflictPolicy::KeepBoth,
        });
        // Wait until the copy is demonstrably inside the file, then ask it to stop.
        let deadline = Instant::now() + Duration::from_secs(30);
        let mut started = false;
        while !started && Instant::now() < deadline {
            for ev in w.poll() {
                if let OpEvent::Progress {
                    id: pid,
                    bytes_done,
                    ..
                } = ev
                {
                    started |= pid == id && bytes_done > 0;
                }
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        assert!(started, "the copy was under way");
        assert!(w.cancel_current(), "an operation is running to stop");

        let o = wait_finished(&w, id);
        assert!(
            o.errors.iter().any(|e| e.to_string().contains("cancelled")),
            "{:?}",
            o.errors
        );
        // A copy that stopped does not leave half a file where a whole one was expected.
        assert!(
            !dst.join("huge.bin").exists(),
            "the partial copy was removed"
        );
    }

    /// A large copy has to report bytes while it is still inside one file. The app reads these events to say how
    /// much of the weight is across and how fast it is going; a copy that only spoke at the end would show nothing
    /// for the whole time it took to move a 24 MB clip, let alone a 2 GB one.
    #[test]
    fn copy_reports_bytes_as_they_land() {
        let d = tempfile::tempdir().expect("tmp");
        let src = d.path().join("big.bin");
        // Big enough to cross the report throttle more than once.
        fs::write(&src, vec![7u8; 24 << 20]).expect("big file");
        let dst = d.path().join("out");
        fs::create_dir(&dst).expect("dst");

        let w = FileOpsWorker::new();
        let id = w.submit(FileOp::Copy {
            sources: vec![src.clone()],
            dest_dir: dst.clone(),
            conflict: ConflictPolicy::KeepBoth,
        });
        let mut partial = false;
        let mut total_seen = 0u64;
        let deadline = Instant::now() + Duration::from_secs(20);
        while Instant::now() < deadline {
            let mut finished = false;
            for ev in w.poll() {
                match ev {
                    OpEvent::Progress {
                        id: pid,
                        bytes_done,
                        bytes_total,
                        ..
                    } if pid == id => {
                        total_seen = total_seen.max(bytes_total);
                        partial |= bytes_done > 0 && bytes_done < bytes_total;
                    }
                    OpEvent::Finished(_) => finished = true,
                    // Another operation's progress, which this test is not watching.
                    _ => {}
                }
            }
            if finished {
                break;
            }
            std::thread::sleep(Duration::from_millis(5));
        }

        assert_eq!(
            total_seen,
            24 << 20,
            "the copy knows what the file weighs before it starts"
        );
        assert!(
            partial,
            "bytes arrive while the copy is still inside the file"
        );
    }
}
