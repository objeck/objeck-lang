# BERT fine-tuning through ONNX Runtime's training API — Design

Real fine-tuning, with every encoder weight trained, for the university lab that
`docs/ML_BERT_DESIGN.md` serves with a frozen encoder ("ML Chapter 3 — Building a BERT-Based
Malware Classifier": POSRATIO, MAX_SEQ_LENGTH, and learning rates 1e-5 to 5e-5). The route is
a binding of ONNX Runtime's training C API (`OrtTrainingApi`), next to the inference binding
in `core/lib/onnx` and `core/compiler/lib_src/onnx.obs`. Writing a transformer autodiff in
Objeck was ruled out before this document; it is not reconsidered here.

This is a design only. No library code changes with it.

## Recommendation

**Conditional go: an optional, CPU-only training bundle pinned to ONNX Runtime 1.19.2, for
Windows x64/arm64 and Linux x64/arm64, with macOS deferred. Phase 0, a two-day spike, is the
gate.** Phase 0 needs no Objeck code. If it fails, the fallback is LibTorch (see
"Alternatives").

The reasons, each backed by a section below:

- **It is the only route that fine-tunes without writing an autodiff.** The C API trains a
  graph that Python generated once: forward, loss, backward and an AdamW step.
- **Upstream has stopped shipping it, but it still works.** No training package has been
  published since 1.19.2 (September 2024). The source and the build option are still in
  `main`, and a May 2026 fix (microsoft/onnxruntime#27191) kept that build working with gcc-15. Treat it as a frozen dependency pinned at one
  version, not one that moves with our inference runtime. That is a real cost (§1).
- **None of the runtimes we ship has it.** A probe run on every platform found
  `GetTrainingApi` NULL in all of them. Upstream's 1.19.2 training build returns non-NULL
  (§2). So training means shipping a second runtime.
- **The Python step is one-time.** It runs once per model, not once per experiment, and the
  lab's three variables need no new artifacts (§3).
- **The bundle is small next to the model.** It adds 12–17 MB per platform unpacked
  (measured). The bert-base checkpoint alone is about 440 MB (§4).
- **CPU fine-tuning works for MiniLM but is slow for bert-base.** Measured forward cost and
  the standard 3× rule give about 0.15–0.2 s per training sequence for bert-base at length
  128 on a 12-core laptop, and 21–28 ms for MiniLM-L6. The full lab grid is about 11 hours
  with bert-base and about 1.5 hours with MiniLM-L6 at 5,000 training rows (§5). The example
  defaults to MiniLM and says why.

**No-go conditions.** Phase 0 rejects the route if any of these happens:

- the 1.19.2 training runtime cannot be loaded in an Objeck process beside the shipped
  inference runtime, and cannot be renamed so it can;
- a TrainStep on a BERT-shaped model is more than 2× slower than §5 predicts;
- artifacts that `generate_artifacts` makes from an Optimum export of `bert-base-uncased`
  fail to train.

## 1. Is ORT training still supported upstream?

**No packages since 1.19.2. The source is kept and builds, but nothing tests it upstream.**

| Evidence | What it says |
|---|---|
| v1.20.0 release notes | "All ONNX Runtime Training packages have been deprecated." 1.19.2 was the last release of `onnxruntime-training` and `onnxruntime-training-cpu` (PyPI), the NuGet, CocoaPods and Maven training packages. |
| PyPI `onnxruntime-training-cpu` | Latest release 1.19.2 (2024-09-03). Wheels for CPython 3.8–3.12 on `win_amd64`, `macosx_11_0_universal2` and `manylinux_2_28_x86_64` only: none for 3.13, Linux aarch64 or Windows arm64. |
| PyPI `onnxruntime-training` (GPU) | 1.19.2, CPython 3.8–3.11, Linux x86-64, 299 MB per wheel. |
| NuGet `Microsoft.ML.OnnxRuntime.Training` | 1.19.2 (2024-09-03), 52.6 MB. Only a `1.20.0-dev` prerelease after it. |
| GitHub release v1.19.2 assets | `onnxruntime-training-{win-x64, win-arm64, win-x86}-1.19.2.zip`, `onnxruntime-training-linux-{x64, aarch64}-1.19.2.tgz`. **No macOS training archive** (`onnxruntime-training-osx-arm64-1.19.2.tgz` returns 404). |
| CocoaPods `onnxruntime-training-c` | Versions up to 1.20.0 (2024-10-31), the only training build after 1.19.2. An Apple-only pod; its contents were not checked. |
| v1.31.1 release (2026-10-26, the latest) | No training assets. |
| `cmake/CMakeLists.txt` on `main` | Still has `onnxruntime_ENABLE_TRAINING_APIS` (default OFF). `orttraining/orttraining/training_api/include/onnxruntime_training_c_api.h` is still present. |
| microsoft/onnxruntime#27191 (merged 2026-05-30) | An outside contributor fixed `--enable_training --enable_training_apis` building with gcc-15. Somebody still builds it. |
| microsoft/onnxruntime#28058 (closed unmerged, 2026-04) | Proposed disabling ORTModule (large-model training) while keeping its source "for reference". The on-device training API this design uses was not part of it. |
| microsoft/onnxruntime#29576 (merged 2026-07) | A JNI fix in `OrtTrainingSession.evalStep`. The API still gets the odd maintenance fix. |
| `.github/workflows`, `tools/ci_build/github/azure-pipelines` on `main` | No training pipeline. The last change under `orttraining/.../training_api` was 2026-03-31, from a refactor elsewhere. |
| `microsoft/onnxruntime-training-examples` | Archived (read-only). |

The ORT version that Objeck's training bundle uses is therefore fixed at **1.19.2**, the last
release with prebuilt training libraries for Windows and Linux. Building a newer version from
source is possible on every platform, but it buys a moving target that upstream no longer
tests. Phase 6 keeps that option for macOS only.

The pin has one useful property. The training C API reaches its functions through a version
table (`GetTrainingApi(version)`), and its ABI is frozen with the release. The pinned bundle
cannot break because the inference runtime moves.

## 2. Do the shipped runtimes have it?

**No. `GetTrainingApi` returns NULL in every runtime Objeck ships.** A 60-line C probe
loads a runtime by path, calls `OrtGetApiBase()->GetApi(ORT_API_VERSION)`, and then calls
`GetTrainingApi` at the header's version and at 1. It is compiled against the vendored
1.19 header, `core/lib/onnx/eq/cuda/lib/include/onnxruntime_c_api.h` (`ORT_API_VERSION 19`).

The probe was run in two places:

- **Locally**, on Windows x64 (MSVC) and in WSL x64 (gcc), against the vendored files.
- **On CI**, on every hosted runner `ci-build` uses, from the throwaway branch
  `probe/ort-training-api` (workflow `probe-ort-training.yml`, runs 40937156195 and
  40937504550). The Windows jobs and the Linux and macOS jobs ran in separate runs.

The positive control (upstream's training build) shows a NULL here comes from the build, not
from the probe.

| Platform | Runtime (where it comes from) | `GetTrainingApi(19)` |
|---|---|---|
| Windows x64 | 1.22.1 DirectML (NuGet; `eq/lib/win/x64/bin/onnxruntime.7z`) | **NULL** (local and CI) |
| Windows arm64 | 1.23.0 QNN (`eq/qnn/win/onnx/arm64`; what `deploy_windows.cmd arm64` ships) | **NULL** (CI, `windows-11-arm`) |
| Windows arm64 | 1.22.1 DirectML (`eq/lib/win/arm64`) | **NULL** (CI) |
| Linux x64 | 1.19.0 CPU (`eq/cuda/lib/x64/lib/libonnxruntime.so.1.19.0`) | **NULL** (WSL and CI) |
| Linux arm64 | 1.19.0 CPU (`eq/cuda/lib/arm64/lib`) | **NULL** (CI, `ubuntu-24.04-arm`) |
| macOS arm64 | Homebrew `onnxruntime` (1.29.0 on the `macos-15` runner that day) | **NULL** (CI) |
| *control* Linux x64 | upstream `onnxruntime-training-linux-x64-1.19.2` | **non-NULL** |
| *control* Linux arm64 | upstream `onnxruntime-training-linux-aarch64-1.19.2` | **non-NULL** |
| *control* Windows x64 | upstream `onnxruntime-training-win-x64-1.19.2` | **non-NULL** |
| *control* Windows arm64 | upstream `onnxruntime-training-win-arm64-1.19.2` | **non-NULL** |

Further facts the probe and the sources settled:

- **`GetTrainingApi(1)` is NULL even in the training builds.** The training API accepts only
  versions 13 up to the runtime's own, so the binding asks at its header's version.
- **The NULL is silent.** In the v1.22.1 source, `OrtApis::GetTrainingApi` is
  `#ifdef ENABLE_TRAINING_APIS … #else return nullptr;`, with no message. None of the four
  shipped binaries scanned (DirectML x64 and arm64, Linux 1.19.0 x64 and arm64) contains the
  strings `TrainStep`, `OptimizerStep` or `LazyResetGrad`. The binding
  must check for NULL and give its own error ("this ONNX Runtime has no training support;
  install the Objeck training bundle").
- **Homebrew cannot be the macOS answer.** The formula (`Formula/o/onnxruntime.rb`, 1.31.1
  today) passes no `onnxruntime_ENABLE_TRAINING*` option, and the CMake default is OFF.
- **The macOS runtime version is whatever Homebrew has on the day of the build.** It was
  1.29.0 on the probe runner. Other documents say 1.30. This is not a training concern, but
  it is worth knowing.
- **The function list this design started from needs one correction.** The C API has no
  `CreateCheckpointState`. A checkpoint comes from Python and is opened with `LoadCheckpoint`
  (or `LoadCheckpointFromBuffer`). The training session is built from it, from the training
  model, and optionally from the eval and optimizer models. The rest exist as named:
  `CreateTrainingSession`, `TrainStep`, `EvalStep`, `OptimizerStep`, `LazyResetGrad`,
  `SetLearningRate`, `RegisterLinearLRScheduler`, `SchedulerStep`, `SaveCheckpoint`,
  `ExportModelForInferencing`, `SetSeed`, `GetParametersSize`, `CopyParametersToBuffer`.

## 3. Training artifacts: can the Python step be avoided?

**Not in practice. It is a documented one-time step per model.**

`onnxruntime.training.artifacts.generate_artifacts(model, requires_grad, frozen_params, loss,
optimizer, artifact_directory, …)` writes four things:

- `training_model.onnx`: forward, loss, and gradient graph;
- `eval_model.onnx`: forward and loss;
- `optimizer_model.onnx`: AdamW or SGD;
- `checkpoint`: every parameter, and later the optimizer state.

The gradient graph comes from `GradientGraphBuilder`. That is a C++ class inside ORT,
reached only through the Python extension (`from onnxruntime.capi._pybind_state import
GradientGraphBuilder` in `orttraining/.../onnxblock/_training_graph_utils.py`). Nothing in
the C API builds a gradient graph.

To avoid Python, Objeck would have to do one of these:

- link ORT's internal C++ in a custom build, which is the moving target §1 rules out;
- write gradient-graph generation itself, which is the autodiff this design exists to avoid.

**What the step depends on decides how often it runs:**

| Lab variable | Needs new artifacts? | Why |
|---|---|---|
| Learning rate (1e-5, 2e-5, 5e-5) | **no** | the optimizer graph takes the rate as an input: `SetLearningRate`, `RegisterLinearLRScheduler` |
| MAX_SEQ_LENGTH (32 … 256) | **no**, if the export has dynamic axes | an Optimum export has `[batch, sequence]` axes. BERT's position table caps sequences at 512 |
| POSRATIO and class weights | **no**, with the loss below | the training data changes, not the graph |
| Model (bert-base vs MiniLM) | yes | one set per model |
| Which layers train (all, or the top k) | yes | `requires_grad` / `frozen_params` are fixed at generation |

**Class weights without new artifacts.** onnxblock's `BCEWithLogitsLoss(pos_weight=…)` fixes
the weight inside the graph, which would mean one artifact set per POSRATIO. Instead, the
generator script uses a five-line custom `onnxblock.Block`: per-sample BCE-with-logits
(`reduction="none"`) times a `sample_weight` graph input, then the mean. The Objeck side
passes the weights with each batch. The same artifacts then serve the unweighted runs, the
balanced runs and any custom weights.

**Requirements for the one-time step**, which the generator script's header states:

- CPython 3.8–3.12, since `onnxruntime-training-cpu` 1.19.2 has no 3.13 wheel;
- Windows x64, macOS or Linux x64, since there are no arm64 Linux or Windows wheels;
- `torch`, `transformers` (or `optimum`) and `onnx` for the export.

The export must keep the weights as initializers: Optimum's `--no-post-process`, or
`torch.onnx.export(..., training=TrainingMode.TRAINING, do_constant_folding=False)`. Steps:

```
python -m venv ftenv && ftenv/Scripts/pip install onnxruntime-training-cpu==1.19.2 onnx torch transformers
python tools/ml/gen_bert_training_artifacts.py --model bert-base-uncased --out artifacts/bert-base
```

The artifacts are portable. They are ONNX and a flatbuffer checkpoint, so a set made on a
Windows x64 box trains on a Linux arm64 one.

**Lightening the step for users, for later:** a release workflow can run the script for a
short curated list (`bert-base-uncased`, `all-MiniLM-L6-v2`) and publish the sets as optional
release assets. That moves Python from every user to one CI job. The bert-base set is about
440 MB, which is too large for a default download, so it stays opt-in.

## 4. Packaging: training runtime everywhere, or a separate bundle?

**A separate optional bundle.** On Linux the cost is low enough that replacing the shipped
runtime outright is also reasonable (Phase 6 decides).

Sizes are from the v1.19.2 release assets (GitHub API, bytes) and from the CI probe, which
unpacked them:

| Platform | Shipped inference runtime (unpacked) | Training 1.19.2 archive | Training `onnxruntime` unpacked |
|---|---|---|---|
| Linux x64 | 16.1 MB (1.19.0 CPU) | 6.6 MB (inference 1.19.2: 6.1 MB) | **17.4 MB** |
| Linux arm64 | 13.4 MB (1.19.0 CPU) | 5.7 MB (inference: 5.3 MB) | **14.4 MB** |
| Windows x64 | 16.5 MB (1.22.1 DirectML) | 69.5 MB, mostly PDBs (inference: 64.5 MB) | **12.1 MB** (PDB 331 MB, not shipped) |
| Windows arm64 | 13.8 MB (1.23.0 QNN) | 69.8 MB (inference: 65.1 MB) | **12.4 MB** (PDB 343 MB, not shipped) |
| macOS arm64 | Homebrew | none published | build from source (Phase 6) |

What each choice costs:

- **Training in the default runtime everywhere: rejected.** The training builds are CPU-only.
  On Windows they would replace DirectML and QNN, and on macOS CoreML, which costs every
  inference user their accelerator. They would also move Windows inference from 1.22/1.23 back
  to 1.19.2.
- **Linux: swapping the runtime is cheap.** Linux already ships a CPU-only 1.19.0, so 1.19.2
  training is a superset at +1.3 MB (x64) or +1.0 MB (arm64) unpacked, with the same soname
  (`libonnxruntime.so.1`). The risk is that a training build differs at inference too.
  Phase 6 runs the existing ONNX suite against it before deciding.
- **Windows and macOS: a side runtime in an optional bundle.** The bundle holds
  `libobjk_onnx_training` plus the training runtime renamed (`onnxruntime_training.dll` /
  `libonnxruntime_training.so.1.19.2`).

**Why the rename and the load-by-path.** `libobjk_onnx` imports `onnxruntime.dll` by name,
and Windows keeps one module per base name in a process. A second `onnxruntime.dll` in `bin`
is impossible, and loading one by name would return the inference runtime. The C API is
reached entirely through one export, `OrtGetApiBase`. So the training library:

- loads its runtime with `LoadLibraryExW(full path)` / `dlopen(full path, RTLD_LOCAL)`;
- calls `GetProcAddress("OrtGetApiBase")`;
- never links an import library.

The probe already loads runtimes this way. Phase 0 checks that both runtimes can live in one
process. If that fails, the rule becomes that a training program does not touch the
`API.Onnx` natives, which the lab does not need: tokenizing is pure Objeck, and evaluation
runs on the training session's eval graph.

The bundle is 12–18 MB unpacked per platform, against a typical model of 90 MB (MiniLM)
to 440 MB (bert-base). There is also a release-pipeline cost: one more artifact per platform,
its SHA256SUMS line, and the local Windows signing step. The bundle is a
zip unpacked over a deploy tree.

## 5. CPU cost: time per epoch at sequence length 128

**Measured:** forward time, on this repository's Windows build box (AMD Ryzen AI 9 HX 370,
12 cores; ONNX Runtime 1.23.2 CPU provider from Python, default threads).

The models are BERT-architecture graphs with random weights, built with the `onnx` package:
embeddings, L × (self-attention, GELU feed-forward, post-LayerNorm), CLS → one logit. Real
weights cost the same as random ones. Batch 32, length 128, 2 warm-up runs and 5 timed. The
figures were the same at `ORT_ENABLE_BASIC` and `ORT_ENABLE_ALL`, within 5%.

| Model | Encoder params | Forward per batch of 32 | Forward per sequence | Achieved |
|---|---|---|---|---|
| bert-base (12 × 768, FFN 3072) | 85.1 M (+23.5 M embedding) | 1,529 ms | 47.8 ms | ~470 GFLOP/s |
| MiniLM-L12-H384 | 21.3 M (+11.8 M) | 422–443 ms | 13.2–13.8 ms | ~420 GFLOP/s |
| MiniLM-L6-H384 (`all-MiniLM-L6-v2`) | 10.6 M (+11.8 M) | 223 ms | 7.0 ms | ~410 GFLOP/s |

The GFLOP/s column is consistent with 2 × params FLOPs per token plus attention. That
confirms the graphs do the work a real BERT does.

**Estimated:** a training step, from two sources:

- **The FLOPs rule.** A training step costs about 3× a forward pass: forward, backward
  through the activations, and backward to the weights. That is the 6N-FLOPs-per-token rule
  of Kaplan et al., "Scaling Laws for Neural Language Models" (2020), §2.1.
- **Optimizer and kernel overheads.** ORT's training graph runs gradient kernels, which are
  less tuned than inference fusions, so this design budgets 3–4×. AdamW touches about 7
  floats per parameter per step. For bert-base (110 M parameters) that is about 3 GB of
  memory traffic, roughly 60 ms a step, or 2 ms per sequence at batch 32.

| Model | Train time per sequence | Epoch of 1,000 | Epoch of 10,000 | Peak memory, batch 32 (est.) |
|---|---|---|---|---|
| bert-base | 0.15–0.20 s | 2.5–3.3 min | 25–33 min | ~5–6 GB |
| MiniLM-L12-H384 | 42–56 ms | 0.7–0.9 min | 7–9 min | ~2.5 GB |
| MiniLM-L6-H384 | 21–28 ms | 21–28 s | 3.5–4.7 min | ~1.5 GB |

How the memory column is estimated: parameters + gradients + two AdamW moments = 4 × params
× 4 bytes. Activations per layer use Korthikanti et al., "Reducing Activation Recomputation
in Large Transformer Models" (2022), `s·b·h·(34 + 5·a·s/h)` bytes at 16 bits, doubled for
FP32. Batch 16 roughly halves the activation share.

Other lengths: 256 costs about 2.1× the 128 figures, 64 about 0.48×, and 32 about 0.24×.
The linear terms scale with length and attention with its square.

**For the lab**, with 5,000 training rows and 3 epochs per run:

- **Runs:** experiment 1 is 4 ratios × unweighted and weighted, so 8 runs; experiment 2 is 4
  lengths; experiment 3 is 3 rates.
- **Cost unit:** experiment 2's lengths cost 0.24 + 0.48 + 1 + 2.1 = 3.8 runs at length 128,
  so the grid is about 15 run-equivalents at 128.
- **bert-base:** 5,000 × 3 × 0.175 s = 44 minutes a run, so about 11 hours on this laptop.
- **MiniLM-L6:** about 6 minutes a run, so about 1.5 hours.

The example therefore takes a `--max-train N` cap and defaults to MiniLM-L6. It prints the
estimate before starting, and says plainly that bert-base on a CPU is an overnight job.
Inference-only evaluation of the fixed test set costs the forward figures above.

**Phase 0 replaces these estimates with a measured `TrainStep` + `OptimizerStep`.** It
generates artifacts from the same random-weight graphs, which `generate_artifacts` accepts
like any ONNX model, so no torch is needed for the timing.

## 6. API

Two layers, in two new libraries, for the same reason `nlp_onnx` exists. The linker resolves
every class a linked library names, and inference programs must not need the training
bundle.

### `API.Onnx.Training` (new `onnx_training.obl` + `libobjk_onnx_training`): the C API, thinly

```objeck
use API.Onnx.Training;
use API.Models;   # Tensor, as the inference Session uses

artifacts := TrainingArtifacts->New("artifacts/minilm");
# reads artifacts/minilm/manifest.json, written by the generator script:
#   training/eval/optimizer model file names, checkpoint dir, ORT version, model inputs,
#   loss inputs ("labels", "sample_weight"), trainable parameter count
if(<>artifacts->IsValid()) { artifacts->GetError()->ErrorLine(); return; };

session := TrainingSession->New(artifacts);            # or New(artifacts, config): "threads", "seed"
if(<>session->IsOpen()) { session->GetError()->ErrorLine(); return; };   # e.g. no training runtime
session->SetSeed(42);                                   # dropout and init, via SetSeed
session->SetLearningRate(2e-5);
session->SetLinearSchedule(warmup_steps, total_steps);  # RegisterLinearLRScheduler

inputs := Vector->New()<Tensor>;                         # model inputs + loss inputs, by name
inputs->AddBack(Tensor->New("input_ids", [batch, len], ids));
inputs->AddBack(Tensor->New("attention_mask", [batch, len], mask));
inputs->AddBack(Tensor->New("labels", [batch], labels));
inputs->AddBack(Tensor->New("sample_weight", [batch], weights));

loss := session->TrainStep(inputs);    # Float: forward + backward; accumulates gradients
session->OptimizerStep();              # AdamW update
session->SchedulerStep();              # when a schedule is registered
session->LazyResetGrad();              # zero before the next TrainStep

outputs := session->EvalStep(inputs);  # Vector<Tensor>: loss and logits, no gradients
session->GetLearningRate();
session->GetParameterCount(true);      # trainable only

session->SaveCheckpoint("ckpt/epoch3", false);          # true: with optimizer state (3x the size)
session->ExportForInference("finetuned.onnx", ["logits"]);   # a plain model for API.Onnx.Session
session->Close();
```

- **The native layer.** `TrainStep`'s inputs go through the conversion `onnx_run` already
  uses (#1047): `Int[]` becomes the INT64 the model declares, and `Float[]` becomes FP32.
  `sample_weight` and `labels` are FP32. The rest is one native function per C call, each
  returning the ORT status message on failure. Nothing is cached across calls except the
  session and checkpoint handles.
- **Starting over between runs.** Every run of the lab must start from the pretrained
  weights. `TrainingSession->New(artifacts)` loads the checkpoint fresh each time, costing
  about 1 s and 440 MB for bert-base. A `Restart()` that copies a saved parameter buffer back
  (`CopyBufferToParameters`) is the faster path if Phase 3 measures the reload as
  significant.
- **Determinism.** Same seed, same data and one thread give the same losses (tested). With
  more threads ORT's reductions may differ in the last bits. The docs say so, rather than
  forcing one thread on users.

### `System.NLP.Onnx.BertFineTuner` (new `nlp_onnx_training.obl`): what the lab calls

```objeck
tuner := BertFineTuner->New("artifacts/minilm", "vocab.txt", 128);   # artifacts, vocab, MAX_SEQ_LENGTH
tuner->SetLearningRate(2e-5);
tuner->SetEpochs(3);
tuner->SetBatchSize(16);
tuner->SetWarmup(0.1);                    # fraction of steps, then linear decay
tuner->SetClassWeights(1.0, 99.0);        # or SetBalancedWeights()
tuner->SetSeed(42);
tuner->Fit(train_texts, train_labels);    # String[], Bool[]; shuffled per epoch by System.ML.Random
tuner->GetLossHistory();                  # Float[epochs]: mean weighted training loss per epoch
tuner->GetStepLosses();                   # Float[steps]: for the learning-rate plot
p := tuner->PredictProba(test_texts);     # Float[]: sigmoid of the eval graph's logit
yhat := tuner->PredictClass(test_texts, 0.5);
tuner->Restart();                         # back to the pretrained weights for the next run
tuner->Export("finetuned.onnx");          # usable by BertEmbedder and API.Onnx.Session
```

- It tokenizes with the existing `WordPieceTokenizer` and pads to MAX_SEQ_LENGTH exactly, as
  `BertEmbedder` does.
- It implements `ScoreModel` over pre-encoded rows, so `Metrics` thresholds and
  `CrossValidation` accept it. The lab computes precision, recall, F1 and false negatives
  with the code it already has.
- MAX_SEQ_LENGTH is a constructor argument. Experiment 2 builds one tuner per length from the
  same artifacts.

### The lab: `bert_malware_lab.obs --finetune artifacts/<model>`

Without `--finetune`, the example runs as today, with the frozen encoder. With it, each head
fit becomes a `BertFineTuner` fit:

- **Experiment 1** fine-tunes per POSRATIO, unweighted and weighted.
- **Experiment 2** fine-tunes per MAX_SEQ_LENGTH.
- **Experiment 3** runs the lab's own 1e-5, 2e-5 and 5e-5. Those rates are now the right
  ones, and the frozen-encoder caveat about them does not apply. It prints per-step and
  per-epoch losses as CSV columns.

`--max-train N`, `--epochs` (default 3) and the printed time estimate keep a CPU run bounded.
The fixed test set and the stratified split are unchanged.

## Phases

Sizes are new lines of code and working days for one person who knows the ONNX binding.
Each phase ends in a merged PR with its tests, as the frozen-encoder stages did.

| Phase | What | Size | Gate / done when |
|---|---|---|---|
| **0. Spike** | Outside the tree, a C++ program against the 1.19.2 training builds (win-x64, linux-x64). Artifacts from the random-weight MiniLM and bert-base graphs, plus one real Optimum export of `bert-base-uncased`. It measures `TrainStep`+`OptimizerStep` and loads the training runtime renamed, by path, beside the shipped inference runtime, in one process. | ~300 lines C++, ~100 Python; **2 days** | Go/no-go: the three conditions under "Recommendation". The measured time per sequence replaces §5's estimate in this document. |
| **1. Artifacts** | `tools/ml/gen_bert_training_artifacts.py`: export (Optimum or torch), classification head, weighted-BCE block with `sample_weight`, AdamW, eval graph, `manifest.json`. `gen_tiny_trainable.py` writes test artifacts from a model small enough to check by hand (a linear layer over an embedding, ~KB), committed under `programs/regression/data/bert_train/`. | ~300 lines Python; **2–3 days** | The tiny set's one-step update equals the closed-form gradient step. |
| **2. Native** | `core/lib/onnx/training/onnx_training.cpp`: runtime load by path, NULL-API check, checkpoint, session, the step calls, LR and schedule, save, export, parameter count, tensor marshalling shared with `onnx.cpp` through `common.h`. Builds: `vs` project (x64, ARM64), `build.sh` (Linux). | ~700–900 lines C++; **4–5 days** | It links and loads on Windows x64 and Linux x64 here. |
| **3. Objeck API** | `onnx_training.obs` (`API.Onnx.Training`), `onnx_training.obl`, `objk_apis.json`, doc lists (`check_doc_lists.py`). Regression tests on the tiny artifacts: an exact one-step loss and weights, LR and schedule values, eval vs train loss, save/reload round trip, export runs through `API.Onnx.Session`, a clear error without the runtime. | ~400 lines Objeck + ~400 test; **3–4 days** | Six VM modes identical; CI green on every leg that has the bundle. |
| **4. Fine-tuner** | `BertFineTuner` in `nlp_onnx_training.obs`: batching, shuffling, class weights, loss histories, predict, `Restart`, `Export`, `ScoreModel`. Tests on the tiny set: loss decreases, a seeded run is reproducible, weights change the loss as computed. | ~450 lines + ~300 test; **3 days** | The tiny model learns a separable synthetic set to 100%. |
| **5. Lab** | `--finetune` in `bert_malware_lab.obs`, the time estimate, `--max-train`, docs (`ML_BERT_DESIGN.md` cross-reference, `MODELS.md` artifacts how-to). One real run with MiniLM-L6 on the synthetic corpus, recorded here. | ~250 lines; **2 days** | All three experiments run end to end, fine-tuned, with numbers recorded. |
| **6. Packaging** | The optional training bundle per platform in `release-build.yml` (Windows x64/arm64, Linux x64/arm64), SHA256SUMS, signing for the Windows DLLs, install notes. The Linux decision: swap the default runtime to 1.19.2 training if the ONNX suite passes on it. macOS: either build 1.19.2 or newer with `--enable_training_apis` in a release job, or leave macOS out and say so. | **4–6 days** (+3–5 for a macOS source build) | A release candidate's bundle installs over a deploy tree and the Phase 3 tests pass against it. |

**Total:** about 20–25 working days without macOS, plus the macOS build if it is wanted.
Phase 0 is the only part to start before a go.

## Alternatives considered

- **LibTorch (C++ PyTorch).** Actively maintained, trains on CPU, CUDA and Apple MPS, with
  builds for all five targets. CPU archives of 2.9.0: 196 MB (Windows), 181 MB (Linux x64),
  72 MB (macOS arm64), compressed. TorchScript, the usual way to carry a Python model into
  C++, is itself in maintenance. The likely shape is BERT written in the LibTorch C++ module
  API (~500 lines) with Hugging Face weights loaded from safetensors, so no Python step at
  all. It costs about 10× the bundle size and a bigger binding. **This is the fallback if
  Phase 0 fails**, and the better long-term route if ORT training disappears from source.
- **A native Objeck autodiff.** Ruled out before this document: attention, layer norm, GELU
  and the embedding backward over 110 M parameters, with performance that would have to
  match ORT's to be usable.
- **Fine-tune in Python and import the result into Objeck.** Already possible today: export
  the fine-tuned model and run it with `BertEmbedder` or `Session`. It answers "use a
  fine-tuned model", not "fine-tune in Objeck". The docs should mention it as the zero-work
  option.
- **Train only the top k encoder layers.** It is a `frozen_params` choice at artifact time,
  not a separate design. It cuts the backward and optimizer cost roughly in proportion to the
  layers frozen, and is a useful middle ground on a CPU. The generator script takes
  `--train-top-layers k`.

## Sources

- ONNX Runtime v1.20.0 release notes (training package deprecation):
  https://github.com/microsoft/onnxruntime/releases/tag/v1.20.0
- PyPI JSON for `onnxruntime-training-cpu` and `onnxruntime-training` (wheel lists and sizes);
  NuGet `Microsoft.ML.OnnxRuntime.Training`; CocoaPods trunk `onnxruntime-training-c`.
- GitHub release assets v1.19.2 and v1.31.1 (`gh api repos/microsoft/onnxruntime/releases/tags/…`).
- `onnxruntime/core/session/onnxruntime_c_api.cc` at v1.22.1 (`GetTrainingApi`);
  `orttraining/orttraining/training_api/include/onnxruntime_training_c_api.h` and
  `orttraining/orttraining/python/training/{artifacts.py, onnxblock/}` on `main`;
  `cmake/CMakeLists.txt` on `main`.
- microsoft/onnxruntime#27191, #28058, #29576.
- Homebrew `Formula/o/onnxruntime.rb`.
- Kaplan et al. 2020, arXiv:2001.08361 (training ≈ 6N FLOPs per token); Korthikanti et al.
  2022, arXiv:2205.05198 (activation memory).
- The probe (`.github/probe/ort_training_probe.c`), its workflow and the timing script
  (`.github/probe/bert_cpu_time.py`) are on the branch `probe/ort-training-api`.

## Appendix: the probe

```c
/* Loads a runtime by path; asks for GetTrainingApi at the header's version and at 1. */
const OrtApiBase* base = ((GetApiBaseFn)SYM(lib, "OrtGetApiBase"))();
const OrtApi* api = base->GetApi(ORT_API_VERSION);
const OrtTrainingApi* t = api->GetTrainingApi(ORT_API_VERSION);
printf("runtime %s: GetTrainingApi(%d) %s\n", base->GetVersionString(), ORT_API_VERSION,
       t ? "non-NULL" : "NULL");
```

The full source is on the probe branch. Local runs:

```
library:          ...\core\release\deploy-x64\bin\onnxruntime.dll
runtime version:  1.22.1
GetTrainingApi(22): NULL
library:          ./libonnxruntime.so.1.19.0        (WSL x64)
runtime version:  1.19.0
GetTrainingApi(19): NULL
```

The timing script (`.github/probe/bert_cpu_time.py` on the probe branch, ~110 lines) builds
the graph with `onnx.helper` and times `InferenceSession.run`:
`python bert_cpu_time.py bert-base 32`. Phase 0 moves it to `tools/ml/bench_bert.py` and adds
the training-step timing that replaces §5's estimate.
