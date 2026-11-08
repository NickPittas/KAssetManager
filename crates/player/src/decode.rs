//! Decode-ahead. Worker threads keep the frame cache filled around the playhead: the frame on
//! screen first, then the frames playback will need next (at playback speed), then, for sources
//! where random access is cheap, a few frames behind for stepping back.
//!
//! Everything a worker decodes is tagged with the load *generation*; `load`/`unload` bump it and
//! flag every in-flight decode as cancelled, so a frame from a previous source can neither enter
//! the cache nor be returned. When the playhead jumps, in-flight decodes that fell out of the
//! wanted window are cancelled too (sources poll `filmcraft_media::cancel`). Pattern adapted
//! from FilmCraft `crates/ui-egui/src/frames.rs` (prioritised jobs, per-job cancel flags,
//! byte-budgeted cache).
//!
//! Sizing: how many frames fit is `budget / bytes per frame` (measured from decoded frames,
//! estimated from the stream size before the first one). The window is planned inside that, so
//! eviction never fights the prefetcher.

use std::collections::HashMap;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex, MutexGuard, PoisonError};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use gallerybrowser_media::{FrameRequest, SharedSource, SourceKind, Tick, VideoFrame};

use crate::cache::{FrameCache, Window};
use crate::timeline::Timeline;

/// Seconds of playback kept decoded ahead of the playhead (before the RAM budget limits it).
const AHEAD_SECONDS: f64 = 3.0;
/// Same while paused: enough to start playing at once, without decoding seconds of every clip
/// that is merely selected.
const AHEAD_PAUSED_SECONDS: f64 = 1.0;
const MIN_AHEAD: i64 = 4;
const MAX_AHEAD: i64 = 600;
/// Frames behind the playhead decoded proactively (only for cheap random access: sequences).
const MAX_BEHIND_DECODE: i64 = 8;
/// Most frames to skip ahead when decoding cannot keep up with the clock.
const MAX_STARVE_SKIP: i64 = 24;
/// Failed-frame bookkeeping is bounded; past this it starts over (a hopeless file).
const MAX_FAILED: usize = 262_144;
/// How long dropping a decoder waits for its workers (a slow decode must not freeze the UI).
const JOIN_LIMIT: Duration = Duration::from_millis(300);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Plan {
    ahead: i64,
    behind: i64,
    behind_decode: i64,
}

struct State {
    generation: u64,
    source: Option<SharedSource>,
    timeline: Timeline,
    kind: SourceKind,
    /// The source has pictures to decode.
    video: bool,
    cache: FrameCache,
    in_flight: HashMap<i64, Arc<AtomicBool>>,
    failed: HashMap<i64, Arc<str>>,
    playhead: i64,
    playing: bool,
    speed: f64,
    looping: bool,
    /// Frame size guessed from the stream before anything is decoded.
    estimate_bytes: u64,
    /// Largest decoded frame so far.
    observed_bytes: u64,
    plan: Plan,
    /// Most decodes in flight at once.
    parallelism: usize,
    /// Moving average of one decode (s).
    ema_decode: f64,
    shutdown: bool,
}

struct Job {
    generation: u64,
    index: i64,
    time: Tick,
    source: SharedSource,
    cancel: Arc<AtomicBool>,
    catch_up: Option<Tick>,
}

enum Outcome {
    Frame(Arc<VideoFrame>),
    Cancelled,
    Failed(String),
}

impl State {
    fn new(budget: u64) -> Self {
        Self {
            generation: 0,
            source: None,
            timeline: Timeline::empty(),
            kind: SourceKind::Movie,
            video: false,
            cache: FrameCache::new(budget),
            in_flight: HashMap::new(),
            failed: HashMap::new(),
            playhead: 0,
            playing: false,
            speed: 1.0,
            looping: false,
            estimate_bytes: 0,
            observed_bytes: 0,
            plan: Plan {
                ahead: 0,
                behind: 0,
                behind_decode: 0,
            },
            parallelism: 1,
            ema_decode: 0.0,
            shutdown: false,
        }
    }

    fn frame_bytes(&self) -> u64 {
        if self.observed_bytes > 0 {
            self.observed_bytes
        } else {
            self.estimate_bytes
        }
        .max(1)
    }

