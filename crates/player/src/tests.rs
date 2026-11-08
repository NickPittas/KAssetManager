// Vendored from GalleryBrowser 55a4483: test-only crate/module; assertions may panic by design. See crates/VENDORED.md.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
//! Player behaviour through its public API, on synthetic sources and a sound device the test drives.
//! Frames encode (source id, frame index) in their pixels; time is injected through `tick(now)`.

use std::sync::Arc;
use std::time::{Duration, Instant};

use gallerybrowser_media::{FrameRate, SharedSource, Tick};

use crate::Player;
use crate::testutil::{
    Synth, TestDevice, audio_value, frame_id, synth_source_with_audio, wait_until,
};

const MB: u64 = 1024 * 1024;

fn player() -> Player {
    Player::with_audio_backend(64 * MB, TestDevice::new(48_000, 2).factory())
}

fn audio_player(device: &Arc<TestDevice>) -> Player {
    Player::with_audio_backend(64 * MB, device.factory())
}

fn movie(id: u8, fps: FrameRate, seconds: f64) -> SharedSource {
    Synth::movie(id, fps, seconds).shared().1
}

fn at(t0: Instant, secs: f64) -> Instant {
    t0 + Duration::from_secs_f64(secs)
}

/// Tick at `now` until the player shows frame `index`; returns every frame handed out on the way.
fn show(p: &mut Player, now: Instant, index: i64) -> Vec<(u8, i64)> {
    let mut seen = Vec::new();
    let arrived = wait_until(|| {
        if let Some(f) = p.tick(now) {
            seen.push(frame_id(&f));
        }
        seen.last().is_some_and(|&(_, i)| i == index)
    });
    assert!(
        arrived,
        "frame {index} never arrived (saw {seen:?}, state {:?})",
        p.state()
    );
    seen
}

fn session_started(p: &Player, device: &TestDevice) {
    assert!(
        wait_until(|| {
            device.pump(0);
            p.audio.clock().session == p.audio_session
        }),
        "the audio session never started"
    );
}

/// Wait until the mixer is `frames` ahead of the device, then let the device consume them.
fn pump_full(p: &Player, device: &TestDevice, frames: usize) -> Vec<f32> {
    assert!(
        wait_until(|| {
            device.pump(0);
            p.audio.produced() >= p.audio.clock().played + frames as u64
        }),
        "the mixer did not get {frames} frames ahead"
    );
    device.pump(frames)
}

/// Like `pump_full` for amounts larger than the ring holds: consumes `frames` in chunks.
fn pump_frames(p: &Player, device: &TestDevice, frames: usize) {
    let mut left = frames;
    while left > 0 {
        let n = left.min(9_600);
        pump_full(p, device, n);
        left -= n;
    }
}

#[test]
fn a_player_can_move_between_threads() {
    fn send<T: Send>() {}
    send::<Player>();
}

#[test]
fn nothing_loaded_is_inert() {
    let mut p = player();
    let s = p.state();
    assert!(!s.loaded && !s.playing && !s.has_audio);
    assert_eq!(
        (s.duration, s.frame_count, s.frame_index, s.position),
        (Tick::ZERO, 0, 0, Tick::ZERO)
    );
    p.play();
    p.step(3);
    p.seek(Tick::from_seconds_f64(1.0));
    p.stop();
    assert!(p.tick(Instant::now()).is_none());
    assert!(!p.state().playing);
    assert!(!p.needs_repaint());
}

