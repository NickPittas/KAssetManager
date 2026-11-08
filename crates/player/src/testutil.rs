// Vendored from GalleryBrowser 55a4483: test-only crate/module; assertions may panic by design. See crates/VENDORED.md.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
//! Test doubles: a synthetic `MediaSource` whose frames encode their source id and index in their
//! pixels (so a test can tell exactly which frame it got, and from which load), and a sound
//! device that is driven by the test instead of by hardware.

use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, PoisonError};
use std::time::{Duration, Instant};

use filmcraft_frame::PixelData;
use filmcraft_media::{AudioStreamInfo, MediaError, VideoStreamInfo};
use gallerybrowser_media::{
    AudioBuffer, FrameRate, FrameRequest, MediaInfo, MediaSource, SharedSource, SourceKind, Tick,
    VideoFrame,
};

use crate::audio::{AudioBackend, AudioFormat, BackendFactory, ErrorFn, RenderFn};

pub(crate) fn synth_info(kind: SourceKind, fps: FrameRate, duration: Tick) -> MediaInfo {
    MediaInfo {
        name: "synthetic".into(),
        kind,
        duration,
        video: Some(VideoStreamInfo {
            width: 2,
            height: 2,
            frame_rate: fps,
            par: (1, 1),
            codec: "synthetic".into(),
            pixel_format: "rgba8".into(),
            color: VideoFrame::rgba8(1, 1, vec![0; 4]).color,
            has_alpha: false,
            bitrate: None,
            hdr: None,
        }),
        audio_streams: Vec::new(),
        container: "synthetic".into(),
        start_timecode: None,
        file_size: None,
    }
}

/// `(source id, frame index)` encoded in a synthetic frame's first pixel.
pub(crate) fn frame_id(frame: &VideoFrame) -> (u8, i64) {
    match &frame.data {
        PixelData::Rgba8(d) => (
            d.get(2).copied().unwrap_or(0),
            i64::from(d.first().copied().unwrap_or(0))
                | (i64::from(d.get(1).copied().unwrap_or(0)) << 8),
        ),
        _ => (0, -1),
    }
}

/// Sample `i` of channel `ch` of the synthetic audio: never exactly zero, so silence is detectable.
pub(crate) fn audio_value(i: i64, ch: usize) -> f32 {
    let base = (i.rem_euclid(1999) as f32 + 1.0) / 4000.0;
    if ch.is_multiple_of(2) {
        base
    } else {
        -base * 0.5
    }
}

pub(crate) struct Synth {
    pub info: MediaInfo,
    pub id: u8,
    fps: FrameRate,
    pub delay_ms: AtomicU64,
    /// Frames that fail to decode.
    pub fail: Mutex<Vec<i64>>,
    /// Frames whose decoder panics.
    pub panic_at: Mutex<Vec<i64>>,
    /// Every frame index asked for, in order.
    pub requests: Mutex<Vec<i64>>,
    pub decoded: AtomicUsize,
    audio_channels: usize,
    audio_fails: bool,
}

impl Synth {
    pub fn movie(id: u8, fps: FrameRate, seconds: f64) -> Self {
        Self::with_kind(id, SourceKind::Movie, fps, Tick::from_seconds_f64(seconds))
    }

    pub fn still(id: u8) -> Self {
        Self::with_kind(
            id,
            SourceKind::Still,
            FrameRate::FPS_24,
            Tick::from_seconds_f64(5.0),
        )
    }

    pub fn sequence(id: u8, fps: FrameRate, frames: i64) -> Self {
        Self::with_kind(id, SourceKind::ImageSequence, fps, fps.tick_of(frames))
    }

    fn with_kind(id: u8, kind: SourceKind, fps: FrameRate, duration: Tick) -> Self {
        Self {
            info: synth_info(kind, fps, duration),
            id,
            fps,
            delay_ms: AtomicU64::new(0),
            fail: Mutex::new(Vec::new()),
            panic_at: Mutex::new(Vec::new()),
            requests: Mutex::new(Vec::new()),
            decoded: AtomicUsize::new(0),
            audio_channels: 0,
            audio_fails: false,
        }
    }

    pub fn with_audio(mut self, channels: usize, rate: u32) -> Self {
        self.audio_channels = channels;
        self.info.audio_streams = vec![AudioStreamInfo {
            sample_rate: rate,
            channels: channels as u32,
            codec: "synthetic".into(),
            bits_per_sample: None,
        }];
        self
    }

    pub fn with_failing_audio(mut self) -> Self {
        self.audio_fails = true;
        self
    }

    pub fn with_delay_ms(self, ms: u64) -> Self {
        self.delay_ms.store(ms, Ordering::Relaxed);
        self
    }

    pub fn failing_at(self, frames: &[i64]) -> Self {
        self.fail
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .extend_from_slice(frames);
        self
    }

    pub fn panicking_at(self, frames: &[i64]) -> Self {
        self.panic_at
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .extend_from_slice(frames);
        self
    }

    pub fn shared(self) -> (Arc<Synth>, SharedSource) {
        let s = Arc::new(self);
        let shared: SharedSource = s.clone();
        (s, shared)
    }
}

impl MediaSource for Synth {
    fn info(&self) -> &MediaInfo {
        &self.info
    }

