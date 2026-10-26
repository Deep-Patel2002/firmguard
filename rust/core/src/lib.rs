#![forbid(unsafe_code)]

pub const MAX_INPUT: usize = 65_536;
pub const MAX_IMAGES: usize = 64;
pub const MAX_RECORDS: usize = 512;
pub const MAX_VALUE: usize = 4096;
pub const MAX_CANONICAL: usize = 12_288;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Error {
    Argument = 1,
    Size = 2,
    Truncated = 3,
    Header = 4,
    Format = 5,
    Limit = 6,
    Semantic = 7,
}
impl Error {
    pub fn name(self) -> &'static str {
        match self {
            Self::Argument => "argument",
            Self::Size => "size",
            Self::Truncated => "truncated",
            Self::Header => "header",
            Self::Format => "format",
            Self::Limit => "limit",
            Self::Semantic => "semantic",
        }
    }
}
#[derive(Clone, Copy, Debug)]
pub struct Image<'a> {
    pub path: &'a [u8],
    pub offset: u64,
    pub size: u64,
    pub version: u64,
    pub sha256: [u8; 32],
}
const EMPTY_IMAGE: Image<'static> = Image {
    path: &[],
    offset: 0,
    size: 0,
    version: 0,
    sha256: [0; 32],
};
#[derive(Debug)]
pub struct Manifest<'a> {
    pub device: &'a [u8],
    pub payload_size: u64,
    pub sequence: u64,
    images: [Image<'a>; MAX_IMAGES],
    count: usize,
}
impl Manifest<'_> {
    pub fn images(&self) -> &[Image<'_>] {
        &self.images[..self.count]
    }
    pub fn canonical(&self) -> Vec<u8> {
        let mut out = Vec::with_capacity(MAX_CANONICAL);
        out.extend_from_slice(&(self.device.len() as u16).to_le_bytes());
        out.extend_from_slice(self.device);
        out.extend_from_slice(&self.payload_size.to_le_bytes());
        out.extend_from_slice(&self.sequence.to_le_bytes());
        out.extend_from_slice(&(self.count as u16).to_le_bytes());
        for image in self.images() {
            out.extend_from_slice(&(image.path.len() as u16).to_le_bytes());
            out.extend_from_slice(image.path);
            out.extend_from_slice(&image.offset.to_le_bytes());
            out.extend_from_slice(&image.size.to_le_bytes());
            out.extend_from_slice(&image.version.to_le_bytes());
            out.extend_from_slice(&image.sha256);
        }
        out
    }
}
struct Cursor<'a> {
    remaining: &'a [u8],
}
struct Record<'a> {
    kind: u16,
    flags: u16,
    value: &'a [u8],
}
impl<'a> Cursor<'a> {
    fn take(&mut self, n: usize) -> Result<&'a [u8], Error> {
        if n > self.remaining.len() {
            return Err(Error::Truncated);
        }
        let (value, rest) = self.remaining.split_at(n);
        self.remaining = rest;
        Ok(value)
    }
    fn record(&mut self, budget: &mut usize) -> Result<Record<'a>, Error> {
        let header = self.take(8)?;
        let kind = le16(header);
        let flags = le16(&header[2..]);
        if flags & !1 != 0 {
            return Err(Error::Format);
        }
        let n = le32(&header[4..]) as usize;
        if *budget == 0 || n > MAX_VALUE {
            return Err(Error::Limit);
        }
        *budget -= 1;
        Ok(Record {
            kind,
            flags,
            value: self.take(n)?,
        })
    }
}
fn le16(b: &[u8]) -> u16 {
    u16::from_le_bytes([b[0], b[1]])
}
fn le32(b: &[u8]) -> u32 {
    u32::from_le_bytes([b[0], b[1], b[2], b[3]])
}
fn le64(b: &[u8]) -> u64 {
    u64::from(le32(b)) | (u64::from(le32(&b[4..])) << 32)
}
fn known(kind: u16) -> bool {
    kind == 1 || (0x10..=0x14).contains(&kind)
}
fn character(c: u8) -> bool {
    c.is_ascii_alphanumeric() || b"_- .".contains(&c) && c != b' '
}
fn valid_path(value: &[u8]) -> bool {
    !value.is_empty()
        && value.len() <= 128
        && value.split(|c| *c == b'/').all(|component| {
            !component.is_empty()
                && component != b"."
                && component != b".."
                && component.iter().all(|c| character(*c))
        })
}
fn parse_image<'a>(value: &'a [u8], budget: &mut usize) -> Result<Image<'a>, Error> {
    let mut cursor = Cursor { remaining: value };
    let mut result = EMPTY_IMAGE;
    let mut fields: u8 = 0;
    while !cursor.remaining.is_empty() {
        let record = cursor.record(budget)?;
        if !(0x11..=0x14).contains(&record.kind) {
            if known(record.kind) || record.flags & 1 != 0 {
                return Err(Error::Format);
            }
            continue;
        }
        let bit = 1 << (record.kind - 0x11);
        if record.flags != 0 || fields & bit != 0 {
            return Err(Error::Format);
        }
        fields |= bit;
        match record.kind {
            0x11 => {
                if !valid_path(record.value) {
                    return Err(Error::Semantic);
                }
                result.path = record.value;
            }
            0x12 => {
                if record.value.len() != 16 {
                    return Err(Error::Format);
                }
                result.offset = le64(record.value);
                result.size = le64(&record.value[8..]);
            }
            0x13 => {
                if record.value.len() != 32 {
                    return Err(Error::Format);
                }
                result.sha256.copy_from_slice(record.value);
            }
            0x14 => {
                if record.value.len() != 8 {
                    return Err(Error::Format);
                }
                result.version = le64(record.value);
                if result.version == 0 {
                    return Err(Error::Semantic);
                }
            }
            _ => return Err(Error::Format),
        }
    }
    if fields != 15 {
        return Err(Error::Format);
    }
    Ok(result)
}

