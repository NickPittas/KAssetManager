// Vendored from GalleryBrowser 55a4483: test-only crate/module; assertions may panic by design. See crates/VENDORED.md.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
//! End-to-end checks on synthesized files: Y4M video (streamed through filmcraft's reader openers),
//! PNG with text chunks, PSD, numbered sequences.

use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime};

use gallerybrowser_core::{Entry, MediaKind, Sequence, SequenceFrame};
use gallerybrowser_media::{
    FrameRate, FrameRequest, MetadataWorker, ScrubSlot, ThumbResult, Thumbnailer,
    ThumbnailerConfig, Tick, frame_to_rgba, open, open_sequence, probe,
};

#[allow(clippy::expect_used)]
fn write_y4m(path: &Path, w: usize, h: usize, frames: usize) {
    let mut out = format!("YUV4MPEG2 W{w} H{h} F24:1 Ip A1:1 C420jpeg\n").into_bytes();
    for i in 0..frames {
        out.extend_from_slice(b"FRAME\n");
        // xorshift noise: regular byte patterns can accidentally look like an MPEG transport stream
        let mut x = 0x9e37_79b9u32.wrapping_add(i as u32);
        out.extend((0..w * h).map(|p| {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            ((x >> 8) as u8) / 2 + (p % w * 100 / w) as u8
        }));
        out.extend(std::iter::repeat_n(128u8, (w / 2) * (h / 2) * 2));
    }
    std::fs::write(path, out).expect("write y4m");
}

#[allow(clippy::expect_used)]
fn write_png(path: &Path, w: u32, h: u32, text: &[(&str, &str)]) {
    let file = std::fs::File::create(path).expect("create png");
    let mut enc = png::Encoder::new(std::io::BufWriter::new(file), w, h);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    for (k, v) in text {
        enc.add_text_chunk((*k).to_string(), (*v).to_string())
            .expect("text chunk");
    }
    let mut wr = enc.write_header().expect("header");
    let data: Vec<u8> = (0..w * h)
        .flat_map(|i| [(i % 256) as u8, 40, 200, 255])
        .collect();
    wr.write_image_data(&data).expect("data");
    wr.finish().expect("finish");
}

#[allow(clippy::expect_used)]
fn entry_for(path: &Path, kind: MediaKind) -> Entry {
    let md = std::fs::metadata(path).expect("stat");
    Entry {
        path: path.to_path_buf(),
        name: path
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default(),
        kind,
        size: md.len(),
        modified: md.modified().ok(),
        sequence: None,
    }
}

fn wait_results(t: &Thumbnailer, n: usize) -> Vec<ThumbResult> {
    let end = Instant::now() + Duration::from_secs(30);
    let mut out = Vec::new();
    while out.len() < n && Instant::now() < end {
        out.extend(t.poll());
        std::thread::sleep(Duration::from_millis(10));
    }
    out
}

fn thumb_cfg(dir: &Path) -> ThumbnailerConfig {
    ThumbnailerConfig {
        cache_dir: dir.to_path_buf(),
        memory_bytes: 64 << 20,
        disk_bytes: 64 << 20,
        threads: 2,
    }
}

#[test]
fn y4m_opens_streams_and_converts_to_rgba() {
    let dir = tempfile::tempdir().expect("tmp");
    let p = dir.path().join("clip.y4m");
    write_y4m(&p, 64, 48, 30);
    let src = open(&p).expect("open");
    let info = src.info();
    let v = info.video.as_ref().expect("video");
    assert_eq!((v.width, v.height), (64, 48));
    assert!((v.frame_rate.as_f64() - 24.0).abs() < 1e-6);
    assert!((info.duration.seconds() - 30.0 / 24.0).abs() < 1e-3);
    let rate = FrameRate::new(24, 1);
    let frame = src
        .video_frame(FrameRequest::full(rate.tick_of(7)))
        .expect("frame");
    let rgba = frame_to_rgba(&frame).expect("rgba");
    assert_eq!(
        (rgba.width, rgba.height, rgba.pixels.len()),
        (64, 48, 64 * 48 * 4)
    );
    assert!(rgba.pixels.as_chunks::<4>().0.iter().all(|p| p[3] == 255));
    // past the end clamps instead of failing hard or panicking
    let _ = src.video_frame(FrameRequest::full(Tick::from_seconds_f64(100.0)));
}

