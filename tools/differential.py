#!/usr/bin/env python3
"""Compare independent parser CLIs on a deterministic malformed-input corpus."""
import argparse
import json
import random
import subprocess
from pathlib import Path


def u16(n): return n.to_bytes(2, "little")
def u32(n): return n.to_bytes(4, "little")
def u64(n): return n.to_bytes(8, "little")
def rec(kind, value, flags=0): return u16(kind) + u16(flags) + u32(len(value)) + value


def image(path, offset, size, version=1, digest=bytes(32)):
    return b"".join((rec(0x11, path), rec(0x12, u64(offset) + u64(size)),
                     rec(0x13, digest), rec(0x14, u64(version))))


def manifest(images=None, payload=1 << 20, device=b"demo-device", sequence=1, extras=b""):
    images = images if images is not None else [image(b"firmware/boot.bin", 0, 512)]
    records = rec(1, device) + b"".join(rec(0x10, x) for x in images) + extras
    return b"FGMF" + u16(1) + u16(0) + u32(32 + len(records)) + u64(payload) + u64(sequence) + u16(len(images)) + u16(0) + records


def cases():
    good = manifest()
    optional = rec(0x7ffe, b"x")
    full_size_padding = rec(0x7ffe, b"x" * 4096) * 15 + rec(0x7ffe, b"x" * 3796)
    out = {"valid-single": good,
           "valid-optional-extension": manifest(extras=rec(0x7ffe, b"future")),
           "valid-max-value": manifest(extras=rec(0x7ffe, b"x" * 4096)),
           "valid-max-input": manifest(extras=full_size_padding),
           "record-limit-exceeded": manifest(extras=optional * 507),
           "value-limit-exceeded": manifest(extras=rec(0x7ffe, b"x" * 4097)),
           "unknown-critical": manifest(extras=rec(0x7ffe, b"future", 1)),
           "duplicate-device": good + rec(1, b"second"),
           "truncated-header": good[:17],
           "truncated-record": good[:-1],
           "wrong-total": good[:8] + u32(len(good) - 1) + good[12:],
           "bad-magic": b"NOPE" + good[4:],
           "zero-sequence": good[:20] + u64(0) + good[28:],
           "empty-device": manifest(device=b""),
           "invalid-path": manifest([image(b"../escape", 0, 1)]),
           "zero-image-size": manifest([image(b"a", 0, 0)]),
           "out-of-payload": manifest([image(b"a", 1 << 20, 1)]),
           "overlap": manifest([image(b"a", 0, 10), image(b"b", 9, 1)]),
           "duplicate-path": manifest([image(b"same", 0, 1), image(b"same", 1, 1)]),
           "zero-version": manifest([image(b"a", 0, 1, version=0)]),
           "duplicate-field": manifest([image(b"a", 0, 1) + rec(0x11, b"b")]),
           "unknown-image-field": manifest([image(b"a", 0, 1) + rec(0x7ffe, b"x", 1)]),
           "huge-payload-range": manifest([image(b"a", (1 << 64) - 1, (1 << 64) - 1)], payload=(1 << 64) - 1),
           "many-images": manifest([image(f"partition/{i:02}.bin".encode(), i * 128, 128, i + 1) for i in range(64)], payload=8192)}
    # One deterministic mutation per byte exercises header, nested lengths, and values.
    for i in range(len(good)):
        mutated = bytearray(good)
        mutated[i] ^= 1 << (i % 8)
        out[f"flip-{i:03}"] = bytes(mutated)
    rng = random.Random(0xF17E6A4D)
    for i in range(128):
        mutated = bytearray(good)
        for _ in range(1 + i % 5):
            pos = rng.randrange(len(mutated))
            mutated[pos] ^= 1 << rng.randrange(8)
        out[f"multi-flip-{i:03}"] = bytes(mutated)
    return out


def invoke(binary, data):
    result = subprocess.run([str(binary), "inspect", "-"], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    try:
        parsed = json.loads(result.stdout)
    except (ValueError, UnicodeDecodeError) as error:
        raise RuntimeError(f"{binary} emitted invalid JSON: {error}") from error
    return result.returncode, parsed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--c", required=True)
    parser.add_argument("--rust", required=True)
    parser.add_argument("--output", default="")
    args = parser.parse_args()
    binaries = [Path(args.c).resolve(), Path(args.rust).resolve()]
    for binary in binaries:
        if not binary.is_file():
            raise SystemExit(f"missing executable: {binary}")
    mismatches = []
    accepted = rejected = 0
    for name, data in cases().items():
        c, rust = (invoke(binary, data) for binary in binaries)
        if c != rust:
            mismatches.append({"case": name, "c": c, "rust": rust})
        if c[0] == 0:
            accepted += 1
        else:
            rejected += 1
    report = {"corpus": "generated FGMF v1 boundaries plus deterministic bit flips",
              "cases": accepted + rejected, "accepted": accepted, "rejected": rejected,
              "matching_representations": len(cases()) - len(mismatches), "mismatches": mismatches}
    rendered = json.dumps(report, indent=2) + "\n"
    print(rendered, end="")
    if args.output:
        Path(args.output).write_text(rendered, encoding="utf-8")
    if mismatches:
        raise SystemExit(1)


if __name__ == "__main__": main()
