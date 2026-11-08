//! The playback clock.
//!
//! With audio at normal speed the audio device is the master: position is the session start plus
//! the frames the device has consumed. Otherwise (no audio, speed != 1, device failure) a monotonic
//! system clock runs at `speed`. The caller passes `now` in, so tests drive time directly, and
//! starting/seeking never reads the time: the system clock anchors itself at its first reading, so
//! an injected `now` can never disagree with a hidden `Instant::now()`.

use std::time::{Duration, Instant};

use gallerybrowser_media::Tick;

use crate::audio::ClockSample;

/// How long a started audio session may take to produce its first sample (device open, first
/// decode) before the clock gives up on it and falls back to the system clock.
pub(crate) const START_GRACE: Duration = Duration::from_secs(4);
/// How long a running audio session may stand still before it is declared stalled.
pub(crate) const STALL: Duration = Duration::from_millis(1500);

#[derive(Debug)]
enum Mode {
    Stopped,
    System {
        base: Tick,
        anchor: Option<Instant>,
        speed: f64,
    },
    Audio {
        base: Tick,
        session: u64,
        since: Option<Instant>,
        last_played: u64,
        last_change: Option<Instant>,
        last_read: Option<Instant>,
    },
}

/// What a clock reading says.
#[derive(Debug, PartialEq)]
pub(crate) enum Reading {
    /// Current media position.
    Time(Tick),
    /// The audio clock failed; the caller switches to the system clock. Carries the reason.
    AudioLost(String),
}

pub(crate) struct Clock {
    mode: Mode,
}

impl Clock {
    pub fn new() -> Self {
        Self {
            mode: Mode::Stopped,
        }
    }

    pub fn stop(&mut self) {
        self.mode = Mode::Stopped;
    }

    pub fn is_audio(&self) -> bool {
        matches!(self.mode, Mode::Audio { .. })
    }

    /// Run on the system clock from `base` at `speed`.
    pub fn run_system(&mut self, base: Tick, speed: f64) {
        let speed = if speed.is_finite() && speed > 0.0 {
            speed
        } else {
            1.0
        };
        self.mode = Mode::System {
            base,
            anchor: None,
            speed,
        };
    }

    /// Run on the audio clock of `session`, from `base`.
    pub fn run_audio(&mut self, base: Tick, session: u64) {
        self.mode = Mode::Audio {
            base,
            session,
            since: None,
            last_played: 0,
            last_change: None,
            last_read: None,
        };
    }

    /// Position of the audio clock for `sample` if it belongs to the current session (used to
    /// pause exactly where the sound is).
    pub fn peek_audio(&self, sample: &ClockSample) -> Option<Tick> {
        match self.mode {
            Mode::Audio { base, session, .. } if sample.session == session && sample.rate > 0 => {
                Some(audio_time(base, sample))
            }
            _ => None,
        }
    }

    /// Current position, or `None` when stopped. `failure` is the fatal audio error of the
    /// running session, if any.
    pub fn read(
        &mut self,
        now: Instant,
        sample: &ClockSample,
        failure: Option<String>,
    ) -> Option<Reading> {
        match &mut self.mode {
            Mode::Stopped => None,
            Mode::System {
                base,
                anchor,
                speed,
            } => {
                let anchor = *anchor.get_or_insert(now);
                let elapsed = now.saturating_duration_since(anchor).as_secs_f64() * *speed;
                Some(Reading::Time(Tick(
                    base.0.saturating_add(Tick::from_seconds_f64(elapsed).0),
                )))
            }
            Mode::Audio {
                base,
                session,
                since,
                last_played,
                last_change,
                last_read,
            } => {
                if let Some(reason) = failure {
                    return Some(Reading::AudioLost(reason));
                }
                let since = *since.get_or_insert(now);
                if sample.session != *session || sample.rate == 0 {
                    return if now.saturating_duration_since(since) > START_GRACE {
                        Some(Reading::AudioLost(
                            "audio output did not start; playing without sound".into(),
                        ))
                    } else {
                        Some(Reading::Time(*base))
                    };
                }
                // a stall must be seen over time: when nobody was reading the clock for a while
                // (hidden window, suspended machine) that says nothing about the device
                let gap = last_read.map(|t| now.saturating_duration_since(t));
                *last_read = Some(now);
                let changed_at = last_change.get_or_insert(now);
                if sample.played != *last_played || gap.is_some_and(|g| g > STALL) {
                    *last_played = sample.played;
                    *changed_at = now;
                } else if now.saturating_duration_since(*changed_at) > STALL && sample.played > 0 {
                    return Some(Reading::AudioLost(
                        "audio output stalled; playing without sound".into(),
                    ));
                }
                Some(Reading::Time(audio_time(*base, sample)))
            }
        }
    }
}

