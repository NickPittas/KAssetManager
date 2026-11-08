//! HEIF / HEIC stills (iPhone photos): ISO/IEC 23008-12 container parsing in this file, HEVC
//! decoding through `filmcraft-hevc` (pure Rust), Y'CbCr to RGBA8 here.
//!
//! Supported: primary item of type `hvc1` (single coded image) or `grid` (tiles of `hvc1`, the
//! iPhone layout), `iloc` versions 0–2 with construction method 0 (file offsets) and 1 (`idat`),
//! `colr`/`nclx` matrix and range (default BT.601 full range), `irot` and `imir` transforms.
//! Alpha (`auxl`) items, `clap` cropping and construction method 2 are not handled.
//!
//! Every count, size and offset comes from a hostile file: reads go through bounds-checked
//! cursors, arithmetic is checked and decoded sizes are capped by [`MAX_PIXELS`].

use filmcraft_hevc::{Decoder, Picture, Plane};
use rayon::prelude::*;

use crate::{MediaError, Result};

/// Largest output picture (pixels): 16384 x 16384, same cap as `image::frame_to_rgba`.
const MAX_PIXELS: usize = 1 << 28;
/// Most items / extents / properties / associations accepted from a single table.
const MAX_ENTRIES: usize = 1 << 16;

fn err(msg: impl Into<String>) -> MediaError {
    MediaError::Decode(format!("HEIF: {}", msg.into()))
}

fn unsupported(msg: impl Into<String>) -> MediaError {
    MediaError::Unsupported(format!("HEIF: {}", msg.into()))
}

/// Whether `bytes` start like a HEIF/HEIC file (`ftyp` with an image brand).
pub(crate) fn sniff(bytes: &[u8]) -> bool {
    let Some(ftyp) = bytes.get(4..8) else {
        return false;
    };
    if ftyp != b"ftyp" {
        return false;
    }
    let size = bytes
        .get(0..4)
        .map(|b| u32::from_be_bytes([b[0], b[1], b[2], b[3]]) as usize)
        .unwrap_or(0)
        .min(bytes.len())
        .min(4096);
    let brands = bytes.get(8..size).unwrap_or_default();
    brands.as_chunks::<4>().0.iter().any(|b| {
        matches!(
            b,
            b"heic" | b"heix" | b"heim" | b"heis" | b"hevc" | b"hevx" | b"mif1" | b"msf1"
        )
    })
}

// ---------------------------------------------------------------------------------------------
// Byte cursor and boxes
// ---------------------------------------------------------------------------------------------

struct Cursor<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> Cursor<'a> {
    fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }
    fn remaining(&self) -> usize {
        self.data.len().saturating_sub(self.pos)
    }
    fn take(&mut self, n: usize) -> Result<&'a [u8]> {
        let end = self
            .pos
            .checked_add(n)
            .ok_or_else(|| err("size overflow"))?;
        let s = self
            .data
            .get(self.pos..end)
            .ok_or_else(|| err("truncated box"))?;
        self.pos = end;
        Ok(s)
    }
    fn u8(&mut self) -> Result<u8> {
        Ok(self.take(1)?[..].first().copied().unwrap_or(0))
    }
    fn u16(&mut self) -> Result<u16> {
        let b = self.take(2)?;
        Ok(u16::from_be_bytes([b[0], b[1]]))
    }
    fn u32(&mut self) -> Result<u32> {
        let b = self.take(4)?;
        Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
    }
    fn u64(&mut self) -> Result<u64> {
        let b = self.take(8)?;
        let mut a = [0u8; 8];
        a.copy_from_slice(b);
        Ok(u64::from_be_bytes(a))
    }
    /// Big-endian unsigned integer of `n` bytes (0, 4 or 8 in `iloc`; 0 reads as 0).
    fn uint(&mut self, n: u8) -> Result<u64> {
        match n {
            0 => Ok(0),
            2 => self.u16().map(u64::from),
            4 => self.u32().map(u64::from),
            8 => self.u64(),
            _ => Err(err(format!("invalid field size {n}"))),
        }
    }
    fn fourcc(&mut self) -> Result<[u8; 4]> {
        let b = self.take(4)?;
        Ok([b[0], b[1], b[2], b[3]])
    }
    /// Version and flags of a FullBox.
    fn full_header(&mut self) -> Result<(u8, u32)> {
        let v = self.u32()?;
        Ok(((v >> 24) as u8, v & 0x00ff_ffff))
    }
}

/// One box: its type and payload (after the header).
struct BoxRef<'a> {
    kind: [u8; 4],
    payload: &'a [u8],
}

/// The boxes laid end to end in `data`.
fn boxes(data: &[u8]) -> Result<Vec<BoxRef<'_>>> {
    let mut out = Vec::new();
    let mut c = Cursor::new(data);
    while c.remaining() > 0 {
        if out.len() >= MAX_ENTRIES {
            return Err(err("too many boxes"));
        }
        let start = c.pos;
        let size32 = c.u32()?;
        let kind = c.fourcc()?;
        let size = match size32 {
            0 => (data.len() - start) as u64,
            1 => c.u64()?,
            n => u64::from(n),
        };
        let header = c.pos - start;
        let size = usize::try_from(size).map_err(|_| err("box too large"))?;
        if size < header {
            return Err(err("box smaller than its header"));
        }
        let payload = c.take(size - header)?;
        out.push(BoxRef { kind, payload });
    }
    Ok(out)
}

