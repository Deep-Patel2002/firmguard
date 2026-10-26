# Threat model

## Protected boundary

The parser accepts an untrusted, bounded byte string and either returns a fully
validated borrowed manifest view or a status with a zeroed C output. Attackers
may control every byte, lengths, flags, extension records, paths, and numeric
ranges. The parser must not allocate based on input, overflow range math, recurse
on nested user records, return partially trusted C structures, or diverge
between the two implementations.

## In scope

- Out-of-bounds reads/writes and integer overflow on malformed/truncated input.
- Unbounded work, record/value counts, stack use, and CLI reads.
- Ambiguity from duplicate/missing records, unknown critical extensions, paths,
  overlapping ranges, and C/Rust parser disagreement.
- Reentrancy and thread safety of the C parser.

## Out of scope and caller duties

- Cryptographic signature verification over the exact original manifest bytes.
- Verifying each payload digest against the bytes that will be installed.
- Authentic device identity, authorization, key management, rollback protection,
  update transactions, power-loss recovery, or secure boot.
- Safe file extraction, symlink handling, device flashing, decompression,
  hardware behavior, denial of service outside the strict per-call input bounds,
  compromised compiler/toolchain, and caller-provided invalid C pointers.
- Future compatibility with any commercial or standards-based firmware format.

The image path is a component identifier, not an extraction destination. The
borrowed input must remain allocated and immutable throughout view use. C API
output and input must not overlap. Only serialize results from a successful
parse. These limits are part of the API contract.

## Failure model

Malformed input returns an error and never exposes a partial C manifest.
Resource bounds are constants, not caller-configurable. Independent parsing
reduces common-mode risk but does not replace review, fuzzing, or formal proof.
This project provides no security certification and has not been independently
audited.
