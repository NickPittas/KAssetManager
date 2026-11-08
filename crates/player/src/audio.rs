//! Audio output: a control thread mixes the source's audio ahead into a lock-free ring and the
//! device callback only copies samples out of it.
//!
//! Design adapted from FilmCraft `crates/ui-egui/src/play_ahead.rs` (ring buffer kept ahead of the
//! device, underruns played as silence and skipped by the mixer so sound stays locked to the
//! clock) and `apps/filmcraft/src/audio.rs` (the played-frame count is the master clock). Changes
//! for GalleryBrowser:
//!
//! * One long-lived thread owns the device (a `cpal::Stream` is `!Send`), opens it lazily on first
//!   play and closes it after [`IDLE_CLOSE`] of silence, so opening the device never blocks the UI.
//! * One ring serves every play/seek ("session"): starting a session records the ring's write
//!   position as a boundary and bumps an epoch; the callback discards everything before the
//!   boundary and restarts its played-frame counter. No lock is taken in the callback.
//! * Volume and mute are atomics read in the callback and applied with a short ramp (no clicks).
//! * The source is asked for audio at the device rate (`MediaSource::audio` resamples); channels
//!   are mapped to the device layout here (mono → L/R, 5.1/7.1 → stereo, otherwise copied).
//!
//! The clock counts frames handed to the device, so it leads what is audible by the device buffer
//! (typically 10–40 ms); it is constant, not drifting.

use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, PoisonError};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use crossbeam_channel::{Receiver, RecvTimeoutError, Sender};
use gallerybrowser_media::{AudioBuffer, SharedSource, Tick};

/// Interleaved f32 samples requested by the device: `render(buffer, device_channels)`.
pub type RenderFn = Box<dyn FnMut(&mut [f32], usize) + Send + 'static>;
/// Called from the device thread when the stream fails (device unplugged, backend error).
pub type ErrorFn = Arc<dyn Fn(String) + Send + Sync + 'static>;
/// Builds the backend on the audio thread (a backend need not be `Send`).
pub type BackendFactory = Box<dyn FnOnce() -> Box<dyn AudioBackend> + Send + 'static>;

/// The format the device runs at; the mixer produces exactly this.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AudioFormat {
    pub sample_rate: u32,
    pub channels: usize,
}

/// An audio output device. `open` picks the device format, calls `attach(format)` to obtain the
/// render callback, and starts a stream that calls it. Lives on the audio thread only.
pub trait AudioBackend {
    fn open(
        &mut self,
        attach: &mut dyn FnMut(AudioFormat) -> RenderFn,
        on_error: ErrorFn,
    ) -> Result<AudioFormat, String>;
    fn close(&mut self);
}

/// How far ahead of the device the ring is kept (s).
const LEAD_S: f64 = 0.4;
/// Frames mixed per block.
pub(crate) const BLOCK_FRAMES: usize = 2048;
/// Blocks mixed per wake-up of the audio thread (bounds how long commands wait).
const BLOCKS_PER_PUMP: usize = 4;
/// Audio thread wake-up interval while a session runs.
const PUMP_INTERVAL: Duration = Duration::from_millis(5);
/// Silence after which the device is closed.
const IDLE_CLOSE: Duration = Duration::from_secs(20);
/// Most device channels / source channels handled.
const MAX_CHANNELS: usize = 32;

/// The session id occupies the top 24 bits of the clock word, the played frames the low 40.
pub(crate) const SESSION_MASK: u64 = (1 << 24) - 1;
const PLAYED_BITS: u32 = 40;
const PLAYED_MASK: u64 = (1 << PLAYED_BITS) - 1;
/// Fault session matching every session (the audio thread died).
const ANY_SESSION: u64 = u64::MAX;

/// Next session id (never 0, which means "no session").
pub(crate) fn next_session(session: u64) -> u64 {
    (session % SESSION_MASK) + 1
}

/// What the device clock says: frames consumed in `session` at `rate`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct ClockSample {
    pub session: u64,
    pub played: u64,
    pub rate: u32,
}

#[derive(Default)]
struct Faults {
    fatal: Option<(u64, String)>,
    soft: Option<(u64, String)>,
}

/// State shared by the Player (control), the audio thread and the device callback.
struct Link {
    /// f32 bits.
    volume: AtomicU32,
    muted: AtomicBool,
    /// Session hand-over, written by the audio thread; `epoch` last (Release) so the callback
    /// that observes it (Acquire) sees the rest.
    epoch: AtomicU64,
    boundary: AtomicU64,
    session: AtomicU64,
    active: AtomicBool,
    /// `(session << 40) | frames played`, written only by the callback; 0 when no session runs.
    clock: AtomicU64,
    /// Frames the callback played as silence (ring ran dry) that the mixer has not skipped yet.
    skip: AtomicU64,
    rate: AtomicU32,
    /// Frames of media time mixed in the current session (diagnostics).
    produced: AtomicU64,
    /// The stream reported an error since the device was opened: reopen on the next session.
    stream_failed: AtomicBool,
    /// The Player paused: the callback plays silence at once, before the audio thread has
    /// processed the pause. Cleared by the next session start.
    hold: AtomicBool,
    faults: Mutex<Faults>,
}

impl Link {
    fn new() -> Self {
        Self {
            volume: AtomicU32::new(1.0f32.to_bits()),
            muted: AtomicBool::new(false),
            epoch: AtomicU64::new(0),
            boundary: AtomicU64::new(0),
            session: AtomicU64::new(0),
            active: AtomicBool::new(false),
            clock: AtomicU64::new(0),
            skip: AtomicU64::new(0),
            rate: AtomicU32::new(0),
            produced: AtomicU64::new(0),
            stream_failed: AtomicBool::new(false),
            hold: AtomicBool::new(false),
            faults: Mutex::new(Faults::default()),
        }
    }