    fn cancel_all(&mut self) {
        for flag in self.in_flight.values() {
            flag.store(true, Ordering::Relaxed);
        }
        self.in_flight.clear();
    }

    /// Size the window from playback speed, frame count and what fits in the budget.
    fn replan(&mut self) {
        let n = self.timeline.frame_count;
        if self.kind == SourceKind::Still || n <= 1 {
            self.plan = Plan {
                ahead: 0,
                behind: 0,
                behind_decode: 0,
            };
            return;
        }
        let capacity = i64::try_from(self.cache.budget() / self.frame_bytes())
            .unwrap_or(i64::MAX)
            .max(2);
        let speed = if self.speed.is_finite() {
            self.speed.clamp(0.1, 8.0)
        } else {
            1.0
        };
        let seconds = if self.playing {
            AHEAD_SECONDS
        } else {
            AHEAD_PAUSED_SECONDS
        };
        let want_ahead = ((self.timeline.frames_per_second() * speed * seconds).ceil() as i64)
            .clamp(MIN_AHEAD, MAX_AHEAD);
        let behind = (want_ahead / 2).max(2).min(capacity / 4);
        let ahead = want_ahead.min(capacity - 1 - behind).max(1);
        let (ahead, behind) = (ahead.min(n - 1), behind.min(n - 1));
        let behind_decode = if self.kind == SourceKind::ImageSequence {
            behind.min(MAX_BEHIND_DECODE)
        } else {
            0
        };
        self.plan = Plan {
            ahead,
            behind,
            behind_decode,
        };
    }

    fn window(&self) -> Window {
        Window {
            playhead: self.playhead,
            ahead: self.plan.ahead,
            behind: self.plan.behind,
            frame_count: self.timeline.frame_count,
            looping: self.looping && self.timeline.frame_count > 1,
        }
    }

    fn wrap(&self, i: i64) -> Option<i64> {
        let n = self.timeline.frame_count;
        if (0..n).contains(&i) {
            Some(i)
        } else if self.looping && n > 1 {
            Some(i.rem_euclid(n))
        } else {
            None
        }
    }

    /// Frames to jump ahead of the playhead when playing and decoding is behind the clock: by the
    /// time the decode finishes the clock will have moved about this far.
    fn starve_skip(&self) -> i64 {
        if !self.playing
            || self.cache.contains(self.playhead)
            || self.in_flight.contains_key(&self.playhead)
        {
            return 0;
        }
        let frames = self.ema_decode * self.timeline.frames_per_second() * self.speed;
        (frames.round() as i64)
            .clamp(0, MAX_STARVE_SKIP)
            .min(self.plan.ahead)
    }

    /// Frames wanted, most urgent first: the playhead, then forward; behind last.
    fn candidates(&self, skip: i64) -> Vec<i64> {
        let n = self.timeline.frame_count;
        let p = self.playhead;
        let ahead = self.plan.ahead;
        let mut out = Vec::with_capacity((ahead + self.plan.behind_decode + 1).max(1) as usize);
        for off in skip..=ahead {
            out.extend(self.wrap(p.saturating_add(off)));
        }
        for off in 0..skip.min(ahead + 1) {
            out.extend(self.wrap(p.saturating_add(off)));
        }
        for off in 1..=self.plan.behind_decode {
            let i = p.saturating_sub(off);
            if (0..n).contains(&i) {
                out.push(i);
            }
        }
        out
    }

    /// Take the most urgent frame nobody has, if any and a decode slot is free.
    fn claim(&mut self) -> Option<Job> {
        if self.shutdown || !self.video || self.in_flight.len() >= self.parallelism {
            return None;
        }
        let source = self.source.clone()?;
        let skip = self.starve_skip();
        let index = self.candidates(skip).into_iter().find(|i| {
            !self.cache.contains(*i)
                && !self.in_flight.contains_key(i)
                && !self.failed.contains_key(i)
        })?;
        let cancel = Arc::new(AtomicBool::new(false));
        self.in_flight.insert(index, cancel.clone());
        // Jumping ahead of a late clock: the frames in between are late, decoders may drop them.
        let catch_up = (skip > 0 && index > self.playhead).then_some(Tick::ZERO);
        Some(Job {
            generation: self.generation,
            index,
            time: self.timeline.request_time(index),
            source,
            cancel,
            catch_up,
        })
    }

