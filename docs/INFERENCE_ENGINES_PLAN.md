# Inference backends: one Engine over ONNX and a server — plan

> Was "TF Lite beside ONNX" until 2026-10-07, when the TF Lite half was
> dropped: it has no published Windows ARM64 build, and a correctly vendored
> ONNX Runtime covers strictly more. See open question 1.

Status: **P1 is built** (2026-10-07) -- `API.Onnx.Session`, `Tensor`,
`TensorSpec`, `ModelInfo` and `RunResult` over two new native entry points,
`onnx_run` and `onnx_model_info`. P2 and P5 are still proposed; **P3 (TF Lite) is dropped** -- see open
question 1 -- and replaced by P3', vendoring ONNX Runtime assets that already
exist. The HTTP client
(`core/compiler/lib_src/inference.obs`, written 2026-10-03, 38 assertions) is
written but **not registered as a library**, so nothing can `use` it yet.

## Why

No single inference backend covers the five platforms Objeck ships on. Measured
on this tree, not assumed:

> **This table was wrong, and the correction removes the reason for P3.**
> Checked 2026-10-07; the corrected version is below it.

| | linux-x64 | linux-arm64 | win-x64 | win-arm64 | macos-arm64 | GPU |
|---|---|---|---|---|---|---|
| ONNX, **as vendored today** | yes | **no** | yes | yes | yes | **no** |
| ONNX, **vendoring the assets that already exist** | yes | **yes** | yes | yes | yes | **yes** |
| TF Lite, in-process | yes | yes | yes | **no** | yes | no |
| HTTP to a server | yes | yes | yes | yes | yes | yes |

Row 2 dominates row 3 on every column. TF Lite adds nothing it can reach that a
correctly vendored ONNX Runtime cannot, and it would cost a second runtime and a
five-platform build matrix to do it.

Two facts behind the first ONNX row, both from
`core/release/deploy_posix.sh:130-144`:

- The vendored runtime is a **CPU-only build**. It "reports only
  `CPUExecutionProvider` despite living under `eq/cuda/lib`", and building with
  `ONNX_EP_CUDA` made every session creation fail.
- Only an x86-64 runtime is vendored, so **linux-arm64 ships no
  `libobjk_onnx`**. `verify_native_libs.sh` declares onnx optional there. This
  was rediscovered on 2026-10-03 when `onnx_failed_session.obs` failed that leg.

**Neither is a platform limitation. Both are vendoring omissions**, and that is
the finding that changes this plan. ONNX Runtime publishes, for the very version
already pinned here (1.19.0, `ORT_API_VERSION 19`):

| asset | closes |
|---|---|
| `onnxruntime-linux-aarch64-1.19.0.tgz` | the linux-arm64 hole — **no version bump needed** |
| `onnxruntime-linux-x64-gpu-1.19.0.tgz` | "no GPU inference path at all" |

`deploy_posix.sh` is already written for this. It sets `ORT_ARCH=arm64` and
guards the whole ONNX step on `[ -d cuda/lib/$ORT_ARCH/lib ]`; the directory is
simply empty. Populating it is the entire change on the build side.

(Upstream is at 1.30.0 as of 2026-09-10 and ships `linux-aarch64`,
`win-arm64`, `win-arm64x`, `osx-arm64` and `gpu_cuda12`/`gpu_cuda13`. Bumping is
a separate, larger piece of work — eleven minor versions — and is **not**
required to close either gap.)

So the argument for TF Lite was that it is the only *in-process* option covering
all five platforms. That argument does not survive the next section.

## The trap: do not mirror the ONNX API

`API.Onnx` is not a runtime API. Its eleven native entry points are
`onnx_new_session`, `onnx_close_session`, `onnx_get_provider_names` and **eight
per-family inference functions** — `onnx_yolo_image_inf`,
`onnx_resnet_image_inf`, `onnx_phi3_text_inf`, `onnx_face_detect_inf` and so on.
`common.h` is 3,113 lines of per-family preprocessing and decoding: letterboxing,
NMS, SCRFD decode, Phi-3 sampling, segmentation summary.

