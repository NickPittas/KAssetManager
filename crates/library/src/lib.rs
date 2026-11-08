//! Organization metadata (tags = catalog keywords, collections = catalog albums, ratings 0–5) keyed by
//! `Entry::path`, persisted with `lightcraft-catalog` (journal + snapshots, crash-safe) under
//! `AppDirs::library_dir()`. Files enter the catalog only when annotated. Path changes from file ops
//! MUST be applied with [`Library::relink`]; deletions with [`Library::forget`].
//!
//! Model mapping (everything lives in the catalog; there is no sidecar):
//! - a file is a catalog `Photo` with `Source::File { path }` (kind `Video` for video extensions);
//! - tags are flat keywords (`Photo::meta.keywords`) and are also *listed* (`Op::SetKeyword`) so a tag
//!   survives losing its last file; `|` (the catalog's hierarchy separator) is not allowed in tag names;
//! - collections are flat, manual albums (the catalog's Quick Collection, folders and smart albums are
//!   never reported);
//! - the rating is `Photo::rating`.
//!
//! Every mutation is a single catalog `Op` (a `Batch` when it has several steps) that is applied and then
//! appended to the journal, so it is atomic and crash-safe.

mod legacy;

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

use gallerybrowser_core::sequence::split_numbered;
use gallerybrowser_core::{Entry, MediaKind as CoreKind, classify};
use lightcraft_catalog::keywords::{self, KeywordInfo};
use lightcraft_catalog::{
    Album, AlbumId, Catalog, CatalogError, FsStore, Journal, LibraryLock, MediaKind as CatKind, Op,
    Photo, PhotoId, Source,
};

#[derive(Debug, thiserror::Error)]
pub enum LibraryError {
    #[error("catalog error: {0}")]
    Catalog(String),
    #[error("I/O error: {0}")]
    Io(String),
    #[error("not found: {0}")]
    NotFound(String),
    #[error("already exists: {0}")]
    Exists(String),
    #[error("legacy import failed: {0}")]
    Legacy(String),
}

pub type Result<T, E = LibraryError> = std::result::Result<T, E>;

fn cat(e: CatalogError) -> LibraryError {
    LibraryError::Catalog(e.to_string())
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct NamedCount {
    pub name: String,
    /// Number of files carrying it.
    pub count: usize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RatingFilter {
    Exactly(u8),
    AtLeast(u8),
    /// Rating 0 or not in the catalog at all.
    Unrated,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct LibraryQuery {
    /// Files must carry ALL of these tags.
    pub tags: Vec<String>,
    pub collection: Option<String>,
    pub rating: Option<RatingFilter>,
}

impl LibraryQuery {
    pub fn is_empty(&self) -> bool {
        self.tags.is_empty() && self.collection.is_none() && self.rating.is_none()
    }
}

/// Annotations of one path.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Annotations {
    pub rating: u8,
    pub tags: Vec<String>,
    pub collections: Vec<String>,
}

pub struct Library {
    catalog: Catalog,
    /// `None` for [`Library::in_memory`].
    journal: Option<Journal>,
    /// Held for as long as the library is open (one process per catalog directory).
    _lock: Option<LibraryLock>,
}

// ---------------------------------------------------------------------------------------------
// helpers

/// Trim a user-supplied name; empty names are rejected.
fn clean_name(raw: &str, what: &str, allow_bar: bool) -> Result<String> {
    let name = raw.trim();
    if name.is_empty() {
        return Err(LibraryError::Catalog(format!(
            "{what} name cannot be empty"
        )));
    }
    if !allow_bar && name.contains(keywords::SEP) {
        return Err(LibraryError::Catalog(format!(
            "{what} names cannot contain '{}'",
            keywords::SEP
        )));
    }
    Ok(name.to_string())
}

fn same(a: &str, b: &str) -> bool {
    keywords::same(a, b)
}

fn photo_path(p: &Photo) -> Option<&str> {
    match &p.source {
        Source::File { path } => Some(path.as_str()),
        Source::Demo { .. } => None,
    }
}

fn is_collection(a: &Album) -> bool {
    !a.folder && a.smart.is_none() && !a.quick
}

/// ISO 8601 UTC "now" (the catalog stores the import time as text).
fn iso_now() -> String {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| i64::try_from(d.as_secs()).unwrap_or(0))
        .unwrap_or(0);
    let days = secs.div_euclid(86_400);
    let rem = secs.rem_euclid(86_400);
    // Howard Hinnant's civil-from-days.
    let z = days.saturating_add(719_468);
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = yoe + era * 400 + i64::from(m <= 2);
    format!(
        "{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z",
        rem / 3600,
        rem % 3600 / 60,
        rem % 60
    )
}

/// A new catalog record for an annotated file.
fn new_photo(id: PhotoId, path: &str) -> Photo {
    let p = Path::new(path);
    let name = p
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| path.to_string());
    let format = p
        .extension()
        .map(|e| e.to_string_lossy().to_lowercase())
        .unwrap_or_default();
    let mut photo = Photo::new(
        id,
        Source::File {
            path: path.to_string(),
        },
        &name,
        &format,
        0,
        0,
        &iso_now(),
    );
    if classify(p) == Some(CoreKind::Video) {
        photo.kind = CatKind::Video;
    }
    photo
}

