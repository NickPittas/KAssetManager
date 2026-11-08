use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::time::UNIX_EPOCH;

use regex::{NoExpand, RegexBuilder};

use crate::fsops::{exists_nofollow, is_case_variant_of_same_file};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum CaseMode {
    #[default]
    Keep,
    Lower,
    Upper,
    Title,
    /// First character upper-cased, the rest lower-cased.
    Sentence,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum NumberMode {
    #[default]
    None,
    Prefix,
    Suffix,
    /// The whole stem becomes the number.
    Replace,
    /// The number is inserted into the stem at character position `number_insert_pos`
    /// (clamped to the stem length; the separator is not used).
    InsertAt,
}

/// Which part of the file name find/replace, insert/remove and case conversion act on.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum RenameScope {
    /// The stem only (default).
    #[default]
    Name,
    /// The extension only (without its dot; the dot is restored when the result is non-empty).
    Extension,
    /// `stem.ext` as one string; the result is split at its last dot afterwards.
    NameAndExtension,
}

/// The Insert / Remove operation.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum InsertRemoveMode {
    #[default]
    Off,
    /// Insert `insert_text` at character position `insert_pos` (beyond the end appends).
    Insert,
    /// Remove `remove_count` characters starting at character position `remove_pos`.
    Remove,
}

/// Order in which numbers are assigned (does not change the preview row order).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum NumberOrder {
    /// The order of the file list.
    #[default]
    Current,
    /// By file name, case-insensitive A–Z.
    NameAsc,
    ModifiedOldestFirst,
    ModifiedNewestFirst,
}

/// Batch-rename rule. Field set mirrors the Python dialog (see behavior spec): scope, find/replace
/// (optionally regex, case-sensitive), insert/remove, numbering (position, order, start, step, padding,
/// separator), case conversion, extension change/remove/add; plus template tokens and prefix/suffix.
///
/// Pipeline per file: template (stem) → find/replace → insert/remove → prefix/suffix (stem) →
/// numbering (stem) → case → extension. Find/replace, insert/remove and case act on the part selected
/// by `scope`. Positions and counts are in characters (never bytes). Regex replacement uses the Rust
/// `regex` syntax (`$1`, `${name}`); literal replacement text is inserted unchanged.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct RenameRule {
    /// Template with tokens, e.g. `{name}_{n}`; empty = keep stem. Tokens: `{name}` (stem), `{ext}`
    /// (extension without dot), `{n}` (number from start/step/padding), `{date}` (`YYYY-MM-DD` of the
    /// modification time, UTC), `{time}` (`HHMMSS`, UTC), `{parent}` (parent folder name), `{width}` and
    /// `{height}` (not known to the planner: empty). Unknown `{…}` text is kept literally.
    pub template: String,
    pub find: String,
    pub replace: String,
    pub use_regex: bool,
    pub case_sensitive: bool,
    pub case_mode: CaseMode,
    pub number_mode: NumberMode,
    pub start: i64,
    pub step: i64,
    pub padding: usize,
    pub separator: String,
    pub prefix: String,
    pub suffix: String,
    /// `None` keep extension; `Some("")` remove; `Some("jpg")` change.
    pub new_extension: Option<String>,
    /// With `new_extension = Some(ext)`: append `.ext` to the existing extension instead of replacing it.
    pub append_extension: bool,
    pub scope: RenameScope,
    pub insert_remove: InsertRemoveMode,
    pub insert_text: String,
    pub insert_pos: usize,
    pub remove_pos: usize,
    pub remove_count: usize,
    /// Character position for [`NumberMode::InsertAt`].
    pub number_insert_pos: usize,
    pub number_order: NumberOrder,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum RenameStatus {
    Ok,
    Unchanged,
    /// Empty name, `/`, NUL, `.`/`..`, invalid regex, name too long.
    Invalid(String),
    /// Two outputs equal, or output equals an existing file that is NOT itself being renamed away.
    Collision(String),
}

