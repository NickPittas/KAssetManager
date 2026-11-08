//! UI-free playback engine. One `Player` per view (preview pane, fullscreen). Decodes ahead on worker
//! threads into a RAM-budgeted frame cache; audio (cpal) is the master clock when the source has audio,
//! otherwise a monotonic clock. Generation counters drop stale decodes after `load`/`seek`.
//! Volume, mute, speed and loop survive `load` (Python bugs: they silently reset).
//!
//! # Model
//!
//! * `load` replaces the source: position, duration and cache are reset (volume, mute, speed and
//!   loop are kept) and the first frame is decoded; playback starts only with `autoplay`.
//! * Frames are cached by index (`frame_at(position)` of the stream's frame rate, guarded against
//!   zero/negative rates). Workers decode the frame on screen first, then about three seconds of
//!   playback ahead (one second while paused; scaled by speed, limited by the RAM budget),
//!   wrapping at the end when looping. Frames already played stay cached behind the playhead for
//!   stepping and scrubbing; for image sequences a few frames behind are decoded too.
//!   Each decode is tagged with the load generation, so a frame of a previous source can never be
//!   cached or shown; decodes the playhead has moved away from are cancelled.
//! * Time: with audio at normal speed the audio device is the master clock; position is where the
//!   device is. At speed != 1 the audio is **muted and not decoded** (no pitch-correct stretch)
//!   and a system clock running at `speed` drives playback. A device that cannot be opened, fails
//!   mid-play or stops consuming samples degrades to silent playback on the system clock and sets
//!   `PlayerState::error`.
//! * `tick(now)` takes the time as a parameter so tests (and the UI's frame time) drive it; it
//!   returns a frame only when the frame to show changed. While playing it never shows a frame
//!   from the future and, when decoding is late, holds the newest earlier frame (dropping frames
//!   rather than slowing down).
//! * At the end: looping wraps to the start; otherwise playback pauses on the last frame
//!   (`play` then restarts from the beginning). `stop` is pause + seek to 0 with the source kept.
//! * Stills have one frame and cannot play.

mod audio;
mod cache;
mod clock;
mod cpal_backend;
mod decode;
mod timeline;

#[cfg(test)]
mod tests;
#[cfg(test)]
mod testutil;

use std::sync::Arc;
use std::time::Instant;

use gallerybrowser_media::{FrameRate, SharedSource, SourceKind, Tick, VideoFrame};

use crate::audio::{AudioEngine, next_session};
use crate::clock::{Clock, Reading};
use crate::decode::Decoder;
use crate::timeline::Timeline;

pub use crate::audio::{AudioBackend, AudioFormat, BackendFactory, ErrorFn, RenderFn};

const SPEED_MIN: f32 = 0.1;
const SPEED_MAX: f32 = 8.0;

#[derive(Clone, Debug, PartialEq)]
pub struct PlayerState {
    pub loaded: bool,
    pub playing: bool,
    pub position: Tick,
    pub duration: Tick,
    pub frame_rate: FrameRate,
    /// 0-based frame index at `position`.
    pub frame_index: i64,
    pub frame_count: i64,
    pub speed: f32,
    pub volume: f32,
    pub muted: bool,
    pub looping: bool,
    pub has_audio: bool,
    /// Cached frames ahead of the playhead / frames wanted (0..=1) for a buffer indicator.
    pub buffered: f32,
    /// Last decode/audio error, shown by the UI.
    pub error: Option<String>,
}

pub struct Player {
    decoder: Decoder,
    audio: AudioEngine,
    clock: Clock,
    source: Option<SharedSource>,
    timeline: Timeline,
    loaded: bool,
    has_audio: bool,
    has_video: bool,
    /// Bumped by every `load`/`unload`.
    generation: u64,
    playing: bool,
    /// Position as of the last `tick` (or the last seek/step).
    position: Tick,
    speed: f32,
    volume: f32,
    muted: bool,
    looping: bool,
    cache_bytes: u64,
    /// The frame last handed out by `tick` and its index.
    displayed: Option<(i64, Arc<VideoFrame>)>,
    /// The frame for the current position has not been shown yet (and is not known to have failed).
    awaiting: bool,
    audio_session: u64,
    decode_error: Option<String>,
    audio_error: Option<String>,
    /// The audio clock failed for the current source: seeks, speed changes and loop wraps stay
    /// on the silent system clock; only an explicit `play` or a new `load` tries the sound again.
    audio_failed: bool,
}

impl Player {
    /// `cache_bytes` from `Settings::ram_preview_mb`. Audio device failure → silent playback + `error`.
    /// The device is opened lazily by the audio thread on the first play of a source with sound.
    pub fn new(cache_bytes: u64) -> Self {
        Self::with_audio_backend(cache_bytes, crate::cpal_backend::factory())
    }