fn find<'a>(list: &[BoxRef<'a>], kind: &[u8; 4]) -> Option<&'a [u8]> {
    list.iter().find(|b| &b.kind == kind).map(|b| b.payload)
}

// ---------------------------------------------------------------------------------------------
// meta box contents
// ---------------------------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Extent {
    offset: u64,
    length: u64,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct Location {
    item_id: u32,
    construction_method: u8,
    base_offset: u64,
    extents: Vec<Extent>,
}

/// An `ipco` property this reader understands.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Property<'a> {
    Hvcc(&'a [u8]),
    Ispe {
        width: u32,
        height: u32,
    },
    /// Anticlockwise quarter turns.
    Irot(u8),
    /// 0: mirror about a vertical axis (left/right), 1: about a horizontal axis (top/bottom).
    Imir(u8),
    Nclx {
        matrix: u16,
        full_range: bool,
    },
    /// Clean aperture: width, height, horizontal and vertical centre offsets as (num, den).
    Clap {
        width: (u32, u32),
        height: (u32, u32),
        horiz_off: (i32, u32),
        vert_off: (i32, u32),
    },
    Other,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Grid {
    rows: u32,
    columns: u32,
    output_width: u32,
    output_height: u32,
}

struct Meta<'a> {
    primary: u32,
    /// (item id, item type)
    items: Vec<(u32, [u8; 4])>,
    locations: Vec<Location>,
    properties: Vec<Property<'a>>,
    /// (item id, 1-based property indices in association order)
    associations: Vec<(u32, Vec<u16>)>,
    /// `dimg` references: (from item, to items in order)
    dimg: Vec<(u32, Vec<u32>)>,
    idat: &'a [u8],
}

fn parse_pitm(p: &[u8]) -> Result<u32> {
    let mut c = Cursor::new(p);
    let (v, _) = c.full_header()?;
    if v == 0 {
        c.u16().map(u32::from)
    } else {
        c.u32()
    }
}

fn parse_iinf(p: &[u8]) -> Result<Vec<(u32, [u8; 4])>> {
    let mut c = Cursor::new(p);
    let (v, _) = c.full_header()?;
    if v == 0 {
        c.u16()?;
    } else {
        c.u32()?;
    }
    let rest = c.take(c.remaining())?;
    let mut items = Vec::new();
    for b in boxes(rest)? {
        if &b.kind != b"infe" {
            continue;
        }
        let mut c = Cursor::new(b.payload);
        let (v, _) = c.full_header()?;
        if v < 2 {
            continue; // no item type: not an image item
        }
        let id = if v == 2 {
            u32::from(c.u16()?)
        } else {
            c.u32()?
        };
        c.u16()?; // item_protection_index
        let kind = c.fourcc()?;
        items.push((id, kind));
    }
    Ok(items)
}

fn parse_iloc(p: &[u8]) -> Result<Vec<Location>> {
    let mut c = Cursor::new(p);
    let (v, _) = c.full_header()?;
    if v > 2 {
        return Err(unsupported(format!("iloc version {v}")));
    }
    let a = c.u8()?;
    let b = c.u8()?;
    let (offset_size, length_size, base_offset_size) = (a >> 4, a & 15, b >> 4);
    let index_size = if v >= 1 { b & 15 } else { 0 };
    let count = if v < 2 { u32::from(c.u16()?) } else { c.u32()? } as usize;
    if count > MAX_ENTRIES {
        return Err(err("too many iloc items"));
    }
    let mut out = Vec::with_capacity(count.min(c.remaining()));
    for _ in 0..count {
        let item_id = if v < 2 { u32::from(c.u16()?) } else { c.u32()? };
        let construction_method = if v >= 1 { (c.u16()? & 15) as u8 } else { 0 };
        c.u16()?; // data_reference_index
        let base_offset = c.uint(base_offset_size)?;
        let n = c.u16()? as usize;
        let mut extents = Vec::with_capacity(n.min(c.remaining()));
        for _ in 0..n {
            if index_size > 0 {
                c.uint(index_size)?;
            }
            let offset = c.uint(offset_size)?;
            let length = c.uint(length_size)?;
            extents.push(Extent { offset, length });
        }
        out.push(Location {
            item_id,
            construction_method,
            base_offset,
            extents,
        });
    }
    Ok(out)
}

fn parse_property<'a>(b: &BoxRef<'a>) -> Result<Property<'a>> {
    let mut c = Cursor::new(b.payload);
    Ok(match &b.kind {
        b"hvcC" => Property::Hvcc(b.payload),
        b"ispe" => {
            c.full_header()?;
            Property::Ispe {
                width: c.u32()?,
                height: c.u32()?,
            }
        }
        b"irot" => Property::Irot(c.u8()? & 3),
        b"clap" => {
            let mut r = || -> Result<(u32, u32)> { Ok((c.u32()?, c.u32()?)) };
            let width = r()?;
            let height = r()?;
            let (hn, hd) = r()?;
            let (vn, vd) = r()?;
            Property::Clap {
                width,
                height,
                horiz_off: (hn as i32, hd),
                vert_off: (vn as i32, vd),
            }
        }
        b"imir" => Property::Imir(c.u8()? & 1),
        b"colr" => {
            if &c.fourcc()? == b"nclx" {
                c.u16()?; // colour_primaries
                c.u16()?; // transfer_characteristics
                let matrix = c.u16()?;
                let full_range = c.u8()? & 0x80 != 0;
                Property::Nclx { matrix, full_range }
            } else {
                Property::Other
            }
        }
        _ => Property::Other,
    })
}

