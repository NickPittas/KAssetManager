use std::fs;
use std::path::{Path, PathBuf};

use gallerybrowser_core::{Entry, MediaKind};

use super::*;

/// `dir` joined with a `/`-separated relative path, one component at a time so every separator is
/// the platform's (the library keys annotations by exact path string).
fn at(dir: &Path, rel: &str) -> PathBuf {
    rel.split('/').fold(dir.to_path_buf(), |p, c| p.join(c))
}

fn touch(dir: &Path, name: &str) -> PathBuf {
    let p = at(dir, name);
    if let Some(parent) = p.parent() {
        fs::create_dir_all(parent).unwrap();
    }
    fs::write(&p, b"x").unwrap();
    p
}

fn entry(p: &Path) -> Entry {
    Entry {
        path: p.to_path_buf(),
        name: p.file_name().unwrap().to_string_lossy().into_owned(),
        kind: MediaKind::Image,
        size: 1,
        modified: None,
        sequence: None,
    }
}

fn names(v: &[NamedCount]) -> Vec<(&str, usize)> {
    v.iter().map(|n| (n.name.as_str(), n.count)).collect()
}

#[test]
fn persists_across_reopen() {
    let tmp = tempfile::tempdir().unwrap();
    let lib_dir = tmp.path().join("lib");
    let a = touch(tmp.path(), "a.mov");
    let b = touch(tmp.path(), "b.png");
    {
        let mut lib = Library::open(&lib_dir, None).unwrap();
        lib.add_tag(&[a.clone(), b.clone()], "Hero").unwrap();
        lib.create_tag("empty").unwrap();
        lib.add_to_collection(std::slice::from_ref(&a), "Reel")
            .unwrap();
        lib.create_collection("Unused").unwrap();
        lib.set_rating(std::slice::from_ref(&a), 4).unwrap();
        // a second process/instance on the same directory is refused
        assert!(Library::open(&lib_dir, None).is_err());
    }
    let lib = Library::open(&lib_dir, None).unwrap();
    assert_eq!(names(&lib.tags()), vec![("empty", 0), ("Hero", 2)]);
    assert_eq!(names(&lib.collections()), vec![("Reel", 1), ("Unused", 0)]);
    let ann = lib.annotations(&a);
    assert_eq!(
        ann,
        Annotations {
            rating: 4,
            tags: vec!["Hero".into()],
            collections: vec!["Reel".into()]
        }
    );
    assert_eq!(lib.annotations(&b).rating, 0);
}

#[test]
fn tag_crud_trims_and_is_case_insensitive() {
    let tmp = tempfile::tempdir().unwrap();
    let a = touch(tmp.path(), "a.png");
    let b = touch(tmp.path(), "b.png");
    let mut lib = Library::in_memory();

    assert!(lib.create_tag("   ").is_err());
    assert!(lib.create_tag("a|b").is_err());
    lib.create_tag("  Hero ").unwrap();
    assert!(matches!(
        lib.create_tag("hero"),
        Err(LibraryError::Exists(_))
    ));
    // adding with another case reuses the stored spelling
    lib.add_tag(std::slice::from_ref(&a), "HERO").unwrap();
    lib.add_tag(&[a.clone(), b.clone()], " hero").unwrap();
    assert_eq!(names(&lib.tags()), vec![("Hero", 2)]);
    assert_eq!(lib.annotations(&a).tags, vec!["Hero".to_string()]);

    lib.add_tag(std::slice::from_ref(&b), "Other").unwrap();
    assert!(matches!(
        lib.rename_tag("Other", "hero"),
        Err(LibraryError::Exists(_))
    ));
    assert!(matches!(
        lib.rename_tag("nope", "x"),
        Err(LibraryError::NotFound(_))
    ));
    lib.rename_tag("Other", "Villain").unwrap();
    assert_eq!(
        lib.annotations(&b).tags,
        vec!["Hero".to_string(), "Villain".to_string()]
    );
    // case-only rename is allowed
    lib.rename_tag("Villain", "VILLAIN").unwrap();
    assert_eq!(names(&lib.tags()), vec![("Hero", 2), ("VILLAIN", 1)]);

    lib.remove_tag(&[a.clone(), b.clone()], "hero").unwrap();
    // the tag survives its last file
    assert_eq!(names(&lib.tags()), vec![("Hero", 0), ("VILLAIN", 1)]);
    lib.delete_tag("hero").unwrap();
    assert_eq!(names(&lib.tags()), vec![("VILLAIN", 1)]);
    lib.delete_tag("VILLAIN").unwrap();
    assert!(lib.tags().is_empty());
    assert_eq!(lib.annotations(&b), Annotations::default());
    assert!(matches!(
        lib.delete_tag("VILLAIN"),
        Err(LibraryError::NotFound(_))
    ));
}