    fn video_frame(&self, req: FrameRequest) -> filmcraft_media::Result<Arc<VideoFrame>> {
        let delay = self.delay_ms.load(Ordering::Relaxed);
        // Wall-clock deadline, not N sleeps: a 1 ms sleep overshoots by several ms on loaded macOS CI.
        let until = Instant::now() + Duration::from_millis(delay);
        while Instant::now() < until {
            if filmcraft_media::cancel::cancelled() {
                return Err(MediaError::Cancelled);
            }
            std::thread::sleep(Duration::from_millis(1));
        }
        let index = if self.info.kind == SourceKind::Still {
            0
        } else {
            self.fps.frame_at(req.time)
        };
        self.requests
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(index);
        if self
            .panic_at
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .contains(&index)
        {
            panic!("synthetic decoder panic at frame {index}");
        }
        if self
            .fail
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .contains(&index)
        {
            return Err(MediaError::Decode(format!(
                "synthetic failure at frame {index}"
            )));
        }
        self.decoded.fetch_add(1, Ordering::Relaxed);
        let px = [
            (index & 0xff) as u8,
            ((index >> 8) & 0xff) as u8,
            self.id,
            255,
        ];
        let data: Vec<u8> = px.iter().copied().cycle().take(16).collect();
        Ok(Arc::new(
            VideoFrame::rgba8(2, 2, data).with_pts(self.fps.tick_of(index)),
        ))
    }

    fn audio(
        &self,
        start: i64,
        frames: usize,
        sample_rate: u32,
    ) -> filmcraft_media::Result<AudioBuffer> {
        if self.audio_channels == 0 {
            return Err(MediaError::NoStream("audio"));
        }
        if self.audio_fails {
            return Err(MediaError::Decode("synthetic audio failure".into()));
        }
        let channels = (0..self.audio_channels)
            .map(|c| {
                (0..frames)
                    .map(|i| audio_value(start + i as i64, c))
                    .collect()
            })
            .collect();
        Ok(AudioBuffer {
            sample_rate,
            channels,
        })
    }
}

pub(crate) fn synth_source_with_audio(
    id: u8,
    fps: FrameRate,
    seconds: f64,
    channels: usize,
    rate: u32,
) -> SharedSource {
    Synth::movie(id, fps, seconds)
        .with_audio(channels, rate)
        .shared()
        .1
}

pub(crate) fn failing_audio_source(id: u8, fps: FrameRate, seconds: f64) -> SharedSource {
    Synth::movie(id, fps, seconds)
        .with_audio(2, 48_000)
        .with_failing_audio()
        .shared()
        .1
}

/// A sound device the test drives: `pump(frames)` is the device asking for `frames` frames.
pub(crate) struct TestDevice {
    format: AudioFormat,
    render: Mutex<Option<RenderFn>>,
    fail: Option<String>,
    opens: AtomicUsize,
    attempts: AtomicUsize,
}

impl TestDevice {
    pub fn new(sample_rate: u32, channels: usize) -> Arc<Self> {
        Arc::new(Self {
            format: AudioFormat {
                sample_rate,
                channels,
            },
            render: Mutex::new(None),
            fail: None,
            opens: AtomicUsize::new(0),
            attempts: AtomicUsize::new(0),
        })
    }

    /// A device that can not be opened.
    pub fn failing(message: &str) -> Arc<Self> {
        Arc::new(Self {
            format: AudioFormat {
                sample_rate: 48_000,
                channels: 2,
            },
            render: Mutex::new(None),
            fail: Some(message.into()),
            opens: AtomicUsize::new(0),
            attempts: AtomicUsize::new(0),
        })
    }

    pub fn factory(self: &Arc<Self>) -> BackendFactory {
        let device = self.clone();
        Box::new(move || Box::new(TestBackend(device)))
    }

    /// Times the audio thread opened the device.
    pub fn opens(&self) -> usize {
        self.opens.load(Ordering::Relaxed)
    }

    /// Times the audio thread tried to open the device (successfully or not).
    pub fn attempts(&self) -> usize {
        self.attempts.load(Ordering::Relaxed)
    }

    /// The device asks for `frames` frames; returns what it was given (silence if no stream is open).
    pub fn pump(&self, frames: usize) -> Vec<f32> {
        let channels = self.format.channels;
        let mut buf = vec![0.0; frames * channels];
        if let Some(render) = self
            .render
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .as_mut()
        {
            render(&mut buf, channels);
        }
        buf
    }
}

struct TestBackend(Arc<TestDevice>);

impl AudioBackend for TestBackend {
    fn open(
        &mut self,
        attach: &mut dyn FnMut(AudioFormat) -> RenderFn,
        _on_error: ErrorFn,
    ) -> Result<AudioFormat, String> {
        self.0.attempts.fetch_add(1, Ordering::Relaxed);
        if let Some(message) = &self.0.fail {
            return Err(message.clone());
        }
        let render = attach(self.0.format);
        *self.0.render.lock().unwrap_or_else(PoisonError::into_inner) = Some(render);
        self.0.opens.fetch_add(1, Ordering::Relaxed);
        Ok(self.0.format)
    }

    fn close(&mut self) {
        *self.0.render.lock().unwrap_or_else(PoisonError::into_inner) = None;
    }
}

/// Poll `f` until it holds (true) or five seconds pass (false).
pub(crate) fn wait_until(mut f: impl FnMut() -> bool) -> bool {
    let deadline = Instant::now() + Duration::from_secs(5);
    loop {
        if f() {
            return true;
        }
        if Instant::now() >= deadline {
            return false;
        }
        std::thread::sleep(Duration::from_millis(1));
    }
}
