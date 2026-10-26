# Verification and performance evidence

Regenerate the evidence on your own host using `make test sanitize differential
fuzz bench`. Keep the compiler, Rust toolchain, OS/kernel, CPU, manifest shape,
iteration count, repeat count, and fuzz duration with results. A local x86_64
WSL2 run with GCC 13.3 and Rust 1.75 measured median rates of 3,556,668 C /
3,150,576 Rust parses/s for a 172-byte one-image manifest and 90,709 C /
101,127 Rust parses/s for a 7,480-byte 64-image manifest (50,000 iterations,
three repeats). A 30-second ASan/UBSan libFuzzer run executed 3,535,719
inputs without a crash. CI artifacts include raw Linux benchmark and fuzz reports.
These numbers are observations from the
recorded environment, not guarantees for firmware payload I/O or a real update
fleet.

`make test` includes an allocation-wrapped C parse call plus concurrent parsing
and zero-on-error assertions. The source review establishes that the C parsing
path performs no I/O or recursion. The deterministic corpus independently
builds well-formed, malformed, boundary, and bit-mutated inputs, then compares
return status and the complete JSON representation emitted by C and Rust.

The fuzzer and benchmark use synthetic FGMF inputs. They establish robustness
and speed for this parser implementation; they do not establish prevented
incidents, adoption, or compatibility with another firmware format.

Update this document with the raw run date, tool versions, host, corpus, exact
command, and output path whenever publishing new measured values.