fn parse_ipma(p: &[u8], out: &mut Vec<(u32, Vec<u16>)>) -> Result<()> {
    let mut c = Cursor::new(p);
    let (v, flags) = c.full_header()?;
    let count = c.u32()? as usize;
    if count > MAX_ENTRIES || out.len().saturating_add(count) > MAX_ENTRIES {
        return Err(err("too many ipma entries"));
    }
    for _ in 0..count {
        let id = if v < 1 { u32::from(c.u16()?) } else { c.u32()? };
        let n = c.u8()?;
        let mut props = Vec::with_capacity(usize::from(n));
        for _ in 0..n {
            let idx = if flags & 1 != 0 {
                c.u16()? & 0x7fff
            } else {
                u16::from(c.u8()? & 0x7f)
            };
            props.push(idx);
        }
        out.push((id, props));
    }
    Ok(())
}

fn parse_iref(p: &[u8]) -> Result<Vec<(u32, Vec<u32>)>> {
    let mut c = Cursor::new(p);
    let (v, _) = c.full_header()?;
    let rest = c.take(c.remaining())?;
    let mut out = Vec::new();
    for b in boxes(rest)? {
        if &b.kind != b"dimg" {
            continue;
        }
        let mut c = Cursor::new(b.payload);
        let id = |c: &mut Cursor<'_>| -> Result<u32> {
            if v == 0 {
                c.u16().map(u32::from)
            } else {
                c.u32()
            }
        };
        let from = id(&mut c)?;
        let n = c.u16()?;
        let mut to = Vec::with_capacity(usize::from(n));
        for _ in 0..n {
            to.push(id(&mut c)?);
        }
        out.push((from, to));
    }
    Ok(out)
}

fn parse_grid(data: &[u8]) -> Result<Grid> {
    let mut c = Cursor::new(data);
    c.u8()?; // version
    let flags = c.u8()?;
    let rows = u32::from(c.u8()?) + 1;
    let columns = u32::from(c.u8()?) + 1;
    let (output_width, output_height) = if flags & 1 != 0 {
        (c.u32()?, c.u32()?)
    } else {
        (u32::from(c.u16()?), u32::from(c.u16()?))
    };
    Ok(Grid {
        rows,
        columns,
        output_width,
        output_height,
    })
}

fn parse_meta(p: &[u8]) -> Result<Meta<'_>> {
    let mut c = Cursor::new(p);
    c.full_header()?;
    let list = boxes(c.take(c.remaining())?)?;
    if let Some(hdlr) = find(&list, b"hdlr") {
        let mut c = Cursor::new(hdlr);
        c.full_header()?;
        c.u32()?; // pre_defined
        if &c.fourcc()? != b"pict" {
            return Err(unsupported("meta handler is not 'pict'"));
        }
    }
    let primary = parse_pitm(find(&list, b"pitm").ok_or_else(|| err("no primary item (pitm)"))?)?;
    let items = parse_iinf(find(&list, b"iinf").ok_or_else(|| err("no item info (iinf)"))?)?;
    let locations = parse_iloc(find(&list, b"iloc").ok_or_else(|| err("no item locations"))?)?;
    let mut properties = Vec::new();
    let mut associations = Vec::new();
    if let Some(iprp) = find(&list, b"iprp") {
        let children = boxes(iprp)?;
        if let Some(ipco) = find(&children, b"ipco") {
            for b in boxes(ipco)? {
                properties.push(parse_property(&b)?);
            }
        }
        for b in children.iter().filter(|b| &b.kind == b"ipma") {
            parse_ipma(b.payload, &mut associations)?;
        }
    }
    let dimg = match find(&list, b"iref") {
        Some(p) => parse_iref(p)?,
        None => Vec::new(),
    };
    Ok(Meta {
        primary,
        items,
        locations,
        properties,
        associations,
        dimg,
        idat: find(&list, b"idat").unwrap_or_default(),
    })
}

impl<'a> Meta<'a> {
    fn item_type(&self, id: u32) -> Option<[u8; 4]> {
        self.items.iter().find(|(i, _)| *i == id).map(|(_, t)| *t)
    }