    fn faults(&self) -> std::sync::MutexGuard<'_, Faults> {
        self.faults.lock().unwrap_or_else(PoisonError::into_inner)
    }

    fn begin_session(&self, session: u64, boundary: u64) {
        self.boundary.store(boundary, Ordering::Release);
        self.session.store(session, Ordering::Release);
        self.active.store(true, Ordering::Release);
        self.hold.store(false, Ordering::Release);
        self.skip.store(0, Ordering::Release);
        self.epoch.fetch_add(1, Ordering::AcqRel);
    }

    fn end_session(&self, boundary: u64) {
        self.boundary.store(boundary, Ordering::Release);
        self.active.store(false, Ordering::Release);
        self.epoch.fetch_add(1, Ordering::AcqRel);
    }

    fn set_fatal(&self, session: u64, msg: String) {
        log::warn!("audio: {msg}");
        self.faults().fatal = Some((session, msg));
    }

    fn set_soft(&self, session: u64, msg: String) {
        log::warn!("audio: {msg}");
        self.faults().soft = Some((session, msg));
    }

    fn stream_error(&self, msg: String) {
        self.stream_failed.store(true, Ordering::Release);
        self.set_fatal(self.session.load(Ordering::Acquire), msg);
    }

    fn gain(&self) -> f32 {
        if self.muted.load(Ordering::Relaxed) {
            return 0.0;
        }
        let v = f32::from_bits(self.volume.load(Ordering::Relaxed));
        if v.is_finite() {
            v.clamp(0.0, 1.0)
        } else {
            1.0
        }
    }
}

/// Single-producer, single-consumer ring of interleaved f32 samples (stored as bits, so it needs
/// no `unsafe`). Indices count samples written/read since creation and only grow.
/// (From FilmCraft `play_ahead.rs`; `skip_to` added for session changes.)
struct Ring {
    slots: Vec<AtomicU32>,
    write: AtomicU64,
    read: AtomicU64,
}

impl Ring {
    fn new(samples: usize) -> Self {
        Self {
            slots: (0..samples.max(1)).map(|_| AtomicU32::new(0)).collect(),
            write: AtomicU64::new(0),
            read: AtomicU64::new(0),
        }
    }

    fn capacity(&self) -> usize {
        self.slots.len()
    }

    fn write_pos(&self) -> u64 {
        self.write.load(Ordering::Acquire)
    }

    fn len(&self) -> usize {
        self.write
            .load(Ordering::Acquire)
            .saturating_sub(self.read.load(Ordering::Acquire)) as usize
    }

    fn free(&self) -> usize {
        self.capacity().saturating_sub(self.len())
    }

    /// Producer: append as many samples as fit; returns how many were written.
    fn push(&self, src: &[f32]) -> usize {
        let cap = self.slots.len();
        let w = self.write.load(Ordering::Relaxed);
        let n = src.len().min(self.free());
        for (i, x) in src.iter().take(n).enumerate() {
            if let Some(s) = self.slots.get(((w + i as u64) % cap as u64) as usize) {
                s.store(x.to_bits(), Ordering::Relaxed);
            }
        }
        self.write.store(w + n as u64, Ordering::Release);
        n
    }

    /// Consumer: discard everything before write position `pos` (never past what was written).
    fn skip_to(&self, pos: u64) {
        let target = pos.min(self.write.load(Ordering::Acquire));
        let r = self.read.load(Ordering::Relaxed);
        if target > r {
            self.read.store(target, Ordering::Release);
        }
    }

    /// Consumer: fill `dst` (`dev_ch` interleaved channels) with whole frames of `ch` channels:
    /// the first `min(ch, dev_ch)` channels copied, any further device channels zeroed. Returns
    /// the frames copied; the rest of `dst` is left for the caller.
    fn pop(&self, dst: &mut [f32], dev_ch: usize, ch: usize) -> usize {
        let cap = self.slots.len();
        let dev_ch = dev_ch.max(1);
        let ch = ch.max(1);
        let r = self.read.load(Ordering::Relaxed);
        let avail = self.write.load(Ordering::Acquire).saturating_sub(r) as usize / ch;
        let n = (dst.len() / dev_ch).min(avail);
        for (f, frame) in dst.chunks_mut(dev_ch).take(n).enumerate() {
            for (c, x) in frame.iter_mut().enumerate() {
                *x = if c < ch {
                    self.slots
                        .get(((r + (f * ch + c) as u64) % cap as u64) as usize)
                        .map_or(0.0, |s| f32::from_bits(s.load(Ordering::Relaxed)))
                } else {
                    0.0
                };
            }
        }
        self.read.store(r + (n * ch) as u64, Ordering::Release);
        n
    }
}

/// The device-thread side: pops the ring, applies volume, advances the clock.
struct Renderer {
    link: Arc<Link>,
    ring: Arc<Ring>,
    ring_channels: usize,
    seen_epoch: u64,
    active: bool,
    gain: f32,
}

impl Renderer {
    fn new(link: Arc<Link>, ring: Arc<Ring>, ring_channels: usize) -> Self {
        // A renderer starts inactive and in step with the current epoch: only a later session
        // start (a new epoch) activates it, so a stale boundary of an older ring is never applied.
        let seen_epoch = link.epoch.load(Ordering::Acquire);
        let gain = link.gain();
        Self {
            link,
            ring,
            ring_channels,
            seen_epoch,
            active: false,
            gain,
        }
    }