#[test]
fn load_resets_duration_position_and_cache_but_keeps_user_settings() {
    let mut p = player();
    p.set_volume(0.3);
    p.set_muted(true);
    p.set_speed(2.0);
    p.set_looping(false);
    p.load(movie(1, FrameRate::FPS_24, 10.0), false);
    let s = p.state();
    assert!(s.loaded && !s.playing);
    assert_eq!(s.duration, Tick::from_seconds_f64(10.0));
    assert_eq!(
        (s.frame_count, s.frame_index, s.position),
        (240, 0, Tick::ZERO)
    );
    assert_eq!(s.frame_rate, FrameRate::FPS_24);
    p.seek(Tick::from_seconds_f64(5.0));
    assert_eq!(p.state().frame_index, 120);

    p.load(movie(2, FrameRate::FPS_25, 2.0), false);
    let s = p.state();
    assert_eq!(
        s.duration,
        Tick::from_seconds_f64(2.0),
        "the old duration is gone"
    );
    assert_eq!(
        (s.frame_count, s.frame_index, s.position),
        (50, 0, Tick::ZERO)
    );
    assert_eq!(s.frame_rate, FrameRate::FPS_25);
    assert!(
        (s.volume - 0.3).abs() < 1e-6 && s.muted && !s.looping && (s.speed - 2.0).abs() < 1e-6,
        "{s:?}"
    );
    // the first frame shown is the new source's, not the old one
    let now = Instant::now();
    assert_eq!(show(&mut p, now, 0).last(), Some(&(2, 0)));

    p.unload();
    let s = p.state();
    assert!(!s.loaded);
    assert_eq!(
        (s.duration, s.frame_count, s.position),
        (Tick::ZERO, 0, Tick::ZERO)
    );
    assert!(
        (s.volume - 0.3).abs() < 1e-6 && s.muted && !s.looping,
        "settings survive an unload too"
    );
    assert!(p.tick(now).is_none());
}

#[test]
fn the_first_frame_is_shown_without_playing_and_a_tick_returns_a_frame_only_when_it_changed() {
    let mut p = player();
    p.load(movie(1, FrameRate::FPS_24, 10.0), false);
    let t0 = Instant::now();
    assert_eq!(show(&mut p, t0, 0), vec![(1, 0)]);
    assert!(p.tick(t0).is_none());
    assert!(
        p.tick(at(t0, 3.0)).is_none(),
        "paused: time does not move the picture"
    );
    assert!(!p.state().playing);

    p.load(movie(1, FrameRate::FPS_24, 10.0), true);
    assert!(p.state().playing, "autoplay");
}

#[test]
fn play_advances_frames_over_time_and_never_shows_the_future() {
    let mut p = player();
    p.load(movie(1, FrameRate::FPS_24, 10.0), false);
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    p.play();
    assert!(p.state().playing);
    assert!(p.tick(t0).is_none(), "the clock anchors at the first tick");

    let seen = show(&mut p, at(t0, 0.5), 12);
    assert!(seen.iter().all(|&(_, i)| i <= 12), "{seen:?}");
    let s = p.state();
    assert_eq!(s.frame_index, 12);
    assert!((s.position.seconds() - 0.5).abs() < 1e-9);

    let seen = show(&mut p, at(t0, 1.0), 24);
    assert!(
        seen.windows(2).all(|w| w[0].1 < w[1].1),
        "frames only move forward: {seen:?}"
    );
    assert!(p.tick(at(t0, 1.0)).is_none());

    p.pause();
    assert!(!p.state().playing);
    let paused_at = p.state().position;
    assert!(p.tick(at(t0, 5.0)).is_none());
    assert_eq!(
        p.state().position,
        paused_at,
        "a paused player does not move"
    );
}

#[test]
fn stop_keeps_the_source_and_play_resumes_from_the_start() {
    let mut p = player();
    p.load(movie(1, FrameRate::FPS_24, 10.0), false);
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    p.play();
    p.tick(t0);
    show(&mut p, at(t0, 1.0), 24);

    p.stop();
    let s = p.state();
    assert!(
        s.loaded && !s.playing,
        "stop does not unload (the Python player lost the source here)"
    );
    assert_eq!(
        (s.position, s.frame_index, s.duration),
        (Tick::ZERO, 0, Tick::from_seconds_f64(10.0))
    );
    let t1 = at(t0, 2.0);
    show(&mut p, t1, 0);

    p.play();
    assert!(p.state().playing);
    assert!(p.tick(t1).is_none());
    show(&mut p, at(t1, 0.5), 12);
    assert!(p.state().playing);
    p.toggle();
    assert!(!p.state().playing);
    p.toggle();
    assert!(p.state().playing);
}