    /// Properties of `id` in association order.
    fn props(&self, id: u32) -> Vec<&Property<'a>> {
        self.associations
            .iter()
            .filter(|(i, _)| *i == id)
            .flat_map(|(_, idx)| idx.iter())
            .filter_map(|&i| {
                usize::from(i)
                    .checked_sub(1)
                    .and_then(|i| self.properties.get(i))
            })
            .collect()
    }

    fn hvcc(&self, id: u32) -> Result<&'a [u8]> {
        self.props(id)
            .into_iter()
            .find_map(|p| match p {
                Property::Hvcc(h) => Some(*h),
                _ => None,
            })
            .ok_or_else(|| err(format!("item {id} has no hvcC")))
    }

    /// Item payload: its extents concatenated (from the file or from `idat`).
    fn item_data(&self, file: &'a [u8], id: u32) -> Result<Vec<u8>> {
        let loc = self
            .locations
            .iter()
            .find(|l| l.item_id == id)
            .ok_or_else(|| err(format!("item {id} has no location")))?;
        let src = match loc.construction_method {
            0 => file,
            1 => self.idat,
            m => return Err(unsupported(format!("construction method {m}"))),
        };
        let mut out = Vec::new();
        for e in &loc.extents {
            let start = loc
                .base_offset
                .checked_add(e.offset)
                .and_then(|s| usize::try_from(s).ok())
                .ok_or_else(|| err("extent offset overflow"))?;
            let end = if e.length == 0 {
                src.len()
            } else {
                usize::try_from(e.length)
                    .ok()
                    .and_then(|l| start.checked_add(l))
                    .ok_or_else(|| err("extent length overflow"))?
            };
            let chunk = src
                .get(start..end)
                .ok_or_else(|| err(format!("item {id} extent lies outside the file")))?;
            // Extents never add up to more than the file holds without overlapping; cap anyway.
            if out.len().saturating_add(chunk.len()) > file.len().saturating_add(self.idat.len()) {
                return Err(err("item data larger than the file"));
            }
            out.extend_from_slice(chunk);
        }
        Ok(out)
    }

    fn ispe(&self, id: u32) -> Option<(u32, u32)> {
        self.props(id).into_iter().find_map(|p| match p {
            Property::Ispe { width, height } if *width > 0 && *height > 0 => {
                Some((*width, *height))
            }
            _ => None,
        })
    }

    /// Colour matrix (H.273 code) and range signalled by an `nclx` box on `id`.
    fn nclx(&self, id: u32) -> Option<(u16, bool)> {
        self.props(id).into_iter().find_map(|p| match p {
            Property::Nclx { matrix, full_range } => Some((*matrix, *full_range)),
            _ => None,
        })
    }
}

// ---------------------------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------------------------

/// Y'CbCr to R'G'B' parameters.
#[derive(Clone, Copy, Debug)]
struct Yuv {
    kr: f32,
    kb: f32,
    full_range: bool,
}

impl Yuv {
    fn from_code(matrix: u16, full_range: bool) -> Option<Self> {
        let (kr, kb) = match matrix {
            1 => (0.2126, 0.0722),
            5 | 6 => (0.299, 0.114),
            9 | 10 => (0.2627, 0.0593),
            _ => return None,
        };
        Some(Self { kr, kb, full_range })
    }
    /// iPhone default: BT.601, full range.
    const DEFAULT: Yuv = Yuv {
        kr: 0.299,
        kb: 0.114,
        full_range: true,
    };
}

/// Colour conversion for `pic`: the item's `nclx` wins, then the stream VUI when it names a known
/// matrix, then [`Yuv::DEFAULT`].
fn colour(nclx: Option<(u16, bool)>, pic: &Picture) -> Yuv {
    if let Some((m, full)) = nclx {
        return Yuv::from_code(m, full).unwrap_or(Yuv {
            full_range: full,
            ..Yuv::DEFAULT
        });
    }
    Yuv::from_code(u16::from(pic.color.matrix), pic.color.full_range).unwrap_or(Yuv::DEFAULT)
}

fn sample(p: &Plane, i: usize) -> f32 {
    match p {
        Plane::U8(v) => v.get(i).copied().map_or(0.0, f32::from),
        Plane::U16(v) => v.get(i).copied().map_or(0.0, f32::from),
    }
}

/// Decode one HEVC image item (length-prefixed NAL units configured by `hvcc`).
fn decode_hevc(hvcc: &[u8], data: &[u8]) -> Result<Picture> {
    let mut dec = Decoder::with_threads(1);
    dec.configure_hvcc(hvcc)
        .map_err(|e| err(format!("hvcC: {e}")))?;
    let mut pics = dec.decode(data, 0).map_err(|e| err(format!("HEVC: {e}")))?;
    pics.extend(dec.flush());
    let pic = pics
        .into_iter()
        .next()
        .ok_or_else(|| err("HEVC image produced no picture"))?;
    let px = (pic.width as usize).saturating_mul(pic.height as usize);
    if pic.width == 0 || pic.height == 0 || px > MAX_PIXELS {
        return Err(err(format!(
            "unusable coded image size {}x{}",
            pic.width, pic.height
        )));
    }
    if !(8..=16).contains(&pic.bit_depth) {
        return Err(unsupported(format!("bit depth {}", pic.bit_depth)));
    }
    Ok(pic)
}

/// Canvas of RGBA8 pixels being composed.
struct Canvas {
    width: usize,
    height: usize,
    px: Vec<u8>,
}

impl Canvas {
    fn new(width: u32, height: u32) -> Result<Self> {
        let (w, h) = (width as usize, height as usize);
        let n = w.checked_mul(h).filter(|&n| n > 0 && n <= MAX_PIXELS);
        let len = n
            .and_then(|n| n.checked_mul(4))
            .ok_or_else(|| err(format!("unusable image size {width}x{height}")))?;
        Ok(Self {
            width: w,
            height: h,
            px: vec![0u8; len],
        })
    }

