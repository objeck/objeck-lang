# Inference backends: TF Lite beside ONNX — plan

Status: **P1 is built** (2026-10-07) -- `API.Onnx.Session`, `Tensor`,
`TensorSpec`, `ModelInfo` and `RunResult` over two new native entry points,
`onnx_run` and `onnx_model_info`. P2-P5 are still proposed. The HTTP client
(`core/compiler/lib_src/inference.obs`, written 2026-10-03, 38 assertions) is
written but **not registered as a library**, so nothing can `use` it yet.

## Why

No single inference backend covers the five platforms Objeck ships on. Measured
on this tree, not assumed:

| | linux-x64 | linux-arm64 | win-x64 | win-arm64 | macos-arm64 | GPU |
|---|---|---|---|---|---|---|
| ONNX, in-process | yes | **no** | yes | yes | yes | **no** |
| TF Lite, in-process | yes | yes | yes | yes | yes | no |
| HTTP to a server | yes | yes | yes | yes | yes | yes |

Two facts behind the ONNX row, both from `core/release/deploy_posix.sh:130-144`:

- The vendored runtime is a **CPU-only build**. It "reports only
  `CPUExecutionProvider` despite living under `eq/cuda/lib`", and building with
  `ONNX_EP_CUDA` made every session creation fail. So Objeck has no GPU
  inference path at all today.
- Only an x86-64 runtime is vendored, so **linux-arm64 ships no
  `libobjk_onnx`**. `verify_native_libs.sh` declares onnx optional there. This
  was rediscovered on 2026-10-03 when `onnx_failed_session.obs` failed that leg.

TF Lite is the only *in-process* option that covers all five, because it is
built for edge devices where ARM is a first-class target rather than an
afterthought. That is the whole argument for adding it.

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
2. **A backend-neutral tensor.** There are now **two** near-identical ones:
   `API.Onnx.Tensor` (shipped with P1) and `API.Inference.Tensor` (written, not
   registered). That duplication is deliberate and temporary — P1 could not wait
   on a new shared library and its five registration points — but it is exactly
   the drift this document warns about, so **P2 must re-home one, not add a
   third.**
3. **An interface both backends implement**, so calling code does not choose a
   backend at every call site.

## Design

```objeck
interface Engine {
   method : virtual : public : Run(inputs : Vector<Tensor>) ~ Vector<Tensor>;
   method : virtual : public : IsOpen() ~ Bool;
   method : virtual : public : Close() ~ Nil;
}

class OnnxEngine   implements Engine   # in-process, needs the generic onnx_run
class TfLiteEngine implements Engine   # in-process, all five platforms
class HttpEngine   implements Engine   # a server; the only GPU path
```

Calling code names a backend once:

```objeck
engine := TfLiteEngine->New("model.tflite");
if(<>engine->IsOpen()) {
   EndPoint->GetLastError()->PrintLine();
   return;
};

outputs := engine->Run(inputs);
engine->Close();
```

Swapping `TfLiteEngine` for `OnnxEngine->New("model.onnx")` or
`HttpEngine->New("http://gpu-box:8000", "model")` changes nothing else. That is
the point: the backend is a deployment decision, not an API.

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

**P2 — `Engine` interface, `OnnxEngine`, `HttpEngine`.** Move `Tensor`,
`EndPoint` and `ModelMetadata` into the shared bundle; wrap the existing HTTP
`Client` as `HttpEngine`. No native work — `OnnxEngine` is now a thin wrapper
over `API.Onnx.Session`, which is the whole reason P1 came first. **This phase
owns the Tensor duplication P1 left behind**; see "What is missing" 2.

**P3 — TF Lite.** Vendor `libtensorflowlite_c`, wrap ~12 of its ~30 C functions
(`TfLiteModelCreateFromFile`, `TfLiteInterpreterCreate`, `AllocateTensors`,
`GetInputTensor`, `TensorCopyFromBuffer`, `Invoke`, `GetOutputTensor`,
`TensorCopyToBuffer`), add `TfLiteEngine`. This is the only phase with a new
native dependency and a five-platform build matrix.

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

1. **TF Lite's prebuilt platform coverage is UNVERIFIED.** The claim that
   `libtensorflowlite_c` ships for all five targets including Windows ARM64 is
   the load-bearing assumption of this plan and rests on knowledge with a
   May 2026 cutoff. **Confirm before starting P3.** If Windows ARM64 is not
   covered, TF Lite loses its one advantage over the generic ONNX path and the
   plan should stop at P2.
2. **Lifetime.** Objeck has no `AutoCloseable`, so `Close()` is manual and
   callers will forget. Java's TF binding answers this with try-with-resources;
   Objeck's options are a finalizer, leaking until process exit, or documenting
   it. Needs a decision, not a default.
3. **Datatype coverage.** `Tensor` carries `Float[]` or `Int[]` with a datatype
   string. FP32 and INT64 cover images, embeddings and token ids. FP16, INT8 and
   BOOL need a decision about whether to widen the storage or convert at the
   boundary — quantized int8 models are a large part of why TF Lite is
   attractive, so this is not deferrable past P3.
4. **Which backend is the default** when more than one can serve a model, and
   whether that is Objeck's choice or the caller's.
5. **Conversion failures.** `tf2onnx` and `TFLiteConverter` both reject models
   with unsupported ops. Neither in-process path helps then; the HTTP engine is
   the only answer. Worth saying so in the user docs rather than leaving people
   to discover it.

## Not in scope

Training. All three backends are inference-only, and nothing here changes that.