Mirroring that shape for TF Lite would mean writing `TfLiteYoloSession`,
`TfLiteResNetSession` and the rest — duplicating letterboxing and NMS for a
second runtime. Letterboxing is a property of **YOLO**, not of ONNX Runtime.

It is also why #1028 and #1030 each had to be fixed eight times: with no
chokepoint, a defect in the pattern is a defect in every copy. A second backend
built the same way doubles that.

## What is missing

1. ~~**A generic `run` on ONNX.**~~ **Built.** `onnx_run` takes named tensors and
   returns the model's outputs as named tensors. `onnx_model_info` came with it
   and is not optional: you cannot build inputs for a multi-input model without
   knowing its names and shapes. The flat marshalling — ranks, concatenated
   dimensions, concatenated elements — stays inside `onnx.obs`;
   `Session->Run` takes and returns `Vector<Tensor>`.
2. ~~**A backend-neutral tensor.**~~ **Done in P2.** There were two
   near-identical ones; `API.Models.Tensor` is the one. Merging them was not a
   compromise between the two: see P2 below for the limit that moved off the
   constructor and onto the conversion that actually needs it.
3. **An interface both backends implement**, so calling code does not choose a
   backend at every call site.

## Design

```objeck
interface Engine {
   method : virtual : public : Run(inputs : Vector<Tensor>) ~ Vector<Tensor>;
   method : virtual : public : IsOpen() ~ Bool;
   method : virtual : public : Close() ~ Nil;
}

class OnnxEngine   implements Engine   # in-process; onnx_run shipped in P1
class HttpEngine   implements Engine   # a server
# TfLiteEngine was here. P3 is dropped -- see open question 1.
```

Calling code names a backend once:

```objeck
engine := OnnxEngine->New("model.onnx");
if(<>engine->IsOpen()) {
   EndPoint->GetLastError()->PrintLine();
   return;
};

outputs := engine->Run(inputs);
engine->Close();
```

Swapping that for `HttpEngine->New("http://gpu-box:8000", "model")` changes
nothing else. That is the point: the backend is a deployment decision, not an
API -- and it is why dropping a backend costs this design nothing.

**`API.Onnx`'s existing per-family classes stay.** They are a convenience layer
and people use them. Nothing here removes or changes them.

## The larger payoff

Once a model runs through `Engine`, the per-family logic can move **above** it.
`YoloDecoder` taking an `Engine` would mean letterboxing and NMS are written
once and work with ONNX, TF Lite or a remote server — instead of once per
runtime, in C++, per family.

That is a bigger prize than TF Lite itself, and it only becomes possible after
step 1. It is listed as a later phase because it is a refactor of working code
and should not gate a new backend.

## Phasing