/// Index in `albums` of the legacy collection `raw` (created on first use); `None` for blank names.
fn legacy_album(
    catalog: &mut Catalog,
    albums: &mut Vec<(String, AlbumId, Vec<PhotoId>)>,
    raw: &str,
    ops: &mut Vec<Op>,
) -> Option<usize> {
    let name = raw.trim();
    if name.is_empty() {
        return None;
    }
    if let Some(i) = albums.iter().position(|(n, _, _)| same(n, name)) {
        return Some(i);
    }
    let id = catalog.alloc_album_id();
    ops.push(Op::AddAlbum {
        album: Album::new(id, name),
    });
    albums.push((name.to_string(), id, Vec::new()));
    Some(albums.len().saturating_sub(1))
}

/// A numbered-sequence identity: `prefix` + `#`×`hashes` + `suffix`.
struct Pattern {
    prefix: String,
    hashes: usize,
    suffix: String,
}

impl Pattern {
    fn parse(name: &str) -> Option<Pattern> {
        let start = name.find('#')?;
        let hashes = name.get(start..)?.chars().take_while(|c| *c == '#').count();
        Some(Pattern {
            prefix: name.get(..start)?.to_string(),
            hashes,
            suffix: name.get(start.checked_add(hashes)?..)?.to_string(),
        })
    }

    /// Whether `name` is one frame of this sequence.
    fn matches(&self, name: &str) -> bool {
        let Some(rest) = name
            .strip_prefix(&self.prefix)
            .and_then(|r| r.strip_suffix(&self.suffix))
        else {
            return false;
        };
        !rest.is_empty() && rest.len() >= self.hashes && rest.bytes().all(|b| b.is_ascii_digit())
    }
}

fn file_name_str(p: &Path) -> Option<&str> {
    p.file_name().and_then(|n| n.to_str())
}

/// A sequence pattern path counts as existing while its directory does (the pattern is not a file).
fn path_exists(p: &Path) -> bool {
    if p.exists() {
        return true;
    }
    file_name_str(p).is_some_and(|n| n.contains('#')) && p.parent().is_some_and(Path::is_dir)
}

/// Working copy of a photo being edited; turned into ops by [`work_ops`].
struct Work {
    photo: Photo,
    /// `None`: not in the catalog yet.
    original: Option<Arc<Photo>>,
}

fn work_ops(works: Vec<Work>) -> Vec<Op> {
    let mut ops = Vec::new();
    for w in works {
        match w.original {
            None => ops.push(Op::AddPhoto {
                photo: Box::new(w.photo),
            }),
            Some(orig) => {
                if orig.rating != w.photo.rating {
                    ops.push(Op::SetRating {
                        id: w.photo.id,
                        rating: w.photo.rating,
                    });
                }
                if orig.meta.keywords != w.photo.meta.keywords {
                    ops.push(Op::SetMeta {
                        id: w.photo.id,
                        meta: Box::new(w.photo.meta),
                    });
                }
            }
        }
    }
    ops
}