#[test]
fn step_and_seek_clamp_to_the_media_and_step_pauses() {
    let mut p = player();
    p.load(movie(1, FrameRate::FPS_25, 2.0), false);
    let t0 = Instant::now();
    assert_eq!(p.state().frame_count, 50);

    p.step(-5);
    assert_eq!((p.state().frame_index, p.state().position), (0, Tick::ZERO));
    p.step(1);
    assert_eq!(
        (p.state().frame_index, p.state().position),
        (1, FrameRate::FPS_25.tick_of(1))
    );
    show(&mut p, t0, 1);
    p.step(1000);
    assert_eq!(p.state().frame_index, 49);
    show(&mut p, t0, 49);
    p.step(-1000);
    assert_eq!(p.state().frame_index, 0);
    show(&mut p, t0, 0);

    p.play();
    assert!(p.state().playing);
    p.step(1);
    assert!(!p.state().playing, "stepping pauses");
    assert_eq!(p.state().frame_index, 1);

    p.seek(Tick(-5));
    assert_eq!(p.state().position, Tick::ZERO);
    p.seek(Tick::MAX);
    assert_eq!(
        (p.state().position, p.state().frame_index),
        (Tick::from_seconds_f64(2.0), 49)
    );
    p.seek_frame(-3);
    assert_eq!(p.state().frame_index, 0);
    p.seek_frame(999);
    assert_eq!(p.state().frame_index, 49);
    p.seek_frame(10);
    assert_eq!(
        (p.state().position, p.state().frame_index),
        (FrameRate::FPS_25.tick_of(10), 10)
    );
    show(&mut p, t0, 10);
    assert!(!p.state().playing, "seeking while paused does not play");
}

#[test]
fn looping_wraps_and_without_looping_playback_stops_on_the_last_frame() {
    let mut p = player();
    p.load(movie(1, FrameRate::FPS_25, 2.0), false);
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    assert!(p.state().looping);
    p.play();
    p.tick(t0);
    // 2.5 s into a 2 s clip: wrapped to 0.5 s and still playing
    show(&mut p, at(t0, 2.5), 12);
    let s = p.state();
    assert!(s.playing);
    assert!(
        (s.position.seconds() - 0.5).abs() < 1e-9,
        "{:?}",
        s.position
    );
    // the time past the end was not counted twice
    show(&mut p, at(t0, 3.0), 25);
    assert!((p.state().position.seconds() - 1.0).abs() < 1e-9);
    // many loops in one tick (a stalled UI) still land inside the clip
    p.tick(at(t0, 100.25));
    assert!(p.state().playing && p.state().position < Tick::from_seconds_f64(2.0));

    p.set_looping(false);
    p.seek(Tick::ZERO);
    let t1 = at(t0, 200.0);
    p.tick(t1);
    show(&mut p, at(t1, 2.5), 49);
    let s = p.state();
    assert!(!s.playing, "stopped at the end");
    assert_eq!(
        (s.frame_index, s.position),
        (49, Tick::from_seconds_f64(2.0))
    );
    assert!(p.tick(at(t1, 9.0)).is_none());

    // play from the end starts over
    p.play();
    assert!(p.state().playing);
    assert_eq!(p.state().position, Tick::ZERO);
    let t2 = at(t1, 10.0);
    p.tick(t2);
    show(&mut p, at(t2, 0.2), 5);
}