    fn render(&mut self, buf: &mut [f32], dev_ch: usize) {
        let dev_ch = dev_ch.max(1);
        let epoch = self.link.epoch.load(Ordering::Acquire);
        if epoch != self.seen_epoch {
            self.seen_epoch = epoch;
            self.ring
                .skip_to(self.link.boundary.load(Ordering::Acquire));
            self.active = self.link.active.load(Ordering::Acquire);
            let session = self.link.session.load(Ordering::Acquire) & SESSION_MASK;
            self.link.clock.store(
                if self.active {
                    session << PLAYED_BITS
                } else {
                    0
                },
                Ordering::Release,
            );
            if self.active {
                // a session starts at the current volume instead of fading in from a stale one
                self.gain = self.link.gain();
            }
        }
        if !self.active || self.link.hold.load(Ordering::Acquire) {
            buf.fill(0.0);
            return;
        }
        let frames = buf.len() / dev_ch;
        let have = self.ring.pop(buf, dev_ch, self.ring_channels);
        if let Some(rest) = buf.get_mut(have * dev_ch..) {
            rest.fill(0.0);
        }
        if have < frames {
            self.link
                .skip
                .fetch_add((frames - have) as u64, Ordering::AcqRel);
        }
        self.apply_gain(buf, dev_ch, frames);
        self.link.clock.fetch_add(frames as u64, Ordering::Release);
    }

    /// Linear ramp from the previous gain to the current one across the buffer; also keeps
    /// samples finite and inside [-1, 1] (a hostile decoder can return anything).
    fn apply_gain(&mut self, buf: &mut [f32], dev_ch: usize, frames: usize) {
        let target = self.link.gain();
        let step = (target - self.gain) / frames.max(1) as f32;
        let mut g = self.gain;
        for frame in buf.chunks_mut(dev_ch) {
            g += step;
            for s in frame {
                let v = *s * g;
                *s = if v.is_finite() {
                    v.clamp(-1.0, 1.0)
                } else {
                    0.0
                };
            }
        }
        self.gain = target;
    }

    /// The callback handed to the backend; a panic plays silence and fails the stream.
    fn into_render_fn(mut self) -> RenderFn {
        let mut failed = false;
        Box::new(move |buf: &mut [f32], dev_ch: usize| {
            if failed || catch_unwind(AssertUnwindSafe(|| self.render(buf, dev_ch))).is_err() {
                buf.fill(0.0);
                if !failed {
                    failed = true;
                    self.link
                        .stream_error("the audio callback failed; playing without sound".into());
                }
            }
        })
    }
}

/// Mixing matrix from `src` channels to `dst` device channels: `gains[d * src + s]`.
#[derive(Clone, Debug, PartialEq)]
pub(crate) struct ChannelMap {
    src: usize,
    dst: usize,
    gains: Vec<f32>,
}

const MINUS_3_DB: f32 = std::f32::consts::FRAC_1_SQRT_2;

impl ChannelMap {
    pub fn new(src: usize, dst: usize) -> Self {
        let src = src.clamp(1, MAX_CHANNELS);
        let dst = dst.clamp(1, MAX_CHANNELS);
        let mut gains = vec![0.0f32; src * dst];
        {
            let mut set = |d: usize, s: usize, g: f32| {
                if let Some(x) = gains.get_mut(d * src + s) {
                    *x = g;
                }
            };
            if src == dst {
                for i in 0..src {
                    set(i, i, 1.0);
                }
            } else if src == 1 {
                // mono to the front pair
                for d in 0..dst.min(2) {
                    set(d, 0, 1.0);
                }
            } else if dst == 1 {
                for s in 0..src {
                    set(0, s, 1.0 / src as f32);
                }
            } else if dst == 2 {
                let (left, right) = stereo_weights(src);
                for (s, g) in left {
                    set(0, s, g);
                }
                for (s, g) in right {
                    set(1, s, g);
                }
            } else {
                // stereo (or anything narrower) onto a wider device, or a wider source onto a
                // device with more than two channels: channels line up, the rest is dropped
                for i in 0..src.min(dst) {
                    set(i, i, 1.0);
                }
            }
        }
        Self { src, dst, gains }
    }

    /// Mix planar `planes` (each at least `frames` long is NOT assumed) into interleaved `out`.
    fn mix(&self, planes: &[Vec<f32>], frames: usize, out: &mut [f32]) {
        for d in 0..self.dst {
            for (s, plane) in planes.iter().take(self.src).enumerate() {
                let g = self.gains.get(d * self.src + s).copied().unwrap_or(0.0);
                if g == 0.0 {
                    continue;
                }
                for (f, x) in plane.iter().take(frames).enumerate() {
                    if let Some(o) = out.get_mut(f * self.dst + d) {
                        *o += g * x;
                    }
                }
            }
        }
    }
}

