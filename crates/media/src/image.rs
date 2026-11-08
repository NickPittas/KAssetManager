use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;

use filmcraft_frame::PixelData;
use rayon::prelude::*;

use crate::guard::panic_message;
use crate::{MediaError, Result, VideoFrame};

/// Largest picture (pixels) converted to RGBA8: 16384 x 16384.
const MAX_PIXELS: usize = 1 << 28;

/// Straight-alpha sRGB RGBA8, tightly packed (`pixels.len() == width*height*4`).
#[derive(Clone, Debug)]
pub struct RgbaImage {
    pub width: u32,
    pub height: u32,
    pub pixels: Arc<Vec<u8>>,
}

fn packed_len(w: u32, h: u32) -> Option<usize> {
    let n = (w as usize).checked_mul(h as usize)?;
    if n == 0 || n > MAX_PIXELS {
        return None;
    }
    n.checked_mul(4)
}

/// Convert any decoded frame (YUV 8/16, f32, RGBA) to packed RGBA8 (via filmcraft-frame conversions).
/// 8-bit RGBA frames share their pixel buffer (no copy). Hostile sizes and inconsistent planes are
/// errors, never panics.
pub fn frame_to_rgba(frame: &VideoFrame) -> Result<RgbaImage> {
    let len = packed_len(frame.width, frame.height).ok_or_else(|| {
        MediaError::Decode(format!(
            "frame has unusable size {}x{}",
            frame.width, frame.height
        ))
    })?;
    if let PixelData::Rgba8(px) = &frame.data {
        if px.len() != len {
            return Err(MediaError::Decode(format!(
                "RGBA frame holds {} bytes, expected {len}",
                px.len()
            )));
        }
        return Ok(RgbaImage {
            width: frame.width,
            height: frame.height,
            pixels: px.clone(),
        });
    }
    let pixels = match catch_unwind(AssertUnwindSafe(|| frame.to_rgba8())) {
        Ok(p) => p,
        Err(p) => {
            return Err(MediaError::Decode(format!(
                "frame conversion failed: {}",
                panic_message(&*p)
            )));
        }
    };
    if pixels.len() != len {
        return Err(MediaError::Decode(format!(
            "converted frame holds {} bytes, expected {len}",
            pixels.len()
        )));
    }
    Ok(RgbaImage {
        width: frame.width,
        height: frame.height,
        pixels: Arc::new(pixels),
    })
}

/// Largest size that fits `max_edge` on the long side, keeping aspect (never upscales).
pub(crate) fn fit_size(w: u32, h: u32, max_edge: u32) -> (u32, u32) {
    let long = w.max(h);
    if long == 0 || long <= max_edge {
        return (w.max(1), h.max(1));
    }
    let scale = |v: u32| -> u32 {
        ((u64::from(v) * u64::from(max_edge) + u64::from(long) / 2) / u64::from(long)).max(1) as u32
    };
    (scale(w), scale(h))
}