#[test]
fn collection_crud_and_counts() {
    let tmp = tempfile::tempdir().unwrap();
    let a = touch(tmp.path(), "a.png");
    let b = touch(tmp.path(), "b.png");
    let mut lib = Library::in_memory();
    assert!(lib.create_collection("").is_err());
    lib.create_collection(" Shots ").unwrap();
    assert!(matches!(
        lib.create_collection("shots"),
        Err(LibraryError::Exists(_))
    ));
    lib.add_to_collection(&[a.clone(), b.clone(), a.clone()], "SHOTS")
        .unwrap();
    lib.add_to_collection(std::slice::from_ref(&a), "shots")
        .unwrap();
    assert_eq!(names(&lib.collections()), vec![("Shots", 2)]);
    lib.create_collection("Alt").unwrap();
    assert!(matches!(
        lib.rename_collection("Alt", "shots"),
        Err(LibraryError::Exists(_))
    ));
    lib.rename_collection("Alt", "Zed").unwrap();
    assert_eq!(names(&lib.collections()), vec![("Shots", 2), ("Zed", 0)]);
    lib.remove_from_collection(std::slice::from_ref(&b), "shots")
        .unwrap();
    assert_eq!(names(&lib.collections()), vec![("Shots", 1), ("Zed", 0)]);
    assert_eq!(lib.annotations(&a).collections, vec!["Shots".to_string()]);
    assert!(matches!(
        lib.remove_from_collection(std::slice::from_ref(&a), "none"),
        Err(LibraryError::NotFound(_))
    ));
    lib.delete_collection("Shots").unwrap();
    assert_eq!(names(&lib.collections()), vec![("Zed", 0)]);
    assert!(lib.annotations(&a).collections.is_empty());
}

#[test]
fn rating_set_clear_and_bounds() {
    let tmp = tempfile::tempdir().unwrap();
    let a = touch(tmp.path(), "a.png");
    let mut lib = Library::in_memory();
    assert!(lib.set_rating(std::slice::from_ref(&a), 6).is_err());
    lib.set_rating(std::slice::from_ref(&a), 0).unwrap();
    // clearing an unknown file does not create a record
    assert!(lib.query(&LibraryQuery::default()).is_empty());
    lib.set_rating(std::slice::from_ref(&a), 5).unwrap();
    assert_eq!(lib.annotations(&a).rating, 5);
    lib.set_rating(std::slice::from_ref(&a), 0).unwrap();
    assert_eq!(lib.annotations(&a).rating, 0);
}

#[test]
fn query_and_semantics_and_rating_filters() {
    let tmp = tempfile::tempdir().unwrap();
    let a = touch(tmp.path(), "a.png");
    let b = touch(tmp.path(), "b.mp4");
    let c = touch(tmp.path(), "c.png");
    let gone = tmp.path().join("gone.png");
    let mut lib = Library::in_memory();
    lib.add_tag(&[a.clone(), b.clone(), gone.clone()], "x")
        .unwrap();
    lib.add_tag(&[b.clone(), c.clone()], "y").unwrap();
    lib.add_to_collection(&[b.clone(), c.clone()], "col")
        .unwrap();
    lib.set_rating(std::slice::from_ref(&a), 2).unwrap();
    lib.set_rating(std::slice::from_ref(&b), 4).unwrap();
    lib.set_rating(std::slice::from_ref(&c), 5).unwrap();

    let q = |tags: &[&str], collection: Option<&str>, rating| LibraryQuery {
        tags: tags.iter().map(|t| t.to_string()).collect(),
        collection: collection.map(str::to_string),
        rating,
    };
    // nonexistent files are never returned
    assert_eq!(
        lib.query(&q(&["x"], None, None)),
        vec![a.clone(), b.clone()]
    );
    assert_eq!(lib.query(&q(&["X", "y"], None, None)), vec![b.clone()]);
    assert_eq!(
        lib.query(&q(&["x", "y", "z"], None, None)),
        Vec::<PathBuf>::new()
    );
    assert_eq!(
        lib.query(&q(&["y"], Some("COL"), None)),
        vec![b.clone(), c.clone()]
    );
    assert_eq!(lib.query(&q(&["x"], Some("col"), None)), vec![b.clone()]);
    assert!(lib.query(&q(&[], Some("missing"), None)).is_empty());
    assert_eq!(
        lib.query(&q(&[], None, Some(RatingFilter::Exactly(4)))),
        vec![b.clone()]
    );
    assert_eq!(
        lib.query(&q(&[], None, Some(RatingFilter::AtLeast(4)))),
        vec![b.clone(), c.clone()]
    );
    assert_eq!(
        lib.query(&q(&["x"], None, Some(RatingFilter::AtLeast(3)))),
        vec![b.clone()]
    );
    assert!(
        lib.query(&q(&[], None, Some(RatingFilter::Unrated)))
            .is_empty()
    );
    lib.set_rating(std::slice::from_ref(&c), 0).unwrap();
    assert_eq!(
        lib.query(&q(&[], None, Some(RatingFilter::Unrated))),
        vec![c.clone()]
    );
    assert_eq!(lib.query(&LibraryQuery::default()), vec![a, b, c]);
}