#[test]
fn garbage_and_truncated_files_error_without_panic() {
    let dir = tempfile::tempdir().expect("tmp");
    let junk = dir.path().join("junk.mp4");
    std::fs::write(&junk, b"definitely not an mp4 file").expect("write");
    assert!(open(&junk).is_err());
    let png = dir.path().join("broken.png");
    std::fs::write(&png, b"\x89PNG\r\n\x1a\nxxxx").expect("write");
    assert!(open(&png).is_err());
    let y4m = dir.path().join("short.y4m");
    std::fs::write(&y4m, b"YUV4MPEG2 W4096 H4096 F24:1 C420jpeg\nFRAME\nabc").expect("write");
    if let Ok(src) = open(&y4m) {
        let _ = src.video_frame(FrameRequest::full(Tick::ZERO));
    }
    assert!(open(&dir.path().join("missing.mov")).is_err());
    let e = entry_for(&junk, MediaKind::Video);
    let m = probe(&e).expect("probe never errors");
    assert!(m.error.is_some() && m.file_size == Some(26));
    // file vanished: no size, an error text, still Ok
    let gone = Entry {
        path: dir.path().join("gone.png"),
        ..e
    };
    let m = probe(&gone).expect("probe");
    assert!(m.file_size.is_none() && m.error.is_some());
}

#[test]
fn probe_video_reports_media_info() {
    let dir = tempfile::tempdir().expect("tmp");
    let p = dir.path().join("clip.y4m");
    write_y4m(&p, 64, 48, 48);
    let m = probe(&entry_for(&p, MediaKind::Video)).expect("probe");
    assert_eq!((m.width, m.height), (Some(64), Some(48)));
    assert_eq!(m.frame_count, Some(48));
    assert!((m.frame_rate.unwrap_or(0.0) - 24.0).abs() < 1e-6);
    assert!((m.duration_secs.unwrap_or(0.0) - 2.0).abs() < 1e-3);
    assert_eq!(m.format.as_deref(), Some("Y4M"));
    assert!(m.video_codec.is_some() && m.pixel_format.is_some() && m.color.is_some());
    assert_eq!(m.has_alpha, Some(false));
    assert!(m.file_size.is_some_and(|s| s > 64 * 48 * 48));
    assert!(m.error.is_none(), "{:?}", m.error);
}