/// Area-average (box filter) downscale of straight-alpha RGBA8 to `dw x dh`, averaging in
/// premultiplied space so transparent texels do not bleed colour. `src` must be `sw*sh*4` bytes.
pub(crate) fn box_downscale(src: &[u8], sw: u32, sh: u32, dw: u32, dh: u32) -> Result<Vec<u8>> {
    let (sw, sh, dw, dh) = (sw as usize, sh as usize, dw as usize, dh as usize);
    let src_len = sw.checked_mul(sh).and_then(|n| n.checked_mul(4));
    let dst_len = dw.checked_mul(dh).and_then(|n| n.checked_mul(4));
    let (Some(src_len), Some(dst_len)) = (src_len, dst_len) else {
        return Err(MediaError::Decode("scale size overflow".into()));
    };
    if sw == 0 || sh == 0 || dw == 0 || dh == 0 || src.len() != src_len || dw > sw || dh > sh {
        return Err(MediaError::Decode("invalid scale request".into()));
    }
    let mut out = vec![0u8; dst_len];
    out.par_chunks_mut(dw * 4)
        .enumerate()
        .for_each(|(dy, row)| {
            let y0 = dy * sh / dh;
            let y1 = ((dy + 1) * sh / dh).max(y0 + 1).min(sh);
            for (dx, px) in row.as_chunks_mut::<4>().0.iter_mut().enumerate() {
                let x0 = dx * sw / dw;
                let x1 = ((dx + 1) * sw / dw).max(x0 + 1).min(sw);
                let (mut r, mut g, mut b, mut a, mut n) = (0u64, 0u64, 0u64, 0u64, 0u64);
                for y in y0..y1 {
                    let base = (y * sw + x0) * 4;
                    let Some(span) = src.get(base..base + (x1 - x0) * 4) else {
                        continue;
                    };
                    for s in span.as_chunks::<4>().0 {
                        let al = u64::from(s[3]);
                        r += u64::from(s[0]) * al;
                        g += u64::from(s[1]) * al;
                        b += u64::from(s[2]) * al;
                        a += al;
                        n += 1;
                    }
                }
                if n == 0 {
                    continue;
                }
                for (channel, sum) in [(0, r), (1, g), (2, b)] {
                    let Some(value) = (sum + a / 2).checked_div(a) else {
                        continue;
                    };
                    px[channel] = value as u8;
                }
                px[3] = ((a + n / 2) / n) as u8;
            }
        });
    Ok(out)
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
    use super::*;
    use filmcraft_frame::Chroma;

    #[test]
    fn rgba_frame_shares_buffer_and_checks_length() {
        let f = VideoFrame::rgba8(2, 1, vec![1, 2, 3, 255, 4, 5, 6, 255]);
        let img = frame_to_rgba(&f).expect("rgba");
        assert_eq!((img.width, img.height, img.pixels.len()), (2, 1, 8));
        let mut bad = VideoFrame::rgba8(2, 1, vec![0; 8]);
        bad.width = 3;
        assert!(frame_to_rgba(&bad).is_err());
        bad.width = 0;
        assert!(frame_to_rgba(&bad).is_err());
    }

    #[test]
    fn yuv8_frame_converts_to_full_size_rgba() {
        let (w, h) = (4u32, 2u32);
        let f = VideoFrame {
            width: w,
            height: h,
            data: PixelData::Yuv8 {
                planes: [
                    Arc::new(vec![235; 8]),
                    Arc::new(vec![128; 2]),
                    Arc::new(vec![128; 2]),
                ],
                chroma: Chroma::C420,
                alpha: None,
            },
            color: Default::default(),
            par: (1, 1),
            pts: Default::default(),
        };
        let img = frame_to_rgba(&f).expect("yuv");
        assert_eq!(img.pixels.len(), 4 * 2 * 4);
        assert!(
            img.pixels
                .as_chunks::<4>()
                .0
                .iter()
                .all(|p| p[0] >= 250 && p[3] == 255),
            "white luma converts to white"
        );
    }

    #[test]
    fn inconsistent_planes_do_not_panic() {
        let f = VideoFrame {
            width: 8,
            height: 8,
            data: PixelData::Yuv8 {
                planes: [
                    Arc::new(vec![0; 4]),
                    Arc::new(vec![0; 1]),
                    Arc::new(vec![0; 1]),
                ],
                chroma: Chroma::C420,
                alpha: None,
            },
            color: Default::default(),
            par: (1, 1),
            pts: Default::default(),
        };
        assert!(frame_to_rgba(&f).is_err());
    }

    #[test]
    fn fit_keeps_aspect_and_never_upscales() {
        assert_eq!(fit_size(1920, 1080, 128), (128, 72));
        assert_eq!(fit_size(1080, 1920, 128), (72, 128));
        assert_eq!(fit_size(64, 32, 128), (64, 32));
        assert_eq!(fit_size(10000, 1, 100), (100, 1));
    }

    #[test]
    fn box_downscale_averages_and_ignores_transparent_colour() {
        let mut src = Vec::new();
        for _ in 0..2 {
            src.extend_from_slice(&[255, 0, 0, 255]);
            src.extend_from_slice(&[0, 255, 0, 0]);
        }
        let out = box_downscale(&src, 2, 2, 1, 1).expect("scale");
        assert_eq!(out, vec![255, 0, 0, 128]);
        assert!(box_downscale(&src, 2, 2, 3, 3).is_err());
    }
}