#[test]
fn frames_of_a_previous_load_are_never_returned() {
    let mut p = player();
    let (slow, a) = Synth::movie(1, FrameRate::FPS_24, 10.0)
        .with_delay_ms(20)
        .shared();
    p.load(a, true);
    let t0 = Instant::now();
    p.tick(t0);
    assert!(
        wait_until(|| slow.decoded.load(std::sync::atomic::Ordering::Relaxed) >= 1),
        "the old source decoded its first frame"
    );
    for k in 0..3 {
        p.tick(at(t0, 0.1 * f64::from(k)));
    }
    p.load(movie(2, FrameRate::FPS_24, 10.0), false);
    let started = Instant::now();
    let mut got = Vec::new();
    while started.elapsed() < Duration::from_millis(300) {
        if let Some(f) = p.tick(t0) {
            got.push(frame_id(&f));
        }
        std::thread::sleep(Duration::from_millis(3));
    }
    assert!(!got.is_empty(), "the new source shows its first frame");
    assert!(
        got.iter().all(|&(id, _)| id == 2),
        "a frame of the old source was returned: {got:?}"
    );
    assert!(slow.decoded.load(std::sync::atomic::Ordering::Relaxed) >= 1);
}

#[test]
fn volume_mute_and_speed_are_validated_clamped_and_kept() {
    let mut p = player();
    p.set_volume(2.0);
    assert_eq!(p.state().volume, 1.0);
    p.set_volume(-1.0);
    assert_eq!(p.state().volume, 0.0);
    p.set_volume(f32::NAN);
    assert_eq!(p.state().volume, 0.0, "NaN is ignored");
    p.set_volume(0.25);
    p.set_speed(100.0);
    assert_eq!(p.state().speed, 8.0);
    p.set_speed(0.0);
    assert_eq!(p.state().speed, 0.1);
    p.set_speed(f32::NAN);
    p.set_speed(f32::INFINITY);
    assert_eq!(p.state().speed, 0.1);
    p.set_speed(1.5);
    p.set_muted(true);
    for id in 1..=3 {
        p.load(movie(id, FrameRate::FPS_24, 2.0), false);
        let s = p.state();
        assert_eq!((s.volume, s.speed, s.muted), (0.25, 1.5, true), "load {id}");
    }
}

#[test]
fn a_frame_that_cannot_be_decoded_is_reported_and_playback_goes_on_without_it() {
    let mut p = player();
    let (_src, shared) = Synth::movie(1, FrameRate::FPS_24, 10.0)
        .failing_at(&[5])
        .shared();
    p.load(shared, false);
    let t0 = Instant::now();
    show(&mut p, t0, 0);

    p.seek_frame(5);
    assert!(wait_until(|| {
        p.tick(t0);
        p.state().error.is_some()
    }));
    let error = p.state().error.unwrap_or_default();
    assert!(
        error.contains("frame 5") && error.contains("synthetic failure"),
        "{error}"
    );
    assert!(
        !p.needs_repaint(),
        "nothing more will arrive for a failed frame"
    );

    p.seek_frame(8);
    show(&mut p, t0, 8);
    assert_eq!(
        p.state().error,
        None,
        "the error clears when the picture is right again"
    );

    // playing across it: the picture holds the frame before, then moves on
    p.seek_frame(3);
    show(&mut p, t0, 3);
    p.play();
    p.tick(t0);
    p.tick(at(t0, 0.09)); // frame 5 is due
    assert!(p.state().error.is_some());
    show(&mut p, at(t0, 0.3), 10);
    assert!(p.state().playing);
    assert_eq!(p.state().error, None);
}

#[test]
fn a_decoder_that_panics_does_not_take_the_player_down() {
    let mut p = player();
    let (_src, shared) = Synth::movie(1, FrameRate::FPS_24, 10.0)
        .panicking_at(&[2])
        .shared();
    p.load(shared, false);
    let t0 = Instant::now();
    p.seek_frame(2);
    assert!(wait_until(|| {
        p.tick(t0);
        p.state().error.is_some()
    }));
    p.seek_frame(7);
    show(&mut p, t0, 7);
    assert_eq!(p.state().error, None);
}

