# Handoff: fuzzing the binaries and protocols (2026-11-16)

Written when work moved from one dev PC to another. Delete this file once the
work below is done or tracked in an issue.

## Goal

Coverage-guided fuzzing of the binaries (`obc`, `obr`, the `.obe` loader) and
the network/protocol parsers (HTTP/1, HTTP/2, QUIC, TLS, WebSocket, DAP, MCP,
JSON-RPC). Nothing has been built or run for this yet; the session only
surveyed what exists.

## What already exists

| area | where | state |
| --- | --- | --- |
| Compiler and VM, generated programs | `tools/fuzz/run_fuzz.py` (see `README.md`) | runs nightly in `.github/workflows/nightly-hardening.yml`; last five nightly runs (2026-11-12 to 11-16) passed |
| Compiler robustness (mutation + hostile inputs) | `programs/regression/run_compiler_robustness.py` | runs in ci-build on every PR; passing on all legs after #1593 |
| `.obe` bytecode loader | `tools/fuzz/loader/` (libFuzzer: `loader_fuzz`, `loader_file_fuzz`) | **by hand only, not in CI**; the README has the build and run commands |
| Protocols | `programs/regression/`: `http_request_hardening.obs`, `mcp_server_malformed_requests.obs`, `dap_robustness_test.py`, `dap_protocol_test.py`, `websocket_test.obs`, `tls_*.obs`, `dtls_*.obs` | hand-written hostile-input tests only; **no coverage-guided fuzzing** |

## The gap, and the plan

1. **Run the loader fuzzers first** (cheap, harness already written):
   `CXX=clang++ tools/fuzz/loader/build.sh ~/loader-fuzz/build`, make seeds with
   `make_corpus.py`, then a bounded `-max_total_time=3600` run of each target.
   Record any artifact. Nothing has been run on the current tree.
2. **Find the protocol parsers.** Not yet located. Start with the HTTP server
   request parsing behind `Web.HTTP.Server` / `net_server`, then `net_h2`
   (nghttp2 callbacks), `net_quic` (ngtcp2), WebSocket framing, and the
   JSON-RPC / DAP / MCP message framing in `json_rpc` and `tools/lsp`.
   Note: `core/lib/web_server` has no native source in-repo (#654), so the
   HTTP server may be Objeck code rather than C++; an Objeck-level driver
   (feed bytes to a loopback socket and check the server survives) may fit
   better than libFuzzer there.
3. **One target per parser**, modelled on `tools/fuzz/loader/loader_fuzz.cpp`
   and `build.sh` (clang, ASan, UBSan, libFuzzer). A refusal with an error is
   a clean result; a sanitizer report, leak, signal, timeout or unbounded
   allocation is a finding. Seed corpora from the existing regression tests'
   hostile inputs.
4. **Wire a short bounded run into nightly** (as the loader already could be)
   only after the targets are quiet by hand.

## Environment notes

- Old PC's WSL Ubuntu already had clang 18 with `libclang_rt.fuzzer`, valgrind,
  and `libmbedtls-dev`, `libnghttp2-dev`, `libngtcp2-dev`, `zlib1g-dev`. On a new
  machine: `sudo apt install clang libclang-rt-18-dev zlib1g-dev libmbedtls-dev`
  plus the nghttp2/ngtcp2 dev packages.
- Build Linux `obc` in a native WSL clone, not under `/mnt/c`: clone the repo to
  `~/obk`, then from `core/compiler` run `make -f make/Makefile.amd64 -j20`
  (the makefile must run from `core/compiler`, not from `make/`). Copy `obc` and
  `core/lib/*.obl` to a scratch `bin/` and `lib/`, and set
  `OBJECK_LIB_PATH=<scratch>/lib/`.
- Never `git stash -u -- core`: it sweeps in the untracked `core/release/deploy-x64t/`.

## Related work already merged

PR #1593 (`31a8bf0`) fixed the compiler's quadratic scanner. Trap worth
knowing: `std::wstring(wchar_t* p, pos, len)` calls `wcslen` on the whole buffer
(string_view overload); write `std::wstring(p + pos, len)`. It cost 5.9 s on a
100,000-statement file on Linux and showed as `__wcslen_avx2` at 60% in
callgrind.

## Open question from the user

The original request ("look for attempts to fuzz the binaries and protocols")
was ambiguous. The user confirmed it means fuzzing them, not detecting attacks
against objeck.org. Server metrics and regional latency for objeck.org were
reported earlier in the session; that work is finished.