/// Unnormalised left/right contributions `(channel, gain)` of a standard layout of `src`
/// channels (3 or more) onto stereo.
fn stereo_downmix(src: usize) -> (Vec<(usize, f32)>, Vec<(usize, f32)>) {
    let k = MINUS_3_DB;
    match src {
        3 => (vec![(0, 1.0), (2, k)], vec![(1, 1.0), (2, k)]),
        4 => (vec![(0, 1.0), (2, k)], vec![(1, 1.0), (3, k)]),
        5 => (
            vec![(0, 1.0), (2, k), (3, k)],
            vec![(1, 1.0), (2, k), (4, k)],
        ),
        6 => (
            vec![(0, 1.0), (2, k), (4, k)],
            vec![(1, 1.0), (2, k), (5, k)],
        ),
        // 7.1 and wider: the first eight channels are L R C LFE Ls Rs Lb Rb
        n if n >= 7 => (
            vec![(0, 1.0), (2, k), (4, k), (6, k)],
            vec![(1, 1.0), (2, k), (5, k), (7usize.min(n - 1), k)],
        ),
        // unknown layout: even channels left, odd right
        n => (
            (0..n).step_by(2).map(|s| (s, 1.0)).collect(),
            (1..n).step_by(2).map(|s| (s, 1.0)).collect(),
        ),
    }
}

/// Scale each side of a downmix so a full-scale signal on every contributing channel stays at
/// full scale.
fn normalized(mut side: Vec<(usize, f32)>) -> Vec<(usize, f32)> {
    let sum: f32 = side.iter().map(|(_, g)| g).sum();
    if sum > 0.0 {
        for (_, g) in side.iter_mut() {
            *g /= sum;
        }
    }
    side
}

/// Left/right contributions `(channel, gain)` for `src` channels (3 or more) onto stereo.
fn stereo_weights(src: usize) -> (Vec<(usize, f32)>, Vec<(usize, f32)>) {
    let (left, right) = stereo_downmix(src);
    (normalized(left), normalized(right))
}

/// Linear-interpolating rate conversion of one plane to `frames` frames at `to` Hz. Only a safety
/// net: sources are asked for the device rate and resample themselves.
fn resample_linear(plane: &[f32], from: u32, to: u32, frames: usize) -> Vec<f32> {
    let ratio = f64::from(from) / f64::from(to.max(1));
    (0..frames)
        .map(|i| {
            let pos = i as f64 * ratio;
            let i0 = pos.floor() as usize;
            let frac = (pos - i0 as f64) as f32;
            let a = plane.get(i0).copied().unwrap_or(0.0);
            let b = plane.get(i0 + 1).copied().unwrap_or(a);
            a + (b - a) * frac
        })
        .collect()
}

/// Interleave `buf` into `out` (`frames` frames of `dev_ch` channels), converting channel layout
/// (and rate, if the source ignored the request). Short buffers leave silence.
pub(crate) fn fill_block(
    buf: &AudioBuffer,
    rate: u32,
    dev_ch: usize,
    frames: usize,
    map: &mut Option<ChannelMap>,
    out: &mut [f32],
) {
    let src_ch = buf.channels.len().min(MAX_CHANNELS);
    if src_ch == 0 || dev_ch == 0 {
        return;
    }
    let planes = buf.channels.get(..src_ch).unwrap_or_default();
    let converted;
    let planes = if buf.sample_rate != 0 && buf.sample_rate != rate {
        converted = planes
            .iter()
            .map(|p| resample_linear(p, buf.sample_rate, rate, frames))
            .collect::<Vec<_>>();
        &converted[..]
    } else {
        planes
    };
    let usable = map
        .as_ref()
        .is_some_and(|m| m.src == src_ch && m.dst == dev_ch);
    if !usable {
        *map = Some(ChannelMap::new(src_ch, dev_ch));
    }
    if let Some(m) = map.as_ref() {
        m.mix(planes, frames, out);
    }
}

/// One play/seek: mixes `source` from `base_sample` (device-rate sample index) on.
struct Mixer {
    source: SharedSource,
    session: u64,
    base_sample: i64,
    /// Device frames of media mixed (or skipped) since `base_sample`.
    cursor: i64,
    /// First sample index with no audio; blocks from there on are silence without asking the source.
    end_sample: i64,
    rate: u32,
    channels: usize,
    block: Vec<f32>,
    map: Option<ChannelMap>,
    reported: bool,
    /// The source panicked: only silence from now on.
    dead: bool,
}

impl Mixer {
    /// Mix the next block into `self.block`.
    fn mix_block(&mut self, link: &Link) {
        let frames = BLOCK_FRAMES;
        self.block.clear();
        self.block.resize(frames * self.channels, 0.0);
        let start = self.base_sample.saturating_add(self.cursor);
        if !self.dead && start < self.end_sample {
            // never ask for audio past the end of the media
            let want = usize::try_from(self.end_sample - start)
                .unwrap_or(frames)
                .min(frames);
            let (source, rate) = (&self.source, self.rate);
            match catch_unwind(AssertUnwindSafe(|| source.audio(start, want, rate))) {
                Ok(Ok(buf)) => fill_block(
                    &buf,
                    self.rate,
                    self.channels,
                    want,
                    &mut self.map,
                    &mut self.block,
                ),
                Ok(Err(e)) => {
                    self.report(link, format!("audio decode failed at sample {start}: {e}"))
                }
                Err(_) => {
                    self.dead = true;
                    self.report(
                        link,
                        "the audio decoder failed; playing without sound".into(),
                    );
                }
            }
        }
        self.cursor = self.cursor.saturating_add(frames as i64);
    }

    fn report(&mut self, link: &Link, msg: String) {
        if !self.reported {
            self.reported = true;
            link.set_soft(self.session, msg);
        }
    }
}

struct Open {
    format: AudioFormat,
    ring: Arc<Ring>,
}

struct AudioThread {
    link: Arc<Link>,
    factory: Option<BackendFactory>,
    backend: Option<Box<dyn AudioBackend>>,
    open: Option<Open>,
    mixer: Option<Mixer>,
}