/// Query evaluation against catalog photos.
struct Matcher<'a> {
    q: &'a LibraryQuery,
    tags: Vec<String>,
    /// `Some`: members of the requested collection.
    members: Option<HashSet<PhotoId>>,
    /// The requested collection does not exist: nothing matches.
    impossible: bool,
}

impl Matcher<'_> {
    fn matches(&self, photo: Option<&Photo>) -> bool {
        if self.impossible {
            return false;
        }
        if let Some(r) = self.q.rating {
            let rating = photo.map_or(0, |p| p.rating);
            let ok = match r {
                RatingFilter::Unrated | RatingFilter::Exactly(0) => rating == 0,
                RatingFilter::Exactly(n) => rating == n,
                RatingFilter::AtLeast(n) => rating >= n,
            };
            if !ok {
                return false;
            }
        }
        if self.tags.is_empty() && self.members.is_none() {
            return true;
        }
        let Some(p) = photo else { return false };
        if !self
            .tags
            .iter()
            .all(|t| p.meta.keywords.iter().any(|k| same(k, t)))
        {
            return false;
        }
        self.members.as_ref().is_none_or(|m| m.contains(&p.id))
    }
}

// ---------------------------------------------------------------------------------------------

impl Library {
    /// Open or create the catalog in `dir`. If `legacy_sqlite` is given and the catalog is new,
    /// import tags/collections/ratings from the Python-era SQLite DB once.
    pub fn open(dir: &Path, legacy_sqlite: Option<&Path>) -> Result<Self> {
        std::fs::create_dir_all(dir)
            .map_err(|e| LibraryError::Io(format!("{}: {e}", dir.display())))?;
        let lock = LibraryLock::acquire(dir, "gallerybrowser")
            .map_err(|e| LibraryError::Catalog(e.to_string()))?;
        let store =
            FsStore::open(dir).map_err(|e| LibraryError::Io(format!("{}: {e}", dir.display())))?;
        let (journal, catalog, report) = Journal::open(Box::new(store)).map_err(cat)?;
        let mut lib = Library {
            catalog,
            journal: Some(journal),
            _lock: Some(lock),
        };
        if report.created
            && let Some(db) = legacy_sqlite.filter(|p| p.is_file())
        {
            lib.import_legacy(db)?;
        }
        Ok(lib)
    }

    /// In-memory catalog (tests, and fallback when the on-disk catalog cannot be opened).
    pub fn in_memory() -> Self {
        Self {
            catalog: Catalog::new(),
            journal: None,
            _lock: None,
        }
    }