#[test]
fn latest_seek_wins_while_scrubbing() {
    let mut p = player();
    let (src, shared) = Synth::movie(1, FrameRate::FPS_24, 30.0)
        .with_delay_ms(150)
        .shared();
    p.load(shared, false);
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    let started = Instant::now();
    for target in [100, 200, 300] {
        p.seek_frame(target);
        p.tick(t0);
    }
    show(&mut p, t0, 300);
    assert!(
        started.elapsed() < Duration::from_millis(400),
        "took {:?}",
        started.elapsed()
    );
    let requests = src.requests.lock().map(|r| r.clone()).unwrap_or_default();
    assert!(
        !requests.contains(&100) && !requests.contains(&200),
        "superseded seeks were decoded: {requests:?}"
    );
}

#[test]
fn needs_repaint_covers_playing_and_a_frame_still_on_its_way() {
    let mut p = player();
    let (_src, shared) = Synth::movie(1, FrameRate::FPS_24, 10.0)
        .with_delay_ms(30)
        .shared();
    p.load(shared, false);
    assert!(p.needs_repaint(), "the first frame is on its way");
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    assert!(!p.needs_repaint());
    p.seek_frame(200);
    assert!(p.needs_repaint());
    show(&mut p, t0, 200);
    assert!(!p.needs_repaint());
    p.play();
    assert!(p.needs_repaint());
}

#[test]
fn stills_have_one_frame_and_do_not_play() {
    let mut p = player();
    let (_src, shared) = Synth::still(3).shared();
    p.load(shared, true);
    let s = p.state();
    assert_eq!(s.frame_count, 1);
    assert!(!s.playing, "a still can not play, not even with autoplay");
    assert_eq!(s.duration, FrameRate::FPS_24.frame_duration());
    let t0 = Instant::now();
    assert_eq!(show(&mut p, t0, 0).last(), Some(&(3, 0)));
    p.play();
    assert!(!p.state().playing);
    p.step(5);
    p.seek(Tick::from_seconds_f64(3.0));
    p.seek_frame(7);
    assert_eq!(p.state().frame_index, 0);
    assert!(p.tick(t0).is_none());
    assert_eq!(p.state().buffered, 1.0);
}

#[test]
fn hostile_frame_rates_fall_back_to_the_default_rate() {
    for rate in [
        FrameRate { num: 0, den: 1 },
        FrameRate { num: 1, den: 0 },
        FrameRate { num: -5, den: 1 },
        FrameRate { num: 0, den: 0 },
    ] {
        let mut p = player();
        p.load(movie(1, rate, 2.0), true);
        assert_eq!(p.state().frame_rate, FrameRate::default());
        assert!(p.state().frame_count > 1);
        let t0 = Instant::now();
        p.tick(t0);
        let want = FrameRate::default().frame_at(Tick::from_seconds_f64(1.0));
        show(&mut p, at(t0, 1.0), want);
        assert!(p.state().playing);
    }
}

#[test]
fn buffer_and_cache_report_the_window_and_follow_the_budget() {
    let mut p = player();
    p.load(movie(1, FrameRate::FPS_24, 10.0), false);
    assert!(wait_until(|| p.state().buffered >= 1.0));
    assert_eq!(p.cached_ranges(), vec![(0, 24)]);
    p.set_cache_bytes(1);
    assert_eq!(
        p.decoder.plan().0,
        1,
        "a budget below two frames still pipelines one frame"
    );
    p.set_cache_bytes(64 * MB);
    assert_eq!(p.decoder.plan().0, 24, "paused: one second ahead");
}

#[test]
fn dropping_a_player_busy_with_a_slow_decode_does_not_hang() {
    let mut p = player();
    let (_src, shared) = Synth::movie(1, FrameRate::FPS_24, 10.0)
        .with_delay_ms(2000)
        .shared();
    p.load(shared, false);
    std::thread::sleep(Duration::from_millis(50));
    let started = Instant::now();
    drop(p);
    assert!(
        started.elapsed() < Duration::from_millis(900),
        "drop took {:?}",
        started.elapsed()
    );
}

