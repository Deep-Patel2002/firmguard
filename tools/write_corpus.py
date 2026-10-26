#!/usr/bin/env python3
"""Write the deterministic differential cases as stable libFuzzer seeds."""
import argparse
from pathlib import Path

from differential import cases


parser = argparse.ArgumentParser()
parser.add_argument("--directory", required=True)
args = parser.parse_args()
directory = Path(args.directory)
directory.mkdir(parents=True, exist_ok=True)
for name, value in cases().items():
    (directory / name).write_bytes(value)
print(f"wrote {len(cases())} deterministic FGMF seeds to {directory}")
