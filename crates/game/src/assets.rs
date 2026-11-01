//! Bounded readers for the game's PC asset containers. These run only on workers.
use std::ops::Range;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AssetError {
    Truncated,
    Invalid,
    Unsupported,
    Budget,
}
pub type Result<T> = std::result::Result<T, AssetError>;

pub fn range(bytes: &[u8], offset: usize, size: usize) -> Result<&[u8]> {
    bytes
        .get(offset..offset.checked_add(size).ok_or(AssetError::Truncated)?)
        .ok_or(AssetError::Truncated)
}
fn u16_at(b: &[u8], p: usize) -> Result<u16> {
    Ok(u16::from_le_bytes(range(b, p, 2)?.try_into().unwrap()))
}
fn u32_at(b: &[u8], p: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(range(b, p, 4)?.try_into().unwrap()))
}
fn u64_at(b: &[u8], p: usize) -> Result<u64> {
    Ok(u64::from_le_bytes(range(b, p, 8)?.try_into().unwrap()))
}
pub fn wide_string(bytes: &[u8], offset: usize) -> Result<String> {
    if offset & 1 != 0 {
        return Err(AssetError::Invalid);
    }
    let mut units = Vec::new();
    for i in 0..=4096 {
        let unit = u16_at(
            bytes,
            offset.checked_add(i * 2).ok_or(AssetError::Truncated)?,
        )?;
        if unit == 0 {
            return String::from_utf16(&units).map_err(|_| AssetError::Invalid);
        }
        units.push(unit);
    }
    Err(AssetError::Budget)
}

#[derive(Debug, Clone)]
pub struct DdsLevel {
    pub offset: usize,
    pub rows: u32,
    pub row_bytes: u32,
}
#[derive(Debug, Clone)]
pub struct DdsImage {
    pub width: u32,
    pub height: u32,
    pub format: u32,
    pub levels: Vec<DdsLevel>,
}
pub fn parse_dds(bytes: &[u8]) -> Result<DdsImage> {
    if range(bytes, 0, 4)? != b"DDS " || u32_at(bytes, 4)? != 124 || u32_at(bytes, 76)? != 32 {
        return Err(AssetError::Invalid);
    }
    let (mut h, mut w) = (u32_at(bytes, 12)?, u32_at(bytes, 16)?);
    let mips = u32_at(bytes, 28)?.max(1);
    if w == 0
        || h == 0
        || w > 16384
        || h > 16384
        || mips > 32 - w.max(h).leading_zeros()
        || u32_at(bytes, 24)? > 1
        || u32_at(bytes, 112)? != 0
    {
        return Err(AssetError::Invalid);
    }
    let (format, mut offset) = match u32_at(bytes, 84)? {
        0x30315844 => {
            if u32_at(bytes, 132)? != 3 || u32_at(bytes, 136)? & 4 != 0 || u32_at(bytes, 140)? != 1
            {
                return Err(AssetError::Unsupported);
            }
            (u32_at(bytes, 128)?, 148)
        }
        0x31545844 => (71, 128),
        0x33545844 => (74, 128),
        0x35545844 => (77, 128),
        _ => return Err(AssetError::Unsupported),
    };
    let block = match format {
        28 | 29 => 0,
        71 | 72 => 8,
        74 | 75 | 77 | 78 | 98 | 99 => 16,
        _ => return Err(AssetError::Unsupported),
    };
    let mut image = DdsImage {
        width: w,
        height: h,
        format,
        levels: Vec::with_capacity(mips as usize),
    };
    for _ in 0..mips {
        let rows = if block == 0 { h } else { h.div_ceil(4) };
        let row_bytes = if block == 0 {
            w * 4
        } else {
            w.div_ceil(4) * block
        };
        let count = rows as usize * row_bytes as usize;
        range(bytes, offset, count)?;
        image.levels.push(DdsLevel {
            offset,
            rows,
            row_bytes,
        });
        offset += count;
        w = (w / 2).max(1);
        h = (h / 2).max(1);
    }
    if offset != bytes.len() {
        return Err(AssetError::Invalid);
    }
    Ok(image)
}