#[test]
fn png_probe_reads_text_chunks_and_comfy_parameters() {
    let dir = tempfile::tempdir().expect("tmp");
    let p = dir.path().join("gen.png");
    let prompt = r#"{"3":{"class_type":"KSampler","inputs":{"seed":123456,"steps":30,"cfg":7.5,"sampler_name":"dpmpp_2m","scheduler":"karras","model":["4",0],"positive":["6",0],"negative":["7",0]}},
        "4":{"class_type":"CheckpointLoaderSimple","inputs":{"ckpt_name":"sd_xl_base_1.0.safetensors"}},
        "6":{"class_type":"CLIPTextEncode","inputs":{"text":"a red fox in snow"}},
        "7":{"class_type":"CLIPTextEncode","inputs":{"text":"lowres"}}}"#;
    write_png(
        &p,
        32,
        16,
        &[
            ("prompt", prompt),
            ("workflow", r#"{"nodes":[]}"#),
            ("Software", "ComfyUI"),
        ],
    );
    let m = probe(&entry_for(&p, MediaKind::Image)).expect("probe");
    assert_eq!((m.width, m.height), (Some(32), Some(16)));
    assert_eq!(m.has_alpha, Some(true), "RGBA PNG carries an alpha channel");
    let c = m.comfy.expect("comfy");
    assert_eq!(
        (c.seed, c.steps, c.cfg),
        (Some(123456), Some(30), Some(7.5))
    );
    assert_eq!(
        (c.sampler.as_deref(), c.scheduler.as_deref()),
        (Some("dpmpp_2m"), Some("karras"))
    );
    assert_eq!(
        (c.positive.as_deref(), c.negative.as_deref()),
        (Some("a red fox in snow"), Some("lowres"))
    );
    assert_eq!(c.model.as_deref(), Some("sd_xl_base_1.0.safetensors"));
    assert_eq!(c.workflow_json.as_deref(), Some(r#"{"nodes":[]}"#));
    assert_eq!(
        m.text_chunks,
        vec![("Software".to_string(), "ComfyUI".to_string())],
        "big JSON lives in `comfy`"
    );
    // a PNG without any text still probes
    let q = dir.path().join("plain.png");
    write_png(&q, 8, 8, &[]);
    let m = probe(&entry_for(&q, MediaKind::Image)).expect("probe");
    assert!(m.comfy.is_none() && m.text_chunks.is_empty() && m.error.is_none());
}

#[test]
fn thumbnails_keep_aspect_and_hit_the_disk_cache() {
    let dir = tempfile::tempdir().expect("tmp");
    let cache = dir.path().join("cache");
    let clip = dir.path().join("clip.y4m");
    write_y4m(&clip, 160, 90, 24);
    let img = dir.path().join("wide.png");
    write_png(&img, 400, 100, &[]);
    let (ev, ei) = (
        entry_for(&clip, MediaKind::Video),
        entry_for(&img, MediaKind::Image),
    );

    let t = Thumbnailer::new(thumb_cfg(&cache));
    t.begin_batch();
    assert!(t.cached(&ev, 64).is_none());
    t.request(&ev, 64, 10);
    t.request(&ei, 64, 5);
    t.request(&ei, 64, 5);
    let results = wait_results(&t, 2);
    assert_eq!(results.len(), 2, "deduplicated: one result per entry");
    for r in &results {
        let im = r.image.as_ref().expect("thumb");
        assert_eq!(im.pixels.len(), (im.width * im.height * 4) as usize);
        assert_eq!(im.width.max(im.height), 64);
        let (w, h) = if r.path == clip {
            (160.0, 90.0)
        } else {
            (400.0, 100.0)
        };
        assert!(
            ((im.width as f64 / im.height as f64) - w / h).abs() < 0.1,
            "aspect {}x{}",
            im.width,
            im.height
        );
    }
    assert!(t.cached(&ev, 64).is_some(), "memory hit after generation");

    // a fresh instance finds the JPEGs on disk
    let on_disk = || {
        walk(&cache)
            .into_iter()
            .filter(|p| p.extension().is_some_and(|e| e == "lcthumb"))
            .count()
    };
    let deadline = Instant::now() + Duration::from_secs(5);
    while on_disk() < 2 && Instant::now() < deadline {
        std::thread::sleep(Duration::from_millis(20));
    }
    assert_eq!(on_disk(), 2);
    let t2 = Thumbnailer::new(thumb_cfg(&cache));
    t2.request(&ei, 64, 0);
    let r = wait_results(&t2, 1);
    assert_eq!(
        r.first()
            .and_then(|r| r.image.as_ref().ok())
            .map(|i| i.width),
        Some(64)
    );
    // a different size is a different key
    assert!(t2.cached(&ei, 128).is_none());
    t2.clear_disk_cache().expect("clear");
    assert_eq!(on_disk(), 0);
}

fn walk(dir: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Ok(rd) = std::fs::read_dir(dir) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                out.extend(walk(&p));
            } else {
                out.push(p);
            }
        }
    }
    out
}

#[test]
fn begin_batch_discards_pending_work() {
    let dir = tempfile::tempdir().expect("tmp");
    let mut cfg = thumb_cfg(&dir.path().join("cache"));
    cfg.threads = 1;
    let t = Thumbnailer::new(cfg);
    let mut entries = Vec::new();
    for i in 0..6 {
        let p = dir.path().join(format!("i{i}.png"));
        write_png(&p, 600, 400, &[]);
        entries.push(entry_for(&p, MediaKind::Image));
    }
    for e in &entries {
        t.request(e, 64, 0);
    }
    t.begin_batch();
    std::thread::sleep(Duration::from_millis(1500));
    assert!(
        t.poll().is_empty(),
        "results of an invalidated batch are never delivered"
    );
    t.request(&entries[0], 64, 0);
    assert_eq!(wait_results(&t, 1).len(), 1);
    t.reconfigure(ThumbnailerConfig {
        threads: 3,
        ..thumb_cfg(&dir.path().join("cache"))
    });
    t.request(&entries[1], 32, 0);
    assert_eq!(wait_results(&t, 1).len(), 1, "works after reconfigure");
}

fn make_sequence(dir: &Path, numbers: &[i64]) -> Sequence {
    let mut frames = Vec::new();
    for &n in numbers {
        let p = dir.join(format!("shot_{n:04}.png"));
        write_png(&p, 48, 32, &[]);
        frames.push(SequenceFrame { number: n, path: p });
    }
    Sequence {
        dir: dir.to_path_buf(),
        prefix: "shot_".into(),
        suffix: ".png".into(),
        padding: 4,
        frames,
    }
}

