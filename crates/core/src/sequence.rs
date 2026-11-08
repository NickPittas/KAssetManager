use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::{MediaKind, classify};

/// Longest digit run treated as a frame number (keeps the value inside `i64`).
const MAX_DIGITS: usize = 18;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SequenceFrame {
    /// Source frame number parsed from the file name (gaps allowed).
    pub number: i64,
    pub path: PathBuf,
}

/// A numbered image sequence in one directory: `prefix` + zero-padded number + `suffix` (incl. extension).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Sequence {
    pub dir: PathBuf,
    pub prefix: String,
    pub suffix: String,
    /// Digit count of the zero padding (0 = unpadded).
    pub padding: usize,
    /// Sorted by `number`, unique numbers.
    pub frames: Vec<SequenceFrame>,
}

impl Sequence {
    /// Stable identity: `dir/prefix####suffix` (`#` repeated `max(padding,1)` times).
    pub fn pattern_path(&self) -> PathBuf {
        let hashes = "#".repeat(self.padding.max(1));
        self.dir
            .join(format!("{}{}{}", self.prefix, hashes, self.suffix))
    }
    /// Display name, e.g. `shot_[1001-1100].exr` (missing frames do not change it). Numbers are shown
    /// zero-padded to the sequence padding (`shot_[0001-0100].exr`); a one-frame sequence shows `[5]`.
    pub fn display_name(&self) -> String {
        let width = self.padding;
        let fmt = |n: i64| format!("{n:0width$}");
        let range = match (self.first(), self.last()) {
            (Some(a), Some(b)) if a.number != b.number => {
                format!("{}-{}", fmt(a.number), fmt(b.number))
            }
            (Some(a), _) => fmt(a.number),
            (None, _) => String::new(),
        };
        format!("{}[{}]{}", self.prefix, range, self.suffix)
    }
    pub fn first(&self) -> Option<&SequenceFrame> {
        self.frames.first()
    }
    pub fn last(&self) -> Option<&SequenceFrame> {
        self.frames.last()
    }
    /// Frame used for the thumbnail (middle frame by index).
    pub fn representative(&self) -> Option<&SequenceFrame> {
        self.frames.get(self.frames.len() / 2)
    }
    pub fn paths(&self) -> Vec<PathBuf> {
        self.frames.iter().map(|f| f.path.clone()).collect()
    }
    /// Inclusive ranges of missing frame numbers between first and last.
    pub fn missing_ranges(&self) -> Vec<(i64, i64)> {
        self.frames
            .windows(2)
            .filter_map(|w| {
                let (a, b) = (w.first()?.number, w.get(1)?.number);
                let lo = a.saturating_add(1);
                let hi = b.saturating_sub(1);
                (lo <= hi).then_some((lo, hi))
            })
            .collect()
    }
}

/// One candidate frame while grouping.
struct Candidate {
    /// Index into the caller's `files`.
    index: usize,
    number: i64,
    digits: usize,
    /// Starts with `0` and has more than one digit: unambiguous zero padding.
    zero_led: bool,
}

type GroupKey = (PathBuf, String, String);

