//! PNG `tEXt` / `zTXt` / `iTXt` chunk reader. Walks the chunk list with seeks (image data is never
//! read) and bounds every length it takes from the file.

use std::fs::File;
use std::io::{BufReader, Read, Seek, SeekFrom};
use std::path::Path;

const SIGNATURE: [u8; 8] = [0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a];
/// Largest text chunk read (ComfyUI workflows are a few MB at most).
const MAX_CHUNK: u32 = 64 << 20;
/// Largest decompressed text kept.
const MAX_TEXT: u64 = 64 << 20;
/// Chunks examined before giving up (a PNG has a handful of metadata chunks).
const MAX_CHUNKS: usize = 100_000;

/// All text chunks of the PNG at `path`, in file order, as (keyword, text). A file that is not a PNG
/// (or is truncated mid-chunk) yields what was read so far.
pub(crate) fn read_text_chunks(path: &Path) -> std::io::Result<Vec<(String, String)>> {
    let mut r = BufReader::new(File::open(path)?);
    let mut sig = [0u8; 8];
    if r.read_exact(&mut sig).is_err() || sig != SIGNATURE {
        return Ok(Vec::new());
    }
    let mut out = Vec::new();
    for _ in 0..MAX_CHUNKS {
        let mut head = [0u8; 8];
        if r.read_exact(&mut head).is_err() {
            break;
        }
        let len = u32::from_be_bytes([head[0], head[1], head[2], head[3]]);
        let kind = [head[4], head[5], head[6], head[7]];
        if &kind == b"IEND" || len > i32::MAX as u32 {
            break;
        }
        let wanted = matches!(&kind, b"tEXt" | b"zTXt" | b"iTXt");
        if wanted && len <= MAX_CHUNK {
            let mut data = vec![0u8; len as usize];
            if r.read_exact(&mut data).is_err() {
                break;
            }
            if let Some(kv) = parse_chunk(&kind, &data) {
                out.push(kv);
            }
            if r.seek(SeekFrom::Current(4)).is_err() {
                break;
            }
        } else if r.seek(SeekFrom::Current(i64::from(len) + 4)).is_err() {
            break;
        }
    }
    Ok(out)
}

fn latin1(b: &[u8]) -> String {
    b.iter().map(|&c| char::from(c)).collect()
}

fn inflate(b: &[u8]) -> Option<Vec<u8>> {
    let mut out = Vec::new();
    flate2::read::ZlibDecoder::new(b)
        .take(MAX_TEXT)
        .read_to_end(&mut out)
        .ok()?;
    Some(out)
}

/// Parse one text chunk's data. `None` for malformed chunks.
pub(crate) fn parse_chunk(kind: &[u8; 4], data: &[u8]) -> Option<(String, String)> {
    let nul = data.iter().position(|&b| b == 0)?;
    let key = latin1(data.get(..nul)?);
    let rest = data.get(nul + 1..)?;
    match kind {
        b"tEXt" => Some((key, latin1(rest))),
        b"zTXt" => {
            // compression method byte (0 = deflate), then the zlib stream
            let (&method, stream) = rest.split_first()?;
            if method != 0 {
                return None;
            }
            Some((key, latin1(&inflate(stream)?)))
        }
        b"iTXt" => {
            let (&flag, rest) = rest.split_first()?;
            let (&method, rest) = rest.split_first()?;
            // language tag and translated keyword, both NUL-terminated
            let l = rest.iter().position(|&b| b == 0)?;
            let rest = rest.get(l + 1..)?;
            let t = rest.iter().position(|&b| b == 0)?;
            let text = rest.get(t + 1..)?;
            let bytes = if flag == 1 {
                if method != 0 {
                    return None;
                }
                inflate(text)?
            } else {
                text.to_vec()
            };
            Some((key, String::from_utf8_lossy(&bytes).into_owned()))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
    use super::*;

    #[test]
    fn parses_the_three_chunk_kinds() {
        assert_eq!(
            parse_chunk(b"tEXt", b"Title\0caf\xe9"),
            Some(("Title".into(), "café".into()))
        );
        let mut z = Vec::new();
        {
            use std::io::Write;
            let mut e = flate2::write::ZlibEncoder::new(&mut z, flate2::Compression::default());
            e.write_all(b"hello hello").expect("deflate");
            e.finish().expect("finish");
        }
        let mut zt = b"Comment\0\0".to_vec();
        zt.extend_from_slice(&z);
        assert_eq!(
            parse_chunk(b"zTXt", &zt),
            Some(("Comment".into(), "hello hello".into()))
        );
        let it = b"Author\0\0\0en\0Autor\0\xc3\xa9t\xc3\xa9".to_vec();
        assert_eq!(
            parse_chunk(b"iTXt", &it),
            Some(("Author".into(), "été".into()))
        );
        assert_eq!(parse_chunk(b"tEXt", b"no-nul"), None);
    }
}