    /// Convert `pic` and place its top-left corner at (`x0`, `y0`), clipped to the canvas.
    fn blit(&mut self, pic: &Picture, x0: usize, y0: usize, yuv: Yuv) {
        let (pw, ph) = (pic.width as usize, pic.height as usize);
        let w = pw.min(self.width.saturating_sub(x0));
        let h = ph.min(self.height.saturating_sub(y0));
        if w == 0 || h == 0 {
            return;
        }
        let bits = pic.bit_depth.clamp(8, 16);
        let max = ((1u32 << bits) - 1) as f32;
        let shift = (bits - 8) as i32;
        let scale = f32::powi(2.0, shift);
        let mid = f32::powi(2.0, bits as i32 - 1);
        let (ys, yo, cs) = if yuv.full_range {
            (255.0 / max, 0.0, 255.0 / max)
        } else {
            (
                255.0 / (219.0 * scale),
                16.0 * scale,
                255.0 / (224.0 * scale),
            )
        };
        let kg = 1.0 - yuv.kr - yuv.kb;
        let (crr, cbb) = (2.0 * (1.0 - yuv.kr) * cs, 2.0 * (1.0 - yuv.kb) * cs);
        let (cgr, cgb) = (crr * yuv.kr / kg, cbb * yuv.kb / kg);
        let (cw, ch) = (pic.chroma_width as usize, pic.chroma_height as usize);
        let sx = usize::from(cw > 0 && cw < pw);
        let sy = usize::from(ch > 0 && ch < ph);
        let stride = self.width * 4;
        let rows = self.px.par_chunks_mut(stride).skip(y0).take(h);
        rows.enumerate().for_each(|(y, row)| {
            let yrow = y.saturating_mul(pic.y_stride);
            let crow = (y >> sy).saturating_mul(pic.uv_stride);
            let Some(out) = row.get_mut(x0 * 4..(x0 + w) * 4) else {
                return;
            };
            for (x, o) in out.as_chunks_mut::<4>().0.iter_mut().enumerate() {
                let yy = (sample(&pic.y, yrow + x) - yo) * ys;
                let ci = crow + (x >> sx);
                let u = sample(&pic.u, ci) - mid;
                let v = sample(&pic.v, ci) - mid;
                o[0] = (yy + crr * v).round().clamp(0.0, 255.0) as u8;
                o[1] = (yy - cgr * v - cgb * u).round().clamp(0.0, 255.0) as u8;
                o[2] = (yy + cbb * u).round().clamp(0.0, 255.0) as u8;
                o[3] = 255;
            }
        });
    }
}

/// Anticlockwise quarter turns of a packed RGBA8 image.
fn rotate_ccw(w: usize, h: usize, px: &[u8], turns: u8) -> (usize, usize, Vec<u8>) {
    let turns = turns & 3;
    if turns == 0 {
        return (w, h, px.to_vec());
    }
    let (nw, nh) = if turns == 2 { (w, h) } else { (h, w) };
    let mut out = vec![0u8; px.len()];
    for y in 0..h {
        for x in 0..w {
            let (dx, dy) = match turns {
                1 => (y, w - 1 - x),
                2 => (w - 1 - x, h - 1 - y),
                _ => (h - 1 - y, x),
            };
            let s = (y * w + x) * 4;
            let d = (dy * nw + dx) * 4;
            if let (Some(src), Some(dst)) = (px.get(s..s + 4), out.get_mut(d..d + 4)) {
                dst.copy_from_slice(src);
            }
        }
    }
    (nw, nh, out)
}

/// Crop region (x, y, w, h) of a `clap` box on a `w`×`h` image, clamped to the image; None when
/// the box is degenerate (zero denominators, empty result).
fn clap_region(
    w: usize,
    h: usize,
    width: (u32, u32),
    height: (u32, u32),
    horiz_off: (i32, u32),
    vert_off: (i32, u32),
) -> Option<(usize, usize, usize, usize)> {
    let frac = |n: f64, d: u32| (d != 0).then(|| n / f64::from(d));
    let cw = frac(f64::from(width.0), width.1)?.round();
    let ch = frac(f64::from(height.0), height.1)?.round();
    let ho = frac(f64::from(horiz_off.0), horiz_off.1)?;
    let vo = frac(f64::from(vert_off.0), vert_off.1)?;
    // Centre of the aperture = image centre + offset (ISO/IEC 14496-12 clap).
    let left = (ho + (w as f64 - cw) / 2.0).round().clamp(0.0, w as f64);
    let top = (vo + (h as f64 - ch) / 2.0).round().clamp(0.0, h as f64);
    let cw = cw.clamp(0.0, w as f64 - left);
    let ch = ch.clamp(0.0, h as f64 - top);
    let r = (left as usize, top as usize, cw as usize, ch as usize);
    (r.2 > 0 && r.3 > 0).then_some(r)
}

/// Crop a packed RGBA8 image to (x, y, cw, ch) (must lie inside the `w`×`h` image).
fn crop(w: usize, px: &[u8], x: usize, y: usize, cw: usize, ch: usize) -> Vec<u8> {
    let mut out = Vec::with_capacity(cw.saturating_mul(ch).saturating_mul(4));
    for row in y..y.saturating_add(ch) {
        let s = (row * w + x) * 4;
        if let Some(line) = px.get(s..s + cw * 4) {
            out.extend_from_slice(line);
        }
    }
    out
}

/// Mirror a packed RGBA8 image: axis 0 flips left/right, axis 1 top/bottom.
fn mirror(w: usize, px: &mut [u8], axis: u8) {
    let stride = w * 4;
    if stride == 0 {
        return;
    }
    if axis == 0 {
        for row in px.chunks_exact_mut(stride) {
            let (pixels, _) = row.as_chunks_mut::<4>();
            pixels.reverse();
        }
    } else {
        let rows = px.len() / stride;
        for y in 0..rows / 2 {
            let (top, bottom) = px.split_at_mut((rows - 1 - y) * stride);
            if let (Some(a), Some(b)) = (
                top.get_mut(y * stride..(y + 1) * stride),
                bottom.get_mut(..stride),
            ) {
                a.swap_with_slice(b);
            }
        }
    }
}

