#!/usr/bin/env python3
"""Reproducible single-thread parser benchmark; reports observations, not SLOs."""
import argparse
import datetime
import json
import platform
import subprocess
import statistics
from pathlib import Path


def u16(n): return n.to_bytes(2, "little")
def u32(n): return n.to_bytes(4, "little")
def u64(n): return n.to_bytes(8, "little")
def rec(kind, value): return u16(kind) + u16(0) + u32(len(value)) + value
def image(path, offset, size, version):
    return b"".join((rec(0x11, path), rec(0x12, u64(offset) + u64(size)),
                     rec(0x13, bytes(32)), rec(0x14, u64(version))))
def manifest(count):
    size = 128
    images = [image(f"firmware/part-{i:02}.bin".encode(), i * size, size, i + 1)
              for i in range(count)]
    body = rec(1, b"benchmark-device") + b"".join(rec(0x10, item) for item in images)
    payload = count * size
    return (b"FGMF" + u16(1) + u16(0) + u32(32 + len(body)) + u64(payload) +
            u64(1) + u16(count) + u16(0) + body)


def sample(binary, data, count, rust):
    args = [str(binary), "bench", "-", str(count), "1"] if rust else [str(binary), "bench", "-", str(count)]
    result = subprocess.run(args, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    value = json.loads(result.stdout)
    expected = count * (1 + int.from_bytes(data[28:30], "little"))
    if value["iterations"] != count or value["checksum"] != expected:
        raise RuntimeError(f"invalid benchmark checksum from {binary}: {value}")
    return value["ns"]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--c", required=True)
    p.add_argument("--rust", required=True)
    p.add_argument("--iterations", type=int, default=50000)
    p.add_argument("--repeats", type=int, default=3)
    p.add_argument("--output", required=True)
    args = p.parse_args()
    if not 100 <= args.iterations <= 100000000 or not 1 <= args.repeats <= 9:
        raise SystemExit("iterations must be 100..100000000 and repeats 1..9")
    measurements = []
    for count in (1, 64):
        data = manifest(count)
        for name, executable, is_rust in (("c11", args.c, False), ("rust", args.rust, True)):
            raw = [sample(executable, data, args.iterations, is_rust) for _ in range(args.repeats)]
            median_ns = statistics.median(raw)
            measurements.append({"parser": name, "images_per_manifest": count,
                                 "manifest_bytes": len(data), "iterations_per_sample": args.iterations,
                                 "repeat_ns": raw, "median_ns": median_ns,
                                 "manifests_per_second": round(args.iterations * 1e9 / median_ns, 2),
                                 "microseconds_per_manifest": round(median_ns / args.iterations / 1000, 4)})
    report = {"recorded_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "purpose": "bounded in-memory parse throughput; excludes payload I/O and trust verification",
              "environment": {"os": platform.platform(), "machine": platform.machine(), "processor": platform.processor(),
                             "python": platform.python_version(), "rustc": subprocess.check_output(["rustc", "--version"], text=True).strip(),
                             "cc": subprocess.check_output(["cc", "--version"], text=True).splitlines()[0]},
              "method": {"iterations": args.iterations, "repeats": args.repeats,
                         "workloads": ["1 image", "64 images"], "warm_process_per_sample": True,
                         "slo_claim": False}, "results": measurements}
    target = Path(args.output)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__": main()
