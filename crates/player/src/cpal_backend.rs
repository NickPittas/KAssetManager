//! The real sound device through cpal: the default output at its default format; the mixer
//! converts to it (rate by asking the source, channels by `audio::ChannelMap`, sample type here).
//!
//! Device choice and fallback order adapted from FilmCraft `apps/filmcraft/src/audio.rs`
//! (`output_device`, `fallback_rank`, `convert_samples`): when the default device cannot be opened
//! (a broken ALSA `default`) a working one is picked, the sound-server bridges first.
//!
//! Everything here runs on the audio thread (a `cpal::Stream` is `!Send`).

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};

use crate::audio::{AudioBackend, AudioFormat, BackendFactory, ErrorFn, RenderFn};

pub(crate) fn factory() -> BackendFactory {
    Box::new(|| {
        Box::new(CpalBackend {
            stream: None,
            fallback: None,
        })
    })
}

struct CpalBackend {
    stream: Option<cpal::Stream>,
    /// The fallback device name picked when the default would not open (reused next time).
    fallback: Option<String>,
}

/// Fallback order when the default device cannot be opened: the sound-server bridges first
/// (`pipewire`, `pulse`: they share the card with every other app), then other named PCMs, and raw
/// `hw:`/`plughw:` devices last (opening one takes the card exclusively and silences the rest of
/// the desktop).
fn fallback_rank(name: &str) -> u8 {
    let n = name.to_ascii_lowercase();
    if n == "pipewire" || n.starts_with("pipewire:") {
        0
    } else if n == "pulse" || n.starts_with("pulse:") {
        1
    } else if n.starts_with("hw:") || n.starts_with("plughw:") {
        3
    } else {
        2
    }
}

/// The default output device if it can be opened, else the best one that can.
fn output_device(host: &cpal::Host, cached: &mut Option<String>) -> Option<cpal::Device> {
    if let Some(d) = host.default_output_device()
        && d.default_output_config().is_ok()
    {
        return Some(d);
    }
    let mut devices: Vec<(String, cpal::Device)> = host
        .output_devices()
        .ok()?
        .filter_map(|d| d.name().ok().map(|n| (n, d)))
        .collect();
    if let Some(name) = cached.as_ref()
        && let Some(i) = devices.iter().position(|(n, _)| n == name)
    {
        return Some(devices.swap_remove(i).1);
    }
    devices.sort_by_key(|(n, _)| fallback_rank(n));
    let (name, device) = devices
        .into_iter()
        .find(|(_, d)| d.default_output_config().is_ok())?;
    log::warn!("the default audio output device cannot be opened; using '{name}'");
    *cached = Some(name);
    Some(device)
}

/// Integer conversion represents [-1, 1): clamp just below 1 and keep NaN/inf out of the device.
fn convert_samples<T: cpal::Sample + cpal::FromSample<f32>>(input: &[f32], output: &mut [T]) {
    let upper = f32::from_bits(1.0_f32.to_bits() - 1);
    for (sample, converted) in input.iter().zip(output) {
        *converted = T::from_sample(if sample.is_finite() {
            sample.clamp(-1.0, upper)
        } else {
            0.0
        });
    }
}

/// Most samples a device callback may ask for at once; a driver asking for more gets silence
/// (a pathological buffer must not make the audio thread allocate without bound).
const MAX_CALLBACK_SAMPLES: usize = 1_048_576;

fn converted_stream<T>(
    device: &cpal::Device,
    config: &cpal::StreamConfig,
    mut render: RenderFn,
    on_error: ErrorFn,
) -> Result<cpal::Stream, cpal::BuildStreamError>
where
    T: cpal::SizedSample + cpal::FromSample<f32>,
{
    let channels = usize::from(config.channels);
    let mut scratch: Vec<f32> = Vec::new();
    device.build_output_stream(
        config,
        move |buf: &mut [T], _| {
            if buf.len() > MAX_CALLBACK_SAMPLES
                || scratch
                    .try_reserve(buf.len().saturating_sub(scratch.len()))
                    .is_err()
            {
                buf.fill(T::EQUILIBRIUM);
                return;
            }
            scratch.clear();
            scratch.resize(buf.len(), 0.0);
            render(&mut scratch, channels);
            convert_samples(&scratch, buf);
        },
        move |e| on_error(e.to_string()),
        None,
    )
}

