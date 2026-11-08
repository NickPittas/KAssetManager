use std::path::Path;
use std::sync::Arc;

use filmcraft_media::reader::{FileReader, SharedReader, open_reader_within};
use filmcraft_media::still::StillSource;
use gallerybrowser_core::Sequence;

use crate::guard::{self, Guarded, convert};
use crate::{FrameRate, MediaError, Result, SharedSource};

/// Largest file read whole into memory to decode a still (or a sequence frame).
pub(crate) const MAX_STILL_BYTES: u64 = 1 << 30;
/// Largest non-streamable file the filmcraft openers read whole (stills ride on this path).
const MAX_WHOLE: u64 = 512 << 20;

pub(crate) fn file_name(path: &Path) -> String {
    path.file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| path.display().to_string())
}

pub(crate) fn ext_lower(path: &Path) -> String {
    path.extension()
        .map(|e| e.to_string_lossy().to_ascii_lowercase())
        .unwrap_or_default()
}

fn io_err(path: &Path, e: &std::io::Error) -> MediaError {
    MediaError::Io(format!("{}: {e}", path.display()))
}

/// Read a file whole (bounded by `MAX_STILL_BYTES`) for image decoding.
pub(crate) fn read_capped(path: &Path) -> Result<Vec<u8>> {
    let meta = std::fs::metadata(path).map_err(|e| io_err(path, &e))?;
    if meta.len() > MAX_STILL_BYTES {
        return Err(MediaError::Unsupported(format!(
            "{}: file too large to decode as an image",
            path.display()
        )));
    }
    std::fs::read(path).map_err(|e| io_err(path, &e))
}

/// Open any supported file as a media source. Video containers are opened through
/// `filmcraft_media::reader::open_reader_within` + `filmcraft_codecs::reader_openers()` on a
/// `FileReader` (same call as filmcraft engine/src/media_pool.rs `probe_file`): only the container
/// index is read at open, samples on demand. Hardware decoders (`filmcraft-platform`, VA-API) are NOT
/// used. PSD/PSB, HEIF/HEIC (`heif.rs`) and other photocraft-only formats are wrapped as still sources.
pub fn open(path: &Path) -> Result<SharedSource> {
    let name = file_name(path);
    let ext = ext_lower(path);
    if matches!(ext.as_str(), "psd" | "psb") {
        return open_psd(path, &name);
    }
    if matches!(ext.as_str(), "heic" | "heif" | "hif") {
        let bytes = read_capped(path)?;
        return rgba_still(&name, heif_rgba(&name, &bytes)?, bytes.len() as u64);
    }
    let reader: SharedReader = Arc::new(FileReader::open(path).map_err(|e| io_err(path, &e))?);
    let opened = guard::catch(|| {
        open_reader_within(
            &name,
            reader,
            &filmcraft_codecs::reader_openers(),
            &filmcraft_codecs::openers(),
            MAX_WHOLE,
        )
        .map_err(convert)
    });
    match opened {
        Ok(src) => Ok(Guarded::wrap(src)),
        // Only stills may be read whole; a damaged video must never be slurped into memory.
        Err(e) if is_av_extension(&ext) => Err(e),
        Err(e) => open_with_photocraft(path, &name).map_err(|_| e),
    }
}

fn is_av_extension(ext: &str) -> bool {
    filmcraft_media::VIDEO_EXTENSIONS.contains(&ext)
        || filmcraft_media::AUDIO_EXTENSIONS.contains(&ext)
}

/// Formats the filmcraft stills do not take (exotic TIFF, PCX, DDS, QOI, FITS, …): decode through
/// photocraft-codecs and serve the 8-bit sRGB result as a still.
fn open_with_photocraft(path: &Path, name: &str) -> Result<SharedSource> {
    let bytes = read_capped(path)?;
    if bytes.starts_with(b"8BPS") {
        return psd_still(name, &bytes);
    }
    let decoded = if crate::heif::sniff(&bytes) {
        heif_rgba(name, &bytes)?
    } else {
        photocraft_rgba(name, &bytes)?
    };
    rgba_still(name, decoded, bytes.len() as u64)
}

/// A decoded RGBA8 picture (width, height, pixels, has alpha) served as a still source.
fn rgba_still(
    name: &str,
    decoded: (u32, u32, Vec<u8>, bool),
    file_size: u64,
) -> Result<SharedSource> {
    let (w, h, rgba, has_alpha) = decoded;
    Ok(Guarded::wrap(Arc::new(StillSource::from_rgba(
        name, w, h, rgba, has_alpha, file_size,
    ))))
}