    fn import_legacy(&mut self, db: &Path) -> Result<()> {
        let data = legacy::read(db)?;
        let mut ops: Vec<Op> = Vec::new();

        let mut tag_names: HashMap<String, String> = HashMap::new();
        let mut canon_tag = |raw: &str| -> Option<String> {
            let name = raw.trim().replace(keywords::SEP, "/");
            let name = name.trim();
            if name.is_empty() {
                return None;
            }
            Some(
                tag_names
                    .entry(name.to_lowercase())
                    .or_insert_with(|| name.to_string())
                    .clone(),
            )
        };
        let mut listed: Vec<String> = Vec::new();
        for raw in &data.tags {
            if let Some(n) = canon_tag(raw)
                && !listed.iter().any(|l| same(l, &n))
            {
                listed.push(n);
            }
        }

        let mut albums: Vec<(String, AlbumId, Vec<PhotoId>)> = Vec::new();
        for raw in &data.collections {
            legacy_album(&mut self.catalog, &mut albums, raw, &mut ops);
        }

        let mut seen_paths: HashSet<String> = HashSet::new();
        let mut photo_ops: Vec<Op> = Vec::new();
        for f in &data.files {
            if f.path.trim().is_empty() || !seen_paths.insert(f.path.clone()) {
                continue;
            }
            let mut tags: Vec<String> = Vec::new();
            for t in &f.tags {
                if let Some(n) = canon_tag(t) {
                    if !listed.iter().any(|l| same(l, &n)) {
                        listed.push(n.clone());
                    }
                    if !tags.iter().any(|x| same(x, &n)) {
                        tags.push(n);
                    }
                }
            }
            let colls: Vec<usize> = f
                .collections
                .iter()
                .filter_map(|c| legacy_album(&mut self.catalog, &mut albums, c, &mut ops))
                .collect();
            if f.rating == 0 && tags.is_empty() && colls.is_empty() {
                continue;
            }
            let mut photo = new_photo(self.catalog.alloc_photo_id(), &f.path);
            photo.rating = f.rating.min(5);
            photo.meta.keywords = tags;
            for i in colls {
                if let Some((_, _, members)) = albums.get_mut(i)
                    && !members.contains(&photo.id)
                {
                    members.push(photo.id);
                }
            }
            photo_ops.push(Op::AddPhoto {
                photo: Box::new(photo),
            });
        }

        let mut all: Vec<Op> = listed
            .into_iter()
            .map(|n| Op::SetKeyword {
                path: n,
                info: Some(KeywordInfo::default()),
            })
            .collect();
        all.extend(ops);
        all.extend(photo_ops);
        all.extend(
            albums
                .into_iter()
                .filter(|(_, _, m)| !m.is_empty())
                .map(|(_, id, photos)| Op::SetAlbumPhotos { id, photos }),
        );
        if all.is_empty() {
            return Ok(());
        }
        self.catalog
            .apply(Op::Batch { ops: all })
            .map_err(|e| LibraryError::Legacy(e.to_string()))?;
        if let Some(j) = self.journal.as_mut() {
            // One snapshot instead of a huge log record; if it fails the import runs again next open.
            j.snapshot(&self.catalog)
                .map_err(|e| LibraryError::Legacy(format!("saving the imported catalog: {e}")))?;
        }
        Ok(())
    }

    /// Apply ops to the live catalog as one atomic step and make them durable.
    fn commit(&mut self, mut ops: Vec<Op>) -> Result<()> {
        let op = match ops.len() {
            0 => return Ok(()),
            1 => match ops.pop() {
                Some(op) => op,
                None => return Ok(()),
            },
            _ => Op::Batch { ops },
        };
        self.catalog.apply(op.clone()).map_err(cat)?;
        let Some(j) = self.journal.as_mut() else {
            return Ok(());
        };
        match j.append(std::slice::from_ref(&op)) {
            Ok(()) => {
                if j.wants_snapshot()
                    && let Err(e) = j.snapshot(&self.catalog)
                {
                    log::warn!(
                        "library: snapshot failed (the journal still holds every change): {e}"
                    );
                }
                Ok(())
            }
            Err(e) => {
                // The change is in memory only; a snapshot (which counts it) makes it durable.
                log::warn!("library: journal append failed ({e}); writing a snapshot instead");
                j.snapshot_with_unlogged(&self.catalog, 1)
                    .map_err(|e2| LibraryError::Io(format!("saving the library: {e2}")))
            }
        }
    }

    // ---- lookups

    fn photo_map(&self) -> HashMap<&str, &Arc<Photo>> {
        self.catalog
            .photos()
            .filter_map(|p| photo_path(p).map(|s| (s, p)))
            .collect()
    }

    fn find_album(&self, name: &str) -> Option<&Album> {
        self.catalog
            .albums()
            .find(|a| is_collection(a) && same(&a.name, name.trim()))
    }

    fn listed_tag(&self, name: &str) -> Option<String> {
        self.catalog
            .listed_keywords()
            .find(|l| same(&l.path, name))
            .map(|l| l.path.clone())
    }

    /// The tag's stored spelling (case-insensitive match), if it exists at all.
    fn canonical_tag(&self, name: &str) -> Option<String> {
        self.listed_tag(name).or_else(|| {
            self.catalog
                .photos()
                .flat_map(|p| p.meta.keywords.iter())
                .find(|k| same(k, name))
                .cloned()
        })
    }