/// Decode the primary image of a HEIF file to straight RGBA8 (opaque): width, height, pixels.
pub(crate) fn decode(bytes: &[u8]) -> Result<(u32, u32, Vec<u8>)> {
    let top = boxes(bytes)?;
    let meta = parse_meta(find(&top, b"meta").ok_or_else(|| err("no meta box"))?)?;
    let id = meta.primary;
    let kind = meta
        .item_type(id)
        .ok_or_else(|| err(format!("primary item {id} has no item info")))?;
    let canvas = match &kind {
        b"hvc1" => {
            let pic = decode_hevc(meta.hvcc(id)?, &meta.item_data(bytes, id)?)?;
            // `ispe` is the reconstructed size; the coded picture can only be larger.
            let (w, h) = meta.ispe(id).map_or((pic.width, pic.height), |(w, h)| {
                (w.min(pic.width), h.min(pic.height))
            });
            let mut c = Canvas::new(w, h)?;
            c.blit(&pic, 0, 0, colour(meta.nclx(id), &pic));
            c
        }
        b"grid" => decode_grid(&meta, bytes, id)?,
        other => {
            return Err(unsupported(format!(
                "primary item type '{}'",
                String::from_utf8_lossy(other)
            )));
        }
    };
    let (mut w, mut h, mut px) = (canvas.width, canvas.height, canvas.px);
    for p in meta.props(id) {
        match p {
            Property::Irot(t) => (w, h, px) = rotate_ccw(w, h, &px, *t),
            Property::Imir(axis) => mirror(w, &mut px, *axis),
            Property::Clap {
                width,
                height,
                horiz_off,
                vert_off,
            } => {
                if let Some((x, y, cw, ch)) =
                    clap_region(w, h, *width, *height, *horiz_off, *vert_off)
                {
                    px = crop(w, &px, x, y, cw, ch);
                    (w, h) = (cw, ch);
                }
            }
            _ => {}
        }
    }
    let w = u32::try_from(w).map_err(|_| err("width overflow"))?;
    let h = u32::try_from(h).map_err(|_| err("height overflow"))?;
    Ok((w, h, px))
}

fn decode_grid(meta: &Meta<'_>, bytes: &[u8], id: u32) -> Result<Canvas> {
    let grid = parse_grid(&meta.item_data(bytes, id)?)?;
    let count = (grid.rows as usize).saturating_mul(grid.columns as usize);
    let tiles = meta
        .dimg
        .iter()
        .find(|(from, _)| *from == id)
        .map(|(_, to)| to.as_slice())
        .unwrap_or_default();
    let tiles = tiles.get(..count).ok_or_else(|| {
        err(format!(
            "grid needs {count} tiles, file references {}",
            tiles.len()
        ))
    })?;
    let mut canvas = Canvas::new(grid.output_width, grid.output_height)?;
    for t in tiles {
        if meta.item_type(*t) != Some(*b"hvc1") {
            return Err(unsupported(format!("grid tile {t} is not an HEVC image")));
        }
    }
    let pics = tiles
        .par_iter()
        .map(|&t| decode_hevc(meta.hvcc(t)?, &meta.item_data(bytes, t)?))
        .collect::<Result<Vec<_>>>()?;
    let (tw, th) = match pics.first() {
        Some(p) => (p.width as usize, p.height as usize),
        None => return Err(err("grid has no tiles")),
    };
    if pics
        .iter()
        .any(|p| p.width as usize != tw || p.height as usize != th)
    {
        return Err(err("grid tiles differ in size"));
    }
    let cols = grid.columns as usize;
    if tw.saturating_mul(cols) < canvas.width
        || th.saturating_mul(grid.rows as usize) < canvas.height
    {
        return Err(err("grid tiles do not cover the output size"));
    }
    for (i, (pic, t)) in pics.iter().zip(tiles).enumerate() {
        let (r, c) = (i / cols, i % cols);
        let yuv = colour(meta.nclx(*t).or_else(|| meta.nclx(id)), pic);
        canvas.blit(pic, c * tw, r * th, yuv);
    }
    Ok(canvas)
}

#[cfg(test)]
mod tests {
    // Vendored from GalleryBrowser 55a4483: test assertions may panic by design. See crates/VENDORED.md.
    #![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
    use super::*;

    fn bx(kind: &[u8; 4], payload: &[u8]) -> Vec<u8> {
        let mut v = ((payload.len() + 8) as u32).to_be_bytes().to_vec();
        v.extend_from_slice(kind);
        v.extend_from_slice(payload);
        v
    }

    fn full(kind: &[u8; 4], version: u8, flags: u32, body: &[u8]) -> Vec<u8> {
        let mut p = ((u32::from(version) << 24) | flags).to_be_bytes().to_vec();
        p.extend_from_slice(body);
        bx(kind, &p)
    }

    fn infe(id: u16, kind: &[u8; 4]) -> Vec<u8> {
        let mut b = id.to_be_bytes().to_vec();
        b.extend_from_slice(&[0, 0]);
        b.extend_from_slice(kind);
        b.push(0); // item_name
        full(b"infe", 2, 0, &b)
    }