#[derive(Clone, Debug, PartialEq)]
pub struct RenameItem {
    pub from: PathBuf,
    pub to: PathBuf,
    pub status: RenameStatus,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct RenamePlan {
    pub items: Vec<RenameItem>,
}

impl RenamePlan {
    /// Executable: no Invalid/Collision items and at least one Ok item.
    pub fn is_valid(&self) -> bool {
        let mut any_ok = false;
        for it in &self.items {
            match it.status {
                RenameStatus::Ok => any_ok = true,
                RenameStatus::Unchanged => {}
                RenameStatus::Invalid(_) | RenameStatus::Collision(_) => return false,
            }
        }
        any_ok
    }
}

const MAX_NAME_BYTES: usize = 255;

/// Pure planning (preview); checks the filesystem for collisions with unrelated existing files.
pub fn plan_renames(rule: &RenameRule, files: &[PathBuf]) -> RenamePlan {
    let finder = match build_finder(rule) {
        Ok(f) => f,
        Err(msg) => {
            return RenamePlan {
                items: files
                    .iter()
                    .map(|f| RenameItem {
                        from: f.clone(),
                        to: f.clone(),
                        status: RenameStatus::Invalid(msg.clone()),
                    })
                    .collect(),
            };
        }
    };

    let ranks = numbering_ranks(rule, files);
    let mut items: Vec<RenameItem> = files
        .iter()
        .enumerate()
        .map(|(idx, from)| {
            let rank = ranks.get(idx).copied().unwrap_or(idx);
            match new_name(rule, finder.as_ref(), rank, from) {
                Ok(name) => {
                    let to = from.with_file_name(&name);
                    let status = match validate_name(&name) {
                        Some(why) => RenameStatus::Invalid(why),
                        None if to == *from => RenameStatus::Unchanged,
                        None => RenameStatus::Ok,
                    };
                    RenameItem {
                        from: from.clone(),
                        to,
                        status,
                    }
                }
                Err(why) => RenameItem {
                    from: from.clone(),
                    to: from.clone(),
                    status: RenameStatus::Invalid(why),
                },
            }
        })
        .collect();

    // Sources that will be vacated (so their names are free as targets).
    let vacated: HashSet<PathBuf> = items
        .iter()
        .filter(|i| i.status == RenameStatus::Ok)
        .map(|i| i.from.clone())
        .collect();
    // Vacated sources by (directory, lowercased name): on a case-insensitive filesystem a target that
    // "exists" may just be a case variant of a source that is about to move away.
    let mut vacated_by_name: HashMap<(Option<PathBuf>, String), Vec<&PathBuf>> = HashMap::new();
    for src in &vacated {
        vacated_by_name.entry(case_key(src)).or_default().push(src);
    }
    let mut target_count: HashMap<PathBuf, usize> = HashMap::new();
    for it in items.iter().filter(|i| i.status == RenameStatus::Ok) {
        *target_count.entry(it.to.clone()).or_default() += 1;
    }
    for it in items.iter_mut().filter(|i| i.status == RenameStatus::Ok) {
        if target_count.get(&it.to).copied().unwrap_or(0) > 1 {
            it.status = RenameStatus::Collision("another item gets the same name".into());
        } else if exists_nofollow(&it.to)
            && !vacated.contains(&it.to)
            && !vacated_by_name.get(&case_key(&it.to)).is_some_and(|srcs| {
                srcs.iter()
                    .any(|src| is_case_variant_of_same_file(src, &it.to))
            })
        {
            it.status = RenameStatus::Collision("a file with this name already exists".into());
        }
    }
    RenamePlan { items }
}

fn case_key(p: &Path) -> (Option<PathBuf>, String) {
    (
        p.parent().map(Path::to_path_buf),
        p.file_name()
            .map(|n| n.to_string_lossy().to_lowercase())
            .unwrap_or_default(),
    )
}

enum Finder {
    Regex(regex::Regex),
    Literal(regex::Regex),
}

fn build_finder(rule: &RenameRule) -> Result<Option<Finder>, String> {
    if rule.find.is_empty() {
        return Ok(None);
    }
    let pattern = if rule.use_regex {
        rule.find.clone()
    } else {
        regex::escape(&rule.find)
    };
    let re = RegexBuilder::new(&pattern)
        .case_insensitive(!rule.case_sensitive)
        .build()
        .map_err(|e| format!("invalid regular expression: {}", first_line(&e.to_string())))?;
    Ok(Some(if rule.use_regex {
        Finder::Regex(re)
    } else {
        Finder::Literal(re)
    }))
}

fn first_line(s: &str) -> String {
    s.lines().last().unwrap_or(s).trim().to_string()
}

fn validate_name(name: &str) -> Option<String> {
    if name.is_empty() {
        Some("empty name".into())
    } else if name == "." || name == ".." {
        Some("reserved name".into())
    } else if name.contains('/') {
        Some("name contains '/'".into())
    } else if name.contains('\0') {
        Some("name contains a NUL character".into())
    } else if name.len() > MAX_NAME_BYTES {
        Some("name is too long".into())
    } else {
        None
    }
}

/// Numbering rank of every file (`ranks[file index]`), per `rule.number_order`. Only stats files for
/// the modification-time orders and only when numbers are actually used.
fn numbering_ranks(rule: &RenameRule, files: &[PathBuf]) -> Vec<usize> {
    let mut order: Vec<usize> = (0..files.len()).collect();
    let used = rule.number_mode != NumberMode::None || rule.template.contains("{n}");
    if used {
        let mtime = |i: &usize| -> (u64, u32) {
            files
                .get(*i)
                .and_then(|p| std::fs::metadata(p).ok())
                .and_then(|m| m.modified().ok())
                .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
                .map_or((0, 0), |d| (d.as_secs(), d.subsec_nanos()))
        };
        match rule.number_order {
            NumberOrder::Current => {}
            NumberOrder::NameAsc => order.sort_by_cached_key(|i| {
                files
                    .get(*i)
                    .and_then(|p| p.file_name())
                    .map(|n| n.to_string_lossy().to_lowercase())
                    .unwrap_or_default()
            }),
            NumberOrder::ModifiedOldestFirst => order.sort_by_cached_key(mtime),
            // Stable descending: ties keep list order.
            NumberOrder::ModifiedNewestFirst => {
                let keys: Vec<(u64, u32)> = (0..files.len()).map(|i| mtime(&i)).collect();
                order.sort_by(|a, b| keys.get(*b).cmp(&keys.get(*a)));
            }
        }
    }
    let mut ranks = vec![0; files.len()];
    for (rank, &idx) in order.iter().enumerate() {
        if let Some(slot) = ranks.get_mut(idx) {
            *slot = rank;
        }
    }
    ranks
}

/// Split a file name into (stem, extension without dot) at the last dot, like Python's
/// `Path.stem`/`Path.suffix`: a leading or trailing dot does not start an extension.
fn split_name(name: &str) -> (&str, &str) {
    match name.rfind('.') {
        Some(i) if i > 0 && i + 1 < name.len() => (
            name.get(..i).unwrap_or(name),
            name.get(i + 1..).unwrap_or_default(),
        ),
        _ => (name, ""),
    }
}

/// Apply `transform` to the part of (stem, ext) selected by `scope`.
fn apply_scope(
    scope: RenameScope,
    stem: String,
    ext: String,
    transform: impl Fn(&str) -> String,
) -> (String, String) {
    match scope {
        RenameScope::Name => (transform(&stem), ext),
        RenameScope::Extension => (stem, transform(&ext)),
        RenameScope::NameAndExtension => {
            let combined = if ext.is_empty() {
                stem
            } else {
                format!("{stem}.{ext}")
            };
            let result = transform(&combined);
            let (s, e) = split_name(&result);
            (s.to_string(), e.to_string())
        }
    }
}

/// Byte offset of the `pos`-th character of `s` (the end of `s` when `pos` is past it).
fn char_offset(s: &str, pos: usize) -> usize {
    s.char_indices().nth(pos).map_or(s.len(), |(i, _)| i)
}

fn insert_at(s: &str, pos: usize, text: &str) -> String {
    let at = char_offset(s, pos);
    format!(
        "{}{}{}",
        s.get(..at).unwrap_or_default(),
        text,
        s.get(at..).unwrap_or_default()
    )
}

fn remove_range(s: &str, pos: usize, count: usize) -> String {
    let start = char_offset(s, pos);
    let end = char_offset(s, pos.saturating_add(count));
    format!(
        "{}{}",
        s.get(..start).unwrap_or_default(),
        s.get(end..).unwrap_or_default()
    )
}

fn sentence_case(s: &str) -> String {
    let mut chars = s.chars();
    match chars.next() {
        Some(first) => {
            let mut out: String = first.to_uppercase().collect();
            out.push_str(&chars.as_str().to_lowercase());
            out
        }
        None => String::new(),
    }
}

fn new_name(
    rule: &RenameRule,
    finder: Option<&Finder>,
    rank: usize,
    from: &Path,
) -> Result<String, String> {
    let name = from
        .file_name()
        .and_then(|n| n.to_str())
        .ok_or("name is not valid UTF-8")?;
    let (stem, ext) = split_name(name);

    let number = rule.start.saturating_add(
        rule.step
            .saturating_mul(i64::try_from(rank).unwrap_or(i64::MAX)),
    );
    let number_text = format_number(number, rule.padding);

    let stem0 = if rule.template.is_empty() {
        stem.to_string()
    } else {
        expand_template(&rule.template, stem, ext, &number_text, from)
    };
    let mut parts = (stem0, ext.to_string());

    // 1. Find & replace.
    if let Some(finder) = finder {
        parts = apply_scope(rule.scope, parts.0, parts.1, |s| match finder {
            Finder::Regex(re) => re.replace_all(s, rule.replace.as_str()).into_owned(),
            Finder::Literal(re) => re
                .replace_all(s, NoExpand(rule.replace.as_str()))
                .into_owned(),
        });
    }

    // 2. Insert / remove.
    match rule.insert_remove {
        InsertRemoveMode::Off => {}
        InsertRemoveMode::Insert => {
            if !rule.insert_text.is_empty() {
                parts = apply_scope(rule.scope, parts.0, parts.1, |s| {
                    insert_at(s, rule.insert_pos, &rule.insert_text)
                });
            }
        }
        InsertRemoveMode::Remove => {
            parts = apply_scope(rule.scope, parts.0, parts.1, |s| {
                remove_range(s, rule.remove_pos, rule.remove_count)
            });
        }
    }
    let (mut s, ext) = parts;

    if !rule.prefix.is_empty() || !rule.suffix.is_empty() {
        s = format!("{}{}{}", rule.prefix, s, rule.suffix);
    }

    // 3. Numbering (always on the stem).
    s = match rule.number_mode {
        NumberMode::None => s,
        NumberMode::Prefix => format!("{number_text}{}{s}", rule.separator),
        NumberMode::Suffix => format!("{s}{}{number_text}", rule.separator),
        NumberMode::Replace => number_text,
        NumberMode::InsertAt => insert_at(&s, rule.number_insert_pos, &number_text),
    };

    // 4. Case.
    let (s, ext) = if rule.case_mode == CaseMode::Keep {
        (s, ext)
    } else {
        apply_scope(rule.scope, s, ext, |t| match rule.case_mode {
            CaseMode::Keep => t.to_string(),
            CaseMode::Lower => t.to_lowercase(),
            CaseMode::Upper => t.to_uppercase(),
            CaseMode::Title => title_case(t),
            CaseMode::Sentence => sentence_case(t),
        })
    };
    Ok(finish_name(rule, s, ext))
}

/// 5. Extension: keep, change/remove, or add; then join.
fn finish_name(rule: &RenameRule, stem: String, ext: String) -> String {
    let ext_out: String = match &rule.new_extension {
        None => ext,
        Some(e) => {
            let e = e.trim().trim_start_matches('.');
            if !rule.append_extension {
                e.to_string()
            } else if e.is_empty() {
                ext
            } else if ext.is_empty() {
                e.to_string()
            } else {
                format!("{ext}.{e}")
            }
        }
    };
    if ext_out.is_empty() {
        stem
    } else {
        format!("{stem}.{ext_out}")
    }
}

fn format_number(n: i64, padding: usize) -> String {
    let digits = n.unsigned_abs().to_string();
    let width = padding.min(64);
    let sign = if n < 0 { "-" } else { "" };
    format!("{sign}{digits:0>width$}")
}

/// Python `str.title()`: a letter is upper-cased when the previous character is not a letter.
fn title_case(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut prev_letter = false;
    for c in s.chars() {
        if c.is_alphabetic() {
            if prev_letter {
                out.extend(c.to_lowercase());
            } else {
                out.extend(c.to_uppercase());
            }
            prev_letter = true;
        } else {
            out.push(c);
            prev_letter = false;
        }
    }
    out
}

fn expand_template(template: &str, stem: &str, ext: &str, number: &str, path: &Path) -> String {
    let mut out = String::with_capacity(template.len() + stem.len());
    let mut rest = template;
    while let Some(open) = rest.find('{') {
        out.push_str(rest.get(..open).unwrap_or_default());
        let after = rest.get(open + 1..).unwrap_or_default();
        match after.find('}') {
            Some(close) => {
                let token = after.get(..close).unwrap_or_default();
                match token {
                    "name" => out.push_str(stem),
                    "ext" => out.push_str(ext),
                    "n" => out.push_str(number),
                    "date" => out.push_str(&date_token(path, true)),
                    "time" => out.push_str(&date_token(path, false)),
                    "parent" => {
                        if let Some(p) = path.parent().and_then(|p| p.file_name()) {
                            out.push_str(&p.to_string_lossy());
                        }
                    }
                    "width" | "height" => {}
                    other => {
                        out.push('{');
                        out.push_str(other);
                        out.push('}');
                    }
                }
                rest = after.get(close + 1..).unwrap_or_default();
            }
            None => {
                out.push('{');
                rest = after;
            }
        }
    }
    out.push_str(rest);
    out
}

fn date_token(path: &Path, date: bool) -> String {
    let secs = std::fs::metadata(path)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| i64::try_from(d.as_secs()).unwrap_or(0))
        .unwrap_or(0);
    let days = secs.div_euclid(86_400);
    let tod = secs.rem_euclid(86_400);
    if date {
        let (y, m, d) = civil_from_days(days);
        format!("{y:04}-{m:02}-{d:02}")
    } else {
        format!("{:02}{:02}{:02}", tod / 3600, (tod % 3600) / 60, tod % 60)
    }
}