    /// Working copies of the photos behind `paths` (deduplicated). Unknown paths get a new record
    /// when `create` is set, and are skipped otherwise.
    fn work_set(&mut self, paths: &[PathBuf], create: bool) -> Result<Vec<Work>> {
        let mut seen: HashSet<&str> = HashSet::new();
        let mut keys: Vec<&str> = Vec::new();
        for p in paths {
            let s = p.to_str().ok_or_else(|| {
                LibraryError::Catalog(format!("path is not valid UTF-8: {}", p.display()))
            })?;
            if seen.insert(s) {
                keys.push(s);
            }
        }
        let known: HashMap<String, Arc<Photo>> = self
            .catalog
            .photos()
            .filter_map(|p| {
                photo_path(p)
                    .filter(|s| seen.contains(s))
                    .map(|s| (s.to_string(), p.clone()))
            })
            .collect();
        let mut out = Vec::with_capacity(keys.len());
        for k in keys {
            match known.get(k) {
                Some(p) => out.push(Work {
                    photo: Photo::clone(p),
                    original: Some(p.clone()),
                }),
                None if create => out.push(Work {
                    photo: new_photo(self.catalog.alloc_photo_id(), k),
                    original: None,
                }),
                None => {}
            }
        }
        Ok(out)
    }

    // ---- tags

    pub fn tags(&self) -> Vec<NamedCount> {
        let mut map: HashMap<String, NamedCount> = HashMap::new();
        for l in self.catalog.listed_keywords() {
            map.entry(l.path.to_lowercase())
                .or_insert_with(|| NamedCount {
                    name: l.path.clone(),
                    count: 0,
                });
        }
        for p in self.catalog.photos() {
            let mut once: HashSet<String> = HashSet::new();
            for k in &p.meta.keywords {
                let lower = k.to_lowercase();
                if once.insert(lower.clone()) {
                    map.entry(lower)
                        .or_insert_with(|| NamedCount {
                            name: k.clone(),
                            count: 0,
                        })
                        .count += 1;
                }
            }
        }
        let mut v: Vec<NamedCount> = map.into_values().collect();
        v.sort_by_cached_key(|n| (n.name.to_lowercase(), n.name.clone()));
        v
    }

    pub fn create_tag(&mut self, name: &str) -> Result<()> {
        let name = clean_name(name, "tag", false)?;
        if self.canonical_tag(&name).is_some() {
            return Err(LibraryError::Exists(name));
        }
        self.commit(vec![Op::SetKeyword {
            path: name,
            info: Some(KeywordInfo::default()),
        }])
    }

    pub fn rename_tag(&mut self, old: &str, new: &str) -> Result<()> {
        let new = clean_name(new, "tag", false)?;
        let old = self
            .canonical_tag(old.trim())
            .ok_or_else(|| LibraryError::NotFound(old.trim().to_string()))?;
        if old == new {
            return Ok(());
        }
        if !same(&old, &new) && self.canonical_tag(&new).is_some() {
            return Err(LibraryError::Exists(new));
        }
        let op = self.catalog.rename_keyword_ops(&old, &new).map_err(cat)?;
        self.commit(vec![op])
    }

    pub fn delete_tag(&mut self, name: &str) -> Result<()> {
        let tag = self
            .canonical_tag(name.trim())
            .ok_or_else(|| LibraryError::NotFound(name.trim().to_string()))?;
        let op = self.catalog.delete_keyword_ops(&tag).map_err(cat)?;
        self.commit(vec![op])
    }