impl AudioThread {
    fn ensure_open(&mut self) -> Result<(), String> {
        if self.link.stream_failed.swap(false, Ordering::AcqRel) {
            self.close();
        }
        if self.open.is_some() {
            return Ok(());
        }
        if self.backend.is_none() {
            let factory = self
                .factory
                .take()
                .ok_or("the audio backend is unavailable")?;
            self.backend = Some(factory());
        }
        let backend = self
            .backend
            .as_mut()
            .ok_or("the audio backend is unavailable")?;
        let link = self.link.clone();
        let mut ring_slot: Option<Arc<Ring>> = None;
        let mut attach = |format: AudioFormat| -> RenderFn {
            let channels = format.channels.clamp(1, MAX_CHANNELS);
            let lead = ((f64::from(format.sample_rate) * LEAD_S) as usize).max(BLOCK_FRAMES * 2);
            let ring = Arc::new(Ring::new(lead.saturating_mul(channels)));
            ring_slot = Some(ring.clone());
            Renderer::new(link.clone(), ring, channels).into_render_fn()
        };
        let on_error: ErrorFn = {
            let link = self.link.clone();
            Arc::new(move |msg| link.stream_error(msg))
        };
        let format = match backend.open(&mut attach, on_error) {
            Ok(f) => f,
            Err(e) => {
                backend.close();
                return Err(e);
            }
        };
        if format.sample_rate == 0 || format.channels == 0 || format.channels > MAX_CHANNELS {
            backend.close();
            return Err(format!(
                "the audio device reported an unusable format ({} Hz, {} channels)",
                format.sample_rate, format.channels
            ));
        }
        let Some(ring) = ring_slot else {
            backend.close();
            return Err("the audio backend did not start a stream".into());
        };
        self.link.rate.store(format.sample_rate, Ordering::Release);
        self.open = Some(Open { format, ring });
        Ok(())
    }

    fn close(&mut self) {
        self.mixer = None;
        self.open = None;
        if let Some(b) = self.backend.as_mut() {
            b.close();
        }
    }

    fn begin(&mut self, start: Start) {
        self.halt();
        if let Err(msg) = self.ensure_open() {
            self.link.set_fatal(start.session, msg);
            return;
        }
        let Some(open) = self.open.as_ref() else {
            return;
        };
        let rate = i64::from(open.format.sample_rate);
        let mut mixer = Mixer {
            source: start.source,
            session: start.session,
            base_sample: start.position.to_units_floor(rate).max(0),
            cursor: 0,
            end_sample: start.duration.to_units_floor(rate).max(0),
            rate: open.format.sample_rate,
            channels: open.format.channels,
            block: Vec::new(),
            map: None,
            reported: false,
            dead: false,
        };
        // The first block is mixed before the session is announced, so the clock starts with sound.
        mixer.mix_block(&self.link);
        self.link.produced.store(0, Ordering::Release);
        self.link
            .begin_session(start.session, open.ring.write_pos());
        open.ring.push(&mixer.block);
        self.link
            .produced
            .store(mixer.cursor.max(0) as u64, Ordering::Release);
        self.mixer = Some(mixer);
    }

    fn halt(&mut self) {
        if self.mixer.take().is_some()
            && let Some(open) = self.open.as_ref()
        {
            self.link.end_session(open.ring.write_pos());
        }
    }

    /// Keep the ring filled to its lead.
    fn pump(&mut self) {
        let (Some(mixer), Some(open)) = (self.mixer.as_mut(), self.open.as_ref()) else {
            return;
        };
        let ch = open.format.channels;
        for _ in 0..BLOCKS_PER_PUMP {
            if open.ring.free() < BLOCK_FRAMES * ch {
                break;
            }
            // frames the device played as silence since: skip them so sound stays on the clock
            mixer.cursor = mixer
                .cursor
                .saturating_add(self.link.skip.swap(0, Ordering::AcqRel) as i64);
            mixer.mix_block(&self.link);
            // silence played while this block was mixing: its first frames are already late
            let late = self.link.skip.swap(0, Ordering::AcqRel) as usize;
            let dropped = late.min(BLOCK_FRAMES);
            mixer.cursor = mixer.cursor.saturating_add((late - dropped) as i64);
            open.ring
                .push(mixer.block.get(dropped * ch..).unwrap_or_default());
            self.link
                .produced
                .store(mixer.cursor.max(0) as u64, Ordering::Release);
        }
    }
}

struct Start {
    session: u64,
    source: SharedSource,
    position: Tick,
    duration: Tick,
}

enum Cmd {
    Start(Start),
    Pause,
    Shutdown,
}

fn audio_main(rx: Receiver<Cmd>, link: Arc<Link>, factory: BackendFactory) {
    let ran = catch_unwind(AssertUnwindSafe(|| run(&rx, &link, factory)));
    if ran.is_err() {
        link.set_fatal(
            ANY_SESSION,
            "the audio thread failed; playing without sound".into(),
        );
    }
}