pub fn parse(input: &[u8]) -> Result<Manifest<'_>, Error> {
    if input.len() > MAX_INPUT {
        return Err(Error::Size);
    }
    if input.len() < 32 {
        return Err(Error::Truncated);
    }
    if &input[..4] != b"FGMF"
        || le16(&input[4..]) != 1
        || le16(&input[6..]) != 0
        || le32(&input[8..]) as usize != input.len()
        || le64(&input[12..]) == 0
        || le64(&input[20..]) == 0
        || le16(&input[30..]) != 0
    {
        return Err(Error::Header);
    }
    let declared = usize::from(le16(&input[28..]));
    if declared == 0 || declared > MAX_IMAGES {
        return Err(Error::Limit);
    }
    let mut result = Manifest {
        device: &[],
        payload_size: le64(&input[12..]),
        sequence: le64(&input[20..]),
        images: [EMPTY_IMAGE; MAX_IMAGES],
        count: 0,
    };
    let mut cursor = Cursor {
        remaining: &input[32..],
    };
    let mut budget = MAX_RECORDS;
    let mut device_seen = false;
    let mut end = 0;
    while !cursor.remaining.is_empty() {
        let record = cursor.record(&mut budget)?;
        match record.kind {
            1 => {
                if record.flags != 0 || device_seen {
                    return Err(Error::Format);
                }
                if record.value.is_empty()
                    || record.value.len() > 64
                    || !record.value.iter().all(|c| character(*c))
                {
                    return Err(Error::Semantic);
                }
                result.device = record.value;
                device_seen = true;
            }
            0x10 => {
                if record.flags != 0 || result.count >= declared {
                    return Err(Error::Format);
                }
                let image = parse_image(record.value, &mut budget)?;
                if image.size == 0
                    || image.offset > result.payload_size
                    || image.size > result.payload_size - image.offset
                    || image.offset < end
                {
                    return Err(Error::Semantic);
                }
                if result
                    .images()
                    .iter()
                    .any(|previous| previous.path == image.path)
                {
                    return Err(Error::Semantic);
                }
                end = image.offset + image.size;
                result.images[result.count] = image;
                result.count += 1;
            }
            _ => {
                if known(record.kind) || record.flags & 1 != 0 {
                    return Err(Error::Format);
                }
            }
        }
    }
    if !device_seen || result.count != declared {
        return Err(Error::Format);
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn size_and_header_guards() {
        assert!(matches!(parse(&[]), Err(Error::Truncated)));
        assert!(matches!(parse(&[0; 32]), Err(Error::Header)));
        assert!(matches!(parse(&vec![0; MAX_INPUT + 1]), Err(Error::Size)));
    }
    #[test]
    fn path_boundaries() {
        for p in [b"a".as_slice(), b"fw/a.bin", b".hidden/boot-1"] {
            assert!(valid_path(p));
        }
        for p in [
            b"".as_slice(),
            b"/a",
            b"a/",
            b"a//b",
            b"a/../b",
            b"./a",
            b"a\\b",
            b"a\0",
        ] {
            assert!(!valid_path(p));
        }
    }
}
