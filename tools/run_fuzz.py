#!/usr/bin/env python3
"""Run libFuzzer for a fixed budget and persist machine-readable run metrics."""
import argparse
import datetime
import json
import platform
import re
import subprocess
import tempfile
from pathlib import Path

from differential import cases


parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
parser.add_argument("--seconds", type=int, default=30)
parser.add_argument("--output", required=True)
args = parser.parse_args()
if not 1 <= args.seconds <= 3600:
    raise SystemExit("fuzz duration must be 1..3600 seconds")
with tempfile.TemporaryDirectory(prefix="firmguard-fuzz-") as temp:
    corpus = Path(temp)
    for name, value in cases().items():
        (corpus / name).write_bytes(value)
    cmd = [str(Path(args.binary).resolve()), str(corpus), "-max_len=65537",
           f"-max_total_time={args.seconds}", "-print_final_stats=1"]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, check=False, timeout=args.seconds + 90)
log = result.stdout
exec_match = re.findall(r"stat::number_of_executed_units:\s*(\d+)", log)
coverage_match = re.findall(r"stat::average_exec_per_sec:\s*(\d+)", log)
edges_match = re.findall(r"stat::edges_covered:\s*(\d+)", log)
features_match = re.findall(r"stat::feature_count:\s*(\d+)", log)
report = {"recorded_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
          "os": platform.platform(), "architecture": platform.machine(),
          "seed_cases": len(cases()), "requested_seconds": args.seconds,
          "exit_code": result.returncode,
          "executed_units": int(exec_match[-1]) if exec_match else None,
          "average_exec_per_second": int(coverage_match[-1]) if coverage_match else None,
          "edges_covered": int(edges_match[-1]) if edges_match else None,
          "feature_count": int(features_match[-1]) if features_match else None,
          "artifact_count": len(re.findall(r"Test unit written to", log)),
          "libfuzzer_output_tail": "\n".join(log.splitlines()[-16:])}
target = Path(args.output)
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
if result.returncode != 0 or not report["executed_units"]:
    raise SystemExit(result.returncode or 1)