fn run(rx: &Receiver<Cmd>, link: &Arc<Link>, factory: BackendFactory) {
    let mut t = AudioThread {
        link: link.clone(),
        factory: Some(factory),
        backend: None,
        open: None,
        mixer: None,
    };
    loop {
        let received = if t.mixer.is_some() {
            rx.recv_timeout(PUMP_INTERVAL)
        } else if t.open.is_some() {
            rx.recv_timeout(IDLE_CLOSE)
        } else {
            rx.recv().map_err(|_| RecvTimeoutError::Disconnected)
        };
        let mut cmd = match received {
            Ok(c) => Some(c),
            Err(RecvTimeoutError::Timeout) => {
                if t.mixer.is_none() {
                    t.close();
                }
                None
            }
            Err(RecvTimeoutError::Disconnected) => break,
        };
        // latest command wins (a scrub while playing queues many starts); shutdown always wins
        while let Ok(next) = rx.try_recv() {
            cmd = Some(match (cmd, next) {
                (Some(Cmd::Shutdown), _) => Cmd::Shutdown,
                (_, next) => next,
            });
        }
        match cmd {
            Some(Cmd::Shutdown) => break,
            Some(Cmd::Start(s)) => t.begin(s),
            Some(Cmd::Pause) => t.halt(),
            None => {}
        }
        t.pump();
    }
    t.close();
}

/// The Player's handle on the audio thread.
pub(crate) struct AudioEngine {
    link: Arc<Link>,
    tx: Sender<Cmd>,
    pending: Option<(Receiver<Cmd>, BackendFactory)>,
    thread: Option<JoinHandle<()>>,
}

impl AudioEngine {
    pub fn new(factory: BackendFactory) -> Self {
        let (tx, rx) = crossbeam_channel::unbounded();
        Self {
            link: Arc::new(Link::new()),
            tx,
            pending: Some((rx, factory)),
            thread: None,
        }
    }

    /// Start (or restart) playing `source` from `position`. The thread is spawned on first use.
    pub fn start(&mut self, session: u64, source: SharedSource, position: Tick, duration: Tick) {
        if let Some((rx, factory)) = self.pending.take() {
            let link = self.link.clone();
            match std::thread::Builder::new()
                .name("gb-audio".into())
                .spawn(move || audio_main(rx, link, factory))
            {
                Ok(handle) => self.thread = Some(handle),
                Err(e) => {
                    self.link.set_fatal(
                        ANY_SESSION,
                        format!("could not start the audio thread: {e}"),
                    );
                    return;
                }
            }
        }
        if self
            .tx
            .send(Cmd::Start(Start {
                session,
                source,
                position,
                duration,
            }))
            .is_err()
        {
            self.link.set_fatal(
                ANY_SESSION,
                "the audio thread is not running; playing without sound".into(),
            );
        }
    }

    /// Silence the output and stop mixing (the device stays open for a while).
    pub fn pause(&self) {
        if self.thread.is_some() {
            // silence at the next device callback, without waiting for the audio thread (which
            // may be in a slow decode); the next session start clears it
            self.link.hold.store(true, Ordering::Release);
            let _ = self.tx.send(Cmd::Pause);
        }
    }

    pub fn set_volume(&self, volume: f32) {
        self.link.volume.store(volume.to_bits(), Ordering::Relaxed);
    }

    pub fn set_muted(&self, muted: bool) {
        self.link.muted.store(muted, Ordering::Relaxed);
    }

    pub fn clock(&self) -> ClockSample {
        let word = self.link.clock.load(Ordering::Acquire);
        ClockSample {
            session: word >> PLAYED_BITS,
            played: word & PLAYED_MASK,
            rate: self.link.rate.load(Ordering::Acquire),
        }
    }

    /// Why audio can not run for `session` (no device, stream lost, thread gone).
    pub fn fatal(&self, session: u64) -> Option<String> {
        self.link
            .faults()
            .fatal
            .as_ref()
            .filter(|(s, _)| *s == session || *s == ANY_SESSION)
            .map(|(_, m)| m.clone())
    }

    /// A non-fatal problem in `session` (a block that failed to decode): sound has a gap, the clock runs on.
    pub fn soft_error(&self, session: u64) -> Option<String> {
        self.link
            .faults()
            .soft
            .as_ref()
            .filter(|(s, _)| *s == session)
            .map(|(_, m)| m.clone())
    }

    /// Frames of media mixed so far in the running session (tests wait on it).
    #[cfg(test)]
    pub fn produced(&self) -> u64 {
        self.link.produced.load(Ordering::Acquire)
    }
}

impl Drop for AudioEngine {
    fn drop(&mut self) {
        let _ = self.tx.send(Cmd::Shutdown);
        if let Some(handle) = self.thread.take() {
            join_within(handle, Duration::from_millis(500));
        }
    }
}