    /// A player on another sound output (tests, or an embedding that owns its audio device). The
    /// factory runs on the audio thread, on first use.
    pub fn with_audio_backend(cache_bytes: u64, backend: BackendFactory) -> Self {
        Self {
            decoder: Decoder::new(cache_bytes),
            audio: AudioEngine::new(backend),
            clock: Clock::new(),
            source: None,
            timeline: Timeline::empty(),
            loaded: false,
            has_audio: false,
            has_video: false,
            generation: 0,
            playing: false,
            position: Tick::ZERO,
            speed: 1.0,
            volume: 1.0,
            muted: false,
            looping: true,
            cache_bytes,
            displayed: None,
            awaiting: false,
            audio_session: 0,
            decode_error: None,
            audio_error: None,
            audio_failed: false,
        }
    }

    /// Replace the source; resets position/duration/cache; keeps volume/mute/speed/loop.
    /// Shows the first frame without starting playback unless `autoplay`.
    pub fn load(&mut self, source: SharedSource, autoplay: bool) {
        self.halt();
        self.generation = self.generation.wrapping_add(1);
        let info = source.info();
        let kind = info.kind;
        let timeline = Timeline::new(info);
        let has_video = info.has_video();
        let has_audio = info.has_audio() && kind != SourceKind::Still;
        let frame_bytes = info.video.as_ref().map_or(0, |v| {
            u64::from(v.width)
                .saturating_mul(u64::from(v.height))
                .saturating_mul(4)
        });

        self.timeline = timeline;
        self.loaded = true;
        self.has_audio = has_audio;
        self.has_video = has_video;
        self.position = Tick::ZERO;
        self.displayed = None;
        self.audio_error = None;
        self.audio_failed = false;
        self.source = Some(source.clone());
        self.decode_error = self
            .decoder
            .load(
                self.generation,
                source,
                timeline,
                kind,
                has_video,
                frame_bytes,
            )
            .err();
        self.awaiting = has_video && self.decode_error.is_none();
        if autoplay {
            self.play();
        }
    }

    /// Unload (selection cleared / non-media).
    pub fn unload(&mut self) {
        self.halt();
        self.generation = self.generation.wrapping_add(1);
        self.decoder.unload(self.generation);
        self.source = None;
        self.timeline = Timeline::empty();
        self.loaded = false;
        self.has_audio = false;
        self.has_video = false;
        self.position = Tick::ZERO;
        self.displayed = None;
        self.awaiting = false;
        self.decode_error = None;
        self.audio_error = None;
        self.audio_failed = false;
    }

    pub fn play(&mut self) {
        if !self.loaded || self.playing || self.timeline.frame_count <= 1 {
            return;
        }
        if !self.looping && self.timeline.index_at(self.position) >= self.timeline.last_index() {
            // paused on the last frame: play from the start
            self.position = Tick::ZERO;
        }
        self.playing = true;
        // an explicit play tries the sound again after a failure
        self.audio_failed = false;
        self.awaiting = true;
        self.start_clock();
    }

    pub fn pause(&mut self) {
        if !self.playing {
            return;
        }
        // stop exactly where the sound is
        if let Some(t) = self.clock.peek_audio(&self.audio.clock()) {
            self.position = self.timeline.clamp_position(t);
        }
        self.halt();
        self.awaiting = true;
    }

    pub fn toggle(&mut self) {
        if self.playing {
            self.pause();
        } else {
            self.play();
        }
    }

    /// Pause and return to the first frame; the source stays loaded (Play resumes).
    pub fn stop(&mut self) {
        if !self.loaded {
            return;
        }
        self.halt();
        self.position = Tick::ZERO;
        self.awaiting = true;
    }

    pub fn seek(&mut self, t: Tick) {
        if !self.loaded {
            return;
        }
        self.position = self.timeline.clamp_position(t);
        self.awaiting = true;
        if self.playing {
            self.start_clock();
        }
    }

    pub fn seek_frame(&mut self, index: i64) {
        let t = self.timeline.tick_of(index);
        self.seek(t);
    }

    /// Pause and move by `delta` frames (clamped).
    pub fn step(&mut self, delta: i64) {
        if !self.loaded {
            return;
        }
        self.pause();
        let target = self
            .timeline
            .index_at(self.position)
            .saturating_add(delta)
            .max(0)
            .min(self.timeline.last_index());
        self.position = self.timeline.tick_of(target);
        self.awaiting = true;
    }

    /// 0.1..=8.0; negative not supported. Non-finite values are ignored. Audio plays only at
    /// exactly 1.0 (see the module docs); any other speed mutes it.
    pub fn set_speed(&mut self, speed: f32) {
        if !speed.is_finite() {
            return;
        }
        let speed = speed.clamp(SPEED_MIN, SPEED_MAX);
        if (speed - self.speed).abs() < f32::EPSILON {
            return;
        }
        self.speed = speed;
        if self.playing {
            self.start_clock();
        }
    }

    pub fn set_volume(&mut self, v: f32) {
        if !v.is_finite() {
            return;
        }
        self.volume = v.clamp(0.0, 1.0);
        self.audio.set_volume(self.volume);
    }

    pub fn set_muted(&mut self, m: bool) {
        self.muted = m;
        self.audio.set_muted(m);
    }

    pub fn set_looping(&mut self, l: bool) {
        self.looping = l;
    }