/// Group image files into sequences (same dir, prefix, padding-compatible number, suffix; >= `min_len` frames).
/// Returns (sequences, leftover single files). Only image kinds form sequences; PSD/PSB never do.
///
/// Padding rule: a number with a leading zero (`0007`) is padded and groups with others of the same width;
/// a number without one groups with the padded group of its own width if there is one (`1000` beside
/// `0999`), otherwise with the unpadded numbers. Sparse frames keep their real numbers. `min_len` below 1
/// is treated as 1. Leftovers keep the input order.
pub fn detect_sequences(files: &[PathBuf], min_len: usize) -> (Vec<Sequence>, Vec<PathBuf>) {
    let min_len = min_len.max(1);
    let mut groups: BTreeMap<GroupKey, Vec<Candidate>> = BTreeMap::new();
    for (index, path) in files.iter().enumerate() {
        if !forms_sequences(path) {
            continue;
        }
        let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
            continue;
        };
        let Some((prefix, digits, suffix)) = split_numbered(name) else {
            continue;
        };
        if digits.len() > MAX_DIGITS {
            continue;
        }
        let Ok(number) = digits.parse::<i64>() else {
            continue;
        };
        let dir = path.parent().map(Path::to_path_buf).unwrap_or_default();
        let cand = Candidate {
            index,
            number,
            digits: digits.len(),
            zero_led: digits.len() > 1 && digits.starts_with('0'),
        };
        groups.entry((dir, prefix, suffix)).or_default().push(cand);
    }

    let mut consumed = vec![false; files.len()];
    let mut sequences = Vec::new();
    for ((dir, prefix, suffix), cands) in groups {
        let mut widths: Vec<usize> = cands
            .iter()
            .filter(|c| c.zero_led)
            .map(|c| c.digits)
            .collect();
        widths.sort_unstable();
        widths.dedup();
        let mut buckets: BTreeMap<usize, Vec<Candidate>> = BTreeMap::new();
        for c in cands {
            let padding = match widths.as_slice() {
                _ if c.zero_led || widths.contains(&c.digits) => c.digits,
                &[only] if c.digits > only => only,
                _ => 0,
            };
            buckets.entry(padding).or_default().push(c);
        }
        for (padding, mut members) in buckets {
            members.sort_by_key(|c| (c.number, c.index));
            members.dedup_by_key(|c| c.number);
            if members.len() < min_len {
                continue;
            }
            let mut frames = Vec::with_capacity(members.len());
            for m in &members {
                if let Some(path) = files.get(m.index) {
                    frames.push(SequenceFrame {
                        number: m.number,
                        path: path.clone(),
                    });
                }
            }
            if frames.len() < min_len {
                continue;
            }
            for m in &members {
                if let Some(flag) = consumed.get_mut(m.index) {
                    *flag = true;
                }
            }
            sequences.push(Sequence {
                dir: dir.clone(),
                prefix: prefix.clone(),
                suffix: suffix.clone(),
                padding,
                frames,
            });
        }
    }
    // Duplicate numbers dropped by `dedup_by_key` stay leftovers: they are real files, not frames.
    let in_sequence: std::collections::HashSet<&Path> = sequences
        .iter()
        .flat_map(|s| s.frames.iter().map(|f| f.path.as_path()))
        .collect();
    let leftover = files
        .iter()
        .enumerate()
        .filter(|(i, p)| {
            !(consumed.get(*i).copied().unwrap_or(false) && in_sequence.contains(p.as_path()))
        })
        .map(|(_, p)| p.clone())
        .collect();
    (sequences, leftover)
}

/// Only image kinds form sequences, and never PSD/PSB (layered documents, not frames).
fn forms_sequences(path: &Path) -> bool {
    if classify(path) != Some(MediaKind::Image) {
        return false;
    }
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .map(str::to_ascii_lowercase);
    !matches!(ext.as_deref(), Some("psd" | "psb"))
}