/// Returns straight-alpha RGBA8; the host converts to its compositor convention.
pub fn decode_dds_rgba(bytes: &[u8], max_bytes: usize) -> Result<(u32, u32, Vec<u8>)> {
    let image = parse_dds(bytes)?;
    let pixels = image.width as usize * image.height as usize;
    if pixels.checked_mul(4).ok_or(AssetError::Budget)? > max_bytes {
        return Err(AssetError::Budget);
    }
    let first = &image.levels[0];
    let input = range(
        bytes,
        first.offset,
        first.rows as usize * first.row_bytes as usize,
    )?;
    if image.format == 28 || image.format == 29 {
        return Ok((image.width, image.height, input.to_vec()));
    }
    let mut rgba = vec![0u8; pixels * 4];
    let w = image.width as usize;
    let h = image.height as usize;
    match image.format {
        71..=78 | 98 | 99 => {
            let bytes_per_block = if image.format <= 72 { 8 } else { 16 };
            for by in 0..h.div_ceil(4) {
                for bx in 0..w.div_ceil(4) {
                    let block =
                        &input[(by * w.div_ceil(4) + bx) * bytes_per_block..][..bytes_per_block];
                    let opaque = bytes_per_block == 16;
                    let mut decoded = if image.format >= 98 {
                        let mut pixels = [0u32; 16];
                        texture2ddecoder::decode_bc7_block(block, &mut pixels);
                        pixels
                    } else {
                        decode_bc_color(&block[if opaque { 8 } else { 0 }..], opaque)
                    };
                    if image.format == 77 || image.format == 78 {
                        let a0 = block[0] as u32;
                        let a1 = block[1] as u32;
                        let mut alpha = [a0, a1, 0, 0, 0, 0, 0, 255];
                        let divisor = if a0 > a1 { 7 } else { 5 };
                        for i in 1..divisor {
                            alpha[(i + 1) as usize] = ((divisor - i) * a0 + i * a1) / divisor;
                        }
                        let mut selectors =
                            u64::from_le_bytes(block[..8].try_into().unwrap()) >> 16;
                        for pixel in &mut decoded {
                            *pixel = (*pixel & 0xff_ffff) | (alpha[(selectors & 7) as usize] << 24);
                            selectors >>= 3;
                        }
                    }
                    for y in 0..4 {
                        for x in 0..4 {
                            if bx * 4 + x < w && by * 4 + y < h {
                                let i = y * 4 + x;
                                let pixel = if image.format == 74 || image.format == 75 {
                                    let alpha = ((block[i / 2] >> ((i & 1) * 4)) & 15) as u32 * 17;
                                    (decoded[i] & 0xff_ffff) | (alpha << 24)
                                } else {
                                    decoded[i]
                                };
                                let [b, g, r, a] = pixel.to_le_bytes();
                                let p = ((by * 4 + y) * w + bx * 4 + x) * 4;
                                rgba[p..p + 4].copy_from_slice(&[r, g, b, a]);
                            }
                        }
                    }
                }
            }
        }
        _ => return Err(AssetError::Unsupported),
    }
    Ok((image.width, image.height, rgba))
}

// Match the existing bcdec endpoint/interpolation rounding. BC2/3 always use
// four opaque RGB endpoints; BC1's fourth selector can instead be transparent.
fn decode_bc_color(block: &[u8], opaque: bool) -> [u32; 16] {
    let c0 = u16::from_le_bytes(block[..2].try_into().unwrap()) as u32;
    let c1 = u16::from_le_bytes(block[2..4].try_into().unwrap()) as u32;
    let (r0, g0, b0) = ((c0 >> 11) & 31, (c0 >> 5) & 63, c0 & 31);
    let (r1, g1, b1) = ((c1 >> 11) & 31, (c1 >> 5) & 63, c1 & 31);
    let pack = |r: u32, g: u32, b: u32| 0xff00_0000 | (r << 16) | (g << 8) | b;
    let mut colors = [
        pack(
            (r0 * 527 + 23) >> 6,
            (g0 * 259 + 33) >> 6,
            (b0 * 527 + 23) >> 6,
        ),
        pack(
            (r1 * 527 + 23) >> 6,
            (g1 * 259 + 33) >> 6,
            (b1 * 527 + 23) >> 6,
        ),
        0,
        0,
    ];
    if c0 > c1 || opaque {
        colors[2] = pack(
            ((2 * r0 + r1) * 351 + 61) >> 7,
            ((2 * g0 + g1) * 2763 + 1039) >> 11,
            ((2 * b0 + b1) * 351 + 61) >> 7,
        );
        colors[3] = pack(
            ((r0 + 2 * r1) * 351 + 61) >> 7,
            ((g0 + 2 * g1) * 2763 + 1039) >> 11,
            ((b0 + 2 * b1) * 351 + 61) >> 7,
        );
    } else {
        colors[2] = pack(
            ((r0 + r1) * 1053 + 125) >> 8,
            ((g0 + g1) * 4145 + 1019) >> 11,
            ((b0 + b1) * 1053 + 125) >> 8,
        );
    }
    let mut selectors = u32::from_le_bytes(block[4..8].try_into().unwrap());
    let mut result = [0u32; 16];
    for pixel in &mut result {
        *pixel = colors[(selectors & 3) as usize];
        selectors >>= 2;
    }
    result
}