/// Decode a HEIF/HEIC file (iPhone photos: HEVC through `filmcraft-hevc`, see `heif.rs`) to opaque
/// RGBA8: width, height, pixels, has alpha (always false).
pub(crate) fn heif_rgba(name: &str, bytes: &[u8]) -> Result<(u32, u32, Vec<u8>, bool)> {
    guard::catch(|| {
        let (w, h, rgba) = crate::heif::decode(bytes).map_err(|e| match e {
            MediaError::Decode(m) => MediaError::Decode(format!("{name}: {m}")),
            MediaError::Unsupported(m) => MediaError::Unsupported(format!("{name}: {m}")),
            other => other,
        })?;
        check_rgba(name, w, h, rgba.len())?;
        Ok((w, h, rgba, false))
    })
}

/// Decode `bytes` through photocraft-codecs (multi-part / multi-layer OpenEXR included: the beauty
/// layer, linear float data encoded to display sRGB) to straight-alpha sRGB RGBA8: width, height,
/// pixels, whether any pixel is translucent.
pub(crate) fn photocraft_rgba(name: &str, bytes: &[u8]) -> Result<(u32, u32, Vec<u8>, bool)> {
    guard::catch(|| {
        let img = photocraft_codecs::decode(bytes)
            .map_err(|e| MediaError::Decode(format!("{name}: {e}")))?;
        let (w, h) = img.dimensions();
        let has_alpha = img.has_translucency();
        let rgba = match img.sample_type() {
            // OpenEXR / float data is scene-linear with premultiplied alpha: encode it for display
            // exactly like filmcraft's own EXR frames (`VideoFrame::to_rgba8`).
            photocraft_codecs::SampleType::F16 | photocraft_codecs::SampleType::F32 => {
                let linear = img.to_rgba_f32();
                let mut out = vec![0u8; linear.len()];
                filmcraft_frame::linear_premul_to_srgb8(&linear, &mut out);
                out
            }
            _ => img.to_rgba8(),
        };
        check_rgba(name, w, h, rgba.len())?;
        Ok((w, h, rgba, has_alpha))
    })
}

fn open_psd(path: &Path, name: &str) -> Result<SharedSource> {
    let bytes = read_capped(path)?;
    psd_still(name, &bytes)
}

/// PSD/PSB merged composite (photocraft-psd) as a still.
fn psd_still(name: &str, bytes: &[u8]) -> Result<SharedSource> {
    guard::catch(|| {
        let file = photocraft_psd::PsdFile::from_bytes(bytes)
            .map_err(|e| MediaError::Decode(format!("{name}: {e}")))?;
        let has_alpha = file.merged_has_alpha();
        let img = file
            .composite_rgba8()
            .map_err(|e| MediaError::Decode(format!("{name}: {e}")))?;
        check_rgba(name, img.width, img.height, img.data.len())?;
        Ok(Guarded::wrap(Arc::new(StillSource::from_rgba(
            name,
            img.width,
            img.height,
            img.data,
            has_alpha,
            bytes.len() as u64,
        ))))
    })
}

fn check_rgba(name: &str, w: u32, h: u32, len: usize) -> Result<()> {
    let expect = (w as usize)
        .checked_mul(h as usize)
        .and_then(|n| n.checked_mul(4));
    if w == 0 || h == 0 || expect != Some(len) {
        return Err(MediaError::Decode(format!(
            "{name}: decoded image has inconsistent size {w}x{h} ({len} bytes)"
        )));
    }
    Ok(())
}

/// Image sequence as a movie source at `fps`; frame `i` of the source is `seq.frames[i]` (sparse
/// numbering is preserved for display by `Sequence`, so there are no gaps to hold). Each frame goes
/// through the same decoder chain as [`open`] (filmcraft stills, then photocraft-codecs, which
/// reads Blender multi-part / multi-layer OpenEXR); decoding is lazy with a bounded frame cache.
pub fn open_sequence(seq: &Sequence, fps: FrameRate) -> Result<SharedSource> {
    let frames = seq.frames.iter().map(|f| f.path.clone()).collect();
    crate::sequence::open_frames(&seq.display_name(), frames, fps.sane())
}