#[test]
fn folder_scope_unrated_includes_unknown_files() {
    let tmp = tempfile::tempdir().unwrap();
    let known = touch(tmp.path(), "known.png");
    let rated = touch(tmp.path(), "rated.png");
    let unknown = touch(tmp.path(), "unknown.png");
    let mut lib = Library::in_memory();
    lib.add_tag(std::slice::from_ref(&known), "t").unwrap();
    lib.set_rating(std::slice::from_ref(&rated), 3).unwrap();
    let all = || vec![entry(&known), entry(&rated), entry(&unknown)];
    let paths = |v: Vec<Entry>| v.into_iter().map(|e| e.path).collect::<Vec<_>>();

    let unrated = LibraryQuery {
        rating: Some(RatingFilter::Unrated),
        ..Default::default()
    };
    assert_eq!(
        paths(lib.filter_entries(all(), &unrated)),
        vec![known.clone(), unknown.clone()]
    );
    let at_least = LibraryQuery {
        rating: Some(RatingFilter::AtLeast(1)),
        ..Default::default()
    };
    assert_eq!(
        paths(lib.filter_entries(all(), &at_least)),
        vec![rated.clone()]
    );
    let tagged = LibraryQuery {
        tags: vec!["T".into()],
        ..Default::default()
    };
    assert_eq!(
        paths(lib.filter_entries(all(), &tagged)),
        vec![known.clone()]
    );
    assert_eq!(
        paths(lib.filter_entries(all(), &LibraryQuery::default())).len(),
        3
    );
}

#[test]
fn relink_file_and_directory() {
    let tmp = tempfile::tempdir().unwrap();
    let d = tmp.path();
    let a = touch(d, "old/a.mov");
    let b = touch(d, "old/sub/b.png");
    let c = touch(d, "other/c.png");
    let mut lib = Library::in_memory();
    lib.add_tag(&[a.clone(), b.clone(), c.clone()], "t")
        .unwrap();
    lib.add_to_collection(&[a.clone(), b.clone()], "col")
        .unwrap();
    lib.set_rating(std::slice::from_ref(&b), 3).unwrap();

    // single file rename
    let a2 = at(d, "old/a2.mov");
    fs::rename(&a, &a2).unwrap();
    lib.relink(&[(a.clone(), a2.clone())]).unwrap();
    assert_eq!(lib.annotations(&a), Annotations::default());
    assert_eq!(lib.annotations(&a2).tags, vec!["t".to_string()]);
    assert_eq!(lib.annotations(&a2).collections, vec!["col".to_string()]);

    // directory move relinks everything below it
    fs::rename(d.join("old"), d.join("new")).unwrap();
    lib.relink(&[(d.join("old"), d.join("new"))]).unwrap();
    let b2 = at(d, "new/sub/b.png");
    assert_eq!(lib.annotations(&b2).rating, 3);
    assert_eq!(
        lib.annotations(&at(d, "new/a2.mov")).collections,
        vec!["col".to_string()]
    );
    assert_eq!(lib.annotations(&b), Annotations::default());
    assert_eq!(lib.annotations(&c).tags, vec!["t".to_string()]);
    assert_eq!(names(&lib.collections()), vec![("col", 2)]);
    // a sibling that merely shares a name prefix is untouched
    assert_eq!(
        lib.query(&LibraryQuery {
            tags: vec!["t".into()],
            ..Default::default()
        })
        .len(),
        3
    );
}