impl AudioBackend for CpalBackend {
    fn open(
        &mut self,
        attach: &mut dyn FnMut(AudioFormat) -> RenderFn,
        on_error: ErrorFn,
    ) -> Result<AudioFormat, String> {
        self.stream = None;
        let host = cpal::default_host();
        let device =
            output_device(&host, &mut self.fallback).ok_or("no audio output device found")?;
        let supported = device
            .default_output_config()
            .map_err(|e| format!("the audio output device has no usable format: {e}"))?;
        let channels = supported.channels();
        let sample_rate = supported.sample_rate().0;
        if channels == 0 || sample_rate == 0 {
            return Err(
                "the audio output device reported an invalid channel count or sample rate".into(),
            );
        }
        let format = AudioFormat {
            sample_rate,
            channels: usize::from(channels),
        };
        let mut render = attach(format);
        let config: cpal::StreamConfig = supported.clone().into();
        let stream = match supported.sample_format() {
            cpal::SampleFormat::F32 => {
                let count = usize::from(channels);
                device.build_output_stream(
                    &config,
                    move |buf: &mut [f32], _| render(buf, count),
                    move |e| on_error(e.to_string()),
                    None,
                )
            }
            cpal::SampleFormat::I8 => converted_stream::<i8>(&device, &config, render, on_error),
            cpal::SampleFormat::I16 => converted_stream::<i16>(&device, &config, render, on_error),
            cpal::SampleFormat::I24 => {
                converted_stream::<cpal::I24>(&device, &config, render, on_error)
            }
            cpal::SampleFormat::I32 => converted_stream::<i32>(&device, &config, render, on_error),
            cpal::SampleFormat::I64 => converted_stream::<i64>(&device, &config, render, on_error),
            cpal::SampleFormat::U8 => converted_stream::<u8>(&device, &config, render, on_error),
            cpal::SampleFormat::U16 => converted_stream::<u16>(&device, &config, render, on_error),
            cpal::SampleFormat::U32 => converted_stream::<u32>(&device, &config, render, on_error),
            cpal::SampleFormat::U64 => converted_stream::<u64>(&device, &config, render, on_error),
            cpal::SampleFormat::F64 => converted_stream::<f64>(&device, &config, render, on_error),
            other => return Err(format!("unsupported audio sample format {other:?}")),
        }
        .map_err(|e| format!("could not open the audio output: {e}"))?;
        stream
            .play()
            .map_err(|e| format!("could not start the audio output: {e}"))?;
        self.stream = Some(stream);
        Ok(format)
    }

    fn close(&mut self) {
        self.stream = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sound_servers_are_preferred_and_raw_hardware_is_last() {
        let mut names = vec![
            "hw:0,0",
            "default",
            "plughw:1",
            "pulse",
            "pipewire",
            "sysdefault:CARD=X",
        ];
        names.sort_by_key(|n| fallback_rank(n));
        assert_eq!(
            names,
            vec![
                "pipewire",
                "pulse",
                "default",
                "sysdefault:CARD=X",
                "hw:0,0",
                "plughw:1"
            ]
        );
    }

    #[test]
    fn sample_conversion_clamps_and_drops_non_finite_values() {
        let input = [0.0, 1.0, -1.0, 2.0, f32::NAN, f32::INFINITY, 0.5];
        let mut out = [0i16; 7];
        convert_samples(&input, &mut out);
        assert_eq!(out[0], 0);
        assert!(
            out[1] >= i16::MAX - 1,
            "full scale stays at the top, got {}",
            out[1]
        );
        assert_eq!(out[2], i16::MIN);
        assert!(out[3] >= i16::MAX - 1);
        assert_eq!(out[4], 0);
        assert_eq!(out[5], 0);
        assert!((i32::from(out[6]) - 16384).abs() <= 1);
    }

    /// Opens the real default output device and plays silence through it for a moment. Run by
    /// hand with `cargo test -p gallerybrowser-player -- --ignored`; with no device it only reports.
    #[test]
    #[ignore = "opens the real sound device"]
    fn the_default_device_opens_and_calls_back() {
        use std::sync::Arc;
        use std::sync::atomic::{AtomicUsize, Ordering};
        use std::time::{Duration, Instant};

        let calls = Arc::new(AtomicUsize::new(0));
        let counter = calls.clone();
        let mut backend = CpalBackend {
            stream: None,
            fallback: None,
        };
        let opened = backend.open(
            &mut |format| {
                assert!(format.sample_rate > 0 && format.channels > 0);
                let counter = counter.clone();
                Box::new(move |buf: &mut [f32], _channels| {
                    buf.fill(0.0);
                    counter.fetch_add(1, Ordering::Relaxed);
                })
            },
            Arc::new(|_| {}),
        );
        match opened {
            Ok(format) => {
                eprintln!(
                    "opened the default output: {} Hz, {} channels",
                    format.sample_rate, format.channels
                );
                let deadline = Instant::now() + Duration::from_secs(2);
                while calls.load(Ordering::Relaxed) == 0 && Instant::now() < deadline {
                    std::thread::sleep(Duration::from_millis(10));
                }
                assert!(
                    calls.load(Ordering::Relaxed) > 0,
                    "the device never asked for samples"
                );
                backend.close();
            }
            Err(e) => eprintln!("no usable sound device: {e}"),
        }
    }
}