    fn finish(&mut self, job: &Job, outcome: Outcome, elapsed: Duration) {
        if job.generation != self.generation {
            return;
        }
        self.in_flight.remove(&job.index);
        match outcome {
            Outcome::Frame(frame) => {
                let secs = elapsed.as_secs_f64();
                self.ema_decode = if self.ema_decode > 0.0 {
                    self.ema_decode * 0.8 + secs * 0.2
                } else {
                    secs
                };
                let bytes = frame.byte_size() as u64;
                if bytes > self.observed_bytes {
                    self.observed_bytes = bytes;
                    self.replan();
                }
                let window = self.window();
                self.cache.insert(job.index, frame, &window);
            }
            Outcome::Cancelled => {}
            Outcome::Failed(msg) => {
                log::warn!("frame {}: {msg}", job.index);
                if self.failed.len() >= MAX_FAILED {
                    self.failed.clear();
                }
                self.failed.insert(job.index, Arc::from(msg));
            }
        }
    }
}

struct Shared {
    state: Mutex<State>,
    wake: Condvar,
}

impl Shared {
    fn lock(&self) -> MutexGuard<'_, State> {
        self.state.lock().unwrap_or_else(PoisonError::into_inner)
    }
}

fn run_job(job: &Job) -> Outcome {
    let request = FrameRequest::full(job.time);
    let decoded = catch_unwind(AssertUnwindSafe(|| {
        filmcraft_media::cancel::with_cancel(&job.cancel, || {
            filmcraft_media::cancel::with_catch_up(job.catch_up, || job.source.video_frame(request))
        })
    }));
    match decoded {
        Ok(Ok(frame)) if frame.width == 0 || frame.height == 0 => {
            Outcome::Failed("the decoder returned an empty picture".into())
        }
        Ok(Ok(frame)) => Outcome::Frame(frame),
        Ok(Err(filmcraft_media::MediaError::Cancelled)) => Outcome::Cancelled,
        Ok(Err(e)) => Outcome::Failed(e.to_string()),
        Err(_) => Outcome::Failed("the decoder failed on this frame".into()),
    }
}

fn worker_loop(shared: &Shared) {
    loop {
        let job = {
            let mut st = shared.lock();
            loop {
                if st.shutdown {
                    return;
                }
                if let Some(job) = st.claim() {
                    break job;
                }
                st = shared.wake.wait(st).unwrap_or_else(PoisonError::into_inner);
            }
        };
        let started = Instant::now();
        let outcome = run_job(&job);
        let elapsed = started.elapsed();
        shared.lock().finish(&job, outcome, elapsed);
        // other workers (and `view` callers) may have something to do now
        shared.wake.notify_all();
    }
}

fn parallelism_for(kind: SourceKind) -> usize {
    match kind {
        // independent files: decode several at once
        SourceKind::ImageSequence => std::thread::available_parallelism()
            .map_or(2, |n| n.get())
            .clamp(1, 4),
        // one stateful decoder: frames must arrive in order
        _ => 1,
    }
}

pub(crate) struct Decoder {
    shared: Arc<Shared>,
    workers: Vec<JoinHandle<()>>,
}

impl Decoder {
    pub fn new(budget: u64) -> Self {
        Self {
            shared: Arc::new(Shared {
                state: Mutex::new(State::new(budget)),
                wake: Condvar::new(),
            }),
            workers: Vec::new(),
        }
    }

    /// Replace the source. Everything of the previous one is dropped and its in-flight decodes cancelled.
    pub fn load(
        &mut self,
        generation: u64,
        source: SharedSource,
        timeline: Timeline,
        kind: SourceKind,
        video: bool,
        estimate_bytes: u64,
    ) -> Result<(), String> {
        let parallelism = if video { parallelism_for(kind) } else { 0 };
        {
            let mut st = self.shared.lock();
            st.cancel_all();
            st.cache.clear();
            st.failed.clear();
            st.generation = generation;
            st.source = Some(source);
            st.timeline = timeline;
            st.kind = kind;
            st.video = video;
            st.estimate_bytes = estimate_bytes;
            st.observed_bytes = 0;
            st.ema_decode = 0.0;
            st.playhead = 0;
            st.playing = false;
            st.parallelism = parallelism;
            st.replan();
        }
        let started = self.ensure_workers(parallelism);
        self.shared.wake.notify_all();
        started
    }

