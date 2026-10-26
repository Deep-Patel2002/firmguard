# FGMF v1 wire format

All integers are unsigned little-endian. No alignment or padding is implied.
The entire manifest is 32..65,536 bytes. Input bytes must remain immutable and
alive while a parsed borrowed view is used.

## Fixed header (32 bytes)

| Offset | Bytes | Field | Validity |
|---|---|---|---|
| 0 | 4 | magic | ASCII `FGMF` |
| 4 | 2 | format version | 1 |
| 6 | 2 | header flags | 0 |
| 8 | 4 | total manifest size | Exactly supplied input size |
| 12 | 8 | payload size | Nonzero, may span the full uint64 range |
| 20 | 8 | update sequence | Nonzero; no trusted rollback state is implied |
| 28 | 2 | declared images | 1..64 |
| 30 | 2 | reserved | 0 |

## Records

Every record has `type:u16`, `flags:u16`, `value_length:u32`, then exactly that
many bytes. The length excludes the eight-byte record header. Values cannot
exceed 4,096 bytes. At most 512 records across both levels are consumed.

Flag bit 0 means critical; all other bits are invalid. Known records require
flags zero. Unknown optional records are skipped; unknown critical records are
rejected. A known type used at the wrong level is a format error. No trailing
bytes, padding records or implicit termination are allowed.

Envelope records, in any order:

| Type | Meaning | Requirements |
|---|---|---|
| 0x0001 | Device identifier | Exactly once, 1..64 ASCII letters/digits/`_-.` |
| 0x0010 | Image | Nested field records, count equals header |

Each image requires exactly one of each field, in any order:

| Type | Meaning | Requirements |
|---|---|---|
| 0x0011 | Component path | 1..128 ASCII bytes; relative slash-separated components |
| 0x0012 | Payload range | 16 bytes: offset:u64, size:u64 |
| 0x0013 | SHA-256 field | Exactly 32 bytes; digest is metadata, not authenticated |
| 0x0014 | Component version | 8 bytes, nonzero |

Path characters are letters, digits, `_`, `-`, `.`, `/`; no leading/trailing or
repeated slash and no `.` or `..` component. Paths are case-sensitive component
identifiers, not safe extraction destinations on every host filesystem.
Windows aliases, symlink resolution and filesystem case folding are out of scope.

Image ranges must appear in ascending, nonoverlapping order. Gaps are allowed.
Each size is nonzero, `offset <= payload_size` and `size <= payload_size-offset`.
No addition is performed before those checks. Component paths must be unique.

## Status precedence

Argument errors first, then input size limit, minimum header size, fixed header
validity, declared-image limit, then TLVs in wire order. For a TLV: require its
header, validate flags, enforce record/value limits, require the value, then
dispatch. Image fields are validated in wire order; required fields and range/
duplicate-path constraints are checked after each complete image. Device must
exist and the final image count must match. Status enum values are in the C API.

## Differential representation

Successful parses serialize to these canonical bytes solely for comparison:
`device_len:u16, device, payload_size:u64, sequence:u64, image_count:u16`, then
for each image `path_len:u16, path, offset:u64, size:u64, version:u64, sha256[32]`.
Ignored optional extensions are omitted; image order is retained. This is not a
signing or signature-verification format: authenticate original wire bytes.