#[test]
fn sequences_open_probe_and_thumbnail() {
    let dir = tempfile::tempdir().expect("tmp");
    let seq = make_sequence(dir.path(), &[1001, 1002, 1005]);
    let src = open_sequence(&seq, FrameRate::new(24, 1)).expect("open sequence");
    let v = src.info().video.clone().expect("video");
    assert_eq!((v.width, v.height), (48, 32));
    let last = src
        .video_frame(FrameRequest::full(FrameRate::new(24, 1).tick_of(2)))
        .expect("frame 2");
    assert_eq!((last.width, last.height), (48, 32));
    assert!(
        open_sequence(
            &Sequence {
                frames: Vec::new(),
                ..seq.clone()
            },
            FrameRate::new(24, 1)
        )
        .is_err()
    );

    let entry = Entry {
        path: dir.path().join("shot_####.png"),
        name: "shot_[1001-1005].png".into(),
        kind: MediaKind::Sequence,
        size: 3,
        modified: Some(SystemTime::now()),
        sequence: Some(Arc::new(seq)),
    };
    let m = probe(&entry).expect("probe");
    assert_eq!(m.sequence_range, Some((1001, 1005, 3, 2)));
    assert_eq!(
        (m.width, m.height, m.frame_count),
        (Some(48), Some(32), Some(3))
    );
    assert_eq!(m.container.as_deref(), Some("Image Sequence"));
    assert!(m.error.is_none(), "{:?}", m.error);

    let t = Thumbnailer::new(thumb_cfg(&dir.path().join("cache")));
    t.request(&entry, 24, 0);
    let r = wait_results(&t, 1);
    let im = r
        .first()
        .and_then(|r| r.image.as_ref().ok())
        .expect("sequence thumb");
    assert_eq!((im.width, im.height), (24, 16));
}

#[test]
fn scrub_slots_give_distinct_frames_for_video_and_sequences() {
    let dir = tempfile::tempdir().expect("tmp");
    let clip = dir.path().join("clip.y4m");
    write_y4m(&clip, 160, 90, 48);
    let ev = entry_for(&clip, MediaKind::Video);
    let t = Thumbnailer::new(thumb_cfg(&dir.path().join("cache")));
    t.begin_batch();

    const SLOTS: u32 = 4;
    assert!(t.cached_scrub(&ev, 64, 0, SLOTS).is_none());
    let mut frames: Vec<(u32, Arc<gallerybrowser_media::RgbaImage>)> = Vec::new();
    for slot in 0..SLOTS {
        t.request_scrub(&ev, 64, slot, SLOTS);
        t.request_scrub(&ev, 64, slot, SLOTS); // deduplicated
        let got = wait_results(&t, 1);
        assert_eq!(got.len(), 1, "one result for slot {slot}");
        let r = got.first().expect("result");
        assert_eq!(r.scrub, Some(ScrubSlot { slot, slots: SLOTS }));
        assert_eq!(r.path, clip);
        let im = r.image.as_ref().expect("scrub frame");
        assert_eq!(im.width.max(im.height), 64);
        frames.push((slot, im.clone()));
    }
    for (i, (_, a)) in frames.iter().enumerate() {
        for (_, b) in frames.iter().skip(i + 1) {
            assert_ne!(a.pixels, b.pixels, "slots show different frames");
        }
    }
    for (slot, img) in &frames {
        let hit = t.cached_scrub(&ev, 64, *slot, SLOTS).expect("memory hit");
        assert_eq!(hit.pixels, img.pixels);
    }
    // a cached slot is answered by the next poll without decoding
    t.request_scrub(&ev, 64, 2, SLOTS);
    assert_eq!(
        wait_results(&t, 1)
            .first()
            .and_then(|r| r.scrub.map(|s| s.slot)),
        Some(2)
    );
    // scrub frames are separate from the thumbnail
    assert!(t.cached(&ev, 64).is_none());
    // slots beyond the range clamp to the last one; other kinds are ignored
    let last = t
        .cached_scrub(&ev, 64, 99, SLOTS)
        .expect("clamped to last slot");
    assert_eq!(last.pixels, frames[3].1.pixels);
    let png = dir.path().join("still.png");
    write_png(&png, 8, 8, &[]);
    let still = entry_for(&png, MediaKind::Image);
    t.request_scrub(&still, 64, 0, SLOTS);
    std::thread::sleep(Duration::from_millis(50));
    assert!(t.poll().is_empty());

    // a new instance reads the frames back from the disk tier
    let deadline = Instant::now() + Duration::from_secs(5);
    let t2 = Thumbnailer::new(thumb_cfg(&dir.path().join("cache")));
    while Instant::now() < deadline {
        t2.request_scrub(&ev, 64, 1, SLOTS);
        if !wait_results(&t2, 1).is_empty() {
            break;
        }
    }
    assert!(t2.cached_scrub(&ev, 64, 1, SLOTS).is_some());

    // image sequences scrub by frame index
    let seq = make_sequence(dir.path(), &[1, 2, 3, 4, 5, 6]);
    let es = Entry {
        path: dir.path().join("shot_####.png"),
        name: "shot_[0001-0006].png".into(),
        kind: MediaKind::Sequence,
        size: 6,
        modified: Some(SystemTime::now()),
        sequence: Some(Arc::new(seq)),
    };
    t.request_scrub(&es, 32, 5, 6);
    let r = wait_results(&t, 1);
    let im = r
        .first()
        .and_then(|r| r.image.as_ref().ok())
        .expect("sequence scrub frame");
    assert_eq!((im.width, im.height), (32, 21));
    assert_eq!(
        r.first().and_then(|r| r.scrub),
        Some(ScrubSlot { slot: 5, slots: 6 })
    );
}