    /// Adds `tag` (created when missing) to every path; unknown files enter the catalog.
    pub fn add_tag(&mut self, paths: &[PathBuf], tag: &str) -> Result<()> {
        let name = clean_name(tag, "tag", false)?;
        let canon = self.canonical_tag(&name).unwrap_or(name);
        let mut ops = Vec::new();
        if self.listed_tag(&canon).is_none() {
            ops.push(Op::SetKeyword {
                path: canon.clone(),
                info: Some(KeywordInfo::default()),
            });
        }
        let mut works = self.work_set(paths, true)?;
        for w in &mut works {
            if !w.photo.meta.keywords.iter().any(|k| same(k, &canon)) {
                w.photo.meta.keywords.push(canon.clone());
            }
        }
        ops.extend(work_ops(works));
        self.commit(ops)
    }

    pub fn remove_tag(&mut self, paths: &[PathBuf], tag: &str) -> Result<()> {
        let name = clean_name(tag, "tag", false)?;
        let mut works = self.work_set(paths, false)?;
        for w in &mut works {
            w.photo.meta.keywords.retain(|k| !same(k, &name));
        }
        self.commit(work_ops(works))
    }

    // ---- collections

    pub fn collections(&self) -> Vec<NamedCount> {
        let mut v: Vec<NamedCount> = self
            .catalog
            .albums()
            .filter(|a| is_collection(a))
            .map(|a| NamedCount {
                name: a.name.clone(),
                count: a.photos.len(),
            })
            .collect();
        v.sort_by_cached_key(|n| (n.name.to_lowercase(), n.name.clone()));
        v
    }

    pub fn create_collection(&mut self, name: &str) -> Result<()> {
        let name = clean_name(name, "collection", true)?;
        if self.find_album(&name).is_some() {
            return Err(LibraryError::Exists(name));
        }
        let id = self.catalog.alloc_album_id();
        self.commit(vec![Op::AddAlbum {
            album: Album::new(id, name),
        }])
    }

    pub fn rename_collection(&mut self, old: &str, new: &str) -> Result<()> {
        let new = clean_name(new, "collection", true)?;
        let album = self
            .find_album(old)
            .ok_or_else(|| LibraryError::NotFound(old.trim().to_string()))?;
        let id = album.id;
        if album.name == new {
            return Ok(());
        }
        if self.find_album(&new).is_some_and(|other| other.id != id) {
            return Err(LibraryError::Exists(new));
        }
        self.commit(vec![Op::RenameAlbum { id, name: new }])
    }

    pub fn delete_collection(&mut self, name: &str) -> Result<()> {
        let id = self
            .find_album(name)
            .ok_or_else(|| LibraryError::NotFound(name.trim().to_string()))?
            .id;
        self.commit(vec![Op::RemoveAlbum { id }])
    }

    /// Adds every path to the collection (created when missing); unknown files enter the catalog.
    pub fn add_to_collection(&mut self, paths: &[PathBuf], name: &str) -> Result<()> {
        let name = clean_name(name, "collection", true)?;
        let mut ops = Vec::new();
        let (id, mut members) = match self.find_album(&name) {
            Some(a) => (a.id, a.photos.clone()),
            None => {
                let id = self.catalog.alloc_album_id();
                ops.push(Op::AddAlbum {
                    album: Album::new(id, name),
                });
                (id, Vec::new())
            }
        };
        let works = self.work_set(paths, true)?;
        let have: HashSet<PhotoId> = members.iter().copied().collect();
        let mut changed = !ops.is_empty();
        for w in &works {
            if !have.contains(&w.photo.id) {
                members.push(w.photo.id);
                changed = true;
            }
        }
        if !changed {
            return Ok(());
        }
        ops.extend(work_ops(works));
        ops.push(Op::SetAlbumPhotos {
            id,
            photos: members,
        });
        self.commit(ops)
    }

    pub fn remove_from_collection(&mut self, paths: &[PathBuf], name: &str) -> Result<()> {
        let album = self
            .find_album(name)
            .ok_or_else(|| LibraryError::NotFound(name.trim().to_string()))?;
        let (id, members) = (album.id, album.photos.clone());
        let gone: HashSet<PhotoId> = self
            .work_set(paths, false)?
            .into_iter()
            .map(|w| w.photo.id)
            .collect();
        let kept: Vec<PhotoId> = members
            .iter()
            .copied()
            .filter(|p| !gone.contains(p))
            .collect();
        if kept.len() == members.len() {
            return Ok(());
        }
        self.commit(vec![Op::SetAlbumPhotos { id, photos: kept }])
    }

