# Handoff — 2026-08-30 (from the 7950X3D Windows box)

State of play for picking this up on another machine. Master is current at
`26db5ddd6` (merge of #682); everything substantive is already on GitHub.

## Just landed
- **#682 merged** — `Web.Server` reimplemented in pure Objeck. Coverage 0/13 → 13/13,
  suite 202 passed / 3 skipped / 0 failed (205 total). All 16 CI checks were green.
- **#681 scoped** — the `WebServer->Serve` + normal-`Main`-exit teardown segfault does
  **NOT reproduce on Linux** (8/8 clean, request verified `200 OK`; Ubuntu 24.04 WSL2,
  gcc 13.3, master @ 26db5dd). Windows x64 only; macOS arm64 still unchecked.
  Details in the issue comment. Fix hunt starts in the Windows shutdown path
  (Winsock/`WSACleanup` or DLL unload racing the thread parked in `accept()`).

## Outstanding
| Item | Status |
|---|---|
| #681 fix | Windows-only teardown crash; deterministic repro in the issue. Also covers the linker dropping `TCPSocket` (allocated by class id from `SockTcpAccept`, no static ref). |
| Release | The one substantial item. 179+ commits past v2026.8.3; `version.h` on master already says 2026.8.4. Use the `release` skill. **Reminder:** version bumps need the lang.obl rebuild (WSL2 `sys_obc`) or windows-x64 CI fails. |
| #669 | `GetLocalAddress` proper fix (getsockname trap + accessor) deferred post-release — touches `lang.obs`/bootstrap. |
| #659 | Deferred design debt: diags analysis is serialized behind one global lock because `TreeFactory`/`TypeFactory` are singletons; real fix is per-`ParsedProgram` factories (compiler refactor). |
| Coverity | 3 CIDs to mark in the web UI — 2 false positives, 1 third-party. |

## Files on this branch
Scratch/helper files from the dev box, pushed for continuity — not intended to merge:
- `bench_test.bat`, `build_vm.bat`, `diag_test.bat`, `run_build.bat`,
  `run_regression.bat`, `test_compile.bat` — Windows build/test helpers.
- `programs/regression/run_linux.sh` — WSL2 regression runner (hardcoded `/mnt/c` paths).
- `programs/regression/closure_min.obs`, `programs/regression/jit_trap_ary_len.obs` —
  minimal repros from fixed bugs (fixes landed; files kept for reference, not in TESTS.md).
- `ONBOARDING.md` — the 2026-08-05 LSP cross-machine install guide.

Not pushed (build artifacts on the dev box only): `core/release/deploy-arm64/` (2.1G),
SDL/matrix/onnx Release dirs, `docs/web/api/` (generated), `.vcxproj.user` files,
copied `Makefile`s, `docker/deploy/`.