#[test]
fn psd_composite_opens_as_a_still() {
    use photocraft_psd::{LayerSpec, PixelData, PsdBuilder};
    let dir = tempfile::tempdir().expect("tmp");
    let mut b = PsdBuilder::new(4, 2);
    let px: Vec<u8> = (0..8).flat_map(|_| [200u8, 100, 50, 255]).collect();
    b.push_layer(LayerSpec::new(
        "L",
        0,
        0,
        4,
        2,
        PixelData::Rgba8(px.clone()),
    ));
    b.composite(PixelData::Rgba8(px));
    let p = dir.path().join("art.psd");
    std::fs::write(&p, b.to_bytes().expect("psd bytes")).expect("write");
    let src = open(&p).expect("open psd");
    let v = src.info().video.clone().expect("video");
    assert_eq!((v.width, v.height), (4, 2));
    let rgba = frame_to_rgba(
        &src.video_frame(FrameRequest::full(Tick::ZERO))
            .expect("frame"),
    )
    .expect("rgba");
    assert_eq!(rgba.pixels.len(), 4 * 2 * 4);
    assert!(
        rgba.pixels
            .as_chunks::<4>()
            .0
            .iter()
            .all(|p| p[3] == 255 && p[0] > 150)
    );
}

#[test]
fn metadata_worker_delivers_only_the_latest_request() {
    let dir = tempfile::tempdir().expect("tmp");
    let a = dir.path().join("a.png");
    let b = dir.path().join("b.png");
    write_png(&a, 10, 10, &[]);
    write_png(&b, 20, 20, &[]);
    let w = MetadataWorker::new();
    w.request(&entry_for(&a, MediaKind::Image));
    w.request(&entry_for(&b, MediaKind::Image));
    let end = Instant::now() + Duration::from_secs(20);
    let mut got = None;
    while got.is_none() && Instant::now() < end {
        got = w.poll();
        std::thread::sleep(Duration::from_millis(10));
    }
    let m = got.expect("result");
    assert_eq!(m.path, b);
    assert_eq!(m.width, Some(20));
    assert!(w.poll().is_none(), "delivered once");
}

/// A Blender-style multi-part OpenEXR: part 0 the beauty pass `ViewLayer.Combined` (linear RGBA),
/// part 1 a depth pass. `red` is the linear red value of every pixel.
#[allow(clippy::expect_used)]
fn write_multipart_exr(path: &Path, w: usize, h: usize, red: f32) {
    use exr::prelude::*;
    let size = Vec2(w, h);
    let beauty = Layer::new(
        size,
        LayerAttributes::named("ViewLayer.Combined"),
        Encoding::FAST_LOSSLESS,
        SpecificChannels::rgba(move |_pos| (red, 0.1_f32, 0.1_f32, 1.0_f32)),
    );
    let depth = Layer::new(
        size,
        LayerAttributes::named("ViewLayer.Depth"),
        Encoding::FAST_LOSSLESS,
        SpecificChannels::build()
            .with_channel("Z")
            .with_pixel_fn(|_pos| (5.0_f32,)),
    );
    Image::empty(ImageAttributes::new(IntegerBounds::from_dimensions(size)))
        .with_layer(beauty)
        .with_layer(depth)
        .write()
        .to_file(path)
        .expect("write exr");
}