**P1 — generic ONNX run. DONE, 2026-10-07** (#1047). ~630 lines in `common.h`,
~830 in `onnx.obs`. No new dependency, no new build leg, nothing added to
packaging. It delivers value alone: TF, PyTorch and scikit-learn models all
reach it via `tf2onnx`, `torch.onnx` and `skl2onnx`, on the four platforms ONNX
works.

Verified against real models rather than argued for, in
`programs/frameworks/opencv_onnx` (neither program can live in
`programs/regression`, which ships no model):

| model | what it establishes |
|---|---|
| `resnet34` | the generic path and `ResNetSession` agree **exactly** — same class, same label, confidence gap `0.000000` (`ab_resnet.obs`) |
| `phi3` | 67 inputs: 3 INT64 + 64 **empty** FP16 cache tensors, FP16 logits read back as Float. "The capital of France is" → "Paris" (`ab_phi3.obs`) |
| `openpose` | four outputs, all returned; the per-family path exposes one |
| `resnet34`, `phi3` | symbolic dimensions reported as -1 rather than guessed |

Three things the real models taught that the design did not:

- an `extent < 1` validation refused `[1,32,0,96]`, which is how **every** SLM
  passes its key/value cache on the first forward pass. Zero elements is a real
  tensor. Found by probing Phi-3, not by reasoning about it.
- `Ort::Value::CreateTensor` takes, in the ORT header's own words, "a user
  supplied buffer" — it wraps the pointer rather than copying, so every input
  buffer must outlive the `Run`. The existing FP16 helper got this wrong and had
  been returning a tensor over a freed local (#1045); the generic path sizes its
  buffers up front.
- all seven per-family `Close` methods left their handle in place after the
  native delete, so `IsOpen()` lied and a second `Close` was a double free
  (#1046). Writing one correct `Close` is what exposed it.

Caveats, stated rather than left to be discovered:

- element types carried: FP32, FP16, FP64, INT8/16/32/64, UINT8/16/32/64, BOOL.
  Anything else is refused **by name**. Tensors only — a sequence or map input is
  refused, not silently mishandled.
- every tensor crosses as `Float[]`, exact for integers up to 2^53. An `Int[]`
  tensor outside that range is refused at construction rather than rounded.
- `Run` prints no timing line, unlike the eight per-family functions. A decoder
  calls it once per generated token.

**P2 — `Engine` interface, `OnnxEngine`, `HttpEngine`. DONE, 2026-10-09.**
`API.Models` (`models.obl`) holds `Tensor`, `TensorSpec`, `EndPoint` and the
`Engine` interface; `API.Onnx.OnnxEngine` and `API.Inference.HttpEngine`
implement it. No native work, as expected.

The bundle is **`API.Models`, not `API.Engine`** — that would have produced
`API.Engine.Engine`, which reads badly in every doc and error message.

`ModelMetadata` stayed in `API.Inference` and `ParseResponse` became
`API.Inference.Wire`: finding an error inside a JSON body is HTTP's business, and
a backend-neutral bundle should not have to know what JSON is. `ModelInfo` and
`RunResult` stayed in `API.Onnx` because the native layer creates them by name.

**The Tensor duplication is gone, and merging improved it.** `API.Onnx.Tensor`
converted `Int[]` to `Float[]` *at construction* and refused past 2^53 there,
which imposed the native crossing's limit on every backend — including an HTTP
server that receives integers as JSON integers with no range limit at all.
`API.Models.Tensor` keeps integers as integers; `AsFloats()` converts, and the
refusal lives there.

**It is a breaking change, made before the tag on purpose.** Library dependencies
are not transitive in Objeck (`-lib onnx` alone fails with "Add it with
`-lib json`"), so every ONNX program gains `-lib models` and `use API.Models`,
and `Session->Run` returns `Vector<API.Models.Tensor>`. v2026.10.0 was unreleased,
so nothing was written against P1's shape yet; after the tag this would have
broken every such program.

Verified: `resnet34` still agrees exactly with `ResNetSession` (gap `0.000000`)
and Phi-3 still answers "Paris", so the refactor changed no numbers; and one
`Vector<API.Models.Engine>` holds an `OnnxEngine` and an `HttpEngine` with the
same loop driving both.

### What the HTTP route is tested against

With P3 dropped, `API.Inference` is the only route to a TensorFlow model, so
what covers it matters more than it would have.

`programs/regression/inference_client_test.obs` (50 assertions) asserts the
request body through `Client->DescribeRequest`. It needs no server, so it runs
in every regression pass on every platform — and it cannot establish that a
server *accepts* that body, or that a real response is parsed back into the
right tensors.

`tools/cicd/test_inference_round_trip.py` (48 assertions) is the other half. It
starts `tools/cicd/mock_inference_server.py` — a strict mock of KServe v2 and
TF Serving v1 that refuses anything malformed with a 400 naming the reason — and
drives `programs/tests/inference_round_trip.obs` against it over localhost. So
each assertion went out over HTTP, was validated against the protocol, and came
back through the library's own parser. The suite also asserts that the server
rejected **nothing**, because a request the mock refuses is one a conforming
server would refuse. It is gating on every platform: it reaches nothing but
127.0.0.1, so there is no upstream to go down.

Its value is measurable rather than assumed. Making the v2 response reader take
only the first output tensor leaves the `DescribeRequest` test **passing** and
fails the round-trip suite — a response-path regression the offline test cannot
see by construction. (A request-path mutation, sending v2 data nested, is caught
by both.)

**What is still not established.** The mock is a careful, strict reading of the
published protocols, not Google's binary: a real server that rejected something
the mock accepts would not be caught. The driver takes a base URL precisely so it
can be aimed at the real thing —
`obr inference_round_trip.obe http://localhost:8501 v1` against a
`tensorflow/serving` container — and that step is container- and
network-dependent, so by this repo's convention it would be non-gating and is
not wired in. No GPU path is covered either; that needs a GPU and a served model.

**P3 — TF Lite. DROPPED, 2026-10-07.** See open question 1: it has no published
Windows ARM64 build and upstream's own Windows ARM64 support is an unmerged,
stale PR. A correctly vendored ONNX Runtime covers strictly more.

Replaced by the work that actually closes the gaps, which is smaller than P3 was
going to be and needs no new dependency:

**P3' — vendor the ONNX Runtime assets that already exist.** Drop
`onnxruntime-linux-aarch64-1.19.0.tgz` into `cuda/lib/arm64/lib`, which
`deploy_posix.sh` already looks for, and linux-arm64 stops shipping without
`libobjk_onnx`; `verify_native_libs.sh` can then stop declaring onnx optional
there, and `onnx_failed_session.obs` / `onnx_generic_run.obs` can drop their
`LibraryPresent()` skips on that leg. Separately,
`onnxruntime-linux-x64-gpu-1.19.0.tgz` gives Objeck the GPU path it has never
had, which is also step 2 of `ONNX_CUDA_PLAN` finally having something true to
validate against. Watch the packaging budget: the cache is at its ceiling.

**P4 — registration and docs.** A new library must be added to
`build_libs.sh`, `code_doc64.in`, `gen_json.cmd`, `gen_json.sh` and
`ci-build.yml`, with `check_doc_lists.py` guarding the lists. Five places, each
of which has gone stale before.

**P5 — per-family decoders over `Engine`**, if the payoff above is wanted.

FastSAM is the intended first consumer. `FASTSAM_DESIGN.md` originally specified a
ninth per-family native entry point; the 2026-10-07 decision defers it behind P1
and P2 and rebuilds it as a decoder, which makes it the test of whether this phase
actually pays. Its model knowledge -- one forward pass yields every mask, a prompt
only selects among them -- is unaffected and still stands.

## What must not happen

- **Do not duplicate `common.h` per backend.** If `TfLiteEngine` grows a
  `yolo_preprocess`, the design has failed.
- **Do not vendor `libtensorflow`.** 300 MB+ per platform against an Actions
  cache already at its 10 GB ceiling, with `core/lib/onnx/packages` alone at
  609 MB, and weak Windows ARM64 prebuilts. TF models reach Objeck through
  conversion or through a server.
- **Do not make `Engine` asynchronous or streaming** in this work. Both are
  real wants and both are separate designs.
- **Do not break `API.Onnx`.** Additive only.

## Overlap with ONNX_CUDA_PLAN

Both touch `onnx.cpp`'s session creation, so they are one piece of work rather
than two. Re-checked 2026-10-07: most of that plan is **already implemented**.
`ep` is already a provider *name* rather than a cpu/dml switch, and a provider the
build cannot supply is already refused loudly, naming what it does provide.

P1 did not touch any of it: `onnx_run` and `onnx_model_info` take a session that
is already open, so this still belongs to session creation. What remains:

- its step 2, which it calls the crux: validate against
  `Ort::GetAvailableProviders()` at runtime instead of the compile-time
  `compiled_ep` constant. The constant cannot know that a DirectML-labelled
  runtime was linked without DirectML -- which is exactly how the CPU-only
  runtime went unnoticed.
- its step 4: explicit opt-in fallback (`ep_fallback`), never implicit.
- the one-source unification, which removes the per-EP variant files whose only
  real difference is a hardcoded provider string -- the copy-paste that had
  `onnx_cuda.cpp` appending `"DML"`.

## Open questions

1. ~~**TF Lite's prebuilt platform coverage is UNVERIFIED.**~~ **ANSWERED
   2026-10-07: it does not ship for Windows ARM64.** This was the load-bearing
   assumption, and the rule this document set for itself was "if Windows ARM64 is
   not covered, TF Lite loses its one advantage over the generic ONNX path and the
   plan should stop at P2." It is not covered, so **the plan stops at P2.**

   Five channels, all agreeing:

   - **LiteRT** (TF Lite's successor, `google-ai-edge/LiteRT`) publishes prebuilt
     desktop backends for Linux x64/arm64, macOS arm64 and **Windows x64 only**.
   - **`tphakala/tflite_c`**, the most-used community redistributor, v2.17.1:
     Linux amd64 + arm64, macOS arm64 + amd64, **Windows amd64 only**.
   - **`ValYouW/tflite-dist` issue #11** asked for exactly this — a Windows ARM64
     `.dll`/`.lib`, for a Qualcomm laptop. Closed with no answer.
   - **vcpkg has no `tensorflowlite` port at all**; the request
     (microsoft/vcpkg#26313) was closed for inactivity.
   - Decisive, because it rules out building it ourselves:
     **tensorflow/tensorflow#124329, "Add support for building Tensorflow on
     Windows ARM64 CPUs", is still OPEN.** Filed 2026-07-30, last human activity
     2026-08-18, and the bot marked it **stale on 2026-09-25** with a close
     warning. Windows ARM64 is not merged upstream, so vendoring our own build
     would mean carrying an unmerged PR.

   What this costs: nothing that was working. P1 shipped and stands on its own.
   What it saves: a second runtime, a new native dependency, a five-platform
   build matrix, and `libtensorflowlite_c` added to a packaging budget whose
   `core/lib/onnx/packages` is already 609 MB against an Actions cache at its
   10 GB ceiling.

   **Caveat on the evidence.** This verified that the official assets *exist and
   are named as above*; it did not download, link or run any of them. That the
   `linux-aarch64` tarball closes our gap is an inference from the build script's
   own structure, and should be confirmed by actually building that leg.

   Reopen this only if TF Lite gains a published Windows ARM64 build, or if a
   model matters that `tf2onnx` cannot convert and a server will not serve.
2. **Lifetime.** Objeck has no `AutoCloseable`, so `Close()` is manual and
   callers will forget. Java's TF binding answers this with try-with-resources;
   Objeck's options are a finalizer, leaking until process exit, or documenting
   it. Needs a decision, not a default.
3. ~~**Datatype coverage.**~~ **Answered by P1, for ONNX.** `API.Onnx.Tensor`
   carries everything as `Float[]` and the native side converts to whatever the
   model declares: FP32, FP16, FP64, INT8/16/32/64, UINT8/16/32/64 and BOOL.
   Quantized int8 models therefore already work, which was the one thing this
   question was urgent about while P3 existed. Anything else is refused by name.
   The residual limit is stated rather than hidden: an integer beyond 2^53 does
   not survive a `Float`, so it is refused at construction instead of rounded.
4. ~~**Which backend is the default**~~ — mostly moot now. With TF Lite dropped
   there are two engines, and they are not interchangeable in practice: one runs
   in-process, the other needs a server to exist. The caller picks, and the
   question only returns if a third in-process backend ever does.
5. **Conversion failures.** `tf2onnx` and `TFLiteConverter` both reject models
   with unsupported ops. Neither in-process path helps then; the HTTP engine is
   the only answer. Worth saying so in the user docs rather than leaving people
   to discover it.

## Not in scope

Training. All three backends are inference-only, and nothing here changes that.
