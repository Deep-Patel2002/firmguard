# Requirements and release scope

## Problem

A firmware update coordinator must inspect attacker-controlled metadata before
allocating payload buffers, selecting components, or deciding whether an update
is applicable. A malformed length, wrapped range, duplicate component, or parser
disagreement can undermine that boundary.

FirmGuard is a reusable C11 parser and a separately implemented safe Rust parser
for the explicitly specified FGMF v1 demonstration format. It is not an Apple,
NVIDIA, UEFI, SUIT, or vendor firmware format and has no vendor affiliation.

## Required behavior

| ID | Requirement | Acceptance evidence |
|---|---|---|
| R1 | Parse unaligned, little-endian input without casting input to structs | Sanitizers, unaligned-input unit test |
| R2 | Reject truncation, inconsistent lengths, unsupported headers, malformed TLVs | Independently constructed negative fixtures |
| R3 | Bound input to 65,536 bytes, images to 64, records to 512, TLV values to 4,096 bytes | Limit and boundary tests |
| R4 | Require exactly one device, the declared number of images, and each required image field once | Duplicate/missing/unknown-critical tests |
| R5 | Reject ambiguous paths, duplicate paths, wrapped/out-of-payload ranges, overlap, zero sizes/versions | Semantic fixtures, UINT64 boundary cases |
| R6 | C parser performs no heap allocation, I/O, recursion, or global mutation | Allocation guard, code/link inspection, thread stress |
| R7 | Return no usable partial result on rejection; borrow input only on success | Atomic-result and lifetime contract tests |
| R8 | Independent C and Rust implementations agree on status and full accepted representation | Differential libFuzzer target and deterministic mutation suite |
| R9 | Fail closed on unknown critical fields; skip bounded unknown optional fields | Extension tests at both nesting levels |
| R10 | Provide bounded CLI tools, installable C library, reproducible builds and CI | Clean build, CLI and installation smoke tests |
| R11 | Publish raw benchmark, differential and fuzz evidence with environment and limitations | Versioned verification reports |

## Scope decisions

Two structural levels only: envelope records and image fields. Unknown optional
values are opaque bytes, never recursively parsed. Work is bounded by the input
and record limits; duplicate-name checks perform at most 2,016 pair comparisons.

Production integration must authenticate the exact original bytes, verify actual
payload hashes, bind to a trusted device identity, enforce a durable rollback
floor and install transactionally. Parsing a hash is not verifying a hash.
FirmGuard performs none of those external trust decisions, flashing, extraction,
networking, decompression, or filesystem path resolution.

## Impact claims

Measured results describe this implementation and controlled workloads. Seeded
unsafe examples are labeled demonstrations, not discovered CVEs. No customer,
fleet, incident-reduction, production deployment or upstream adoption is claimed.