#[test]
fn audio_is_the_master_clock_when_the_source_has_sound() {
    let device = TestDevice::new(48_000, 2);
    let mut p = audio_player(&device);
    p.load(
        synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000),
        false,
    );
    assert!(p.state().has_audio);
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    p.play();
    session_started(&p, &device);
    pump_frames(&p, &device, 24_000);
    // `now` is irrelevant: the device has played half a second
    let seen = show(&mut p, t0, 12);
    assert!(seen.iter().all(|&(_, i)| i <= 12), "{seen:?}");
    assert!((p.state().position.seconds() - 0.5).abs() < 1e-6);
    pump_frames(&p, &device, 24_000);
    show(&mut p, t0, 24);
    assert!((p.state().position.seconds() - 1.0).abs() < 1e-6);
    assert!(p.state().playing && p.state().error.is_none());

    // pausing stops the sound where it is
    pump_full(&p, &device, 480);
    p.pause();
    assert!(
        (p.state().position.seconds() - 1.01).abs() < 1e-6,
        "paused at the device position"
    );
    assert!(wait_until(|| {
        device.pump(0);
        p.audio.clock().session == 0
    }));
    assert!(device.pump(64).iter().all(|&x| x == 0.0));
}

#[test]
fn volume_and_mute_reach_the_device_and_survive_a_load() {
    let device = TestDevice::new(48_000, 2);
    let mut p = audio_player(&device);
    p.set_volume(0.5);
    p.set_muted(true);
    p.load(
        synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000),
        false,
    );
    p.play();
    session_started(&p, &device);
    assert!(
        pump_full(&p, &device, 200).iter().all(|&x| x == 0.0),
        "muted"
    );
    p.set_muted(false);
    pump_full(&p, &device, 4096); // the gain ramps up over this callback
    let out = pump_full(&p, &device, 100);
    let base = 200 + 4096;
    for f in 0..100usize {
        let want = audio_value(base + f as i64, 0) * 0.5;
        assert!(
            (out[f * 2] - want).abs() < 1e-6,
            "frame {f}: {} vs {want}",
            out[f * 2]
        );
    }

    // another source keeps the volume
    p.load(
        synth_source_with_audio(2, FrameRate::FPS_24, 10.0, 2, 48_000),
        true,
    );
    session_started(&p, &device);
    let out = pump_full(&p, &device, 10);
    assert!((out[0] - audio_value(0, 0) * 0.5).abs() < 1e-6);
    let s = p.state();
    assert_eq!((s.volume, s.muted), (0.5, false));
}

#[test]
fn other_speeds_mute_the_audio_and_run_on_the_system_clock() {
    let device = TestDevice::new(48_000, 2);
    let mut p = audio_player(&device);
    p.set_speed(2.0);
    p.load(
        synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000),
        false,
    );
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    p.play();
    p.tick(t0);
    show(&mut p, at(t0, 1.0), 48);
    assert!((p.state().position.seconds() - 2.0).abs() < 1e-9);
    assert_eq!(device.opens(), 0, "no audio is decoded or played at 2x");

    // back to normal speed while playing: the sound takes over from where the picture is
    p.set_speed(1.0);
    session_started(&p, &device);
    assert_eq!(device.opens(), 1);
    pump_frames(&p, &device, 24_000);
    p.tick(at(t0, 1.0));
    assert!(
        (p.state().position.seconds() - 2.5).abs() < 1e-6,
        "{:?}",
        p.state().position
    );

    // and slower again: silent, system clock, half speed
    p.set_speed(0.5);
    assert!(wait_until(|| {
        device.pump(0);
        p.audio.clock().session == 0
    }));
    assert!(device.pump(64).iter().all(|&x| x == 0.0));
    p.tick(at(t0, 1.1));
    p.tick(at(t0, 2.1));
    assert!(
        (p.state().position.seconds() - 3.0).abs() < 1e-6,
        "{:?}",
        p.state().position
    );
}