    pub fn set_cache_bytes(&mut self, bytes: u64) {
        self.cache_bytes = bytes;
        self.decoder.set_budget(bytes);
    }

    /// Advance the clock; returns a new frame to display when it changed since the last call.
    /// Call once per UI frame; the UI should `request_repaint` while `state().playing`
    /// (and while `needs_repaint()`, so a frame that was still decoding when a paused seek
    /// happened still appears).
    pub fn tick(&mut self, now: Instant) -> Option<Arc<VideoFrame>> {
        if !self.loaded {
            return None;
        }
        if self.playing {
            self.advance(now);
        }
        if !self.has_video {
            return None;
        }
        let target = self.timeline.index_at(self.position);
        let picked = self
            .decoder
            .view(target, self.playing, f64::from(self.speed), self.looping);
        let failure = self.decoder.failure(target);

        let mut shown = None;
        if let Some((index, frame)) = picked {
            let better = match &self.displayed {
                None => true,
                Some((current, _)) if *current == index => false,
                // only ever move closer to the target
                Some((current, _)) => (index - target).abs() < (current - target).abs(),
            };
            if better {
                self.displayed = Some((index, frame.clone()));
                shown = Some(frame);
            }
        }
        let on_target = self.displayed.as_ref().is_some_and(|(i, _)| *i == target);
        self.awaiting = !on_target && failure.is_none();
        match failure {
            Some(msg) => self.decode_error = Some(format!("frame {target}: {msg}")),
            None if on_target => self.decode_error = None,
            None => {}
        }
        shown
    }

    /// The UI should keep repainting: playing, or waiting for the frame of the current position.
    pub fn needs_repaint(&self) -> bool {
        self.loaded && (self.playing || self.awaiting)
    }

    /// Inclusive `(first, last)` ranges of frame indices in the cache, ascending (for a cache bar).
    pub fn cached_ranges(&self) -> Vec<(i64, i64)> {
        self.decoder.cached_ranges()
    }

    pub fn state(&self) -> PlayerState {
        let loaded = self.loaded;
        PlayerState {
            loaded,
            playing: self.playing,
            position: self.position,
            duration: if loaded {
                self.timeline.duration
            } else {
                Tick::ZERO
            },
            frame_rate: self.timeline.rate,
            frame_index: self.timeline.index_at(self.position),
            frame_count: if loaded { self.timeline.frame_count } else { 0 },
            speed: self.speed,
            volume: self.volume,
            muted: self.muted,
            looping: self.looping,
            has_audio: self.has_audio,
            buffered: if loaded { self.decoder.buffered() } else { 0.0 },
            error: self
                .decode_error
                .clone()
                .or_else(|| self.audio_error.clone()),
        }
    }

    /// Stop the clock and the sound; `playing` becomes false.
    fn halt(&mut self) {
        if self.clock.is_audio() {
            self.audio.pause();
        }
        self.clock.stop();
        self.playing = false;
    }

    /// (Re)start the clock from `self.position`: the audio clock when there is sound and the speed
    /// is 1, else the system clock at `speed`.
    fn start_clock(&mut self) {
        let unity = (self.speed - 1.0).abs() < 1e-3;
        match &self.source {
            Some(source) if self.has_audio && unity && !self.audio_failed => {
                self.audio_error = None;
                self.audio_session = next_session(self.audio_session);
                self.audio.start(
                    self.audio_session,
                    source.clone(),
                    self.position,
                    self.timeline.duration,
                );
                self.clock.run_audio(self.position, self.audio_session);
            }
            _ => {
                if self.clock.is_audio() {
                    self.audio.pause();
                }
                self.clock.run_system(self.position, f64::from(self.speed));
            }
        }
    }

    /// Move `position` to the clock, handling the end of the media and a failed audio clock.
    fn advance(&mut self, now: Instant) {
        let sample = self.audio.clock();
        let failure = if self.clock.is_audio() {
            self.audio.fatal(self.audio_session)
        } else {
            None
        };
        let Some(reading) = self.clock.read(now, &sample, failure) else {
            return;
        };
        let time = match reading {
            Reading::Time(t) => t,
            Reading::AudioLost(reason) => {
                // silent playback from where we are, on the system clock
                self.audio_error = Some(reason);
                self.audio_failed = true;
                self.audio.pause();
                self.clock.run_system(self.position, f64::from(self.speed));
                self.position
            }
        };
        if self.clock.is_audio()
            && let Some(msg) = self.audio.soft_error(self.audio_session)
        {
            self.audio_error = Some(msg);
        }
        let end = self.timeline.end;
        if time < end {
            self.position = time.max(Tick::ZERO);
        } else if self.looping {
            self.position = Tick(time.0.rem_euclid(end.0.max(1)));
            self.awaiting = true;
            self.start_clock();
            // anchor the new run now, so the time past the end is not counted twice
            let _ = self.clock.read(now, &self.audio.clock(), None);
        } else {
            // paused on the last frame
            self.position = self.timeline.duration;
            self.awaiting = true;
            self.halt();
        }
    }
}