    /// Drop the source and everything cached.
    pub fn unload(&mut self, generation: u64) {
        {
            let mut st = self.shared.lock();
            st.cancel_all();
            st.cache.clear();
            st.failed.clear();
            st.generation = generation;
            st.source = None;
            st.video = false;
            st.timeline = Timeline::empty();
            st.plan = Plan {
                ahead: 0,
                behind: 0,
                behind_decode: 0,
            };
        }
        self.shared.wake.notify_all();
    }

    pub fn set_budget(&self, bytes: u64) {
        {
            let mut st = self.shared.lock();
            st.cache.set_budget(bytes);
            st.replan();
            let window = st.window();
            st.cache.evict(&window);
        }
        self.shared.wake.notify_all();
    }

    /// Tell the workers where playback is, and return the cached frame to show for `target`
    /// (the frame itself, else the nearest one; never one from the future while playing).
    pub fn view(
        &self,
        target: i64,
        playing: bool,
        speed: f64,
        looping: bool,
    ) -> Option<(i64, Arc<VideoFrame>)> {
        let (picked, wake) = {
            let mut st = self.shared.lock();
            let target = target.max(0).min(st.timeline.last_index());
            let moved = st.playhead != target;
            let changed =
                moved || st.playing != playing || st.looping != looping || st.speed != speed;
            let replan = st.looping != looping || st.speed != speed || st.playing != playing;
            st.playhead = target;
            st.playing = playing;
            st.looping = looping;
            st.speed = speed;
            if replan {
                st.replan();
            }
            if moved {
                if !playing {
                    // seeking or stepping onto a frame that failed before tries it again
                    st.failed.remove(&target);
                }
                let window = st.window();
                for (index, flag) in &st.in_flight {
                    if !window.contains(*index) {
                        flag.store(true, Ordering::Relaxed);
                    }
                }
            }
            (
                st.cache
                    .nearest(target, playing)
                    .map(|(i, f)| (i, f.clone())),
                changed,
            )
        };
        if wake {
            self.shared.wake.notify_all();
        }
        picked
    }

    /// Why decoding `index` failed, if it did.
    pub fn failure(&self, index: i64) -> Option<Arc<str>> {
        self.shared.lock().failed.get(&index).cloned()
    }

    /// Fraction (0..=1) of the frames wanted from the playhead on that are cached without a gap.
    pub fn buffered(&self) -> f32 {
        let st = self.shared.lock();
        if !st.video || st.source.is_none() {
            return 0.0;
        }
        let n = st.timeline.frame_count;
        let ahead = if st.looping {
            st.plan.ahead
        } else {
            st.plan.ahead.min(n - 1 - st.playhead).max(0)
        };
        let wanted = ahead + 1;
        let have = (0..wanted)
            .take_while(|off| {
                st.wrap(st.playhead.saturating_add(*off))
                    .is_some_and(|i| st.cache.contains(i))
            })
            .count() as f32;
        (have / wanted as f32).clamp(0.0, 1.0)
    }

    /// Inclusive ranges of cached frame indices.
    pub fn cached_ranges(&self) -> Vec<(i64, i64)> {
        self.shared.lock().cache.ranges()
    }

    fn ensure_workers(&mut self, wanted: usize) -> Result<(), String> {
        while self.workers.len() < wanted {
            let shared = self.shared.clone();
            let name = format!("gb-decode-{}", self.workers.len());
            let spawned = std::thread::Builder::new().name(name).spawn(move || {
                if catch_unwind(AssertUnwindSafe(|| worker_loop(&shared))).is_err() {
                    log::error!("a decode worker stopped after a panic");
                }
            });
            match spawned {
                Ok(handle) => self.workers.push(handle),
                Err(e) if self.workers.is_empty() => {
                    return Err(format!("could not start a decode thread: {e}"));
                }
                Err(e) => {
                    log::warn!("could not start another decode thread: {e}");
                    break;
                }
            }
        }
        Ok(())
    }

    #[cfg(test)]
    pub fn plan(&self) -> (i64, i64) {
        let st = self.shared.lock();
        (st.plan.ahead, st.plan.behind)
    }

    #[cfg(test)]
    pub fn cache_len(&self) -> usize {
        self.shared.lock().cache.len()
    }

