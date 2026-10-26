# Security policy

## Scope

Report security issues in the parser, C ABI, or command-line tools privately to
the maintainer at `dpatel57963@gmail.com`. Include a reproduction and the affected
commit. Do not include firmware or other sensitive customer data.

This repository implements only the demonstration FGMF v1 format. It is not
affiliated with Apple, NVIDIA, UEFI, or a firmware vendor. It does not authenticate
the manifest, verify payload hashes, check device identity or rollback state,
extract files, or install firmware. A successful parse is not authorization to
flash a device. Do not connect it to a production update pipeline without a
separate format review, cryptographic verification, integration tests, and
security assessment.

## Supported versions

The `main` branch is maintained. There is no promise of a response-time SLA.