#[test]
fn multipart_exr_sequences_decode_through_the_photocraft_fallback() {
    let dir = tempfile::tempdir().expect("tmp");
    let mut frames = Vec::new();
    for (k, n) in [49i64, 50, 51].into_iter().enumerate() {
        let p = dir.path().join(format!("shot_{n:04}.exr"));
        write_multipart_exr(&p, 40, 24, 0.25 * (k as f32 + 1.0));
        frames.push(SequenceFrame { number: n, path: p });
    }
    let seq = Sequence {
        dir: dir.path().to_path_buf(),
        prefix: "shot_".into(),
        suffix: ".exr".into(),
        padding: 4,
        frames,
    };
    let fps = FrameRate::new(24, 1);
    let src = open_sequence(&seq, fps).expect("open multi-part exr sequence");
    let v = src.info().video.clone().expect("video");
    assert_eq!((v.width, v.height), (40, 24));
    assert_eq!(src.info().duration, fps.tick_of(3));

    // linear 0.25 / 0.5 / 0.75 in the beauty layer, displayed as sRGB
    let mut reds = Vec::new();
    for i in 0..3 {
        let f = src
            .video_frame(FrameRequest::full(fps.tick_of(i)))
            .expect("frame");
        let im = frame_to_rgba(&f).expect("rgba");
        assert_eq!((im.width, im.height), (40, 24));
        let px = im.pixels.as_chunks::<4>().0;
        let first = px.first().expect("pixel");
        assert!(
            px.iter().all(|p| p == first),
            "constant picture (frame {i})"
        );
        assert_eq!(first[3], 255);
        assert!(
            first[1] < first[0],
            "green is the dim 0.1 channel: {first:?}"
        );
        reds.push(first[0]);
    }
    for (got, want) in reds.iter().zip([137u8, 188, 224]) {
        assert!(got.abs_diff(want) <= 3, "{reds:?} vs sRGB of 0.25/0.5/0.75");
    }

    // frames decode on worker threads (player prefetch) and out of order
    std::thread::scope(|s| {
        for i in [2i64, 0, 1] {
            let src = Arc::clone(&src);
            s.spawn(move || {
                src.video_frame(FrameRequest::full(fps.tick_of(i)))
                    .expect("threaded frame");
            });
        }
    });

    // thumbnails of the sequence use the same decoder chain
    let t = Thumbnailer::new(thumb_cfg(&dir.path().join("cache")));
    let entry = Entry {
        path: dir.path().join("shot_####.exr"),
        name: "shot_[49-51].exr".into(),
        kind: MediaKind::Sequence,
        size: 3,
        modified: Some(SystemTime::now()),
        sequence: Some(Arc::new(seq.clone())),
    };
    t.request(&entry, 20, 0);
    let r = wait_results(&t, 1);
    let im = r
        .first()
        .and_then(|r| r.image.as_ref().ok())
        .expect("multi-part exr sequence thumb");
    assert_eq!((im.width, im.height), (20, 12));

    // a damaged frame is an error for that frame only
    std::fs::write(&seq.frames[1].path, b"not an exr").expect("corrupt");
    let fresh = open_sequence(&seq, fps).expect("first frame still opens");
    assert!(
        fresh
            .video_frame(FrameRequest::full(fps.tick_of(1)))
            .is_err()
    );
    assert!(
        fresh
            .video_frame(FrameRequest::full(fps.tick_of(2)))
            .is_ok()
    );
}

#[test]
#[allow(clippy::expect_used)]
fn other_files_probe_facts_and_have_no_thumbnail() {
    let dir = tempfile::tempdir().expect("tmp");
    let p = dir.path().join("scene.blend");
    std::fs::write(&p, vec![0u8; 123]).expect("write");
    let e = entry_for(&p, MediaKind::Other);

    let m = probe(&e).expect("probe");
    assert_eq!(m.file_size, Some(123));
    assert_eq!(m.format.as_deref(), Some("BLEND"));
    assert!(m.error.is_none() && m.modified.is_some());

    let t = Thumbnailer::new(thumb_cfg(&dir.path().join("cache")));
    t.begin_batch();
    assert!(t.cached(&e, 64).is_none());
    t.request(&e, 64, 0);
    let results = t.poll();
    assert_eq!(results.len(), 1);
    assert!(results[0].image.is_err());
    assert_eq!(results[0].path, p);
    t.request_scrub(&e, 64, 0, 8);
    std::thread::sleep(Duration::from_millis(30));
    assert!(t.poll().is_empty());
}
