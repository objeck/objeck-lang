# Loader fuzz targets

libFuzzer targets for the VM's bytecode loader (`Loader::Load`,
`core/vm/loader.cpp`), the code that reads an `.obe` before anything runs.
An `.obe` is untrusted input to `obr` (see
[docs/SECURITY_MODEL.md](../../../docs/SECURITY_MODEL.md)): whatever its
bytes, the loader must refuse it with a message, never crash, hang or
allocate without bound.

| file | role |
| --- | --- |
| `loader_fuzz.cpp` | the harness; built twice (below) |
| `build.sh` | compiles the VM's sources with clang, AddressSanitizer, UBSan and libFuzzer coverage, links both targets |
| `make_corpus.py` | the seed corpus: every regression program compiled by a deploy tree's `obc` |
| `obe_image.py` | reads a decompressed image; cuts small seeds out of big ones |
| `make_regression_inputs.py` | writes the regression inputs (below) |

The two targets:

- `loader_fuzz` takes a **decompressed image**, the bytes `Loader::Load` reads
  after the file layer has inflated them. This is where the work is done.
- `loader_file_fuzz` takes an **`.obe` file** as `obc` writes it
  (`[uncompressed size:4][zlib]`, or raw zlib), decodes it with
  `Loader::DecodeImage` and loads what decodes.

A refusal is a clean result: `Loader::Load` returns false with its message,
which must not be empty. So is a method signature `StackMethod` cannot parse
(a `std::runtime_error`, which `obr` reports as a load error). Anything else --
a sanitizer report, a leak, a signal, a timeout, an allocation over the limit
-- is a finding. The image target runs the loader's verification pass too
(`Loader::Verify`), so the checks that relate operands to the program are
fuzzed with the rest.

## Running it (WSL2 Ubuntu 24.04)

```
sudo apt install clang libclang-rt-18-dev zlib1g-dev libmbedtls-dev
CXX=clang++ tools/fuzz/loader/build.sh ~/loader-fuzz/build

# seeds: needs a built Linux deploy tree (tools/cicd/local_linux.sh makes one)
python3 tools/fuzz/loader/make_corpus.py core/release/deploy/bin ~/loader-fuzz/seeds

cd ~/loader-fuzz && mkdir -p corpus art
build/loader_fuzz -fork=24 -ignore_crashes=1 -ignore_ooms=1 \
  -ignore_timeouts=1 -timeout=10 -rss_limit_mb=4096 -use_value_profile=1 -max_len=65536 \
  -max_total_time=3600 -artifact_prefix=art/ corpus seeds/slices
build/loader_file_fuzz -fork=4 -ignore_crashes=1 -ignore_ooms=1 \
  -timeout=10 -max_total_time=3600 -artifact_prefix=art/ corpus_file seeds/files

build/loader_fuzz art/crash-...          # replay one finding with its stack
build/loader_fuzz -minimize_crash=1 -runs=100000 art/crash-...
```

`seeds/images` holds the whole decompressed programs (100 KB to 1.5 MB each:
every program carries the standard library), which run at about 100 inputs a
second. `seeds/slices` holds pieces cut from them by `obe_image.py` -- one to
three classes, a few methods, string tables emptied -- at a few thousand inputs
a second per worker, which is where mutation pays.

A refused load frees what it had read, so leak detection stays on (the
commands above turn it off only to keep fork mode's jobs fast. Seeds carry the toolchain version they were compiled with; rebuild them
after a version bump, or every input is refused at the version check.

## Findings

Every finding is fixed and has a case in
`programs/regression/run_vm_flag_tests.py` (section 30), which runs `obr` on a
reduced input and expects the loader's message and exit status 1. The inputs
are in `programs/regression/vm_flag_fixtures/loader/`, written by
`make_regression_inputs.py` from a small compiled program; the test stamps
each with the current toolchain version before it runs, so a version bump does
not turn them into version-mismatch refusals.

| input | before | fix |
| --- | --- | --- |
| any refused or unparsable image | `~StackProgram` freed fields its constructor never set (the fuzzer's first input, the empty one); in `obr`, an unparsable method signature reached that path | `StackProgram` and `StackClass` start every owned field empty |
| class, method or interface count near 2^31 | `new[]` of up to 16 GB before a byte of the elements was read | counts are bounded by the bytes left (`ReadCount`) |
| a repeated class or method id | the slot it should have filled stayed unset, and `SetClasses`/`SetMethods` dereferenced it (SIGSEGV) | arrays zeroed; a repeated id is refused |
| an unknown opcode | release `obr` exited 1 with no message | the message is printed in every build |
| an `.obe` shorter than 4 bytes | `memcpy` read past the buffer and the zlib length underflowed | refused before decoding (found while splitting out `DecodeImage`) |
| any refused image (fuzz build) | everything read so far leaked; libFuzzer's RSS limit fired after a few minutes per job | partial loads free what they read; the targets run with leak detection on and report none |
