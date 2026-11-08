//! One-time, read-only import of the Python-era SQLite database (`files`, `tags`, `file_tags`,
//! `collections`, `collection_items`, `ratings`). Missing tables or rows that fail to decode are
//! skipped: a partial import beats none.

use std::collections::HashMap;
use std::path::Path;

use rusqlite::{Connection, OpenFlags};

use crate::{LibraryError, Result};

/// One annotated file of the legacy database.
#[derive(Debug, Default)]
pub(crate) struct LegacyFile {
    pub path: String,
    pub rating: u8,
    pub tags: Vec<String>,
    pub collections: Vec<String>,
}

#[derive(Debug, Default)]
pub(crate) struct LegacyData {
    /// Every tag name, including tags no file carries.
    pub tags: Vec<String>,
    /// Every collection name, including empty collections (names are not unique in the old schema).
    pub collections: Vec<String>,
    /// Files with a rating, a tag or a collection membership.
    pub files: Vec<LegacyFile>,
}

fn rows<T>(
    conn: &Connection,
    sql: &str,
    map: impl FnMut(&rusqlite::Row<'_>) -> rusqlite::Result<T>,
) -> Vec<T> {
    let mut stmt = match conn.prepare(sql) {
        Ok(s) => s,
        Err(e) => {
            log::info!("legacy import: skipping `{sql}`: {e}");
            return Vec::new();
        }
    };
    match stmt.query_map([], map) {
        Ok(it) => it.filter_map(|r| r.ok()).collect(),
        Err(e) => {
            log::warn!("legacy import: `{sql}` failed: {e}");
            Vec::new()
        }
    }
}

fn entry<'a>(
    map: &'a mut HashMap<i64, LegacyFile>,
    files: &HashMap<i64, String>,
    id: i64,
) -> Option<&'a mut LegacyFile> {
    let path = files.get(&id)?;
    Some(map.entry(id).or_insert_with(|| LegacyFile {
        path: path.clone(),
        ..Default::default()
    }))
}

pub(crate) fn read(db: &Path) -> Result<LegacyData> {
    let conn = Connection::open_with_flags(
        db,
        OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX,
    )
    .map_err(|e| LibraryError::Legacy(format!("{}: {e}", db.display())))?;

    let files: HashMap<i64, String> = rows(&conn, "SELECT id, path FROM files", |r| {
        Ok((r.get::<_, i64>(0)?, r.get::<_, String>(1)?))
    })
    .into_iter()
    .collect();
    let tag_names: HashMap<i64, String> = rows(&conn, "SELECT id, name FROM tags", |r| {
        Ok((r.get::<_, i64>(0)?, r.get::<_, String>(1)?))
    })
    .into_iter()
    .collect();
    let coll_names: HashMap<i64, String> = rows(&conn, "SELECT id, name FROM collections", |r| {
        Ok((r.get::<_, i64>(0)?, r.get::<_, String>(1)?))
    })
    .into_iter()
    .collect();

    let mut by_file: HashMap<i64, LegacyFile> = HashMap::new();
    for (file_id, tag_id) in rows(&conn, "SELECT file_id, tag_id FROM file_tags", |r| {
        Ok((r.get::<_, i64>(0)?, r.get::<_, i64>(1)?))
    }) {
        if let (Some(name), Some(f)) =
            (tag_names.get(&tag_id), entry(&mut by_file, &files, file_id))
        {
            f.tags.push(name.clone());
        }
    }
    for (coll_id, file_id) in rows(
        &conn,
        "SELECT collection_id, file_id FROM collection_items",
        |r| Ok((r.get::<_, i64>(0)?, r.get::<_, i64>(1)?)),
    ) {
        if let (Some(name), Some(f)) = (
            coll_names.get(&coll_id),
            entry(&mut by_file, &files, file_id),
        ) {
            f.collections.push(name.clone());
        }
    }
    for (file_id, rating) in rows(&conn, "SELECT file_id, rating FROM ratings", |r| {
        Ok((r.get::<_, i64>(0)?, r.get::<_, i64>(1)?))
    }) {
        if let Some(f) = entry(&mut by_file, &files, file_id) {
            f.rating = u8::try_from(rating.clamp(0, 5)).unwrap_or(0);
        }
    }

    let mut tags: Vec<(i64, String)> = tag_names.into_iter().collect();
    tags.sort();
    let mut collections: Vec<(i64, String)> = coll_names.into_iter().collect();
    collections.sort();
    let mut files: Vec<(i64, LegacyFile)> = by_file.into_iter().collect();
    files.sort_by_key(|(id, _)| *id);
    Ok(LegacyData {
        tags: tags.into_iter().map(|(_, n)| n).collect(),
        collections: collections.into_iter().map(|(_, n)| n).collect(),
        files: files.into_iter().map(|(_, f)| f).collect(),
    })
}