    #[test]
    fn iloc_v1_with_idat_method_and_v0() {
        // v1: offset 4, length 4, base 4, index 0; one item, method 1, one extent.
        let mut b = vec![0x44, 0x40, 0, 1];
        b.extend_from_slice(&7u16.to_be_bytes());
        b.extend_from_slice(&1u16.to_be_bytes()); // construction method 1
        b.extend_from_slice(&0u16.to_be_bytes());
        b.extend_from_slice(&2u32.to_be_bytes()); // base
        b.extend_from_slice(&1u16.to_be_bytes());
        b.extend_from_slice(&3u32.to_be_bytes());
        b.extend_from_slice(&5u32.to_be_bytes());
        let boxed = full(b"iloc", 1, 0, &b);
        let locs = parse_iloc(&boxed[8..]).unwrap();
        assert_eq!(
            locs,
            vec![Location {
                item_id: 7,
                construction_method: 1,
                base_offset: 2,
                extents: vec![Extent {
                    offset: 3,
                    length: 5
                }],
            }]
        );
        // v0 with 8-byte offsets and no base offset.
        let mut b = vec![0x84, 0x00, 0, 1];
        b.extend_from_slice(&1u16.to_be_bytes());
        b.extend_from_slice(&0u16.to_be_bytes());
        b.extend_from_slice(&1u16.to_be_bytes());
        b.extend_from_slice(&100u64.to_be_bytes());
        b.extend_from_slice(&9u32.to_be_bytes());
        let boxed = full(b"iloc", 0, 0, &b);
        let locs = parse_iloc(&boxed[8..]).unwrap();
        assert_eq!(locs[0].construction_method, 0);
        assert_eq!(
            locs[0].extents,
            vec![Extent {
                offset: 100,
                length: 9
            }]
        );
    }

    #[test]
    fn ipma_small_and_large_indices() {
        let mut b = 2u32.to_be_bytes().to_vec();
        b.extend_from_slice(&1u16.to_be_bytes());
        b.extend_from_slice(&[2, 0x81, 0x03]);
        b.extend_from_slice(&2u16.to_be_bytes());
        b.extend_from_slice(&[1, 0x02]);
        let boxed = full(b"ipma", 0, 0, &b);
        let mut out = Vec::new();
        parse_ipma(&boxed[8..], &mut out).unwrap();
        assert_eq!(out, vec![(1, vec![1, 3]), (2, vec![2])]);

        let mut b = 1u32.to_be_bytes().to_vec();
        b.extend_from_slice(&5u32.to_be_bytes());
        b.push(1);
        b.extend_from_slice(&0x8123u16.to_be_bytes());
        let boxed = full(b"ipma", 1, 1, &b);
        let mut out = Vec::new();
        parse_ipma(&boxed[8..], &mut out).unwrap();
        assert_eq!(out, vec![(5, vec![0x123])]);
    }

    #[test]
    fn grid_descriptor_16_and_32_bit() {
        assert_eq!(
            parse_grid(&[0, 0, 5, 7, 0x0f, 0xc0, 0x0b, 0xd0]).unwrap(),
            Grid {
                rows: 6,
                columns: 8,
                output_width: 4032,
                output_height: 3024
            }
        );
        let mut b = vec![0, 1, 0, 0];
        b.extend_from_slice(&70000u32.to_be_bytes());
        b.extend_from_slice(&3u32.to_be_bytes());
        let g = parse_grid(&b).unwrap();
        assert_eq!((g.output_width, g.output_height), (70000, 3));
        assert!(parse_grid(&[0, 1, 0, 0, 1, 2]).is_err());
    }

    /// A minimal meta box: primary grid item 1 with tiles 2 and 3, grid stored in idat.
    fn grid_meta() -> Vec<u8> {
        let mut m = Vec::new();
        m.extend(full(
            b"hdlr",
            0,
            0,
            b"\0\0\0\0pict\0\0\0\0\0\0\0\0\0\0\0\0\0",
        ));
        m.extend(full(b"pitm", 0, 0, &1u16.to_be_bytes()));
        let mut iinf = 3u16.to_be_bytes().to_vec();
        iinf.extend(infe(1, b"grid"));
        iinf.extend(infe(2, b"hvc1"));
        iinf.extend(infe(3, b"hvc1"));
        m.extend(full(b"iinf", 0, 0, &iinf));
        // iloc v1: 4-byte offset/length, no base; item 1 in idat.
        let mut iloc = vec![0x44, 0x00, 0, 1];
        iloc.extend_from_slice(&1u16.to_be_bytes());
        iloc.extend_from_slice(&1u16.to_be_bytes());
        iloc.extend_from_slice(&0u16.to_be_bytes());
        iloc.extend_from_slice(&1u16.to_be_bytes());
        iloc.extend_from_slice(&0u32.to_be_bytes());
        iloc.extend_from_slice(&8u32.to_be_bytes());
        m.extend(full(b"iloc", 1, 0, &iloc));
        let mut ipco = full(b"ispe", 0, 0, &[0, 0, 0, 64, 0, 0, 0, 32]);
        ipco.extend(bx(b"irot", &[1]));
        let mut ipma = 1u32.to_be_bytes().to_vec();
        ipma.extend_from_slice(&1u16.to_be_bytes());
        ipma.extend_from_slice(&[2, 0x01, 0x82]);
        let mut iprp = bx(b"ipco", &ipco);
        iprp.extend(full(b"ipma", 0, 0, &ipma));
        m.extend(bx(b"iprp", &iprp));
        let mut dimg = 1u16.to_be_bytes().to_vec();
        dimg.extend_from_slice(&2u16.to_be_bytes());
        dimg.extend_from_slice(&2u16.to_be_bytes());
        dimg.extend_from_slice(&3u16.to_be_bytes());
        m.extend(full(b"iref", 0, 0, &bx(b"dimg", &dimg)));
        m.extend(bx(b"idat", &[0, 0, 0, 1, 0, 64, 0, 32]));
        full(b"meta", 0, 0, &m)
    }

