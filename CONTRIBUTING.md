# Contributing

Keep the wire-format contract in `docs/FORMAT.md` authoritative. Changes to
accepted bytes, status precedence, limits, or canonical representation require
format-version review and fixtures for both parsers.

Before opening a change, run:

```sh
make test sanitize differential format-check
make fuzz
```

Keep fuzz and benchmark reports with their actual toolchain, host, workload, and
run duration. Never label a synthetic parser demonstration as a discovered CVE,
customer incident, deployment, or fleet-wide improvement.
