# CUDA execution provider for ONNX — design

> **Status: PLAN, not implemented.** Written for review before any code, on the
> pattern that made the HTTP/3-on-Windows work cheap: three reviewers rejected
> the expensive route before it cost anything.

> **CORRECTION (same day, before review).** Two premises below were wrong and
> are struck through in place. Verify claims against the tree, not against
> this doc:
>
> 1. **`core/lib/onnx/eq/cuda` ALREADY EXISTS** — `onnx_cuda.cpp` plus a
>    `build_linux.sh`. What is missing is only a **Windows project**; the
>    variant itself does not need creating. CUDA is therefore buildable on
>    Linux today and the Windows gap is far smaller than stated below.
> 2. **KV cache is NOT globally disabled on DML.** `common.h:1836` computes
>    `use_kv_cache = has_kv && present_kv_consistent()` for the text path —
>    cache is ON. The hardcoded `use_kv_cache = false` with the GQA comment is
>    at `common.h:2413`, inside the **Phi-3-V vision decoder** path only.
>    So the "capability the DML path cannot have" argument applies to
>    multimodal decode, not to Phi-3 text inference.
>
> Net effect: the case for CUDA is **weaker and cheaper** than written — less
> to build, and a narrower correctness argument. Re-scope before acting.

> **CORRECTION 2 — how the EP is actually chosen, and why CUDA does not exist.**
>
> There are TWO selection mechanisms and they do not agree:
>
> - `core/lib/onnx/eq/onnx.cpp` uses the compile-time
>   `#if ONNX_EP_DML / #elif ONNX_EP_CUDA / ...` chain. `vs/vs.vcxproj` defines
>   all four, so DML wins by ordering.
> - The **per-EP variant files** — `eq/dml/onnx_dml.cpp`, `eq/cuda/onnx_cuda.cpp`,
>   `eq/qnn`, `eq/vitis` — do **not** use those macros at all. Each hardcodes
>   its provider with a literal `AppendExecutionProvider("...")`.
>
> **The shipped DLL comes from the variant, not the macro chain**:
> `deploy_windows.cmd` copies `x64\Release-DML\libobjk_onnx.dll`. So the EP is
> chosen by *which source file you build*.
>
> And the decisive part: **`eq/cuda/onnx_cuda.cpp:64` appends `"DML"`**, under a
> comment reading *"Create session options with DML execution provider"*. It is
> an unconverted copy of the DML variant. **CUDA is not implemented anywhere in
> the tree** — correction 1 above was itself too generous.
>
> Consequence for scope: shipping CUDA/cuDNN DLLs now would ship a dependency
> for a provider that does not exist — the same "documented but dead" pattern
> as Http3Client-on-Windows. **Implement and verify the provider first; ship
> its DLLs only once `OnnxRuntime->GetProviders()` actually reports CUDA.**

## Why

GPU inference already works — via **DirectML**, verified at runtime:

```
available execution providers (2):
  DmlExecutionProvider
  CPUExecutionProvider
```

Two reasons to want CUDA anyway on NVIDIA hardware (this box has an RTX 5080):

1. **Speed.** ORT's CUDA EP is materially faster than DML on NVIDIA parts.
2. **A correctness workaround disappears.** `core/lib/onnx/eq/common.h:2413`:
   ```cpp
   bool use_kv_cache = false; // DML GQA decode bug produces garbage with KV cache; use full recompute
   ```
   Phi-3 decode currently recomputes the full context per token because KV
   caching is broken under DML's GroupQueryAttention. That is a large,
   ongoing cost, and it is **DML-specific**.

Point 2 is arguably the stronger one: it is not a speed preference, it is a
capability the DML path cannot currently have.

## The blocker: provider choice is compile-time and exclusive

`core/lib/onnx/eq/onnx.cpp`:

```cpp
#if   defined(ONNX_EP_DML)   ... AppendExecutionProvider("DML",  ...)
#elif defined(ONNX_EP_CUDA)  ... AppendExecutionProvider("CUDA", ...)
#elif defined(ONNX_EP_QNN)   ...
#elif defined(ONNX_EP_VITIS) ...
#endif
```

`core/lib/onnx/vs/vs.vcxproj` defines **all four**, so DML always wins by
ordering. The shipped `libobjk_onnx.dll` is built from the per-EP variant tree:

```
core/lib/onnx/eq/dml/     onnx_dml.cpp   + onnx_dml.sln + packages/   <- shipped
core/lib/onnx/eq/qnn/
core/lib/onnx/eq/vitis/
core/lib/onnx/eq/cuda/    <- DOES NOT EXIST
```

