//! RAM-budgeted cache of decoded frames, keyed by frame index.
//!
//! Eviction is by distance from the playhead, not recency: frames far from where playback or
//! scrubbing is happening go first, frames behind the playhead before frames ahead of it (playback
//! needs the ones ahead; scrubbing back only needs a few). Frames inside the wanted window are
//! never evicted to satisfy the budget, so the window sizes (computed from the budget by the
//! decoder) are what bound memory; this prevents decode/evict thrash.

use std::collections::BTreeMap;
use std::sync::Arc;

use gallerybrowser_media::VideoFrame;

/// Hard cap on cached frames (tiny frames would otherwise make eviction scans long).
const MAX_ENTRIES: usize = 8192;

/// What playback currently wants resident: `behind` frames before and `ahead` frames after the
/// playhead (wrapping at the ends when looping).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct Window {
    pub playhead: i64,
    pub ahead: i64,
    pub behind: i64,
    pub frame_count: i64,
    pub looping: bool,
}

/// "Infinite" distance that cannot overflow when doubled.
const FAR: i64 = i64::MAX / 4;

impl Window {
    /// Frames to travel forward from the playhead to reach `index`.
    fn forward(&self, index: i64) -> i64 {
        if index >= self.playhead {
            index.saturating_sub(self.playhead)
        } else if self.looping {
            index
                .saturating_add(self.frame_count)
                .saturating_sub(self.playhead)
        } else {
            FAR
        }
    }

    /// Frames to travel backward from the playhead to reach `index`.
    fn backward(&self, index: i64) -> i64 {
        if index <= self.playhead {
            self.playhead.saturating_sub(index)
        } else if self.looping {
            self.playhead
                .saturating_add(self.frame_count)
                .saturating_sub(index)
        } else {
            FAR
        }
    }

    pub fn contains(&self, index: i64) -> bool {
        self.forward(index) <= self.ahead || self.backward(index) <= self.behind
    }

    /// Eviction priority: larger is evicted first.
    fn score(&self, index: i64) -> i64 {
        self.forward(index)
            .min(self.backward(index).saturating_mul(2))
    }
}

struct Entry {
    frame: Arc<VideoFrame>,
    bytes: u64,
}

pub(crate) struct FrameCache {
    map: BTreeMap<i64, Entry>,
    bytes: u64,
    budget: u64,
}

impl FrameCache {
    pub fn new(budget: u64) -> Self {
        Self {
            map: BTreeMap::new(),
            bytes: 0,
            budget,
        }
    }

    pub fn budget(&self) -> u64 {
        self.budget
    }

    pub fn set_budget(&mut self, budget: u64) {
        self.budget = budget;
    }

    pub fn clear(&mut self) {
        self.map.clear();
        self.bytes = 0;
    }

    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    #[cfg(test)]
    pub fn bytes(&self) -> u64 {
        self.bytes
    }

    pub fn contains(&self, index: i64) -> bool {
        self.map.contains_key(&index)
    }

    /// Adds a frame (replacing one at the same index) and evicts down to the budget around `window`.
    pub fn insert(&mut self, index: i64, frame: Arc<VideoFrame>, window: &Window) {
        let bytes = frame.byte_size() as u64;
        if let Some(old) = self.map.insert(index, Entry { frame, bytes }) {
            self.bytes = self.bytes.saturating_sub(old.bytes);
        }
        self.bytes = self.bytes.saturating_add(bytes);
        self.evict(window);
    }

    /// Drops frames outside `window` (farthest first) until within the budget and the entry cap.
    pub fn evict(&mut self, window: &Window) {
        let over = |c: &Self| c.bytes > c.budget || c.map.len() > MAX_ENTRIES;
        if !over(self) {
            return;
        }
        let mut victims: Vec<(i64, i64)> = self
            .map
            .keys()
            .copied()
            .filter(|&i| !window.contains(i))
            .map(|i| (window.score(i), i))
            .collect();
        victims.sort_unstable_by_key(|a| std::cmp::Reverse(a.0));
        for (_, index) in victims {
            if !over(self) {
                break;
            }
            if let Some(e) = self.map.remove(&index) {
                self.bytes = self.bytes.saturating_sub(e.bytes);
            }
        }
    }

    /// The cached frame to show for `target`: the frame itself, else the nearest cached one (the
    /// earlier one on a tie). While `playing` a frame from the future is never shown.
    pub fn nearest(&self, target: i64, playing: bool) -> Option<(i64, &Arc<VideoFrame>)> {
        if let Some(e) = self.map.get(&target) {
            return Some((target, &e.frame));
        }
        let before = self.map.range(..target).next_back();
        let after = if playing {
            None
        } else {
            self.map.range(target.saturating_add(1)..).next()
        };
        let pick = match (before, after) {
            (Some(b), Some(a)) => {
                if target.saturating_sub(*b.0) <= a.0.saturating_sub(target) {
                    b
                } else {
                    a
                }
            }
            (Some(b), None) => b,
            (None, Some(a)) => a,
            (None, None) => return None,
        };
        Some((*pick.0, &pick.1.frame))
    }