#[test]
fn relink_swap_and_collision() {
    let tmp = tempfile::tempdir().unwrap();
    let d = tmp.path();
    let a = touch(d, "a.png");
    let b = touch(d, "b.png");
    let mut lib = Library::in_memory();
    lib.set_rating(std::slice::from_ref(&a), 1).unwrap();
    lib.set_rating(std::slice::from_ref(&b), 2).unwrap();
    // swap
    lib.relink(&[(a.clone(), b.clone()), (b.clone(), a.clone())])
        .unwrap();
    assert_eq!(lib.annotations(&a).rating, 2);
    assert_eq!(lib.annotations(&b).rating, 1);
    // moving onto an annotated path: the destination keeps its record, the source record is dropped
    lib.relink(&[(a.clone(), b.clone())]).unwrap();
    assert_eq!(lib.annotations(&b).rating, 1);
    assert_eq!(lib.annotations(&a), Annotations::default());
    assert_eq!(lib.catalog.len(), 1);
}

#[test]
fn relink_and_forget_numbered_sequences() {
    let tmp = tempfile::tempdir().unwrap();
    let d = tmp.path();
    let pattern = d.join("shot_####.exr");
    let mut lib = Library::in_memory();
    lib.set_rating(std::slice::from_ref(&pattern), 4).unwrap();
    let frames = |p: &str| {
        (1..=3)
            .map(|n| d.join(format!("{p}_{n:04}.exr")))
            .collect::<Vec<_>>()
    };
    let moved: Vec<(PathBuf, PathBuf)> = frames("shot").into_iter().zip(frames("plate")).collect();
    lib.relink(&moved).unwrap();
    assert_eq!(lib.annotations(&pattern).rating, 0);
    assert_eq!(lib.annotations(&d.join("plate_####.exr")).rating, 4);

    // existence: a sequence pattern counts while its directory does
    assert_eq!(
        lib.query(&LibraryQuery::default()),
        vec![d.join("plate_####.exr")]
    );
    lib.forget(&frames("plate")).unwrap();
    assert!(lib.catalog.is_empty());
}

#[test]
fn forget_file_and_directory() {
    let tmp = tempfile::tempdir().unwrap();
    let d = tmp.path();
    let a = touch(d, "dir/a.png");
    let b = touch(d, "dir/deep/b.png");
    let c = touch(d, "dirty/c.png");
    let e = touch(d, "e.png");
    let mut lib = Library::in_memory();
    lib.add_tag(&[a.clone(), b.clone(), c.clone(), e.clone()], "t")
        .unwrap();
    lib.add_to_collection(&[a.clone(), b.clone(), e.clone()], "col")
        .unwrap();
    lib.forget(std::slice::from_ref(&e)).unwrap();
    assert_eq!(names(&lib.collections()), vec![("col", 2)]);
    assert_eq!(names(&lib.tags()), vec![("t", 3)]);
    lib.forget(&[d.join("dir")]).unwrap();
    assert_eq!(names(&lib.collections()), vec![("col", 0)]);
    assert_eq!(names(&lib.tags()), vec![("t", 1)]);
    assert_eq!(lib.annotations(&c).tags, vec!["t".to_string()]);
    lib.forget(&[PathBuf::from("/does/not/exist")]).unwrap();
}

#[test]
fn relink_and_forget_persist() {
    let tmp = tempfile::tempdir().unwrap();
    let lib_dir = tmp.path().join("lib");
    let a = touch(tmp.path(), "a.png");
    let b = touch(tmp.path(), "b.png");
    {
        let mut lib = Library::open(&lib_dir, None).unwrap();
        lib.set_rating(&[a.clone(), b.clone()], 2).unwrap();
        lib.relink(&[(a.clone(), tmp.path().join("a2.png"))])
            .unwrap();
        lib.forget(std::slice::from_ref(&b)).unwrap();
    }
    let lib = Library::open(&lib_dir, None).unwrap();
    assert_eq!(lib.annotations(&tmp.path().join("a2.png")).rating, 2);
    assert_eq!(lib.annotations(&a).rating, 0);
    assert_eq!(lib.annotations(&b).rating, 0);
}