#[derive(Debug, Clone)]
pub struct TpfEntry<'a> {
    pub name: String,
    pub dds: &'a [u8],
    pub data_range: Range<usize>,
}
pub fn parse_tpf(bytes: &[u8]) -> Result<Vec<TpfEntry<'_>>> {
    if range(bytes, 0, 4)? != b"TPF\0"
        || *bytes.get(12).ok_or(AssetError::Truncated)? != 0
        || *bytes.get(13).ok_or(AssetError::Truncated)? != 3
    {
        return Err(AssetError::Unsupported);
    }
    let count = u32_at(bytes, 8)? as usize;
    if count == 0 || count > 4096 {
        return Err(AssetError::Budget);
    }
    let mut p = if bytes.get(15).ok_or(AssetError::Truncated)? & 1 != 0 {
        48
    } else {
        16
    };
    let mut result = Vec::with_capacity(count);
    for _ in 0..count {
        let off = u32_at(bytes, p)? as usize;
        let size = u32_at(bytes, p + 4)? as usize;
        if *bytes.get(p + 9).ok_or(AssetError::Truncated)? != 0 {
            return Err(AssetError::Unsupported);
        }
        let name = wide_string(bytes, u32_at(bytes, p + 12)? as usize)?;
        let extra = u32_at(bytes, p + 16)? as usize;
        if extra > 4096 {
            return Err(AssetError::Budget);
        }
        let dds = range(bytes, off, size)?;
        result.push(TpfEntry {
            name,
            dds,
            data_range: off..off + size,
        });
        p += 20;
        for _ in 0..extra {
            let size = u32_at(bytes, p + 4)? as usize;
            range(bytes, p + 8, size)?;
            p += 8 + size;
        }
    }
    Ok(result)
}

#[derive(Debug, Clone)]
pub struct BinderEntry<'a> {
    pub name: String,
    pub id: u32,
    pub bytes: &'a [u8],
    pub offset: u64,
    pub size: u64,
}
pub fn parse_binder(bytes: &[u8]) -> Result<Vec<BinderEntry<'_>>> {
    let tag = range(bytes, 0, 4)?;
    let index = tag == b"BHF4";
    if (!index && tag != b"BND4")
        || range(bytes, 4, 8)? != [0, 0, 0, 0, 0, 0, 1, 0]
        || u64_at(bytes, 16)? != 64
        || u64_at(bytes, 32)? != 36
    {
        return Err(AssetError::Unsupported);
    }
    let count = u32_at(bytes, 12)? as usize;
    if count > 65536 {
        return Err(AssetError::Budget);
    }
    range(bytes, 64, count * 36)?;
    let mut result = Vec::with_capacity(count);
    for i in 0..count {
        let p = 64 + i * 36;
        let compressed = u64_at(bytes, p + 8)?;
        let size = u64_at(bytes, p + 16)?;
        let offset = u32_at(bytes, p + 24)? as u64;
        let data = if index {
            &bytes[0..0]
        } else {
            if compressed != size {
                return Err(AssetError::Unsupported);
            }
            range(
                bytes,
                offset as usize,
                usize::try_from(size).map_err(|_| AssetError::Budget)?,
            )?
        };
        result.push(BinderEntry {
            name: wide_string(bytes, u32_at(bytes, p + 32)? as usize)?,
            id: u32_at(bytes, p + 28)?,
            bytes: data,
            offset,
            size,
        });
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn dds() -> Vec<u8> {
        let mut b = vec![0u8; 136];
        b[..4].copy_from_slice(b"DDS ");
        for (p, v) in [(4, 124u32), (12, 4), (16, 4), (76, 32), (84, 0x31545844)] {
            b[p..p + 4].copy_from_slice(&v.to_le_bytes());
        }
        b[128..130].copy_from_slice(&0xf800u16.to_le_bytes());
        b
    }
    #[test]
    fn dds_bounds_and_color() {
        let b = dds();
        let (w, h, rgba) = decode_dds_rgba(&b, 64).unwrap();
        assert_eq!((w, h), (4, 4));
        assert_eq!(&rgba[..4], &[255, 0, 0, 255]);
        assert_eq!(decode_dds_rgba(&b, 63).unwrap_err(), AssetError::Budget);
        assert!(parse_dds(&b[..135]).is_err());
        let mut extra = b;
        extra.push(0);
        assert!(parse_dds(&extra).is_err());
    }
    #[test]
    fn parsers_reject_truncated_offsets() {
        for n in 0..148 {
            let b = vec![0; n];
            assert!(parse_dds(&b).is_err());
            assert!(parse_tpf(&b).is_err());
            assert!(parse_binder(&b).is_err());
        }
        assert!(range(&[0], usize::MAX, 2).is_err());
        assert!(wide_string(&[65, 0], 0).is_err());
    }
    #[test]
    fn bc1_transparency_and_bc2_bc3_opaque_interpolation() {
        let mut b = dds();
        b[128..132].copy_from_slice(&[0, 0, 255, 255]);
        b[132..136].fill(255);
        assert_eq!(&decode_dds_rgba(&b, 64).unwrap().2[..4], &[0, 0, 0, 0]);
        for fourcc in [0x3354_5844u32, 0x3554_5844] {
            let mut b = dds();
            b.resize(144, 0);
            b[84..88].copy_from_slice(&fourcc.to_le_bytes());
            b[128..136].fill(255);
            b[136..140].copy_from_slice(&[0, 0, 255, 255]);
            b[140..144].fill(255);
            assert_eq!(
                &decode_dds_rgba(&b, 64).unwrap().2[..4],
                &[170, 170, 170, 255]
            );
        }
    }
}
