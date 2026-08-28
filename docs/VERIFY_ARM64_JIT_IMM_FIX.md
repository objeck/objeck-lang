# Windows ARM64: unsigned ops are wrong — diagnosis procedure

> **Status: OPEN. The first fix attempt (PR #663) did NOT fix it.** On native
> Windows ARM64 `unsigned_ops` reports 30 passed / 14 failed both before and
> after that change, with byte-identical output. Do not treat #663 as the fix.

Written to be self-contained: assume the reader has no prior context. **This is
a diagnosis task, not a confirmation task.** The goal is to answer one question
with evidence. Do not attempt a fix.

## The symptom

`programs/regression/unsigned_ops.obs` fails 14 of 44 assertions on Windows
ARM64 and passes on linux-arm64, macos-arm64 and windows-x64. Everything
unsigned is wrong: `>>>`, `DivideUnsigned`, `ModUnsigned`, `ToUnsignedString`,
unsigned literals.

They all funnel through one primitive in `core/compiler/lib_src/lang.obs`:

```objeck
function : native : ShiftRightUnsigned(v : Int, count : Int) ~ Int {
  ...
  return ((v >> 1) and 0x7FFFFFFFFFFFFFFF) >> (count - 1);
}
```

`native` forces JIT compilation, and that mask needs all 64 bits. `lang.obl` is
committed, so the bytecode is identical on every platform; only execution
differs.

## What was already tried and did NOT work

**PR #663** widened all 15 ARM64 immediate emitters from `long` to `int64_t`,
on the theory that Windows is LLP64 (`long` is 32-bit) while Linux and macOS
are LP64 (64-bit), so the mask truncated. The signature drift was real — AMD64
had been widened long ago and ARM64 never was — but it is **not this bug**.
Result was unchanged, 30/14, identical output. Keep the change; it is a genuine
latent hazard. Just do not credit it with fixing anything.

Also ruled out already, so do not spend time re-checking:

- **x18**, Windows' reserved platform register: the ARM64 JIT register pool is
  X0–X15 only, it is never allocated.
- **Instruction-cache maintenance**: `jit_arm_a64.cpp` already calls
  `FlushInstructionCache` on Windows.
- **64-bit literal loading**: ARM64's `RegInstr` already uses
  `GetInt64Operand()` for `LOAD_INT_LIT`, not the 32-bit `GetOperand()`.

## Step 1 — the decisive experiment. Do this first.

Nobody has yet established that the JIT is involved at all. One run splits the
problem in half.

Build ARM64 (see "Building" below), then:

```
cd core\release\deploy-arm64\bin
obc -src ..\..\..\..\programs\regression\unsigned_ops.obs -dest u.obe
obr u.obe
set OBJECK_JIT_THRESHOLD=999999999
obr u.obe
```

Report **both** counts.

| result with JIT disabled | what it means |
| --- | --- |
| **44 / 44** | Confirmed a JIT miscompile. The immediates were the wrong suspect within it; look elsewhere in `core/vm/arch/jit/arm64/jit_arm_a64.cpp`. |
| **still 30 / 14** | **Not the JIT.** The bug is in the interpreter or the compiler on this target, and every hypothesis so far has been aimed at the wrong layer. |

This is the same test that cracked the AMD64 float-compare miscompile
(`jit_float_compare_store.obs`). It should have been run first.

## Step 2 — the two library-load failures

`core_opencv` and `onnx_runtime_test` also fail, with:

```
>>> Runtime error loading shared library: ...\libobjk_opencv.dll <<<
```

That line carries no reason, which is useless. **PR #664 fixes it.** To get a
usable message:

```
git pull origin fix/win-dll-load-diagnostic
```

Rebuild, rerun, and report the error code verbatim:

- **126** — the DLL is present but something it imports is missing
- **193** — architecture mismatch
- **2** — the file itself is absent

Do not guess from the filename. Two earlier hypotheses (wrong architecture, a
missing vcpkg OpenCV build) were both wrong: the committed ARM64 OpenCV DLLs
are genuinely ARM64 (checked via their PE machine field) and the ARM64 wrapper
links the modular import libraries that match them.

## Step 3 — a new failure

`odbc_sqlite_test` reports `FAIL: Cannot open connection`. It passed in the
previous ARM64 run, so something moved. Report whether it reproduces and
whether an ODBC SQLite driver is registered on the box.

## Building

```
git checkout master && git pull
vcpkg install mbedtls:arm64-windows nghttp2:arm64-windows
cd core\release
deploy_windows.cmd arm64
cd ..\..\programs\regression
run_regression.cmd arm64
```

Two traps that have already cost time here:

- Run `vcpkg install` from a directory containing no `vcpkg.json`, or vcpkg
  switches to manifest mode, rejects the package arguments and installs into a
  `vcpkg_installed\` tree nothing in this repo reads. The build probes
  `%VCPKG_ROOT%`, `C:\vcpkg`, then `%USERPROFILE%\vcpkg`; note Visual Studio
  ships its own vcpkg and points `VCPKG_ROOT` at it, usually empty.
- Run `deploy_windows.cmd` **only** from a Visual Studio Developer Command
  Prompt. It wipes the deploy tree *before* it checks the environment, so a
  plain shell destroys the tree and then aborts.

Baseline for comparison — native ARM64, most recent run: **191 passed,
4 skipped, 4 failed** (`unsigned_ops`, `core_opencv`, `onnx_runtime_test`,
`odbc_sqlite_test`).

## Do not

- Do not attempt a fix. Report evidence; the analysis happens elsewhere.
- Do not merge, push, or open PRs.
- Do not run `deploy_windows.cmd` outside a VS Developer Command Prompt.