/// Howard Hinnant's days→civil algorithm (days since 1970-01-01).
fn civil_from_days(z: i64) -> (i64, i64, i64) {
    let z = z.saturating_add(719_468);
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    (if m <= 2 { y + 1 } else { y }, m, d)
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used)]

    use super::*;
    use std::fs;

    fn touch(dir: &Path, name: &str) -> PathBuf {
        let p = dir.join(name);
        fs::write(&p, b"x").expect("write");
        p
    }

    fn numbering(start: i64) -> RenameRule {
        RenameRule {
            number_mode: NumberMode::Replace,
            start,
            step: 1,
            padding: 1,
            ..Default::default()
        }
    }

    #[test]
    fn cycle_is_allowed() {
        let d = tempfile::tempdir().expect("tmp");
        let a = touch(d.path(), "1.jpg");
        let b = touch(d.path(), "2.jpg");
        let plan = plan_renames(&numbering(2), &[a, b]);
        assert!(plan.is_valid(), "{plan:?}");
        assert_eq!(plan.items[0].to, d.path().join("2.jpg"));
        assert_eq!(plan.items[1].to, d.path().join("3.jpg"));
    }

    #[test]
    fn collision_with_unrelated_file() {
        let d = tempfile::tempdir().expect("tmp");
        let a = touch(d.path(), "1.jpg");
        touch(d.path(), "2.jpg");
        let plan = plan_renames(&numbering(2), &[a]);
        assert!(matches!(plan.items[0].status, RenameStatus::Collision(_)));
        assert!(!plan.is_valid());
    }

    #[test]
    fn duplicate_outputs_and_cross_dir() {
        let d = tempfile::tempdir().expect("tmp");
        fs::create_dir(d.path().join("A")).expect("mkdir");
        fs::create_dir(d.path().join("B")).expect("mkdir");
        let a = touch(&d.path().join("A"), "image.jpg");
        let b = touch(&d.path().join("B"), "image.jpg");
        let rule = RenameRule {
            case_mode: CaseMode::Upper,
            new_extension: None,
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[a.clone(), b]);
        assert!(plan.is_valid(), "different dirs must not collide: {plan:?}");
        let c = touch(d.path(), "x.jpg");
        let d2 = touch(d.path(), "y.jpg");
        let rule = RenameRule {
            template: "same".into(),
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[c, d2]);
        assert!(
            plan.items
                .iter()
                .all(|i| matches!(i.status, RenameStatus::Collision(_)))
        );
    }

    #[test]
    fn case_only_rename_is_not_a_collision() {
        let d = tempfile::tempdir().expect("tmp");
        let a = touch(d.path(), "image.jpg");
        let rule = RenameRule {
            case_mode: CaseMode::Upper,
            ..Default::default()
        };
        let plan = plan_renames(&rule, std::slice::from_ref(&a));
        assert_eq!(plan.items[0].to, d.path().join("IMAGE.jpg"));
        assert!(plan.is_valid(), "{plan:?}");
    }

    #[test]
    fn invalid_regex_and_names() {
        let d = tempfile::tempdir().expect("tmp");
        let a = touch(d.path(), "a.jpg");
        let rule = RenameRule {
            find: "(".into(),
            use_regex: true,
            ..Default::default()
        };
        let plan = plan_renames(&rule, std::slice::from_ref(&a));
        assert!(matches!(plan.items[0].status, RenameStatus::Invalid(_)));
        let rule = RenameRule {
            template: "a/b".into(),
            ..Default::default()
        };
        assert!(matches!(
            plan_renames(&rule, std::slice::from_ref(&a)).items[0].status,
            RenameStatus::Invalid(_)
        ));
        let rule = RenameRule {
            template: "..".into(),
            new_extension: Some(String::new()),
            ..Default::default()
        };
        assert!(matches!(
            plan_renames(&rule, &[a]).items[0].status,
            RenameStatus::Invalid(_)
        ));
    }

    #[test]
    fn literal_replace_is_not_regex() {
        let d = tempfile::tempdir().expect("tmp");
        let a = touch(d.path(), "Foo.1.txt");
        let rule = RenameRule {
            find: "foo".into(),
            replace: r"\1$1".into(),
            case_sensitive: false,
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[a]);
        assert_eq!(
            plan.items[0].to,
            d.path().join(format!("{}1$1.1.txt", '\\'))
        );
    }

    #[test]
    fn numbering_case_and_extension() {
        let d = tempfile::tempdir().expect("tmp");
        let a = touch(d.path(), "my shot.MOV");
        let rule = RenameRule {
            number_mode: NumberMode::Suffix,
            start: 7,
            step: 1,
            padding: 3,
            separator: "_".into(),
            case_mode: CaseMode::Title,
            new_extension: Some(".mp4".into()),
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[a]);
        assert_eq!(plan.items[0].to, d.path().join("My Shot_007.mp4"));
    }

    #[test]
    fn template_tokens() {
        let d = tempfile::tempdir().expect("tmp");
        let a = touch(d.path(), "a.jpg");
        let rule = RenameRule {
            template: "{name}-{n}-{ext}-{bogus}".into(),
            start: 5,
            step: 1,
            padding: 2,
            ..Default::default()
        };
        let plan = plan_renames(&rule, &[a]);
        assert_eq!(plan.items[0].to, d.path().join("a-05-jpg-{bogus}.jpg"));
    }

    #[test]
    fn civil_dates() {
        assert_eq!(civil_from_days(0), (1970, 1, 1));
        assert_eq!(civil_from_days(19_723), (2024, 1, 1));
    }

    /// Plan `rule` over files named `names` in a fresh dir and return the new names.
    fn names_for(rule: &RenameRule, names: &[&str]) -> Vec<String> {
        let d = tempfile::tempdir().expect("tmp");
        let files: Vec<PathBuf> = names.iter().map(|n| touch(d.path(), n)).collect();
        plan_renames(rule, &files)
            .items
            .iter()
            .map(|i| {
                i.to.file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_default()
            })
            .collect()
    }

    #[test]
    fn scope_extension_only() {
        let rule = RenameRule {
            scope: RenameScope::Extension,
            find: "jp".into(),
            replace: "x".into(),
            case_mode: CaseMode::Upper,
            ..Default::default()
        };
        // Find/replace then upper-case, both on the extension only.
        assert_eq!(names_for(&rule, &["Shot.jpg"]), vec!["Shot.XG"]);
        let rule = RenameRule {
            scope: RenameScope::Extension,
            find: "jpg".into(),
            ..Default::default()
        };
        // Empty extension result drops the dot.
        assert_eq!(names_for(&rule, &["a.jpg"]), vec!["a"]);
    }

    #[test]
    fn scope_name_and_extension_resplits() {
        let rule = RenameRule {
            scope: RenameScope::NameAndExtension,
            find: "a.j".into(),
            replace: "_".into(),
            ..Default::default()
        };
        // "a.jpg" -> "_pg" (no extension left).
        assert_eq!(names_for(&rule, &["a.jpg"]), vec!["_pg"]);
        let rule = RenameRule {
            scope: RenameScope::NameAndExtension,
            case_mode: CaseMode::Upper,
            ..Default::default()
        };
        assert_eq!(names_for(&rule, &["a.jpg"]), vec!["A.JPG"]);
    }

    #[test]
    fn scope_name_leaves_extension() {
        let rule = RenameRule {
            find: "jpg".into(),
            replace: "x".into(),
            ..Default::default()
        };
        assert_eq!(names_for(&rule, &["jpg.jpg"]), vec!["x.jpg"]);
    }

    #[test]
    fn insert_text_positions() {
        let mut rule = RenameRule {
            insert_remove: InsertRemoveMode::Insert,
            insert_text: "XY".into(),
            insert_pos: 0,
            ..Default::default()
        };
        assert_eq!(names_for(&rule, &["abc.png"]), vec!["XYabc.png"]);
        rule.insert_pos = 2;
        assert_eq!(names_for(&rule, &["abc.png"]), vec!["abXYc.png"]);
        // Beyond the end clamps (appends).
        rule.insert_pos = 999;
        assert_eq!(names_for(&rule, &["abc.png"]), vec!["abcXY.png"]);
        // Empty text is a no-op.
        rule.insert_text.clear();
        assert_eq!(names_for(&rule, &["abc.png"]), vec!["abc.png"]);
    }

    #[test]
    fn insert_into_extension_scope() {
        let rule = RenameRule {
            scope: RenameScope::Extension,
            insert_remove: InsertRemoveMode::Insert,
            insert_text: "_".into(),
            insert_pos: 1,
            ..Default::default()
        };
        assert_eq!(names_for(&rule, &["a.jpg"]), vec!["a.j_pg"]);
    }

    #[test]
    fn remove_range_clamps() {
        let mut rule = RenameRule {
            insert_remove: InsertRemoveMode::Remove,
            remove_pos: 1,
            remove_count: 2,
            ..Default::default()
        };
        assert_eq!(names_for(&rule, &["abcde.png"]), vec!["ade.png"]);
        // Range runs past the end.
        rule.remove_count = 999;
        assert_eq!(names_for(&rule, &["abcde.png"]), vec!["a.png"]);
        // Start past the end: unchanged.
        rule.remove_pos = 50;
        assert_eq!(names_for(&rule, &["abcde.png"]), vec!["abcde.png"]);
        // Usize overflow never panics.
        rule.remove_pos = 1;
        rule.remove_count = usize::MAX;
        assert_eq!(names_for(&rule, &["abcde.png"]), vec!["a.png"]);
        // Removing the whole stem leaves just the extension.
        rule.remove_pos = 0;
        assert_eq!(names_for(&rule, &["abc.png"]), vec![".png"]);
    }

    #[test]
    fn unicode_positions_are_characters() {
        let ins = RenameRule {
            insert_remove: InsertRemoveMode::Insert,
            insert_text: "-".into(),
            insert_pos: 2,
            ..Default::default()
        };
        assert_eq!(names_for(&ins, &["日本語é.txt"]), vec!["日本-語é.txt"]);
        let rem = RenameRule {
            insert_remove: InsertRemoveMode::Remove,
            remove_pos: 1,
            remove_count: 2,
            ..Default::default()
        };
        assert_eq!(names_for(&rem, &["日本語é.txt"]), vec!["日é.txt"]);
        let num = RenameRule {
            number_mode: NumberMode::InsertAt,
            number_insert_pos: 1,
            start: 1,
            step: 1,
            padding: 2,
            ..Default::default()
        };
        assert_eq!(names_for(&num, &["éa.txt"]), vec!["é01a.txt"]);
        let case = RenameRule {
            case_mode: CaseMode::Sentence,
            ..Default::default()
        };
        assert_eq!(names_for(&case, &["éCOLE.txt"]), vec!["École.txt"]);
    }

    #[test]
    fn sentence_case_mode() {
        let rule = RenameRule {
            case_mode: CaseMode::Sentence,
            ..Default::default()
        };
        assert_eq!(
            names_for(&rule, &["hELLO wORLD.JPG", "x.y"]),
            vec!["Hello world.JPG", "X.y"]
        );
        assert_eq!(sentence_case(""), "");
    }

    #[test]
    fn number_insert_at_position_clamps() {
        let rule = RenameRule {
            number_mode: NumberMode::InsertAt,
            number_insert_pos: 2,
            start: 1,
            step: 1,
            padding: 3,
            separator: "_".into(),
            ..Default::default()
        };
        assert_eq!(
            names_for(&rule, &["abcd.jpg", "z.jpg"]),
            vec!["ab001cd.jpg", "z002.jpg"]
        );
    }

    #[test]
    fn number_order_by_name_and_mtime() {
        let d = tempfile::tempdir().expect("tmp");
        let b = touch(d.path(), "b.jpg");
        let a = touch(d.path(), "A.jpg");
        let c = touch(d.path(), "c.jpg");
        let set = |p: &Path, secs: u64| {
            let f = fs::OpenOptions::new().write(true).open(p).expect("open");
            f.set_modified(UNIX_EPOCH + std::time::Duration::from_secs(secs))
                .expect("mtime");
        };
        set(&b, 300);
        set(&a, 100);
        set(&c, 200);
        let files = vec![b, a, c];
        let nums = |order: NumberOrder| -> Vec<String> {
            let rule = RenameRule {
                number_mode: NumberMode::Replace,
                number_order: order,
                start: 1,
                step: 1,
                padding: 1,
                new_extension: Some("n".into()),
                ..Default::default()
            };
            plan_renames(&rule, &files)
                .items
                .iter()
                .map(|i| {
                    i.to.file_name()
                        .map(|n| n.to_string_lossy().into_owned())
                        .unwrap_or_default()
                })
                .collect()
        };
        // Row order stays [b, A, c]; only the assigned numbers change.
        assert_eq!(nums(NumberOrder::Current), vec!["1.n", "2.n", "3.n"]);
        assert_eq!(nums(NumberOrder::NameAsc), vec!["2.n", "1.n", "3.n"]);
        assert_eq!(
            nums(NumberOrder::ModifiedOldestFirst),
            vec!["3.n", "1.n", "2.n"]
        );
        assert_eq!(
            nums(NumberOrder::ModifiedNewestFirst),
            vec!["1.n", "3.n", "2.n"]
        );
    }

    #[test]
    fn extension_add_mode() {
        let mut rule = RenameRule {
            new_extension: Some(".bak".into()),
            append_extension: true,
            ..Default::default()
        };
        assert_eq!(
            names_for(&rule, &["a.jpg", "b"]),
            vec!["a.jpg.bak", "b.bak"]
        );
        rule.new_extension = Some(String::new());
        assert_eq!(names_for(&rule, &["a.jpg"]), vec!["a.jpg"]);
    }

    #[test]
    fn operations_chain_in_python_order() {
        // find/replace -> insert -> numbering -> case -> extension
        let rule = RenameRule {
            find: "old".into(),
            replace: "new".into(),
            insert_remove: InsertRemoveMode::Insert,
            insert_text: "X".into(),
            insert_pos: 0,
            number_mode: NumberMode::Suffix,
            start: 1,
            step: 1,
            padding: 2,
            separator: "-".into(),
            case_mode: CaseMode::Upper,
            new_extension: Some("png".into()),
            ..Default::default()
        };
        assert_eq!(names_for(&rule, &["old.jpg"]), vec!["XNEW-01.png"]);
    }

    #[test]
    fn split_name_edge_cases() {
        assert_eq!(split_name("a.tar.gz"), ("a.tar", "gz"));
        assert_eq!(split_name(".hidden"), (".hidden", ""));
        assert_eq!(split_name("a."), ("a.", ""));
        assert_eq!(split_name("noext"), ("noext", ""));
        assert_eq!(split_name(""), ("", ""));
    }
}
