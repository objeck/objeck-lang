# Handoff: validate ARM64, then merge `perf/jit-safepoint-inline`

> Temporary handoff doc. Delete it when merging (instructions below).
> Authored on a Windows/x64 box that can't build/run ARM64; you're on Apple Silicon.

## What this branch does

Inlines the GC **stop-the-world safepoint poll** in JIT-compiled code. Since the
cooperative-STW work (commits `9ada11ade` AMD64 / `fdf38103e` ARM64), the JIT
emitted an **unconditional `call MemoryManager::SafePoint` at every label**.
`SafePoint`'s fast path is just an acquire-load of `stw_active` + branch, so the
cost is the *call itself* on every loop back-edge (call/ret, frame/shadow setup,
and the optimization barrier that spills loop vars). That regressed label-dense
integer loops badly — fannkuchredux(12) ~35s → ~59–72s — while barely touching
float/call-dominated loops.

This branch inlines the flag test and only calls when a collection is actually
active:
- **AMD64** (`core/vm/arch/jit/amd64/jit_amd_lp64.cpp::EmitJitSafePoint`):
  `mov &stw_active→RAX ; cmp byte [rax],0 ; je skip ; <call> ; skip:`
- **ARM64** (`core/vm/arch/jit/arm64/jit_arm_a64.cpp::EmitJitSafePoint`):
  `ldarb W11,[X10] ; cbz W11,skip ; <call> ; skip:` — `ldarb` (load-acquire)
  mirrors `SafePoint`'s acquire load on the weak ARM64 memory model.

New: `MemoryManager::StwActiveAddr()` in `core/vm/arch/memory.h` exposes the flag
address. Semantics are identical to the old unconditional call (park iff
`stw_active`); a flag flip right after a poll is caught on the next iteration.

## Status

- **AMD64: fully validated.** `jit_gc_stress` PASS 6/6 (incl.
  `OBJECK_JIT_THRESHOLD=1`), `core_thread_gc_stress` PASS 5/5, fannkuch −20%,
  nbody/spectralnorm/mandelbrot/binarytrees output matches the pre-branch binary.
- **ARM64: NOT validated on device.** Reasons it needs real hardware: (1) the
  ARM64 path **hand-emits machine code** (`ldarb` = `0x08DFFC00 | (X10<<5) | X11`,
  `cbz` with a backpatched imm19) — a bad encoding faults; (2) the `ldarb`
  load-acquire is exactly the weak-memory-ordering behavior that **QEMU does not
  reproduce**, so emulation can't validate it. Apple Silicon can.

## Validate on Apple Silicon

```sh
git fetch origin && git checkout perf/jit-safepoint-inline

# Build obr CLEAN so the JIT change is recompiled (macOS VM = Xcode VM-A64 scheme)
xcodebuild -project core/vm/xcode/VM.xcodeproj -scheme VM-A64 -configuration Release \
  -derivedDataPath build clean build
NEW_OBR="$(find build/Build/Products -name obr -type f | head -1)"
echo "built: $NEW_OBR"

# Compile the two GC/JIT stress guards with your existing deploy's obc + libs.
# (bytecode is arch-independent; any obc works)
obc -src programs/regression/jit_gc_stress.obs        -lib gen_collect -dest /tmp/jit_gc_stress.obe
obc -src programs/regression/core_thread_gc_stress.obs -lib gen_collect -dest /tmp/core_thread_gc_stress.obe

# Run with the NEW obr. Point OBJECK_LIB_PATH at your deploy's lib dir if needed.
# Force-JIT everything so the safepoint is emitted in as much code as possible.
for i in 1 2 3;       do OBJECK_JIT_THRESHOLD=1 "$NEW_OBR" /tmp/jit_gc_stress.obe        | tail -1; done
for i in 1 2 3 4 5;   do                        "$NEW_OBR" /tmp/core_thread_gc_stress.obe | tail -1; done
```

### Expected (all PASS)
- `PASS: JIT/GC stress` — deterministic; a wrong total = pointer corruption.
- `PASS: multithreaded GC stop-the-world stress` — a **hang** = STW deadlock
  regression (the exact bug the safepoint prevents).
- No `Illegal instruction` / SIGILL (that = bad `ldarb`/`cbz` encoding), no crash.

If you want more confidence, also run a JIT-heavy benchmark for output parity:
`obc -src programs/tests/clbg/fannkuchredux.obs -opt s3 -dest /tmp/fk.obe && "$NEW_OBR" /tmp/fk.obe 11`
→ must print `Pfannkuchen(11) = 51`.

## If GREEN → merge to master

```sh
git checkout master && git pull
git merge --no-ff perf/jit-safepoint-inline \
  -m "Merge perf/jit-safepoint-inline: inline GC safepoint poll (ARM64 validated on Apple Silicon)"
git rm ARM64_VALIDATION.md && git commit -m "chore: drop ARM64 validation handoff doc"
git push origin master
git push origin --delete perf/jit-safepoint-inline   # optional: clean up the branch
```

## If it FAILS
- **SIGILL / Illegal instruction** at the first JIT'd loop → the `ldarb`
  (`0x08DFFC00 | ((X10 & 0x1F) << 5) | (X11 & 0x1F)`) or the `cbz` imm19
  backpatch (`code[cbz_index] |= ((uint32_t)(imm19 & 0x7FFFF)) << 5`) is wrong.
- **Wrong checksum / crash** → logic error: scratch reg X10/X11 is live across the
  label, or the branch target is off.
- **Hang** → the safepoint is never reached → STW deadlock.

Capture the failing test + symptom and hand it back; the fix is local to
`jit_arm_a64.cpp::EmitJitSafePoint`. Do **not** merge unless both stress tests are
green across all iterations.

---
Full background: see commit `b5950f5f3` and `docs/performance.md` (P0 in the
Speedup Roadmap). The AMD64 result is the reference for expected behavior.