    #[test]
    fn meta_with_grid_parses() {
        let meta_box = grid_meta();
        let meta = parse_meta(&meta_box[8..]).unwrap();
        assert_eq!(meta.primary, 1);
        assert_eq!(meta.item_type(1), Some(*b"grid"));
        assert_eq!(meta.dimg, vec![(1, vec![2, 3])]);
        let props = meta.props(1);
        assert_eq!(
            props,
            vec![
                &Property::Ispe {
                    width: 64,
                    height: 32
                },
                &Property::Irot(1)
            ]
        );
        let data = meta.item_data(&meta_box, 1).unwrap();
        let g = parse_grid(&data).unwrap();
        assert_eq!((g.rows, g.columns, g.output_width), (1, 2, 64));
        // The tiles have no location: decoding fails with an error, not a panic.
        let mut file = bx(b"ftyp", b"heic\0\0\0\0mif1heic");
        file.extend(meta_box);
        assert!(sniff(&file));
        assert!(decode(&file).is_err());
    }

    #[test]
    fn hostile_input_is_an_error() {
        let meta_box = grid_meta();
        for cut in 0..meta_box.len() {
            let mut file = bx(b"ftyp", b"heic\0\0\0\0mif1");
            file.extend_from_slice(&meta_box[..cut]);
            assert!(decode(&file).is_err(), "cut at {cut}");
        }
        // Huge box size, extent beyond EOF, absurd counts.
        assert!(boxes(&[0xff, 0xff, 0xff, 0xff, b'm', b'e', b't', b'a']).is_err());
        assert!(
            boxes(&[
                0, 0, 0, 1, b'm', b'e', b't', b'a', 0xff, 0, 0, 0, 0, 0, 0, 0
            ])
            .is_err()
        );
        assert!(boxes(&[0, 0, 0, 3, b'x', b'x', b'x', b'x']).is_err());
        let mut iloc = vec![0, 0, 0, 0, 0x44, 0x00];
        iloc.extend_from_slice(&0xffffu16.to_be_bytes());
        assert!(parse_iloc(&iloc).is_err());
        let mut iloc = vec![2, 0, 0, 0, 0x44, 0x00];
        iloc.extend_from_slice(&u32::MAX.to_be_bytes());
        assert!(parse_iloc(&iloc).is_err());
        let ipma = [0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff];
        assert!(parse_ipma(&ipma, &mut Vec::new()).is_err());
        assert!(!sniff(b"\0\0\0\x08ftyp"));
        assert!(!sniff(b"\x89PNG\r\n\x1a\n"));
    }

    #[test]
    fn extent_outside_file_is_rejected() {
        let meta = Meta {
            primary: 1,
            items: vec![(1, *b"hvc1")],
            locations: vec![Location {
                item_id: 1,
                construction_method: 0,
                base_offset: u64::MAX,
                extents: vec![Extent {
                    offset: 2,
                    length: 4,
                }],
            }],
            properties: Vec::new(),
            associations: Vec::new(),
            dimg: Vec::new(),
            idat: &[],
        };
        assert!(meta.item_data(&[0; 16], 1).is_err());
    }

    #[test]
    fn clap_crops_centred_and_clamps() {
        // libheif's odd-size aperture: 1001x707 on a 1002x708 picture, offset -1/2.
        assert_eq!(
            clap_region(1002, 708, (1001, 1), (707, 1), (-1, 2), (-1, 2)),
            Some((0, 0, 1001, 707))
        );
        assert_eq!(
            clap_region(10, 10, (4, 1), (2, 1), (0, 1), (0, 1)),
            Some((3, 4, 4, 2))
        );
        assert_eq!(clap_region(10, 10, (4, 0), (2, 1), (0, 1), (0, 1)), None);
        // Absurd sizes and offsets are clamped inside the picture.
        let (x, y, w, h) = clap_region(10, 10, (u32::MAX, 1), (u32::MAX, 1), (i32::MAX, 1), (0, 1))
            .unwrap_or_default();
        assert!(x + w <= 10 && y + h <= 10);
        let px: Vec<u8> = (0..16 * 4).map(|i| (i / 4) as u8).collect();
        let c = crop(4, &px, 1, 2, 2, 2);
        assert_eq!(
            c.iter().step_by(4).copied().collect::<Vec<_>>(),
            vec![9, 10, 13, 14]
        );
    }

    #[test]
    fn rotate_and_mirror() {
        // 2x1: red, green.
        let px = [255, 0, 0, 255, 0, 255, 0, 255];
        let (w, h, r) = rotate_ccw(2, 1, &px, 1);
        assert_eq!((w, h), (1, 2));
        // Anticlockwise: the right pixel (green) goes to the top.
        assert_eq!(&r[..4], &[0, 255, 0, 255]);
        let (_, _, r) = rotate_ccw(2, 1, &px, 2);
        assert_eq!(&r[..4], &[0, 255, 0, 255]);
        let mut m = px;
        mirror(2, &mut m, 0);
        assert_eq!(&m[..4], &[0, 255, 0, 255]);
        let mut col = [1, 1, 1, 1, 2, 2, 2, 2];
        mirror(1, &mut col, 1);
        assert_eq!(col, [2, 2, 2, 2, 1, 1, 1, 1]);
    }
}