#[test]
fn a_missing_audio_device_means_silent_playback_with_an_error() {
    let device = TestDevice::failing("no output device");
    let mut p = audio_player(&device);
    p.load(
        synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000),
        false,
    );
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    p.play();
    let mut now = t0;
    assert!(wait_until(|| {
        now += Duration::from_millis(5);
        p.tick(now);
        p.state().error.is_some()
    }));
    let error = p.state().error.unwrap_or_default();
    assert!(error.contains("no output device"), "{error}");
    assert!(p.state().playing, "playback goes on, silently");
    // the system clock carries on from where the audio clock gave up
    let from = p.state().position;
    p.tick(now + Duration::from_millis(5));
    p.tick(now + Duration::from_millis(1005));
    assert!((p.state().position.seconds() - from.seconds() - 1.0).abs() < 1e-6);

    // seeking and speed changes do not hammer the broken device; an explicit play tries again
    assert_eq!(device.attempts(), 1);
    p.seek(Tick::from_seconds_f64(4.0));
    p.set_speed(1.5);
    p.set_speed(1.0);
    p.tick(now + Duration::from_millis(1010));
    assert_eq!(device.attempts(), 1);
    assert!(
        p.state().error.is_some(),
        "the error stays up while the sound is down"
    );
    p.pause();
    p.play();
    assert!(wait_until(|| device.attempts() == 2));
    assert!(wait_until(|| {
        now += Duration::from_millis(5);
        p.tick(now);
        p.state().error.is_some()
    }));
}

#[test]
fn seeking_while_playing_restarts_the_sound_at_the_new_position_and_stop_silences_it() {
    let device = TestDevice::new(48_000, 2);
    let mut p = audio_player(&device);
    p.load(
        synth_source_with_audio(1, FrameRate::FPS_24, 10.0, 2, 48_000),
        false,
    );
    p.play();
    session_started(&p, &device);
    let first = p.audio_session;
    p.seek(Tick::from_seconds_f64(5.0));
    assert_ne!(p.audio_session, first);
    session_started(&p, &device);
    let out = pump_full(&p, &device, 10);
    assert_eq!(out[0], audio_value(5 * 48_000, 0));
    p.tick(Instant::now());
    assert!((p.state().position.seconds() - (5.0 + 10.0 / 48_000.0)).abs() < 1e-6);

    p.stop();
    assert!(wait_until(|| {
        device.pump(0);
        p.audio.clock().session == 0
    }));
    assert!(device.pump(64).iter().all(|&x| x == 0.0));
    assert_eq!(p.state().position, Tick::ZERO);

    p.play();
    session_started(&p, &device);
    assert_eq!(pump_full(&p, &device, 10)[0], audio_value(0, 0));
}

#[test]
fn an_audio_decode_failure_is_reported_but_the_clock_and_picture_keep_going() {
    let device = TestDevice::new(48_000, 2);
    let mut p = audio_player(&device);
    p.load(
        crate::testutil::failing_audio_source(1, FrameRate::FPS_24, 10.0),
        false,
    );
    let t0 = Instant::now();
    show(&mut p, t0, 0);
    p.play();
    session_started(&p, &device);
    pump_full(&p, &device, 12_000);
    assert!(wait_until(|| {
        p.tick(at(t0, 0.01));
        p.state().error.is_some_and(|e| e.contains("audio decode"))
    }));
    assert!(p.state().playing);
    assert!(
        (p.state().position.seconds() - 0.25).abs() < 1e-6,
        "the audio clock still runs: {:?}",
        p.state().position
    );
}

