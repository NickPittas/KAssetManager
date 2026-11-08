//! Frame/time arithmetic for one loaded source. Every number derived from the file (frame rate,
//! duration) is hostile: zero or negative rates fall back through `FrameRate::sane`, and no
//! function here can divide by zero, overflow or panic.

use gallerybrowser_media::{FrameRate, MediaInfo, SourceKind, Tick};

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) struct Timeline {
    /// Always `sane()` (positive numerator and denominator).
    pub rate: FrameRate,
    /// At least 1.
    pub frame_count: i64,
    /// Reported duration (never negative). Stills report one frame.
    pub duration: Tick,
    /// Start of the (non-existent) frame after the last one; playback ends when the clock reaches
    /// it, so the last frame is shown for its whole duration. Never before `duration`.
    pub end: Tick,
}

impl Timeline {
    /// Nothing loaded.
    pub fn empty() -> Self {
        let rate = FrameRate::default().sane();
        Self {
            rate,
            frame_count: 1,
            duration: Tick::ZERO,
            end: rate.frame_duration(),
        }
    }

    pub fn new(info: &MediaInfo) -> Self {
        let rate = info.frame_rate().sane();
        let frame_duration = rate.frame_duration();
        if info.kind == SourceKind::Still {
            return Self {
                rate,
                frame_count: 1,
                duration: frame_duration,
                end: frame_duration,
            };
        }
        let duration = Tick(info.duration.0.clamp(0, Tick::MAX.0));
        // ceil(duration / frame duration): the last frame may be partial
        let frame_count = if duration.0 == 0 {
            1
        } else {
            rate.frame_at(Tick(duration.0 - 1)).saturating_add(1).max(1)
        };
        let end = rate.tick_of(frame_count).max(duration);
        Self {
            rate,
            frame_count,
            duration,
            end,
        }
    }

    pub fn last_index(&self) -> i64 {
        self.frame_count.saturating_sub(1).max(0)
    }

    /// Index of the frame shown at `t`, clamped to the frames that exist.
    pub fn index_at(&self, t: Tick) -> i64 {
        self.rate
            .frame_at(t.max(Tick::ZERO))
            .max(0)
            .min(self.last_index())
    }

    /// Start of frame `index` (clamped to the frames that exist).
    pub fn tick_of(&self, index: i64) -> Tick {
        self.rate.tick_of(index.max(0).min(self.last_index()))
    }

    /// Media time to request frame `index` at: the middle of the frame, so containers that round
    /// time stamps to a coarse time base land on this frame whichever way they round.
    pub fn request_time(&self, index: i64) -> Tick {
        let start = self.tick_of(index);
        Tick(start.0.saturating_add(self.rate.frame_duration().0 / 2))
    }

    /// `t` limited to `0..=duration`.
    pub fn clamp_position(&self, t: Tick) -> Tick {
        t.max(Tick::ZERO).min(self.duration)
    }

    pub fn frames_per_second(&self) -> f64 {
        let fps = self.rate.as_f64();
        if fps.is_finite() && fps > 0.0 {
            fps
        } else {
            FrameRate::default().as_f64()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testutil::synth_info;

    #[test]
    fn frame_count_is_the_ceiling_of_duration_over_frame_duration() {
        let fps = FrameRate::FPS_24;
        let exact = Timeline::new(&synth_info(SourceKind::Movie, fps, fps.tick_of(240)));
        assert_eq!(exact.frame_count, 240);
        assert_eq!(exact.end, exact.duration);
        let partial = Timeline::new(&synth_info(
            SourceKind::Movie,
            fps,
            Tick(fps.tick_of(240).0 + 1),
        ));
        assert_eq!(partial.frame_count, 241);
        assert!(partial.end > partial.duration);
    }

    #[test]
    fn stills_have_one_frame_and_unknown_durations_do_not_break() {
        let still = Timeline::new(&synth_info(
            SourceKind::Still,
            FrameRate::FPS_24,
            Tick::from_seconds_f64(5.0),
        ));
        assert_eq!(still.frame_count, 1);
        assert_eq!(still.duration, FrameRate::FPS_24.frame_duration());
        let unknown = Timeline::new(&synth_info(
            SourceKind::Movie,
            FrameRate::FPS_24,
            Tick::ZERO,
        ));
        assert_eq!(unknown.frame_count, 1);
        let negative = Timeline::new(&synth_info(SourceKind::Movie, FrameRate::FPS_24, Tick(-5)));
        assert_eq!((negative.frame_count, negative.duration), (1, Tick::ZERO));
    }

    #[test]
    fn hostile_frame_rates_fall_back_to_the_default() {
        for rate in [
            FrameRate { num: 0, den: 1 },
            FrameRate { num: 24, den: 0 },
            FrameRate { num: -24, den: 1 },
            FrameRate { num: 0, den: 0 },
        ] {
            let t = Timeline::new(&synth_info(
                SourceKind::Movie,
                rate,
                Tick::from_seconds_f64(2.0),
            ));
            assert_eq!(t.rate, FrameRate::default());
            assert!(t.frame_count > 1);
            assert_eq!(
                t.index_at(Tick::from_seconds_f64(1.0)),
                t.rate.frame_at(Tick::from_seconds_f64(1.0))
            );
        }
        let absurd = Timeline::new(&synth_info(
            SourceKind::Movie,
            FrameRate {
                num: i64::MAX,
                den: 1,
            },
            Tick::MAX,
        ));
        assert!(absurd.frame_count >= 1);
        let _ = absurd.request_time(absurd.last_index());
    }

    #[test]
    fn indices_and_positions_clamp() {
        let t = Timeline::new(&synth_info(
            SourceKind::Movie,
            FrameRate::FPS_25,
            Tick::from_seconds_f64(2.0),
        ));
        assert_eq!(t.frame_count, 50);
        assert_eq!(t.index_at(Tick(-10)), 0);
        assert_eq!(t.index_at(Tick::from_seconds_f64(1.0)), 25);
        assert_eq!(t.index_at(t.duration), 49);
        assert_eq!(t.index_at(Tick::MAX), 49);
        assert_eq!(t.tick_of(-4), Tick::ZERO);
        assert_eq!(t.tick_of(500), t.tick_of(49));
        assert_eq!(t.clamp_position(Tick(-1)), Tick::ZERO);
        assert_eq!(t.clamp_position(Tick::MAX), t.duration);
        // the request time is inside the frame
        for i in [0, 1, 17, 49] {
            assert_eq!(t.index_at(t.request_time(i)), i);
        }
    }
}
