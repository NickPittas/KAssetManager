use std::cmp::Ordering;

/// Natural ("human") ordering: `shot2` < `shot10`, case-insensitive.
///
/// Digit runs compare by numeric value (arbitrary length, no overflow); everything else compares
/// case-insensitively char by char. Strings equal under those rules fall back to a plain byte
/// comparison so the order is total and deterministic.
pub fn natural_cmp(a: &str, b: &str) -> Ordering {
    let (mut i, mut j) = (0usize, 0usize);
    loop {
        let (ra, rb) = (a.get(i..).unwrap_or(""), b.get(j..).unwrap_or(""));
        let (ca, cb) = match (ra.chars().next(), rb.chars().next()) {
            (None, None) => return a.cmp(b),
            (None, Some(_)) => return Ordering::Less,
            (Some(_), None) => return Ordering::Greater,
            (Some(ca), Some(cb)) => (ca, cb),
        };
        if ca.is_ascii_digit() && cb.is_ascii_digit() {
            let ea = digit_run_len(ra);
            let eb = digit_run_len(rb);
            let na = ra.get(..ea).unwrap_or("").trim_start_matches('0');
            let nb = rb.get(..eb).unwrap_or("").trim_start_matches('0');
            let ord = na.len().cmp(&nb.len()).then_with(|| na.cmp(nb));
            if ord != Ordering::Equal {
                return ord;
            }
            i += ea;
            j += eb;
        } else {
            let ord = ca.to_lowercase().cmp(cb.to_lowercase());
            if ord != Ordering::Equal {
                return ord;
            }
            i += ca.len_utf8();
            j += cb.len_utf8();
        }
    }
}

/// Byte length of the leading ASCII digit run of `s`.
fn digit_run_len(s: &str) -> usize {
    s.bytes().take_while(u8::is_ascii_digit).count()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sorted(mut v: Vec<&str>) -> Vec<&str> {
        v.sort_by(|a, b| natural_cmp(a, b));
        v
    }

    #[test]
    fn digit_runs_compare_numerically() {
        assert_eq!(
            sorted(vec!["shot10", "shot2", "shot1"]),
            vec!["shot1", "shot2", "shot10"]
        );
        assert_eq!(natural_cmp("a9", "a10"), Ordering::Less);
        assert_eq!(natural_cmp("a007", "a8"), Ordering::Less);
    }

    #[test]
    fn case_insensitive() {
        assert_eq!(sorted(vec!["b", "A", "c", "B"]), vec!["A", "B", "b", "c"]);
        assert_eq!(natural_cmp("Apple", "banana"), Ordering::Less);
    }

    #[test]
    fn prefix_sorts_first_and_huge_numbers_do_not_overflow() {
        assert_eq!(natural_cmp("a", "a1"), Ordering::Less);
        let big_a = format!("x{}", "9".repeat(60));
        let big_b = format!("x1{}", "0".repeat(60));
        assert_eq!(natural_cmp(&big_a, &big_b), Ordering::Less);
    }

    #[test]
    fn total_and_deterministic_on_ties() {
        assert_eq!(natural_cmp("a", "a"), Ordering::Equal);
        assert_ne!(natural_cmp("a", "A"), Ordering::Equal);
        assert_eq!(natural_cmp("a", "A"), natural_cmp("a", "A"));
        assert_eq!(natural_cmp("a01", "a1").reverse(), natural_cmp("a1", "a01"));
    }

    #[test]
    fn handles_non_ascii() {
        assert_eq!(natural_cmp("é1", "é2"), Ordering::Less);
        assert_eq!(natural_cmp("", "a"), Ordering::Less);
    }
}