/// Join `handle` if it finishes within `limit`; otherwise leave it to finish on its own (a thread
/// stuck in a slow decode must not freeze the UI on exit).
pub(crate) fn join_within(handle: JoinHandle<()>, limit: Duration) {
    let deadline = Instant::now() + limit;
    while !handle.is_finished() {
        if Instant::now() >= deadline {
            log::warn!("a worker thread did not stop within {limit:?}; detaching it");
            return;
        }
        std::thread::sleep(Duration::from_millis(2));
    }
    let _ = handle.join();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testutil::{TestDevice, audio_value, synth_source_with_audio, wait_until};
    use gallerybrowser_media::FrameRate;

    #[test]
    fn ring_wraps_and_pops_whole_frames_with_channel_padding() {
        let ring = Ring::new(8);
        assert_eq!(ring.push(&[1.0, 2.0, 3.0, 4.0, 5.0, 6.0]), 6);
        assert_eq!(ring.push(&[7.0, 8.0, 9.0]), 2, "only what fits");
        let mut out = [9.0f32; 3];
        // ring holds stereo; device is 3-channel: third channel zeroed
        assert_eq!(ring.pop(&mut out, 3, 2), 1);
        assert_eq!(out[..3], [1.0, 2.0, 0.0]);
        assert_eq!(ring.len(), 6);
        assert_eq!(ring.push(&[10.0, 11.0]), 2);
        let mut out = [0.0f32; 8];
        assert_eq!(ring.pop(&mut out, 2, 2), 4);
        assert_eq!(out, [3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 10.0, 11.0]);
    }

    #[test]
    fn ring_skip_never_passes_the_write_position() {
        let ring = Ring::new(16);
        ring.push(&[1.0; 6]);
        ring.skip_to(4);
        assert_eq!(ring.len(), 2);
        ring.skip_to(1_000);
        assert_eq!(ring.len(), 0);
        ring.skip_to(2);
        assert_eq!(ring.len(), 0, "never goes backwards");
    }

    #[test]
    fn channel_maps() {
        // mono -> stereo duplicates, 3-channel device leaves the rest silent
        let m = ChannelMap::new(1, 3);
        let mut out = vec![0.0; 6];
        m.mix(&[vec![0.5, -0.5]], 2, &mut out);
        assert_eq!(out, [0.5, 0.5, 0.0, -0.5, -0.5, 0.0]);
        // stereo -> mono averages
        let m = ChannelMap::new(2, 1);
        let mut out = vec![0.0; 2];
        m.mix(&[vec![1.0, 0.0], vec![0.0, 1.0]], 2, &mut out);
        assert_eq!(out, [0.5, 0.5]);
        // 5.1 -> stereo: full-scale on every contributing channel stays at full scale; LFE dropped
        let m = ChannelMap::new(6, 2);
        let planes: Vec<Vec<f32>> = (0..6)
            .map(|c| vec![if c == 3 { 1.0 } else { 0.5 }])
            .collect();
        let mut out = vec![0.0; 2];
        m.mix(&planes, 1, &mut out);
        assert!(
            (out[0] - 0.5).abs() < 1e-5 && (out[1] - 0.5).abs() < 1e-5,
            "{out:?}"
        );
        // odd layouts fold alternating channels
        let m = ChannelMap::new(9, 2);
        let mut out = vec![0.0; 2];
        m.mix(&vec![vec![1.0]; 9], 1, &mut out);
        assert!(
            out[0] <= 1.0 + 1e-5 && out[1] <= 1.0 + 1e-5 && out[0] > 0.9,
            "{out:?}"
        );
        // hostile channel counts clamp
        let m = ChannelMap::new(0, 0);
        assert_eq!((m.src, m.dst), (1, 1));
        let m = ChannelMap::new(10_000, 10_000);
        assert_eq!((m.src, m.dst), (MAX_CHANNELS, MAX_CHANNELS));
    }

    #[test]
    fn fill_block_handles_short_uneven_and_mismatched_buffers() {
        let mut map = None;
        let mut out = vec![0.0; 8 * 2];
        // uneven planes, shorter than the block: the rest stays silent
        let buf = AudioBuffer {
            sample_rate: 48_000,
            channels: vec![vec![1.0; 4], vec![1.0; 2]],
        };
        fill_block(&buf, 48_000, 2, 8, &mut map, &mut out);
        assert_eq!(out[..8], [1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 1.0, 0.0]);
        assert!(out[8..].iter().all(|&x| x == 0.0));
        // a source that ignored the requested rate is resampled linearly (24k -> 48k doubles)
        let mut out = vec![0.0; 4];
        let buf = AudioBuffer {
            sample_rate: 24_000,
            channels: vec![vec![0.0, 1.0]],
        };
        fill_block(&buf, 48_000, 1, 4, &mut map, &mut out);
        assert_eq!(out, [0.0, 0.5, 1.0, 1.0]);
        // empty buffers are fine
        fill_block(&AudioBuffer::default(), 48_000, 2, 8, &mut map, &mut out);
    }

    /// Wait until the mixer is `frames` ahead of the device, then let the device consume them.
    fn pump_full(engine: &AudioEngine, device: &TestDevice, frames: usize) -> Vec<f32> {
        assert!(
            wait_until(|| {
                device.pump(0);
                engine.link.produced.load(Ordering::Acquire)
                    >= engine.clock().played + frames as u64
            }),
            "the mixer did not get {frames} frames ahead"
        );
        device.pump(frames)
    }

    fn wait_for_session(engine: &AudioEngine, device: &TestDevice, session: u64) {
        assert!(
            wait_until(|| {
                device.pump(0);
                engine.clock().session == session
            }),
            "session {session} never started"
        );
    }

    #[test]
    fn sessions_follow_the_device_clock_and_restart_on_a_new_session() {
        let device = TestDevice::new(48_000, 2);
        let mut engine = AudioEngine::new(device.factory());
        let source = synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000);
        let duration = source.info().duration;
        engine.start(1, source.clone(), Tick::ZERO, duration);
        wait_for_session(&engine, &device, 1);
        assert_eq!(
            engine.clock(),
            ClockSample {
                session: 1,
                played: 0,
                rate: 48_000
            }
        );
        let out = pump_full(&engine, &device, 1000);
        assert_eq!(engine.clock().played, 1000);
        // the first frames are the source's first frames, channel by channel
        assert_eq!(out[0], audio_value(0, 0));
        assert_eq!(out[2 * 999 + 1], audio_value(999, 1));

        // a new session at 1.0 s discards what is left of the old one
        engine.start(2, source, Tick::from_seconds_f64(1.0), duration);
        wait_for_session(&engine, &device, 2);
        let out = pump_full(&engine, &device, 10);
        assert_eq!(out[0], audio_value(48_000, 0));
        assert_eq!(
            engine.clock(),
            ClockSample {
                session: 2,
                played: 10,
                rate: 48_000
            }
        );

        // pause: silence at once (before the audio thread has even looked at the command), then
        // the clock is cleared
        engine.pause();
        assert!(device.pump(64).iter().all(|&x| x == 0.0));
        wait_for_session(&engine, &device, 0);
        assert!(device.pump(64).iter().all(|&x| x == 0.0));
        assert_eq!(engine.fatal(2), None);
    }

    #[test]
    fn volume_and_mute_apply_in_the_callback_without_a_session_restart() {
        let device = TestDevice::new(48_000, 2);
        let mut engine = AudioEngine::new(device.factory());
        let source = synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000);
        engine.set_volume(0.5);
        engine.start(
            1,
            source.clone(),
            Tick::from_seconds_f64(2.0),
            source.info().duration,
        );
        wait_for_session(&engine, &device, 1);
        pump_full(&engine, &device, 4096);
        let out = pump_full(&engine, &device, 100);
        let base = 2 * 48_000 + 4096;
        for f in 0..100usize {
            let want = audio_value(base + f as i64, 0) * 0.5;
            assert!(
                (out[f * 2] - want).abs() < 1e-6,
                "frame {f}: {} vs {want}",
                out[f * 2]
            );
        }
        engine.set_muted(true);
        pump_full(&engine, &device, 4096); // the gain ramps down over this callback
        assert!(pump_full(&engine, &device, 100).iter().all(|&x| x == 0.0));
        engine.set_muted(false);
        pump_full(&engine, &device, 4096);
        assert!(pump_full(&engine, &device, 10).iter().any(|&x| x != 0.0));
        engine.set_volume(f32::NAN);
        pump_full(&engine, &device, 4096);
        assert!(
            pump_full(&engine, &device, 10)
                .iter()
                .all(|x| x.is_finite())
        );
        assert_eq!(
            engine.clock().session,
            1,
            "volume and mute never restart the session"
        );
    }

    #[test]
    fn a_device_that_cannot_open_is_a_fatal_fault_for_that_session_only() {
        let device = TestDevice::failing("no output device");
        let mut engine = AudioEngine::new(device.factory());
        let source = synth_source_with_audio(1, FrameRate::FPS_24, 1.0, 2, 48_000);
        engine.start(5, source.clone(), Tick::ZERO, source.info().duration);
        assert!(wait_until(|| engine.fatal(5).is_some()));
        assert_eq!(engine.fatal(5).as_deref(), Some("no output device"));
        assert_eq!(engine.fatal(6), None);
        assert_eq!(engine.clock().session, 0);
    }

    #[test]
    fn audio_is_never_requested_past_the_end_of_the_media() {
        let device = TestDevice::new(48_000, 2);
        let mut engine = AudioEngine::new(device.factory());
        // start 10 ms (480 frames) before the end of a 1 s clip whose source would happily go on
        let source = synth_source_with_audio(1, FrameRate::FPS_24, 5.0, 2, 48_000);
        engine.start(
            1,
            source,
            Tick::from_seconds_f64(0.99),
            Tick::from_seconds_f64(1.0),
        );
        wait_for_session(&engine, &device, 1);
        let out = pump_full(&engine, &device, 2000);
        let audible = (0..2000).filter(|&f| out[f * 2] != 0.0).count();
        assert_eq!(audible, 480);
        assert_eq!(engine.soft_error(1), None);
    }

    #[test]
    fn a_source_that_fails_leaves_a_gap_and_a_soft_error_but_the_clock_runs() {
        let device = TestDevice::new(48_000, 2);
        let mut engine = AudioEngine::new(device.factory());
        let source = crate::testutil::failing_audio_source(2, FrameRate::FPS_24, 2.0);
        engine.start(3, source.clone(), Tick::ZERO, source.info().duration);
        wait_for_session(&engine, &device, 3);
        let out = pump_full(&engine, &device, 1000);
        assert!(out.iter().all(|&x| x == 0.0));
        assert!(wait_until(|| engine.soft_error(3).is_some()));
        assert_eq!(engine.fatal(3), None);
        assert_eq!(engine.clock().played, 1000);
    }

    #[test]
    fn underruns_are_skipped_so_sound_stays_on_the_clock() {
        let device = TestDevice::new(48_000, 2);
        let link = Arc::new(Link::new());
        let mut t = AudioThread {
            link: link.clone(),
            factory: Some(device.factory()),
            backend: None,
            open: None,
            mixer: None,
        };
        let source = synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000);
        t.begin(Start {
            session: 1,
            source: source.clone(),
            position: Tick::ZERO,
            duration: source.info().duration,
        });
        t.pump();
        device.pump(0);
        device.pump(1000);
        // the device asks for more than the ring holds: the rest is played as silence
        let queued = t.open.as_ref().map_or(0, |o| o.ring.len() / 2);
        assert!(queued > 1000);
        let out = device.pump(queued + 500);
        assert_eq!(
            out[(queued - 1) * 2],
            audio_value(1000 + queued as i64 - 1, 0)
        );
        assert!(
            out[queued * 2..].iter().all(|&x| x == 0.0),
            "the shortfall is silent"
        );
        assert_eq!(link.skip.load(Ordering::Acquire), 500);
        // the mixer skips the 500 silent frames: the next sound is the one at the device position
        t.pump();
        let played = 1000 + queued as i64 + 500;
        let out = device.pump(10);
        assert_eq!(out[0], audio_value(played, 0));
        assert_eq!(out[19], audio_value(played + 9, 1));
    }
}