fn python_db(path: &Path) {
    let conn = rusqlite::Connection::open(path).unwrap();
    conn.execute_batch(
        "CREATE TABLE files (id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, filename TEXT NOT NULL, folder_path TEXT NOT NULL, file_type TEXT NOT NULL);
         CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, color TEXT);
         CREATE TABLE file_tags (file_id INTEGER, tag_id INTEGER, PRIMARY KEY (file_id, tag_id));
         CREATE TABLE collections (id INTEGER PRIMARY KEY, name TEXT NOT NULL);
         CREATE TABLE collection_items (collection_id INTEGER, file_id INTEGER, PRIMARY KEY (collection_id, file_id));
         CREATE TABLE ratings (file_id INTEGER PRIMARY KEY, rating INTEGER NOT NULL);
         INSERT INTO files VALUES (1, '/m/a.mov', 'a.mov', '/m', 'video'), (2, '/m/b.png', 'b.png', '/m', 'image'), (3, '/m/c.png', 'c.png', '/m', 'image');
         INSERT INTO tags VALUES (1, 'Hero', NULL), (2, 'unused', NULL), (3, 'hero', NULL), (4, 'a|b', NULL);
         INSERT INTO file_tags VALUES (1, 1), (2, 1), (2, 3), (2, 4);
         INSERT INTO collections VALUES (1, 'Reel'), (2, 'reel'), (3, 'Empty');
         INSERT INTO collection_items VALUES (1, 1), (2, 2);
         INSERT INTO ratings VALUES (1, 5), (2, 0), (3, 3);",
    )
    .unwrap();
}

#[test]
fn legacy_import_once_into_new_catalog() {
    let tmp = tempfile::tempdir().unwrap();
    let db = tmp.path().join("database.sqlite");
    python_db(&db);
    let lib_dir = tmp.path().join("lib");
    {
        let lib = Library::open(&lib_dir, Some(&db)).unwrap();
        assert_eq!(
            names(&lib.tags()),
            vec![("a/b", 1), ("Hero", 2), ("unused", 0)]
        );
        assert_eq!(names(&lib.collections()), vec![("Empty", 0), ("Reel", 2)]);
        let a = lib.annotations(Path::new("/m/a.mov"));
        assert_eq!(
            a,
            Annotations {
                rating: 5,
                tags: vec!["Hero".into()],
                collections: vec!["Reel".into()]
            }
        );
        assert_eq!(lib.annotations(Path::new("/m/c.png")).rating, 3);
        let b = lib.annotations(Path::new("/m/b.png"));
        assert_eq!(b.rating, 0);
        assert_eq!(b.tags, vec!["Hero".to_string(), "a/b".to_string()]);
    }
    // The import happens once: later edits persist, and reopening with the DB again does not re-import.
    {
        let mut lib = Library::open(&lib_dir, Some(&db)).unwrap();
        lib.delete_tag("unused").unwrap();
    }
    let lib = Library::open(&lib_dir, Some(&db)).unwrap();
    assert_eq!(names(&lib.tags()), vec![("a/b", 1), ("Hero", 2)]);
}

#[test]
fn legacy_import_tolerates_missing_tables_and_bad_db() {
    let tmp = tempfile::tempdir().unwrap();
    let db = tmp.path().join("partial.sqlite");
    {
        let conn = rusqlite::Connection::open(&db).unwrap();
        conn.execute_batch("CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT); INSERT INTO tags VALUES (1, ' solo ');").unwrap();
    }
    let lib = Library::open(&tmp.path().join("l1"), Some(&db)).unwrap();
    assert_eq!(names(&lib.tags()), vec![("solo", 0)]);

    let junk = tmp.path().join("junk.sqlite");
    fs::write(&junk, b"this is not sqlite").unwrap();
    // an unreadable legacy DB never blocks opening the library
    assert!(
        Library::open(&tmp.path().join("l2"), Some(&junk))
            .unwrap()
            .tags()
            .is_empty()
    );
    // a missing DB file is simply skipped
    let lib = Library::open(
        &tmp.path().join("l3"),
        Some(&tmp.path().join("nope.sqlite")),
    )
    .unwrap();
    assert!(lib.tags().is_empty());
}

#[test]
fn iso_now_is_well_formed() {
    let s = iso_now();
    assert_eq!(s.len(), 20);
    assert!(s.starts_with("20"), "{s}");
}