    // ---- ratings

    /// 0 clears the rating. Values > 5 are an error.
    pub fn set_rating(&mut self, paths: &[PathBuf], rating: u8) -> Result<()> {
        if rating > 5 {
            return Err(LibraryError::Catalog(format!(
                "rating must be 0..=5, got {rating}"
            )));
        }
        let mut works = self.work_set(paths, rating > 0)?;
        for w in &mut works {
            w.photo.rating = rating;
        }
        self.commit(work_ops(works))
    }

    pub fn annotations(&self, path: &Path) -> Annotations {
        let Some(key) = path.to_str() else {
            return Annotations::default();
        };
        let Some(photo) = self.catalog.photos().find(|p| photo_path(p) == Some(key)) else {
            return Annotations::default();
        };
        let mut collections: Vec<String> = self
            .catalog
            .albums()
            .filter(|a| is_collection(a) && a.photos.contains(&photo.id))
            .map(|a| a.name.clone())
            .collect();
        collections.sort_by_cached_key(|n| n.to_lowercase());
        Annotations {
            rating: photo.rating,
            tags: photo.meta.keywords.clone(),
            collections,
        }
    }

    // ---- queries

    fn matcher<'a>(&self, q: &'a LibraryQuery) -> Matcher<'a> {
        let (members, impossible) = match &q.collection {
            None => (None, false),
            Some(name) => match self.find_album(name) {
                Some(a) => (Some(a.photos.iter().copied().collect()), false),
                None => (None, true),
            },
        };
        Matcher {
            q,
            tags: q.tags.iter().map(|t| t.trim().to_string()).collect(),
            members,
            impossible,
        }
    }

    /// Library scope: every catalog path matching `q` that still exists on disk.
    pub fn query(&self, q: &LibraryQuery) -> Vec<PathBuf> {
        let m = self.matcher(q);
        let mut out: Vec<PathBuf> = self
            .catalog
            .photos()
            .filter(|p| m.matches(Some(p)))
            .filter_map(|p| photo_path(p))
            .map(PathBuf::from)
            .filter(|p| path_exists(p))
            .collect();
        out.sort();
        out
    }

    /// Folder scope: keep scanned entries matching `q` (Unrated keeps entries absent from the catalog).
    pub fn filter_entries(&self, entries: Vec<Entry>, q: &LibraryQuery) -> Vec<Entry> {
        if q.is_empty() {
            return entries;
        }
        let m = self.matcher(q);
        let map = self.photo_map();
        entries
            .into_iter()
            .filter(|e| m.matches(e.path.to_str().and_then(|k| map.get(k)).map(|p| p.as_ref())))
            .collect()
    }

    // ---- path maintenance

    /// Apply path changes (files and directories: a moved directory relinks everything below it).
    pub fn relink(&mut self, moved: &[(PathBuf, PathBuf)]) -> Result<()> {
        let exact: HashMap<&Path, &Path> = moved
            .iter()
            .filter(|(a, b)| a != b)
            .map(|(a, b)| (a.as_path(), b.as_path()))
            .collect();
        if exact.is_empty() {
            return Ok(());
        }
        // Frames of numbered sequences, by directory: renaming frames moves the sequence's pattern identity.
        let mut frames_by_dir: HashMap<&Path, Vec<(&Path, &Path)>> = HashMap::new();
        for (from, to) in &exact {
            if let Some(dir) = from.parent() {
                frames_by_dir.entry(dir).or_default().push((from, to));
            }
        }

        let mut changes: Vec<(PhotoId, PathBuf)> = Vec::new();
        for photo in self.catalog.photos() {
            let Some(path) = photo_path(photo).map(Path::new) else {
                continue;
            };
            // Nearest moved ancestor (the path itself first): a moved directory carries everything below it.
            let by_ancestor = path.ancestors().find_map(|a| {
                let to = exact.get(a)?;
                let rel = path.strip_prefix(a).ok()?;
                Some(if rel.as_os_str().is_empty() {
                    to.to_path_buf()
                } else {
                    to.join(rel)
                })
            });
            let new = by_ancestor.or_else(|| sequence_target(path, &frames_by_dir));
            if let Some(n) = new.filter(|n| n != path) {
                changes.push((photo.id, n));
            }
        }
        if changes.is_empty() {
            return Ok(());
        }

        let moving: HashSet<PhotoId> = changes.iter().map(|(id, _)| *id).collect();
        let mut occupied: HashSet<String> = self
            .catalog
            .photos()
            .filter(|p| !moving.contains(&p.id))
            .filter_map(|p| photo_path(p).map(str::to_string))
            .collect();
        let mut ops = Vec::new();
        let mut dropped: Vec<PhotoId> = Vec::new();
        for (id, new) in changes {
            // A destination that already carries annotations keeps them; a path that can't be stored cannot be tracked.
            match new.to_str() {
                Some(s) if occupied.insert(s.to_string()) => {
                    let name = file_name_str(&new)
                        .map(str::to_string)
                        .unwrap_or_else(|| s.to_string());
                    ops.push(Op::Relink {
                        id,
                        file_name: name,
                        source: Source::File {
                            path: s.to_string(),
                        },
                        format: None,
                    });
                }
                _ => dropped.push(id),
            }
        }
        if !dropped.is_empty() {
            ops.push(self.catalog.delete_photos_permanently_ops(&dropped));
        }
        self.commit(ops)
    }

    /// Drop annotations of permanently removed/trashed paths (and everything below removed directories).
    pub fn forget(&mut self, removed: &[PathBuf]) -> Result<()> {
        if removed.is_empty() {
            return Ok(());
        }
        let exact: HashSet<&Path> = removed.iter().map(PathBuf::as_path).collect();
        let mut frames_by_dir: HashMap<&Path, Vec<&Path>> = HashMap::new();
        for r in removed {
            if let Some(dir) = r.parent() {
                frames_by_dir.entry(dir).or_default().push(r);
            }
        }
        let ids: Vec<PhotoId> = self
            .catalog
            .photos()
            .filter(|photo| {
                let Some(path) = photo_path(photo).map(Path::new) else {
                    return false;
                };
                if path.ancestors().any(|a| exact.contains(a)) {
                    return true;
                }
                // a sequence pattern goes when any of its frames is removed
                let pattern = file_name_str(path).and_then(Pattern::parse);
                match (pattern, path.parent().and_then(|d| frames_by_dir.get(d))) {
                    (Some(pat), Some(frames)) => frames
                        .iter()
                        .any(|f| file_name_str(f).is_some_and(|n| pat.matches(n))),
                    _ => false,
                }
            })
            .map(|p| p.id)
            .collect();
        if ids.is_empty() {
            return Ok(());
        }
        let op = self.catalog.delete_photos_permanently_ops(&ids);
        self.commit(vec![op])
    }
}

/// New pattern path of the sequence `path` when some of its frames were renamed/moved.
fn sequence_target(
    path: &Path,
    frames_by_dir: &HashMap<&Path, Vec<(&Path, &Path)>>,
) -> Option<PathBuf> {
    let pattern = Pattern::parse(file_name_str(path)?)?;
    let frames = frames_by_dir.get(path.parent()?)?;
    let (from, to) = frames
        .iter()
        .find(|(f, _)| file_name_str(f).is_some_and(|n| pattern.matches(n)))?;
    let (_, from_digits, _) = split_numbered(file_name_str(from)?)?;
    let (prefix, to_digits, suffix) = split_numbered(file_name_str(to)?)?;
    let hashes = if to_digits.len() == from_digits.len() {
        pattern.hashes
    } else {
        to_digits.len().max(1)
    };
    Some(
        to.parent()?
            .join(format!("{prefix}{}{suffix}", "#".repeat(hashes))),
    )
}

#[cfg(test)]
#[allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
mod tests;