fn audio_time(base: Tick, sample: &ClockSample) -> Tick {
    let played = i64::try_from(sample.played).unwrap_or(i64::MAX);
    Tick(
        base.0
            .saturating_add(Tick::from_units(played, i64::from(sample.rate)).0),
    )
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
    use super::*;

    const NONE: ClockSample = ClockSample {
        session: 0,
        played: 0,
        rate: 0,
    };

    fn secs(t: Tick) -> f64 {
        t.seconds()
    }

    fn time(r: Option<Reading>) -> Tick {
        match r {
            Some(Reading::Time(t)) => t,
            other => panic!("expected a time reading, got {other:?}"),
        }
    }

    #[test]
    fn system_clock_anchors_at_first_reading_and_scales_with_speed() {
        let t0 = Instant::now();
        let mut c = Clock::new();
        assert_eq!(c.read(t0, &NONE, None), None);
        c.run_system(Tick::from_seconds_f64(10.0), 2.0);
        let a = time(c.read(t0 + Duration::from_secs(100), &NONE, None));
        assert_eq!(secs(a), 10.0, "the first reading anchors the clock");
        let b = time(c.read(t0 + Duration::from_secs(101), &NONE, None));
        assert!((secs(b) - 12.0).abs() < 1e-9);
        // time going backwards does not rewind
        let d = time(c.read(t0, &NONE, None));
        assert_eq!(secs(d), 10.0);
    }

    #[test]
    fn audio_clock_counts_played_frames_of_its_own_session() {
        let t0 = Instant::now();
        let mut c = Clock::new();
        c.run_audio(Tick::from_seconds_f64(1.0), 7);
        // not started yet: holds the base
        let t = time(c.read(t0, &NONE, None));
        assert_eq!(secs(t), 1.0);
        // another session's samples are ignored
        let other = ClockSample {
            session: 6,
            played: 48_000,
            rate: 48_000,
        };
        let t = time(c.read(t0, &other, None));
        assert_eq!(secs(t), 1.0);
        let mine = ClockSample {
            session: 7,
            played: 24_000,
            rate: 48_000,
        };
        let t = time(c.read(t0, &mine, None));
        assert!((secs(t) - 1.5).abs() < 1e-9);
        assert_eq!(c.peek_audio(&mine).map(secs), Some(1.5));
        assert_eq!(c.peek_audio(&other), None);
    }

    #[test]
    fn audio_clock_gives_up_when_it_never_starts_stalls_or_fails() {
        let t0 = Instant::now();
        let mut c = Clock::new();
        c.run_audio(Tick::ZERO, 1);
        assert!(matches!(c.read(t0, &NONE, None), Some(Reading::Time(_))));
        assert!(matches!(
            c.read(t0 + START_GRACE + Duration::from_millis(1), &NONE, None),
            Some(Reading::AudioLost(_))
        ));

        c.run_audio(Tick::ZERO, 2);
        let s = ClockSample {
            session: 2,
            played: 100,
            rate: 48_000,
        };
        // watched continuously, the device standing still for longer than STALL is a stall
        for ms in [0, 500, 1000, 1500] {
            assert!(
                matches!(
                    c.read(t0 + Duration::from_millis(ms), &s, None),
                    Some(Reading::Time(_))
                ),
                "at {ms} ms"
            );
        }
        assert!(matches!(
            c.read(t0 + STALL + Duration::from_millis(100), &s, None),
            Some(Reading::AudioLost(_))
        ));

        c.run_audio(Tick::ZERO, 3);
        assert_eq!(
            c.read(t0, &NONE, Some("device lost".into())),
            Some(Reading::AudioLost("device lost".into()))
        );
    }

    #[test]
    fn a_gap_in_reading_is_not_a_stall() {
        // a hidden window or a suspended machine stops the ticks, not necessarily the device
        let t0 = Instant::now();
        let mut c = Clock::new();
        c.run_audio(Tick::ZERO, 2);
        let s = ClockSample {
            session: 2,
            played: 100,
            rate: 48_000,
        };
        assert!(matches!(c.read(t0, &s, None), Some(Reading::Time(_))));
        assert!(matches!(
            c.read(t0 + Duration::from_secs(60), &s, None),
            Some(Reading::Time(_))
        ));
        // but watched again, it still has to move
        assert!(matches!(
            c.read(
                t0 + Duration::from_secs(60) + Duration::from_millis(1000),
                &s,
                None
            ),
            Some(Reading::Time(_))
        ));
        assert!(matches!(
            c.read(
                t0 + Duration::from_secs(60) + Duration::from_millis(1600),
                &s,
                None
            ),
            Some(Reading::AudioLost(_))
        ));
    }
}