`deploy_windows.cmd` builds `onnx.sln` and copies
`x64\Release-DML\libobjk_onnx.dll`. So **DML and CUDA cannot coexist in one
binary as written** — this is the central design question, not an
implementation detail.

## What is missing

| piece | state |
| --- | --- |
| `ONNX_EP_CUDA` source path | exists, unused |
| `eq/cuda` project variant | **absent** — must mirror `eq/dml` |
| `Microsoft.ML.OnnxRuntime.Gpu` nuget | **not vendored** (only `.DirectML` is) |
| CUDA Toolkit + cuDNN runtime DLLs | **absent** (~2-3 GB redistributables) |
| Deploy wiring | absent |
| Provider reporting | `OnnxRuntime->GetProviders()` already exists and would show it |

## Options

### A — a second DLL, selected at load time

Build `libobjk_onnx_cuda.dll` alongside the DML one; the Objeck side probes
for CUDA and falls back to DML, then CPU.

*For:* no change to the existing DML build, so the working path cannot
regress. Matches the existing per-EP variant structure.
*Against:* two DLLs to build, deploy and version; the probe logic is new
surface; doubles the ONNX build time in CI.

### B — one DLL, runtime EP selection

Remove the `#if/#elif` exclusivity, link an ORT that carries both providers,
and choose at session creation from config (`"ep"->"cuda"`).

*For:* one artifact, and `onnx.obs:2119` already documents an `ep` config key,
so the API surface exists.
*Against:* requires an ORT build carrying both DML and CUDA — the stock nuget
packages are one or the other. Likely means building ORT from source, which is
a far bigger commitment than the whole rest of this plan.

### C — CUDA-only build for NVIDIA users, DML remains default

Ship DML as today; provide the CUDA variant as an opt-in build documented for
people with NVIDIA hardware.

*For:* smallest change; no runtime probing; no redistributable bloat in the
default install.
*Against:* users must rebuild to get it, so in practice almost nobody will.

**Recommendation: A**, with C as the fallback if the redistributable size
proves unacceptable. B should be rejected unless a reviewer can show a stock
ORT package carrying both EPs — its cost is dominated by building ORT from
source.

## Open questions for review

1. **Does ORT's CUDA EP actually avoid the GQA/KV-cache bug?** This is the
   load-bearing question. If CUDA has an equivalent defect, the main
   justification collapses and only the speed argument remains. **Verify
   before building anything** — a single Phi-3 decode with `use_kv_cache = true`
   under CUDA answers it.
2. **Vendor CUDA/cuDNN, or require the user to install the CUDA Toolkit?**
   Vendoring adds gigabytes to the installer; requiring it adds a setup step
   and a support burden. What do the Windows users actually have?
3. **Does CI build this?** The ONNX build is already the slowest part of the
   Windows leg. A second EP variant may need to be off by default in CI and
   built only on release.
4. **Which GPU wins when there are two?** This box has an RTX 5080 *and* an
   AMD 890M. `device_id = 0` is hardcoded (`onnx.cpp:76`); on a hybrid laptop
   device 0 is not reliably the discrete GPU.
5. **Version coupling.** The DirectML downgrade (#625) was caused by shipping
   a pinned redistributable older than the OS. CUDA has the same hazard shape
   with a much larger surface — the plan must say explicitly how cuDNN/CUDA
   versions are pinned and checked.

## What must not happen

- The working DML path regressing. It is the only GPU path most users have.
- Shipping a pinned CUDA/cuDNN older than what the user's driver provides —
  see #625.
- Claiming GPU support that silently falls back to CPU. `runtime.feature.*`
  and `OnnxRuntime->GetProviders()` exist precisely so this is detectable;
  the CUDA path must be visible in `GetProviders()` or it does not count.
- Enabling `use_kv_cache` for CUDA without proving decode correctness against
  a known-good CPU transcript.

## Phasing

1. **Answer question 1 first**, with the smallest possible experiment: a stock
   ORT GPU package, one Phi-3 decode, KV cache on, compared against the CPU
   transcript. If CUDA has the same GQA bug, stop and reconsider.
2. Create `eq/cuda` mirroring `eq/dml`; build `libobjk_onnx_cuda.dll` locally.
3. Objeck-side probe with an explicit, reportable fallback chain.
4. Deploy wiring, with the version-pinning rule from question 5.
5. CI decision per question 3.