    #[cfg(test)]
    pub fn in_flight(&self) -> usize {
        self.shared.lock().in_flight.len()
    }
}

impl Drop for Decoder {
    fn drop(&mut self) {
        {
            let mut st = self.shared.lock();
            st.shutdown = true;
            st.cancel_all();
        }
        self.shared.wake.notify_all();
        // bounded: a worker stuck in a slow decode finishes on its own
        let deadline = Instant::now() + JOIN_LIMIT;
        while self.workers.iter().any(|w| !w.is_finished()) && Instant::now() < deadline {
            std::thread::sleep(Duration::from_millis(2));
        }
        for worker in self.workers.drain(..) {
            if worker.is_finished() {
                let _ = worker.join();
            } else {
                log::warn!("a decode worker did not stop in time; detaching it");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
    use super::*;
    use crate::testutil::{Synth, frame_id, wait_until};
    use gallerybrowser_media::FrameRate;
    use std::sync::atomic::Ordering;

    const MB: u64 = 1024 * 1024;

    fn load(d: &mut Decoder, generation: u64, synth: Synth) -> Arc<Synth> {
        let timeline = Timeline::new(&synth.info);
        let kind = synth.info.kind;
        let (concrete, shared) = synth.shared();
        d.load(generation, shared, timeline, kind, true, 16)
            .expect("load");
        concrete
    }

    #[test]
    fn prefetches_ahead_of_the_playhead_in_order_and_reports_the_buffer() {
        let mut d = Decoder::new(64 * MB);
        let src = load(&mut d, 1, Synth::movie(1, FrameRate::FPS_24, 10.0));
        let (ahead, _) = d.plan();
        assert_eq!(
            ahead, 24,
            "paused: 1 s at 24 fps is enough to start playing at once"
        );
        d.view(0, false, 1.0, false);
        assert!(wait_until(|| d.buffered() >= 1.0));
        assert_eq!(d.cached_ranges(), vec![(0, 24)]);
        // a single stateful decoder is fed in order: 0, 1, 2, ...
        let requests = src.requests.lock().unwrap().clone();
        assert_eq!(requests, (0..=24).collect::<Vec<i64>>());
        // playing: 3 s ahead from the playhead (a jump past the cached frames leaves a gap behind it)
        d.view(30, true, 1.0, false);
        assert_eq!(d.plan().0, 72);
        assert!(wait_until(|| d.cached_ranges() == vec![(0, 24), (30, 102)]));
    }

    #[test]
    fn the_window_is_planned_inside_the_ram_budget() {
        let mut d = Decoder::new(16 * 10);
        load(&mut d, 1, Synth::movie(1, FrameRate::FPS_24, 10.0));
        // 16-byte frames, 160-byte budget: 10 frames fit; the window must leave room for both sides
        let (ahead, behind) = d.plan();
        assert!(
            ahead + behind < 10 && ahead >= 1,
            "ahead {ahead} behind {behind}"
        );
        d.view(100, true, 1.0, false);
        assert!(wait_until(|| d.buffered() >= 1.0));
        d.view(110, true, 1.0, false);
        assert!(wait_until(|| d.buffered() >= 1.0));
        assert!(d.cache_len() <= 10, "cache holds {} frames", d.cache_len());
        // a budget smaller than two frames still pipelines one frame ahead
        d.set_budget(1);
        assert_eq!(d.plan().0, 1);
        // and growing it again widens the window
        d.set_budget(64 * MB);
        assert_eq!(d.plan().0, 72);
    }

    #[test]
    fn playback_speed_scales_the_window() {
        let mut d = Decoder::new(64 * MB);
        load(&mut d, 1, Synth::movie(1, FrameRate::FPS_24, 60.0));
        d.view(0, true, 2.0, false);
        assert_eq!(d.plan().0, 144);
        d.view(0, true, 0.1, false);
        assert_eq!(d.plan().0, 8);
        d.view(0, true, f64::NAN, false);
        assert_eq!(d.plan().0, 72, "a non-finite speed plans as normal speed");
    }

    fn planned_state(seconds: f64, playhead: i64, looping: bool) -> State {
        let synth = Synth::movie(1, FrameRate::FPS_24, seconds);
        let mut st = State::new(64 * MB);
        st.timeline = Timeline::new(&synth.info);
        st.video = true;
        st.estimate_bytes = 16;
        st.playhead = playhead;
        st.looping = looping;
        st.playing = true;
        st.replan();
        st
    }

    #[test]
    fn the_wanted_frames_run_forward_and_wrap_only_when_looping() {
        // 48 frames: the whole clip is the window
        let st = planned_state(2.0, 46, false);
        assert_eq!(st.candidates(0), vec![46, 47]);
        let st = planned_state(2.0, 46, true);
        let c = st.candidates(0);
        assert_eq!(c[..4], [46, 47, 0, 1]);
        let mut sorted = c.clone();
        sorted.sort_unstable();
        sorted.dedup();
        assert_eq!(
            (c.len(), sorted.len()),
            (48, 48),
            "every frame exactly once"
        );
        // a long clip with the playhead near the end: the window wraps to the start
        let st = planned_state(10.0, 238, true);
        let c = st.candidates(0);
        assert_eq!(c[..4], [238, 239, 0, 1]);
        assert_eq!(c.len(), 73);
        // a late clock jumps ahead first; the skipped frames come after the rest of the window
        let st = planned_state(10.0, 10, false);
        let c = st.candidates(3);
        assert_eq!(c[..2], [13, 14]);
        assert_eq!(c[c.len() - 3..], [10, 11, 12]);
    }

    #[test]
    fn looping_prefetch_buffers_across_the_wrap() {
        let mut d = Decoder::new(64 * MB);
        load(&mut d, 1, Synth::movie(1, FrameRate::FPS_24, 2.0)); // 48 frames
        d.view(46, true, 1.0, true);
        assert!(wait_until(|| d.buffered() >= 1.0));
        assert_eq!(d.cached_ranges(), vec![(0, 47)]);
    }

    #[test]
    fn nothing_from_a_previous_load_survives() {
        let mut d = Decoder::new(64 * MB);
        // source 1 decodes slowly: several of its decodes are in flight or finishing during the reload
        let slow = load(
            &mut d,
            1,
            Synth::movie(1, FrameRate::FPS_24, 10.0).with_delay_ms(15),
        );
        d.view(0, false, 1.0, false);
        assert!(wait_until(|| slow.decoded.load(Ordering::Relaxed) >= 2));
        load(&mut d, 2, Synth::movie(2, FrameRate::FPS_24, 10.0));
        let deadline = Instant::now() + Duration::from_millis(400);
        while Instant::now() < deadline {
            if let Some((_, f)) = d.view(0, false, 1.0, false) {
                assert_eq!(frame_id(&f).0, 2, "a frame of the old source was returned");
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        assert!(wait_until(|| d.view(0, false, 1.0, false).is_some()));
        // every cached frame is of the new source
        for (first, last) in d.cached_ranges() {
            for i in first..=last {
                let (_, f) = d.view(i, false, 1.0, false).expect("cached");
                assert_eq!(frame_id(&f), (2, i));
            }
        }
    }

    #[test]
    fn unload_drops_everything_and_stops_decoding() {
        let mut d = Decoder::new(64 * MB);
        let src = load(&mut d, 1, Synth::movie(1, FrameRate::FPS_24, 10.0));
        d.view(0, false, 1.0, false);
        assert!(wait_until(|| d.buffered() >= 1.0));
        d.unload(2);
        assert!(d.view(0, false, 1.0, false).is_none());
        assert_eq!(d.cache_len(), 0);
        assert_eq!(d.buffered(), 0.0);
        std::thread::sleep(Duration::from_millis(30));
        let decoded = src.decoded.load(Ordering::Relaxed);
        std::thread::sleep(Duration::from_millis(30));
        assert_eq!(src.decoded.load(Ordering::Relaxed), decoded);
    }

    #[test]
    fn a_failing_frame_is_remembered_not_retried_and_does_not_stop_the_rest() {
        let mut d = Decoder::new(64 * MB);
        let src = load(
            &mut d,
            1,
            Synth::movie(1, FrameRate::FPS_24, 10.0).failing_at(&[3]),
        );
        d.view(0, true, 1.0, false);
        assert!(wait_until(|| d.failure(3).is_some()));
        assert!(
            d.failure(3)
                .is_some_and(|m| m.contains("synthetic failure at frame 3"))
        );
        assert!(wait_until(|| d
            .cached_ranges()
            .last()
            .is_some_and(|r| r.1 >= 72)));
        std::thread::sleep(Duration::from_millis(30));
        assert_eq!(
            src.requests
                .lock()
                .unwrap()
                .iter()
                .filter(|&&i| i == 3)
                .count(),
            1,
            "playback prefetch does not retry a failed frame"
        );
        // landing on it explicitly (a seek) tries again — and now it works
        src.fail.lock().unwrap().clear();
        d.view(3, false, 1.0, false);
        assert!(wait_until(|| d
            .view(3, false, 1.0, false)
            .is_some_and(|(i, _)| i == 3)));
        assert!(d.failure(3).is_none());
    }

    #[test]
    fn a_panicking_decoder_is_a_failed_frame_not_a_dead_worker() {
        let mut d = Decoder::new(64 * MB);
        let _src = load(
            &mut d,
            1,
            Synth::movie(1, FrameRate::FPS_24, 10.0).panicking_at(&[2]),
        );
        d.view(0, false, 1.0, false);
        assert!(wait_until(|| d.failure(2).is_some()));
        assert!(
            wait_until(|| d.cached_ranges().last().is_some_and(|r| r.1 >= 10)),
            "frames after the panic are decoded"
        );
    }

    #[test]
    fn a_jump_cancels_decodes_that_are_no_longer_wanted() {
        let mut d = Decoder::new(64 * MB);
        let src = load(
            &mut d,
            1,
            Synth::movie(1, FrameRate::FPS_24, 20.0).with_delay_ms(400),
        );
        d.view(0, false, 1.0, false);
        assert!(wait_until(|| d.in_flight() == 1));
        let jumped = Instant::now();
        d.view(300, false, 1.0, false);
        assert!(wait_until(|| d
            .view(300, false, 1.0, false)
            .is_some_and(|(i, _)| i == 300)));
        // frame 0 was cancelled (not decoded for 400 ms), so frame 300 arrived after one decode
        assert!(
            jumped.elapsed() < Duration::from_millis(700),
            "took {:?}",
            jumped.elapsed()
        );
        assert!(!src.requests.lock().unwrap().contains(&0));
        assert!(d.view(0, false, 1.0, false).is_none_or(|(i, _)| i != 0));
    }

    #[test]
    fn sequences_decode_in_parallel_and_stills_only_once() {
        let mut d = Decoder::new(64 * MB);
        load(&mut d, 1, Synth::sequence(1, FrameRate::FPS_24, 100));
        d.view(50, false, 1.0, false);
        // behind frames are decoded too for sequences (random access is cheap)
        assert!(wait_until(
            || d.buffered() >= 1.0 && d.cached_ranges().first().is_some_and(|r| r.0 <= 42)
        ));
        let mut d = Decoder::new(64 * MB);
        let still = load(&mut d, 1, Synth::still(1));
        assert_eq!(d.plan(), (0, 0));
        d.view(0, false, 1.0, true);
        assert!(wait_until(|| d.view(0, false, 1.0, true).is_some()));
        std::thread::sleep(Duration::from_millis(20));
        assert_eq!(still.decoded.load(Ordering::Relaxed), 1);
        assert_eq!(d.buffered(), 1.0);
    }

    #[test]
    fn a_slow_decoder_skips_ahead_instead_of_falling_further_behind() {
        let mut d = Decoder::new(64 * MB);
        let src = load(
            &mut d,
            1,
            Synth::movie(1, FrameRate::FPS_24, 20.0).with_delay_ms(120),
        );
        d.view(0, true, 1.0, false);
        assert!(wait_until(|| d.view(0, true, 1.0, false).is_some()));
        // the playhead runs on while decodes take ~3 frames each: the next decode targets a frame
        // the clock will reach, not the one it just left behind
        d.view(24, true, 1.0, false);
        let before = src.requests.lock().unwrap().len();
        assert!(wait_until(|| src
            .requests
            .lock()
            .unwrap()
            .iter()
            .skip(before)
            .any(|&i| i >= 24)));
        let requests = src.requests.lock().unwrap().clone();
        let next = requests.iter().skip(before).copied().find(|&i| i >= 24);
        assert!(
            next.is_some_and(|i| i > 24),
            "decoded {requests:?} after {before}"
        );
    }
}