/// Parse `name` into (prefix, digits, suffix) using the LAST digit run before the extension.
/// The suffix includes the extension (`shot_0001.exr` → `("shot_", "0001", ".exr")`).
pub fn split_numbered(name: &str) -> Option<(String, String, String)> {
    let stem_end = name.rfind('.').filter(|&i| i > 0).unwrap_or(name.len());
    let stem = name.get(..stem_end)?;
    let end = stem.rfind(|c: char| c.is_ascii_digit())? + 1;
    let head = stem.get(..end)?;
    let start = head.trim_end_matches(|c: char| c.is_ascii_digit()).len();
    Some((
        name.get(..start)?.to_string(),
        name.get(start..end)?.to_string(),
        name.get(end..)?.to_string(),
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn paths(dir: &str, names: &[&str]) -> Vec<PathBuf> {
        names.iter().map(|n| Path::new(dir).join(n)).collect()
    }

    fn numbered(dir: &str, prefix: &str, ext: &str, width: usize, nums: &[i64]) -> Vec<PathBuf> {
        nums.iter()
            .map(|n| Path::new(dir).join(format!("{prefix}{n:0width$}.{ext}")))
            .collect()
    }

    #[test]
    fn split_uses_last_digit_run() {
        assert_eq!(
            split_numbered("shot_0001.exr"),
            Some(("shot_".into(), "0001".into(), ".exr".into()))
        );
        assert_eq!(
            split_numbered("a1_v22.png"),
            Some(("a1_v".into(), "22".into(), ".png".into()))
        );
        assert_eq!(
            split_numbered("clip.0100.exr"),
            Some(("clip.".into(), "0100".into(), ".exr".into()))
        );
        assert_eq!(
            split_numbered("1.png"),
            Some((String::new(), "1".into(), ".png".into()))
        );
        assert_eq!(split_numbered("name.png"), None);
        assert_eq!(
            split_numbered("v2.final.png"),
            Some(("v".into(), "2".into(), ".final.png".into()))
        );
    }

    #[test]
    fn padded_sequence_groups_and_names() {
        let files = numbered("/d", "shot_", "exr", 4, &[1, 2, 3, 4]);
        let (seqs, left) = detect_sequences(&files, 2);
        assert!(left.is_empty());
        assert_eq!(seqs.len(), 1);
        let s = &seqs[0];
        assert_eq!(s.padding, 4);
        assert_eq!(s.pattern_path(), PathBuf::from("/d/shot_####.exr"));
        assert_eq!(s.display_name(), "shot_[0001-0004].exr");
        assert_eq!(s.representative().map(|f| f.number), Some(3));
        assert!(s.missing_ranges().is_empty());
        assert_eq!(s.paths(), files);
    }

    #[test]
    fn unpadded_numbers_group_across_widths() {
        let files = numbered("/d", "f", "png", 0, &[8, 9, 10, 11, 100]);
        let (seqs, left) = detect_sequences(&files, 2);
        assert!(left.is_empty());
        assert_eq!(seqs.len(), 1);
        assert_eq!(seqs[0].padding, 0);
        assert_eq!(seqs[0].pattern_path(), PathBuf::from("/d/f#.png"));
        assert_eq!(seqs[0].display_name(), "f[8-100].png");
        assert_eq!(seqs[0].missing_ranges(), vec![(12, 99)]);
    }

    #[test]
    fn sparse_frames_keep_real_numbers() {
        let files = numbered("/d", "s.", "exr", 4, &[1001, 1002, 1005, 1006, 1010]);
        let (seqs, _) = detect_sequences(&files, 2);
        assert_eq!(seqs.len(), 1);
        let nums: Vec<i64> = seqs[0].frames.iter().map(|f| f.number).collect();
        assert_eq!(nums, vec![1001, 1002, 1005, 1006, 1010]);
        assert_eq!(seqs[0].missing_ranges(), vec![(1003, 1004), (1007, 1009)]);
        assert_eq!(seqs[0].display_name(), "s.[1001-1010].exr");
        // zero-less 4-digit numbers form an unpadded group.
        assert_eq!(seqs[0].padding, 0);
    }

    #[test]
    fn unpadded_number_joins_padded_group_of_same_width() {
        let files = numbered("/d", "p_", "exr", 4, &[998, 999, 1000, 1001]);
        let (seqs, left) = detect_sequences(&files, 2);
        assert!(left.is_empty());
        assert_eq!(seqs.len(), 1);
        assert_eq!(seqs[0].padding, 4);
        assert_eq!(seqs[0].frames.len(), 4);
    }

    #[test]
    fn different_padding_widths_stay_separate() {
        let mut files = numbered("/d", "x_", "png", 3, &[1, 2]);
        files.extend(numbered("/d", "x_", "png", 5, &[1, 2, 3]));
        let (seqs, left) = detect_sequences(&files, 2);
        assert!(left.is_empty());
        assert_eq!(seqs.len(), 2);
        let mut pads: Vec<(usize, usize)> =
            seqs.iter().map(|s| (s.padding, s.frames.len())).collect();
        pads.sort_unstable();
        assert_eq!(pads, vec![(3, 2), (5, 3)]);
    }

    #[test]
    fn mixed_prefixes_dirs_and_extensions_split() {
        let mut files = numbered("/a", "one_", "exr", 4, &[1, 2, 3]);
        files.extend(numbered("/a", "two_", "exr", 4, &[1, 2]));
        files.extend(numbered("/a", "one_", "png", 4, &[1, 2]));
        files.extend(numbered("/b", "one_", "exr", 4, &[1, 2]));
        let (seqs, left) = detect_sequences(&files, 2);
        assert!(left.is_empty());
        assert_eq!(seqs.len(), 4);
        for s in &seqs {
            assert!(
                s.frames
                    .iter()
                    .all(|f| f.path.parent() == Some(s.dir.as_path()))
            );
            assert!(
                s.frames
                    .iter()
                    .all(|f| f.path.to_string_lossy().ends_with(&s.suffix))
            );
        }
    }

    #[test]
    fn min_len_and_leftovers_in_input_order() {
        let mut files = paths("/d", &["lonely_0001.png", "z.png"]);
        files.extend(numbered("/d", "seq_", "png", 2, &[1, 2, 3]));
        let (seqs, left) = detect_sequences(&files, 4);
        assert!(seqs.is_empty());
        assert_eq!(left, files);
        let (seqs, left) = detect_sequences(&files, 3);
        assert_eq!(seqs.len(), 1);
        assert_eq!(left, paths("/d", &["lonely_0001.png", "z.png"]));
        let (seqs, left) = detect_sequences(&paths("/d", &["only_01.png"]), 1);
        assert_eq!(seqs.len(), 1);
        assert!(left.is_empty());
        assert_eq!(seqs[0].display_name(), "only_[01].png");
    }

    #[test]
    fn psd_psb_videos_and_unknown_never_form_sequences() {
        let mut files = numbered("/d", "layer_", "psd", 3, &[1, 2, 3]);
        files.extend(numbered("/d", "layer_", "psb", 3, &[1, 2, 3]));
        files.extend(numbered("/d", "clip_", "mov", 3, &[1, 2, 3]));
        files.extend(numbered("/d", "doc_", "txt", 3, &[1, 2, 3]));
        let (seqs, left) = detect_sequences(&files, 2);
        assert!(seqs.is_empty());
        assert_eq!(left, files);
    }

    #[test]
    fn extension_case_is_part_of_the_suffix_and_huge_digit_runs_are_ignored() {
        let mut files = paths("/d", &["a_0001.EXR", "a_0002.exr", "a_0003.exr"]);
        files.push(PathBuf::from(format!("/d/big_{}.png", "1".repeat(30))));
        files.push(PathBuf::from(format!("/d/big_{}.png", "2".repeat(30))));
        let (seqs, left) = detect_sequences(&files, 2);
        assert_eq!(seqs.len(), 1);
        assert_eq!(seqs[0].frames.len(), 2);
        assert_eq!(left.len(), 3);
    }

    #[test]
    fn empty_and_degenerate_sequences_do_not_panic() {
        let s = Sequence {
            dir: PathBuf::from("/d"),
            prefix: "a".into(),
            suffix: ".png".into(),
            padding: 0,
            frames: vec![],
        };
        assert_eq!(s.display_name(), "a[].png");
        assert!(s.representative().is_none());
        assert!(s.missing_ranges().is_empty());
        let (seqs, left) = detect_sequences(&[], 2);
        assert!(seqs.is_empty() && left.is_empty());
    }
}
