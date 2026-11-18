# Negative, crash and fuzz testing: inventory and results (2026-11-17)

What exists in the repository for testing that bad input is refused cleanly, what was run today and what it found, and what is not covered. Every number below comes from a run on this machine today (Windows x64, one process at a time) unless it says it comes from a document.

Toolchain under test: a Windows x64 deploy tree built from `e071da79f7` (the fix commit of PR #1593, merged as `31a8bf074f`). The main checkout was 13 commits behind `origin/master` and was not rebuilt.

## 1. What exists

| Layer | Input | Where | Runs in CI? |
|---|---|---|---|
| Compiler negative tests | 128 `bad_*.obs` programs that must be rejected, or must trap at run time | `programs/regression/bad_*.obs` | yes (regression) |
| Compiler mutation + stress | broken copies of real programs, hostile files | `programs/regression/run_compiler_robustness.py` | not checked |
| Differential program fuzzer | random valid programs across 5 compile/JIT configs | `tools/fuzz/run_fuzz.py` (nightly-hardening runs it); `emi.py`, which mutates regression tests into variants that must print the same, is run by hand only | `run_fuzz.py`: yes; `emi.py`: no |
| Loader fuzzer (binary `.obe`) | mutated images into `Loader::Load` and `Loader::Verify`, libFuzzer + ASan/UBSan | `tools/fuzz/loader/` | **since 2026-11-18:** 30 minutes nightly (nightly-hardening) and 5 minutes advisory on every CI Build (PRs #1594, #1595, #1596); it was manual in WSL2 when this report was written |
| Loader regression fixtures | 7 reduced findings, 6 fixed bugs | `programs/regression/vm_flag_fixtures/loader/`, `run_vm_flag_tests.py` section 30 | yes |
| Protocol hardening tests | malformed requests, header and URL attacks, TLS and socket failures | `http_request_hardening`, `http_header_validation_test`, `mcp_server_malformed_requests`, `tls_*`, `dtls_socket_test`, `websocket_test`, `udp_failover`, `client_socket_errors` | yes (regression) |
| DAP robustness | malformed debug-adapter traffic | `dap_robustness_test.py` | yes |

## 2. Results today

### 2.1 Compiler mutation and stress: pass

`run_compiler_robustness.py`, four runs (seeds 1 to 4), 4,365 mutated programs in total plus 92 hostile inputs per run.

| Run | Mutation cases | Reported at the fault (OK) | Cascade | Far from fault | Compiled with no error | Stress failures |
|---|---|---|---|---|---|---|
| seed 1, 30 programs | 428 | 381 | 4 | 7 | 36 | 0 |
| seed 2, 150 programs | 1,312 | 1,164 | 22 | 5 | 121 | 0 |
| seed 3, 150 programs | 1,312 | 1,166 | 21 | 8 | 117 | 0 |
| seed 4, 150 programs | 1,313 | 1,161 | 17 | 8 | 127 | 0 |

No crash and no hang in any case. All gates passed. "Compiled with no error" is expected for some mutations (a deleted `;` at a line end is valid).

### 2.2 Regression suite, including the negative tests: 801 passed, 8 skipped, 3 failed

All 128 `bad_*` tests passed. The three failures are network tests:

- `http_stream_to_file`: passes when run alone (20 checks); it failed inside the full run only.
- `http_request_hardening`: 3 of its checks fail. One returns an AdGuard blocking page instead of the server's reply. The other two got `200` where `400` was expected. I believe the cause is AdGuard on this machine sitting in the loopback path, as in earlier sessions, but I did not rerun with AdGuard off, so that is unproven.
- `static_files_test`: not investigated further (listed as an earlier environment failure).

### 2.3 Binary fuzz of `.obe` files: 6 findings in 30,000 mutants

A Python mutation fuzzer (`tools/fuzz/adhoc/obe_mutfuzz.py`) corrupts the decompressed image of a compiled program (bit flips, boundary integers, chunk deletion and duplication, truncation, zeroing, header edits), recompresses it and runs `obr`. Three runs of 10,000 mutants.

| Outcome | Count |
|---|---|
| Refused with a message and exit status 1 or 255 | 28,181 |
| Ran to completion | 1,813 |
| Access violation (`0xC0000005`) | 5 |
| Hang (over 8 s) | 1 |

All six reproduce every time. Where each mutation landed:

| Case | Mutated structure | Outcome |
|---|---|---|
| `m11_5292` | opcodes of `Float:ToString` and `String:New` | crash |
| `m13_3005` | operand of `JMP` in `Hello:Main` | crash |
| `m13_3403` | `$BaseArray:ToString`, `String:Append` opcodes and operands | crash |
| `m13_6260` | `Hello:Main` and `$Char:CopyNative` operands | crash |
| `m13_8297` | operand of `LOAD_CHAR_ARY_ELM` in `String:Get` (one byte, 1 to 2) | crash |
| `m13_6988` | **method record** of `String:New` (counts or sizes, set to `0x7fffffff`) | hang |

Reading: the five crashes are in bytecode that loaded and passed `Loader::Verify`, then misbehaved while running. `docs/SECURITY_MODEL.md` already lists this as open (operand-stack depth and types are not checked). The hang comes from a size field in a method record rather than from an instruction operand; it may be a loader bound worth adding. I did not file anything or triage the crashes any further.

#### Triage (2026-11-18)

Each case was reduced to the single mutation that matters (the other half of a two-part case is harmless), then replayed under an ASan/UBSan build of `obr` from `3042a96eee` (clang, `-O1 -g`, interpreter). All five crashes also reproduce with `--jit=off`, so they are in the interpreter, not the JIT. The hang is the opposite: it needs the JIT.

| Case | The one mutation | Where it fails | Class |
|---|---|---|---|
| `m11_5292` | `String:New` instr 1, `STOR_INT_VAR` becomes `STOR_FUNC_VAR` | `ProcessStoreFunctionVar` pops two words, one was pushed | operand-stack underflow |
| `m13_3005` | `Hello:Main` instr 19, `JMP (6,-1)` becomes `JMP (0,-65536)` | `interpreter.cpp:334`: the jump now pops a condition nobody pushed | operand-stack underflow |
| `m13_8297` | `String:Get` instr 14, `LOAD_CHAR_ARY_ELM` dimension count 1 becomes 2 | `ArrayIndex` (`interpreter.cpp:3825`) pops a second index and reads `array[1]` as a dimension size | operand-stack underflow, and the operand is never compared with the array's own dimension count |
| `m13_3403` | `String:Append` instr 47, `LOAD_INT_VAR` becomes `LOAD_INT_LIT` | `LoadArySize` (`interpreter.cpp:1286`) dereferences the literal as an array | stack type confusion (integer used as pointer) |
| `m13_6260` | `Hello:Main` instr 32, `LOAD_INT_VAR` becomes `LOAD_INT_LIT` | `LoadClsInstIntVar` (`interpreter.cpp:899`) dereferences the literal as an object | stack type confusion |
| `m13_6988` (hang) | `String:New` method record: `params` 1 becomes -255 (and `mem_size` 8 becomes 127) | `ref_map_detail::ParamWords` (`ref_map.h:167`) casts the count to `size_t` and loops `push_back` toward 2^64: memory grows without bound; `--jit=off` runs the program normally | unchecked loader field, JIT only |

Reading: three of the five crashes are one gap, stack underflow, and two are another, stack types; both are the verifier gaps `docs/SECURITY_MODEL.md` lists, so none is a new class of bug. They are reachable only with a hand-crafted `.obe`. The hang is different: it is a plain missing bound in the loader. `LoadMethods` reads `params` and `mem_size` with no range check (only a negative `mem_size` is refused, in `LoadStatements`), so a negative or oversized `params` reaches the JIT. A fix is a check in `LoadMethods` (0 <= params <= twice the declaration count, since a function parameter takes two words) plus a clamp in `ParamWords`, with a fixture beside the other seven in `programs/regression/vm_flag_fixtures/loader/`. The other five need the verifier work, not a point fix.

#### After the fixes (2026-11-18)

PR #1597 bounds a method record's parameter count (the hang). PR #1598 makes `Loader::Verify` run the JIT's reference-map builder over every method with a body and refuse a program whose method pops an empty operand stack, joins two paths at different stack heights, or reads through a non-zero integer literal as an array or object (the five crashes), and makes `ArrayIndex` check an array has as many dimensions as it is indexed with. The same three runs of 10,000 mutants (seeds 11, 12, 13, the same seed program) gave no crash and no hang; each run refused 9,489 to 9,503 mutants with a message and ran 497 to 511 to completion. Before, the same runs refused 28,181 of 30,000, ran 1,813, crashed 5 and hung 1. The six reduced cases are in the replay column of the triage table above. No valid program is refused: 684 regression programs and 613 example, deploy and test programs, each compiled with every library, load clean, and the full regression suite gives the same result with and without the check apart from two loopback tests that fail on this machine with or without it (802 passed, 8 skipped, 2 failed). Loading the 160 KB hello image takes about 3 ms longer (10.4 to 13.3 ms).

What remains unchecked is listed in [SECURITY_MODEL.md](SECURITY_MODEL.md): a bound on how high the operand stack grows, the number of words each trap pops, and `Int` versus reference mixing.

Limits of this run: it is not the libFuzzer harness (no coverage guidance, no sanitizers, so silent heap corruption would not show), it used one seed program, and the build is a release build.

### 2.4 HTTP protocol fuzz of the Objeck web server: pass

A raw-socket fuzzer (`tools/fuzz/adhoc/http_fuzz.py`) against a `Web.HTTP.Server` instance (`tools/fuzz/adhoc/http_fuzz_server.obs`), checking after every 100 cases that the process is alive and answers a plain GET. 2,000 cases: byte flips, insertions, truncation, duplicated lines, oversized and numerous headers, bad `Content-Length` values, odd methods and versions, pipelined requests, malformed chunk sizes, slow sends.

| Status | Count | | Status | Count |
|---|---|---|---|---|
| 400 | 558 | | 414 | 6 |
| 431 | 296 | | 408 | 10 |
| 200 | 292 | | 206 | 46 |
| 505 | 201 | | slow client, closed | 145 |
| 405 | 178 | | silent wait (incomplete request) | 215 |
| 413 | 30 | | closed with no reply | 22 |

The server survived and answered normally throughout, and no case produced a 5xx. Non-standard `Content-Length` values (`+5`, `0x10`, `1e9`, `-1`, `5,5`) were refused with 400; `05`, `5` with spaces were accepted.

Caveats: an earlier 3,000-case pass was discarded. My client counted keep-alive replies as timeouts, and AdGuard's service (`AdguardSvc.exe`) crashed during it (Windows error report at 17:39), which also produced a few 502s from the proxy. The numbers above are from the corrected rerun. Only plain HTTP/1.1 on loopback was tested.

## 3. Gaps

1. **The `.obe` loader fuzzer** was not run automatically when this report was written; it is now (see the inventory). Its 30-minute nightly pass found nothing; the six findings below came from the blind mutation fuzzer, which reaches executed bytecode that the loader-only libFuzzer targets do not.
2. **The verifier is partial.** Five of today's six findings are the documented unchecked areas. Until stack depth and types are checked, a crafted `.obe` is still native-code equivalent.
3. **Library `.obl` files.** Not tested; I did not check whether they go through the same loader path.
4. **Other protocols are untested by any fuzzer:** TLS and HTTP/2/QUIC (`net_h2.obl`, `net_quic.obl`), WebSocket framing, DTLS and UDP, the MCP and JSON-RPC servers beyond one malformed-request test, and the binary LSP and DAP streams. Only HTTP/1.1 plaintext was fuzzed today.
5. **Data parsers** (`json`, `xml`, `csv`, `regex`) have no fuzzers listed.
6. **`run_compiler_robustness.py`** has not been confirmed as part of any CI workflow.
7. **Environment:** AdGuard on this machine interferes with loopback tests and crashed once under fuzz traffic.

## 4. Files added

- `tools/fuzz/adhoc/obe_mutfuzz.py`, `obe_seed_hello.obs`: the `.obe` mutation fuzzer and its seed program. Compile the seed with `obc -src obe_seed_hello.obs -dest hello.obe`, then `python tools/fuzz/adhoc/obe_mutfuzz.py <path to obr> hello.obe 10000 <seed> 4 <output dir>`. It prints a count per outcome (refused, ran, error, crash, hang) and per mutation kind, and keeps the inputs that crashed or hung
- `tools/fuzz/adhoc/http_fuzz.py`, `http_fuzz_server.obs`: the HTTP fuzzer and the server it targets (start the server on a port, then `python tools/fuzz/adhoc/http_fuzz.py <port> <count> <seed> <server pid>`; after every 100 cases the server must still answer a plain GET and its process must be alive)
- this report