    /// Inclusive `(first, last)` ranges of cached indices, ascending.
    pub fn ranges(&self) -> Vec<(i64, i64)> {
        let mut out: Vec<(i64, i64)> = Vec::new();
        for &i in self.map.keys() {
            match out.last_mut() {
                Some(last) if last.1.saturating_add(1) == i => last.1 = i,
                _ => out.push((i, i)),
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn frame(bytes: usize) -> Arc<VideoFrame> {
        Arc::new(VideoFrame::rgba8(bytes as u32 / 4, 1, vec![0; bytes]))
    }

    fn window(playhead: i64, ahead: i64, behind: i64, looping: bool) -> Window {
        Window {
            playhead,
            ahead,
            behind,
            frame_count: 100,
            looping,
        }
    }

    #[test]
    fn evicts_farthest_outside_the_window_and_keeps_the_window() {
        let mut c = FrameCache::new(400);
        let w = window(50, 2, 1, false);
        for i in [10, 20, 49, 50, 51, 52] {
            c.insert(i, frame(100), &w);
        }
        // 6 frames x 100 B > 400 B: the two farthest (10 and 20) are evicted
        assert_eq!(c.len(), 4);
        for i in [49, 50, 51, 52] {
            assert!(c.contains(i), "{i} must stay");
        }
        assert_eq!(c.bytes(), 400);
    }

    #[test]
    fn behind_frames_go_before_equally_distant_ahead_frames() {
        let mut c = FrameCache::new(300);
        let w = window(50, 0, 0, false);
        for i in [48, 52, 50] {
            c.insert(i, frame(100), &w);
        }
        c.insert(51, frame(100), &w);
        // 48 is 2 behind (score 4), 52 is 2 ahead (score 2), 51 is 1 ahead: 48 goes first
        assert!(!c.contains(48));
        assert!(c.contains(52) && c.contains(51) && c.contains(50));
    }

    #[test]
    fn window_frames_may_exceed_the_budget_instead_of_thrashing() {
        let mut c = FrameCache::new(100);
        let w = window(5, 3, 0, false);
        for i in 5..9 {
            c.insert(i, frame(100), &w);
        }
        assert_eq!(c.len(), 4);
        assert!(c.bytes() > c.budget());
        // moving the window on lets the old ones go
        let w = window(40, 3, 0, false);
        c.insert(40, frame(100), &w);
        // the old frames are 32+ away from the new playhead and over budget: all go
        assert_eq!(c.len(), 1);
        assert!(c.contains(40));
        assert!(!c.contains(5));
    }

    #[test]
    fn looping_window_wraps() {
        let w = Window {
            playhead: 98,
            ahead: 4,
            behind: 0,
            frame_count: 100,
            looping: true,
        };
        assert!(w.contains(99) && w.contains(0) && w.contains(2));
        assert!(!w.contains(3));
        let w = Window {
            looping: false,
            ..w
        };
        assert!(!w.contains(0));
    }

    #[test]
    fn nearest_prefers_exact_then_earlier_and_never_the_future_while_playing() {
        let mut c = FrameCache::new(u64::MAX);
        let w = window(0, 100, 100, false);
        for i in [3, 7] {
            c.insert(i, frame(4), &w);
        }
        assert_eq!(c.nearest(7, false).map(|x| x.0), Some(7));
        assert_eq!(
            c.nearest(5, false).map(|x| x.0),
            Some(3),
            "tie goes to the earlier frame"
        );
        assert_eq!(c.nearest(6, false).map(|x| x.0), Some(7));
        assert_eq!(c.nearest(6, true).map(|x| x.0), Some(3));
        assert_eq!(c.nearest(1, true).map(|x| x.0), None);
        assert_eq!(c.nearest(1, false).map(|x| x.0), Some(3));
    }

    #[test]
    fn ranges_group_consecutive_indices() {
        let mut c = FrameCache::new(u64::MAX);
        let w = window(0, 100, 100, false);
        for i in [1, 2, 3, 7, 9, 10] {
            c.insert(i, frame(4), &w);
        }
        assert_eq!(c.ranges(), vec![(1, 3), (7, 7), (9, 10)]);
    }

    #[test]
    fn reinserting_replaces_without_double_counting() {
        let mut c = FrameCache::new(u64::MAX);
        let w = window(0, 10, 0, false);
        c.insert(1, frame(100), &w);
        c.insert(1, frame(60), &w);
        assert_eq!((c.len(), c.bytes()), (1, 60));
        c.clear();
        assert_eq!((c.len(), c.bytes()), (0, 0));
    }
}