#[test]
fn looping_restarts_the_sound_at_the_start() {
    let device = TestDevice::new(48_000, 2);
    let mut p = audio_player(&device);
    p.load(
        synth_source_with_audio(1, FrameRate::FPS_24, 1.0, 2, 48_000),
        false,
    );
    p.play();
    session_started(&p, &device);
    let first = p.audio_session;
    let t0 = Instant::now();
    // the device plays past the end of the 1 s clip
    let mut played = 0usize;
    while played < 48_000 + 2_400 {
        let chunk = 4_800;
        pump_full(&p, &device, chunk);
        played += chunk;
    }
    p.tick(t0);
    assert_ne!(
        p.audio_session, first,
        "wrapping started a new audio session"
    );
    assert!(
        p.state().playing && p.state().position < Tick::from_seconds_f64(0.2),
        "{:?}",
        p.state().position
    );
    // the new session starts where the wrap put the position, not at the old one
    let base = p.state().position.to_units_floor(48_000);
    assert!(base > 0);
    session_started(&p, &device);
    assert_eq!(pump_full(&p, &device, 10)[0], audio_value(base, 0));
}

/// A mono 16-bit PCM WAV of a sine at `hz`, amplitude 0.5.
fn wav_bytes(rate: u32, seconds: f64, hz: f64) -> Vec<u8> {
    let frames = (f64::from(rate) * seconds) as usize;
    let mut data = Vec::with_capacity(frames * 2);
    for i in 0..frames {
        let v = (f64::sin(2.0 * std::f64::consts::PI * hz * i as f64 / f64::from(rate))
            * 0.5
            * 32767.0) as i16;
        data.extend_from_slice(&v.to_le_bytes());
    }
    let mut out = Vec::new();
    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&(36 + data.len() as u32).to_le_bytes());
    out.extend_from_slice(b"WAVEfmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes()); // PCM
    out.extend_from_slice(&1u16.to_le_bytes()); // mono
    out.extend_from_slice(&rate.to_le_bytes());
    out.extend_from_slice(&(rate * 2).to_le_bytes());
    out.extend_from_slice(&2u16.to_le_bytes());
    out.extend_from_slice(&16u16.to_le_bytes());
    out.extend_from_slice(b"data");
    out.extend_from_slice(&(data.len() as u32).to_le_bytes());
    out.extend_from_slice(&data);
    out
}

#[test]
fn a_real_audio_file_plays_on_the_audio_clock_resampled_and_spread_to_the_device_channels() {
    let dir = tempfile::tempdir().expect("tempdir");
    let path = dir.path().join("tone.wav");
    std::fs::write(&path, wav_bytes(44_100, 1.0, 440.0)).expect("write wav");
    let source = filmcraft_media::open_path(&path, &[]).expect("open wav");

    let device = TestDevice::new(48_000, 2);
    let mut p = audio_player(&device);
    p.load(source, false);
    let s = p.state();
    assert!(s.loaded && s.has_audio);
    assert!(
        (s.duration.seconds() - 1.0).abs() < 1e-3,
        "{:?}",
        s.duration
    );
    assert!(
        p.tick(Instant::now()).is_none(),
        "an audio file has no pictures"
    );

    p.play();
    session_started(&p, &device);
    let out = pump_full(&p, &device, 4_800);
    // the 44.1 kHz mono file reaches the 48 kHz stereo device as the same tone on both channels
    let (left, right): (Vec<f32>, Vec<f32>) = out.chunks(2).map(|f| (f[0], f[1])).unzip();
    assert_eq!(left, right);
    let peak = left.iter().fold(0.0f32, |m, x| m.max(x.abs()));
    assert!((0.45..=0.51).contains(&peak), "peak {peak}");
    let crossings = left
        .windows(2)
        .filter(|w| (w[0] < 0.0) != (w[1] < 0.0))
        .count();
    assert!(
        (86..=90).contains(&crossings),
        "440 Hz over 0.1 s is 88 crossings, got {crossings}"
    );

    p.tick(Instant::now());
    assert!((p.state().position.seconds() - 0.1).abs() < 1e-6);
    assert!(p.state().playing && p.state().error.is_none());
}
