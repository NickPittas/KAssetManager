use std::path::Path;

/// What an entry is. `Sequence` is only produced by sequence detection, never by [`classify`].
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, Hash, PartialOrd, Ord, serde::Serialize, serde::Deserialize,
)]
pub enum MediaKind {
    Image,
    Video,
    Sequence,
    Audio,
    /// Non-media file; only produced by scanning with `show_all_files`, never by [`classify`].
    Other,
}

/// Lower-case extensions without dot. Superset of the Python app (png jpg jpeg tif tiff bmp gif webp exr tga psd)
/// plus formats the ArtCraft decoders handle (dpx hdr dng psb).
///
/// Literals mirror `filmcraft_media::STILL_EXTENSIONS` (core must not depend on filmcraft) plus the Python
/// extras `psd`/`psb` and `heic`/`heif` (decoded by gallerybrowser-media's HEIF reader).
pub const IMAGE_EXTENSIONS: &[&str] = &[
    "png", "jpg", "jpeg", "gif", "webp", "tif", "tiff", "bmp", "tga", "dpx", "exr", "hdr", "dng",
    "psd", "psb", "heic", "heif",
];
/// Superset of the Python app (mp4 mov avi mkv mxf webm mpg mpeg) plus filmcraft_media::VIDEO_EXTENSIONS.
pub const VIDEO_EXTENSIONS: &[&str] = &[
    "mp4", "m4v", "mov", "mkv", "webm", "avi", "mxf", "mts", "m2ts", "m2t", "ts", "mpg", "mpeg",
    "vob", "mod", "tod", "m2v", "m1v", "mpv", "3gp", "y4m", "apv",
];
/// Mirrors `filmcraft_media::AUDIO_EXTENSIONS`.
pub const AUDIO_EXTENSIONS: &[&str] = &[
    "wav", "wave", "bwf", "aif", "aiff", "mp3", "mp2", "m4a", "aac", "flac", "ogg", "opus",
];

/// Classify by (case-insensitive) extension. `None` = not browsable media.
pub fn classify(path: &Path) -> Option<MediaKind> {
    let ext = path.extension()?.to_str()?.to_ascii_lowercase();
    let ext = ext.as_str();
    if IMAGE_EXTENSIONS.contains(&ext) {
        Some(MediaKind::Image)
    } else if VIDEO_EXTENSIONS.contains(&ext) {
        Some(MediaKind::Video)
    } else if AUDIO_EXTENSIONS.contains(&ext) {
        Some(MediaKind::Audio)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classifies_case_insensitively() {
        assert_eq!(classify(Path::new("a/b/CLIP.MOV")), Some(MediaKind::Video));
        assert_eq!(classify(Path::new("x.ExR")), Some(MediaKind::Image));
        assert_eq!(classify(Path::new("x.psb")), Some(MediaKind::Image));
        assert_eq!(classify(Path::new("x.flac")), Some(MediaKind::Audio));
        assert_eq!(classify(Path::new("x.mxf")), Some(MediaKind::Video));
    }

    #[test]
    fn rejects_unknown_and_extensionless() {
        assert_eq!(classify(Path::new("notes.txt")), None);
        assert_eq!(classify(Path::new("png")), None);
        assert_eq!(classify(Path::new(".png")), None);
        assert_eq!(classify(Path::new("")), None);
    }
}
