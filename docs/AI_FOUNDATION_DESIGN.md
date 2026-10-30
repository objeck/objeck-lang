# AI Foundation — Design (BERT is the first consumer)

Objeck is meant to be a language for AI research. Today it can *run* models (`API.Onnx`), call
hosted models (`API.OpenAI`, `API.Google`, `API.Ollama`), and train small networks
(`System.ML`). It cannot train or fine-tune a transformer, and every model needs hand
downloading and, often, Python.

This document designs a foundation that closes that gap: tensors on CPU and GPU, autograd,
`nn` modules, optimizers, data loading, `safetensors` and a Hugging Face hub cache, tokenizers,
task pipelines, and ONNX as both a runtime and an export target. BERT, "like with Python's
`transformers`", is the first thing built on it.

This is a design only. No library code changes with it. Target: the release **after**
v2026.10.2.

Every number below marked *measured* was measured for this document on 2026-10-29 (method in
the appendix). Everything else is labelled as an estimate or as something a phase must check.

## Summary

- **Two tracks that meet.**
  - **Phase A, use models:** download ONNX exports and tokenizers from the Hugging Face Hub, then
    run Python `pipeline()`-style tasks over the existing `API.Onnx`. It needs no new native
    dependency, and it ships value first.
  - **Phase B, the training foundation:** a binding of **libtorch**, PyTorch's C++ library and
    the same engine PyTorch runs on. Transformer architectures are written **in Objeck** over
    tensors and autograd, and load Hugging Face `safetensors` directly, as Rust's candle does.
    The end state needs no Python at all.
- **No deprecated or unmaintained dependency, not even as a fallback.**
  - Rejected: ONNX Runtime's training API, deprecated in 1.20.0, which is why the earlier
    design, PR #1449, is superseded.
  - Rejected: TorchScript, which PyTorch's own source marks deprecated, and `torch-directml`,
    whose last release was in 2024.
  - Each dependency has a status table with primary-source evidence (§3).
  - Where the rule leaves a gap, the gap is stated: GPU training on Windows arm64, and on AMD
    GPUs under Windows (§3.3).
- **Existing programs keep compiling.**
  - `System.ML`, `API.Onnx`, `System.NLP` and `System.NLP.Onnx` stay as they are.
  - New layers are new libraries. Old classes are kept, or gain a second backend behind the same
    API, and are never removed (§2).
- **The first end-to-end target is the user's lab model:** DistilBERT fine-tuned as a malware
  classifier on Colab, run from Objeck through the Phase A `TextClassifier`. Later, the same
  model is fine-tuned in Objeck itself through Phase B.
- **First PR:** `TextClassifier` over a local model folder, with a tiny checked-in
  DistilBERT-shaped fixture and a parity test (§11). It is small and testable, and it serves the
  lab directly.

## 1. Goals, rules and non-goals

**Goals**

1. **Use pretrained models by name**, as in Python's
   `pipeline("text-classification", model=...)`: no hand downloads, no Python.
2. **Fine-tune and train transformers in Objeck**, on CPU and GPU, with the results matching
   PyTorch.
3. **One set of layers for all future AI work.** Decoder LLMs, vision, audio, embeddings and RAG,
   and reinforcement learning all build on the same tensor, `nn` and hub layers, not on one-off
   wrappers.
4. **Parity is the test oracle.** Every task and every training step is checked against
   PyTorch, `transformers` or `tokenizers` on fixed inputs, with stated tolerances.

**Rules**

- **No deprecated or unmaintained library**, as primary dependency, fallback or optional path.
  Every dependency's status is cited from primary sources (§3).
- **Python-free end state.** Python is allowed only in test-fixture generators that a developer
  runs to refresh fixtures, never on a user's path.
- **The project's own conventions:**
  - a phased PR for each step;
  - `objk_apis.json` regenerated when library classes are added;
  - all libraries rebuilt after any compiler change;
  - `TESTS.md` never committed in a PR.

**Non-goals**

- **Writing tensor kernels or an autodiff engine in Objeck.** That was ruled out before #1449, and
  it stays ruled out. libtorch supplies both.
- **Distributed or multi-node training, and serving at scale.**
- **Replacing hosted-model clients** (OpenAI, Gemini, Ollama). They stay, and agents keep using
  them (§4).

## 2. What exists, and what happens to it

Line counts are from the `master` checkout at `c816f3f9ae`.

| Library (source) | What it is | Decision |
|---|---|---|
| `System.ML` `ml_core.obs` (2,500 lines): `Matrix2D`, `NeuralNetwork`, `DenseNetwork`, `Random` | Pure-Objeck MLPs over `Float[,]`. `DenseNetwork` has PyTorch-matching Adam and deterministic seeding | **Keep unchanged.** It needs no native library, which is right for teaching and small tabular work. A later, optional `DenseNetwork` backend on the tensor core gets the same API (Phase F) |
| `System.ML` trees, SVM, Bayes, clustering, outliers (`ml_tree.obs`, `ml_svm.obs`, …) | Classical ML | **Keep.** They are outside this design. They interoperate through `Float[,]`, which the tensor core converts to and from (§6.3) |
| `System.ML` `ml_embed.obs`: `EmbeddingClassifier` (`ScoreModel`) | A trainable head over frozen embeddings | **Keep.** It stays the frozen-encoder baseline. Phase B's fine-tuner also implements `ScoreModel`, so `Metrics` and `CrossValidation` accept both |
| `API.Models` `models.obs`: `Tensor`, `TensorSpec`, `EndPoint` | The model I/O carrier: name, shape, flat `Float[]` / `Int[]`. Remote KServe/Triton endpoints | **Keep, and keep the name.** It is the *interchange* type for named model inputs and outputs. The new compute tensor is a different class in a different bundle (`System.AI.Tensor.Tensor`), with converters both ways. The two have different jobs (§6.1) |
| `API.Onnx` `onnx.obs` (3,136 lines): `Session`, `RunResult`, YOLO, ResNet, DeepLab, OpenPose, face, Phi-3 | ONNX Runtime inference | **Keep.** It becomes the ONNX *runtime* layer of the foundation. Phase A's pipelines run on it, and Phase E's ONNX export targets it |
| `System.NLP` `nlp.obs`: `WordPieceTokenizer`, `TokenEncoding`, `ApiSequenceTokenizer`, TF-IDF, … | Pure-Objeck NLP | **Keep and extend.** The `tokenizer.json` loader, BPE and Unigram are added here (Phase A3–A5). `TokenEncoding` gains offsets, word ids and sequence ids, as new fields and methods; existing constructors keep working |
| `System.NLP.Onnx` `nlp_onnx.obs`: `BertEmbedder` | Sentence embeddings over ONNX | **Keep, and generalise behind the same API.** The text pipelines join it. A runner interface lets each pipeline run on ONNX or, later, on the torch backend without changing the caller's code (§7.4) |
| `Query.Structured.ML` / `.Vision` (`query_ml.obs`, `query_vision.obs`) | Query sources over ML results and YOLO | **Keep.** They gain an embedding source for RAG once the pipelines exist (§4) |
| `System.AI` `ai_rl.obs`, `ai_search.obs`, … | Tabular Q-learning, SARSA, MDPs, search | **Keep.** Deep RL (a DQN or a policy gradient) builds on the tensor core as new classes (§4) |
| `API.OpenAI`, `API.Google` (Gemini), `API.Ollama` | Hosted and local model clients | **Keep.** Agents keep using them. The foundation adds local embeddings and rerankers beside them |

**The compatibility rule.** No existing class changes its public signatures. Where a class gains
a second backend, that is a new constructor or a new configuration key. Each phase's PR runs the
existing ML, NLP and ONNX regression tests unchanged.

## 3. Dependency status (primary sources)

### 3.1 Proposed dependencies

| Dependency | Role | Status evidence | License |
|---|---|---|---|
| **libtorch** (PyTorch C++) | Tensor core, autograd, kernels, optimizers, devices (Phase B) | GitHub release **v2.15.0 on 2026-10-28**. Earlier: v2.14.1 on 2026-09-30, v2.14.0 on 2026-09-02, then 2.13.0 (2026-07-08) and 2.12.0 (2026-05-13) on PyPI. That is a release about every 1–2 months. `libtorch-*-2.15.0` archives were published on download.pytorch.org on 2026-10-28 (HTTP `Last-Modified`). The C++ frontend docs (`docs/cpp/source/frontend.md`) carry no deprecation notice. The repository adds a **Stable ABI** C++ API (`docs/cpp/source/api/stable/index.md`, "guaranteed to remain stable across PyTorch versions") | BSD-style (`LICENSE`: "Redistribution and use in source and binary forms…"). GitHub reports `NOASSERTION` because the file concatenates several notices. Compatible with redistribution |
| **ONNX Runtime** (already vendored, inference only) | ONNX runtime layer (Phase A, E) | **v1.31.1 on 2026-10-26** and v1.30.1 on 2026-10-29 (GitHub releases). Inference is actively released | MIT |
| **safetensors** (file format; no code dependency) | Weight I/O, read and written by Objeck (Phase B) | The format is defined by `huggingface/safetensors`: v0.8.0 on 2026-06-09, v0.9.0-rc.1 on 2026-09-23, PyPI 0.8.0 on 2026-09-24. Objeck implements the format itself (an 8-byte little-endian header length, a JSON header, then raw bytes), so no library is linked | Apache-2.0 (format reference) |
| **Hugging Face `tokenizers`** (reference only, in fixture generators) | The parity oracle for the tokenizers | v0.23.3 on 2026-10-09 (GitHub) and 2026-10-19 (PyPI); v1.0.0-rc.3 on 2026-10-19. **Not linked.** Objeck reimplements the algorithms | Apache-2.0 |
| **`transformers` / `torch`** (reference only, in fixture generators) | The parity oracle for the tasks and for training | `torch` 2.15.0 on 2026-10-28; `transformers` 5.22.0 installed for the measurements. **Never on a user's path** | BSD / Apache-2.0 |
| **ONNX** (the format) | The export target (Phase E). Objeck writes the protobuf itself | `onnx/onnx` v1.23.2 on 2026-10-06. Not linked | Apache-2.0 |

### 3.2 Rejected

- **ONNX Runtime training API.**
  - **Evidence:** the v1.20.0 release notes say "All ONNX Runtime Training packages have been
    deprecated. ORT 1.19.2 was the last release…".
  - **Consequence:** PR #1449, which used it, is superseded by this document.
- **TorchScript / `torch.jit`.**
  - **Evidence:** PyTorch's own docstrings in `torch/jit/__init__.py`, `torch/jit/_freeze.py`,
    `torch/jit/_fuser.py` and `torch/jit/_async.py` read "TorchScript is deprecated, please use
    `torch.compile` instead". `torch/csrc/jit/frontend/script_type_parser.cpp` adds "2024.08.14:
    Since we are starting to deprecate Torchscript usages".
  - **Consequence:** no TorchScript models and no `torch::jit::load`. Architectures are defined in
    Objeck instead (§7).
- **`torch-directml`.**
  - **Evidence:** its last PyPI release is 0.2.5.dev240914, on 2024-09-15. Its last GitHub
    pre-release is 2024-08-19. The `microsoft/DirectML` README opens with "DirectML is in
    maintenance mode" and points to Windows ML.
- **Python-side export tools on the user's path** (`optimum`, `torch.onnx.export`).
  - They are maintained.
  - **Why rejected:** they break the Python-free rule. Phase A downloads exports that already
    exist; Phase E writes ONNX from Objeck.
  - They stay acceptable for the lab's *own* Colab notebook, which is already Python (§9).

### 3.3 Gaps the rule leaves, stated plainly

| Gap | Why | What users get |
|---|---|---|
| **GPU training on Windows arm64** | libtorch publishes only a CPU build for Windows arm64 (`libtorch-win-arm64-shared-with-deps-2.15.0+cpu.zip`). No CUDA, ROCm or XPU build exists for it | CPU training on Windows arm64. GPU *inference* still works through ONNX Runtime (QNN on Snapdragon) |
| **GPU training on AMD GPUs under Windows** | ROCm libtorch is Linux-only (`rocm7.14`, `rocm10.0` builds are `manylinux_2_28_x86_64`). DirectML is excluded | CPU on Windows. AMD GPU training on Linux through ROCm |
| **Linux arm64 has no libtorch zip** | download.pytorch.org has no `libtorch-*aarch64*.zip` (404). The CPU **wheel** for `manylinux_2_28_aarch64` carries the same libraries (`torch/lib/libtorch_cpu.so` and its dependencies, measured) | The release pipeline takes the libraries from the wheel. That is a supported artifact, used as an archive, without running Python |
| **DeBERTa-v3-base weights as `safetensors`** | `microsoft/deberta-v3-base` ships only `pytorch_model.bin` (a Python pickle). Objeck must never unpickle: loading a pickle can run arbitrary code | Phase B's DeBERTa support loads only `safetensors` repos; many fine-tuned DeBERTa repos have them. ONNX inference of DeBERTa (Phase A) is unaffected |
| **Objeck's own Windows ONNX GPU path uses DirectML** | The DirectML README says it is in maintenance mode. Objeck ships the DirectML 1.22.1 ONNX Runtime build on Windows x64 and arm64 | **Outside this design, but flagged:** an existing dependency violates the new rule. A separate design should move Windows GPU inference to a maintained execution provider; ONNX Runtime publishes plugin EPs such as `plugin-ep-webgpu/v0.5.0` (2026-10-29). See the follow-ups (§13) |

## 4. What future AI work needs from the foundation

A short survey, to make sure the layers are the right ones. The foundation's job is to make each
of these a library on top, not a new binding.

| Area | What it needs | Layers it uses | Built on today's pieces |
|---|---|---|---|
| **Decoder LLMs** (GPT-style, Llama-style) | Causal attention; RoPE; a KV cache updated in place; sampling (top-k, top-p, temperature); BPE tokenizers; int8/int4 weights; **LoRA** adapters (a frozen base, small trainable matrices); bf16/fp16 | tensor core (in-place `IndexCopy`, `Narrow`, dtypes), `nn` (Embedding, RMSNorm, Linear with an optional LoRA), autograd (parameters partly frozen), hub (sharded `safetensors` with `model.safetensors.index.json`), tokenizers (BPE) | `Phi3Session` in `onnx.obs` keeps working for ONNX-exported decoders; the foundation adds native Objeck decoders that can also be trained |
| **Vision** (ViT, CNNs, detection) | Conv2d, pooling, BatchNorm, patch embedding, image tensors from OpenCV mats with no extra copy where possible | tensor core (`FromImage` from `API.OpenCV` pixel buffers), `nn` (Conv2d, BatchNorm2d), data (image datasets and augmentation) | `opencv.obs` for I/O; `query_vision.obs` keeps YOLO-over-ONNX; a ViT classifier is §8.3 |
| **Audio** (Whisper-style encoders, keyword spotting) | STFT / mel features, 1-D convolutions, encoder–decoder attention | tensor core (FFT via libtorch `torch::fft`), `nn` (Conv1d), data (WAV loading) | The existing audio I/O (`lame.obs`, SDL mixer) is playback-oriented; a WAV reader goes in the data layer |
| **Embeddings and RAG** | Sentence embeddings, rerankers, vector search over `Query.Structured` | pipelines (`SentenceEmbedder`, `CrossEncoder`), tensor core (cosine top-k), `query_ml.obs` (an embedding column source) | `BertEmbedder` today; Phase A adds sentence-transformers pooling configs and rerankers |
| **Reinforcement learning** | Small MLP/CNN policies, autograd through custom losses, replay buffers, many small steps per second | tensor core, autograd, optimizers, data (replay buffer) | `ai_rl.obs` keeps the tabular algorithms; DQN and PPO go beside it |
| **Agents** | Tool calling against hosted LLMs, plus local embeddings, rerankers and small local models | pipelines; hub | `openai.obs`, `gemini.obs`, `ollama.obs` stay the LLM clients. The foundation supplies the local parts |

What this asks of the core, beyond BERT:

- **dtypes:** f32, f16, bf16, i64, i32, i8/u8 (quantized weights), and bool.
- **In-place ops:** the KV cache, optimizer updates.
- **Views:** `View`, `Narrow`, `Permute`, `Transpose`.
- **Indexing.**
- **FFT.**
- **Frozen versus trainable parameters, per tensor.**
- **Sharded `safetensors` files.**

Every one of these is in libtorch. The binding must not paint them out: the native shim's tensor
handle carries any dtype and device from day one, even though BERT needs only f32 and i64 (§6).

## 5. The layers

Each layer is its own library, so a program links only what it uses. This follows the rule that
made `nlp_onnx` a separate library: the compiler resolves every class a linked library names, so
an inference-only program must never need libtorch.

```
 programs ─────────────────────────────────────────────────────────────────────────────
   pipelines        System.NLP.Onnx   (nlp_onnx.obl)      TextClassifier, TokenClassifier, QuestionAnswerer,
                                                          FillMask, SentenceEmbedder, CrossEncoder, BertEmbedder
                    System.AI.Torch.Runner (ai_torch_runner.obl)   runs those pipelines on torch-defined models
   model zoo        System.AI.Transformers (ai_transformers.obl)   Bert, DistilBert, Roberta, XlmRoberta, DebertaV2,
                                                          Gpt (decoder), Vit; task heads; FineTuner
   training         System.AI.Optim   (ai_optim.obl)       AdamW, SGD, LR schedules, gradient clipping, Trainer
                    System.AI.Data    (ai_data.obl)        Dataset, DataLoader, samplers, collate, text/image/audio readers
   modules          System.AI.NN      (ai_nn.obl)          Module, Parameter, Linear, Embedding, LayerNorm, RMSNorm,
                                                          Dropout, MultiHeadAttention, Conv1d/2d, functional ops
   core             System.AI.Tensor  (ai_tensor.obl  + libobjk_torch)  Tensor, DType, Device, autograd, Scope, Generator
   I/O              System.AI.Hub     (ai_hub.obl)         HubClient, HubCache, SafeTensors, ModelConfig (config.json)
                    System.NLP        (nlp.obl)            WordPiece, BPE, Unigram, TokenizerJson, TokenEncoding
   runtime / export API.Onnx          (onnx.obl)           Session (existing); OnnxExporter (Phase E)
   existing         System.ML, API.Models, Query.Structured.*, System.AI (RL, search), API.OpenAI/Google/Ollama
```

**Dependency direction.**

- `ai_hub` and `nlp` need no native compute library.
- `ai_tensor` is the only library that loads libtorch.
- `nlp_onnx`, the pipelines, needs ONNX and never libtorch. The torch runner lives in its own
  library, which depends on both. So a pipeline program links libtorch only when it asks for the
  torch backend.

**Bundle names.** The new bundles extend the existing `System.AI` family, which today holds the
classic AI library: search, games, optimisation and RL. Decision D7 below.

## 6. Language and VM fit

### 6.1 One `Tensor` class, dtype as a value

The compute tensor is `System.AI.Tensor.Tensor`: a handle to a libtorch tensor, plus a reference
to any Objeck array it views.

- **Dtype and device are values on the tensor**, not type parameters. Code like
  `x->To(DType->F16)->To(Device->Cuda(0))` changes them at run time. A generic `Tensor<T>`
  would put the dtype into the static type and break that. PyTorch, candle and JAX all keep the
  dtype dynamic for the same reason.
- **Generics still serve the layers above:**
  - `Module` holds `Vector<Parameter>`;
  - `DataLoader<T>` yields a user's batch type;
  - `Map<String, Tensor>` is the `safetensors` state dict.
- **`API.Models.Tensor` stays the interchange type for named model I/O.** The core tensor
  converts to and from it:

  ```objeck
  t := Tensor->From(api_tensor);
  api := t->ToModelTensor("logits");
  ```

### 6.2 No operator overloading: methods, chained

Objeck has no operator overloading (`docs/ROSETTA_HARDENING.md`: "Objeck has no operator
overloading. Arithmetic types expose named methods."). The tensor API is therefore method-based
and fluent:

```objeck
scores := q->MatMul(k->Transpose(-2, -1))->Mul(scale)->Add(mask);   # Python: q @ k.transpose(-2, -1) * scale + mask
probs := scores->Softmax(-1);
```

- **This is enough to write every architecture in §8.**
- **Operator overloading is a language change, so it is decision L1:** `a + b`, `a * b` and `a @ b`
  dispatching to `Add`, `Mul` and `MatMul` on any class that declares them. Recommended **later**,
  as its own design, once the tensor API exists and real model code shows where the method
  chains hurt. It is not a prerequisite for anything here.

### 6.3 Memory: native tensors, the GC, and zero-copy

- **Tensor storage is native memory owned by libtorch.** An Objeck `Tensor` object holds the handle
  as an `Int` and stores **no object references inside C++**, so no `WriteBarrier` is needed.
  Results that come back as Objeck arrays are allocated by the VM in the old generation
  (`docs/native_interface.md`, and the native-allocation rule since PR #650).
- **Zero-copy from Objeck arrays is possible, but only for 64-bit types.**
  - Objeck's `Float` is a 64-bit double and `Int` is 64-bit. `Tensor->View(values : Float[], shape)`
    wraps the array's memory as an **f64** tensor (`torch::from_blob`). `Int[]` becomes **i64**,
    which is exactly what token ids need.
  - This is safe because arrays are born in the old generation, and "old objects are never moved"
    (`docs/native_interface.md`). The Objeck `Tensor` keeps a reference to the array, so the array
    lives as long as the view.
- **f32, the dtype models train in, needs one conversion at the boundary.**
  `Tensor->FromFloats(values, shape, DType->F32)` copies and narrows. `ToFloats()` widens back.
  Inside a model, nothing crosses the boundary: activations and weights stay native from load to
  result.
- **Lifetime: no finalizers today.**
  - Objeck has no `AutoCloseable` and no finalizers (`docs/INFERENCE_ENGINES_PLAN.md` §"Lifetime"
    asked the same question for ONNX sessions).
  - A forward pass creates hundreds of temporary tensors, so manual `Close()` per tensor is not
    workable. The design uses **scopes**: every tensor created inside a scope is freed when the
    scope ends, except the ones returned or marked to keep.

    ```objeck
    loss := Tensor->Scope(\() => {
      logits := model->Forward(batch);
      return logits->CrossEntropy(batch->GetLabels());     # survives the scope
    });
    ```

  - **Owned outside scopes:** parameters (by their `Module`), optimizer state (by the optimizer),
    and tensors explicitly kept.
  - **Native-side backstop:** a debug counter of live handles, which the tests assert returns to
    the baseline after each step.
  - **Finalizers, or a `using`-style block, are decision L2:** a VM change, recommended as a
    separate design that would let scopes become a convenience rather than a requirement.

### 6.4 Autograd lives in libtorch; closures serve the training loop

- **Gradients are recorded and computed by libtorch's autograd**, the same engine as PyTorch:
  - `loss->Backward()`, `Tensor->NoGrad(\() => ...)` and `param->RequiresGrad(false)` map
    directly onto it;
  - Objeck closures never implement a backward pass for built-in ops.
- **Where closures are used:**
  - the bodies of `Scope` and `NoGrad`;
  - `Trainer` callbacks (on step, on epoch, on evaluate);
  - data-loader transforms;
  - later, custom autograd functions (a forward and a backward written in Objeck, through libtorch's
    custom `Function`).

  The FEATURES guide's lambda forms (`\() => body`, and the annotated `\(T) ~ R : (x) => body`)
  are enough for all of these.

### 6.5 The JIT and native-call overhead

- **Every tensor op is one native call.** A BERT-base forward is about 12 layers × ~25 ops ≈ 300
  calls per batch, against about 1.5 s of compute for a batch of 32 at length 128 (measured, §10.2).
  Call overhead is noise at that scale.
- **Where it matters:** RL with tiny networks and many small steps. B0 measures the per-call cost
  under the JIT and under the interpreter. If it is significant, the shim adds fused calls for the
  hot paths (`Linear`, attention) rather than changing the VM.
- **Threads:** libtorch's intra-op pool does the parallel work. Objeck threads may each run their
  own models. A tensor shared across Objeck threads is the user's responsibility, as in PyTorch.

## 7. Phase A — use models: hub, tokenizers, pipelines (over ONNX)

### 7.1 The Hub has the models (measured)

Exports that already exist cover the common BERT family. Counted from the Hub API
(`/api/models`, 2026-10-29):

| Pipeline tag | Repos tagged `onnx` (Hub-wide) | `Xenova/*` | `onnx-community/*` |
|---|---|---|---|
| text-classification | 1,877 | 24 | 90 |
| token-classification | 1,648 | 16 | 57 |
| question-answering | 218 | 6 | 24 |
| fill-mask | 182 | 40 | 35 |
| feature-extraction | 1,127 | 108 | 47 |
| sentence-similarity | 1,019 | 0 | 26 |
| zero-shot-classification | 167 | 17 | 8 |
| text-ranking (rerankers) | 147 | 2 | 1 |

Specific models (`/api/models/{repo}?blobs=true` and each repo's `config.json`):

| Model | Ready ONNX export | Files | `tokenizer.json` | Labels in `config.json` |
|---|---|---|---|---|
| bert-base-uncased | `Xenova/bert-base-uncased`; also `google-bert/bert-base-uncased` (`model.onnx` at the root, 532 MB) | `onnx/model.onnx` 438 MB, `model_quantized.onnx` 111 MB, plus fp16, int8, uint8, q4, q4f16, bnb4 | yes | — (fill-mask) |
| DistilBERT | `Xenova/distilbert-base-uncased`; the original repo has none | 8 variants | yes | — |
| MiniLM (all-MiniLM-L6-v2) | `sentence-transformers/all-MiniLM-L6-v2` ships its own `onnx/` (fp32 90 MB; `qint8_arm64`, `qint8_avx512`, … 23 MB; O1–O4) | 9 files | yes | — |
| RoBERTa | `Xenova/roberta-base`, `Xenova/distilroberta-base` | model 499 / 329 MB, fp16, quantized | yes (BPE) | — |
| DeBERTa-v3 | **none** for `microsoft/deberta-v3-base` (it has no `tokenizer.json` either, only `spm.model`). Fine-tuned DeBERTa-v3 repos do have exports: `Xenova/nli-deberta-v3-small`, `cross-encoder/nli-deberta-v3-base`, `mixedbread-ai/mxbai-rerank-xsmall-v1`, `protectai/deberta-v3-base-prompt-injection-v2` | 8 variants (Xenova) | yes (Unigram) | 3 (NLI) |
| XLM-R | `Xenova/xlm-roberta-base` (model 1,114 MB) | fp32, fp16, quantized | yes (Unigram, 17 MB) | — |
| Classifier: SST-2 | `Xenova/distilbert-base-uncased-finetuned-sst-2-english`; the original `distilbert/…` has `onnx/model.onnx` but no `tokenizer.json` | 8 variants | yes | 2: NEGATIVE, POSITIVE |
| Classifiers: others | `Xenova/twitter-roberta-base-sentiment-latest` (3), `Xenova/bert-base-multilingual-uncased-sentiment` (5), `Xenova/toxic-bert` (6), `SamLowe/roberta-base-go_emotions-onnx` (28) | | yes | yes |
| NER | `Xenova/bert-base-NER`; `dslim/bert-base-NER` has `onnx/model.onnx` but no `tokenizer.json`; `Xenova/bert-base-multilingual-cased-ner-hrl` | 8 variants | yes | 9 (`O`, `B-PER`, `I-PER`, …) |
| QA | `Xenova/distilbert-base-cased-distilled-squad`, `Xenova/distilbert-base-uncased-distilled-squad`, `onnx-community/roberta-base-squad2-ONNX` | 8 variants | yes | — |
| Rerankers | `cross-encoder/ms-marco-MiniLM-L6-v2` (own `onnx/`), `Xenova/bge-reranker-base` (XLM-R), `mixedbread-ai/mxbai-rerank-xsmall-v1` (DeBERTa) | | yes | 1 |

**The layout to support:**

- `config.json`, `tokenizer.json` and `tokenizer_config.json` at the repo root;
- the models under `onnx/`;
- some repos (`google-bert/*`) with `model.onnx` at the root;
- some repos with no `tokenizer.json`, only `vocab.txt` with `tokenizer_config.json`
  (`do_lower_case`). The WordPiece path must take that too.

**Variant names:**

- `model.onnx` (fp32);
- `model_quantized.onnx` and `model_int8` / `model_uint8` (dynamic int8 quantization);
- `model_fp16`, `model_q4`, `model_q4f16`, `model_bnb4`;
- sentence-transformers' `model_O1`…`O4` and `model_qint8_*`.

### 7.2 Model signatures (measured from real exports)

Read with the `onnx` package from the downloaded files:

| Export | Inputs (all INT64 `[batch, sequence]`) | Outputs (FP32) |
|---|---|---|
| DistilBERT SST-2 | `input_ids`, `attention_mask` | `logits [batch, 2]` |
| bert-base-NER | `input_ids`, `attention_mask`, `token_type_ids` | `logits [batch, sequence, 9]` |
| DistilBERT SQuAD | `input_ids`, `attention_mask` | `start_logits`, `end_logits [batch, sequence]` |
| distilroberta-base (fill-mask) | `input_ids`, `attention_mask` | `logits [batch, sequence, 50265]` |
| bert-base-uncased (fill-mask) | `input_ids`, `attention_mask`, `token_type_ids` | `logits [batch, sequence, 30522]` |
| ms-marco cross-encoder | `input_ids`, `attention_mask`, `token_type_ids` | `logits [batch, 1]` |
| mxbai-rerank (DeBERTa) | `input_ids`, `attention_mask` | `logits [batch, 1]` |
| nli-deberta-v3-small | `input_ids`, `attention_mask` | `logits [batch, 3]` |
| all-MiniLM-L6-v2 | `input_ids`, `attention_mask`, `token_type_ids` | `last_hidden_state [batch, sequence, 384]` |

All axes are dynamic. **Only BERT-type exports declare `token_type_ids`.** DistilBERT, RoBERTa
and DeBERTa-v3 exports do not. The pipelines therefore do what `BertEmbedder` already does:

- build only the inputs the session declares;
- ask only for the outputs they need.

### 7.3 Hub client and cache (`System.AI.Hub`, `ai_hub.obl`)

**What the Hub answers (measured with raw HEAD requests, no redirects followed):**

- **`GET https://huggingface.co/{repo}/resolve/{revision}/{path}`**
  - **Small (git) files:** `307` with a **relative** `Location:
    /api/resolve-cache/models/{repo}/{commit}/{path}?…`.
  - **Large (LFS/Xet) files:** `302` to an **absolute, signed CDN URL**
    (`https://us.aws.cdn.hf.co/xet-bridge-us/…&Expires=…&Signature=…`).
- **The first response carries the metadata:**
  - `X-Repo-Commit`, the commit the revision resolved to;
  - `X-Linked-Size`;
  - `X-Linked-Etag`, which is the file's **SHA-256** for LFS files (it matches the
    `lfs.sha256` that the API lists) and the **git blob SHA-1** for small files.
- **The CDN answers `Accept-Ranges: bytes`**, so interrupted downloads can resume.

**What Objeck's HTTPS client does with that today** (`core/compiler/lib_src/net_secure.obs`,
`net_common.obs`):

- **Redirects work.** It follows 301/302/303/307/308, and `BuildRedirectUrl` resolves a relative
  `Location` against the current host with `https://`.
- **Large files fail.**
  - The whole body is read into memory, and `WebCommon->ReadLength` returns `Nil` above 256 MB
    (`if(length > 256*1048576)`).
  - **Every fp32 base model fails:** bert-base 438 MB, RoBERTa 499 MB, XLM-R 1,114 MB.
- **Tokens would leak.**
  - `@request_headers` are re-sent on **every** hop, including a redirect to another host.
  - An `Authorization: Bearer` token for a gated repo would be sent to the CDN.
  - **This is a security bug to fix before any token support ships.** Strip `Authorization` (and
    cookies) on a cross-origin redirect, as curl and browsers do.
- **Checksums need streaming.** `Cipher.Hash->SHA256` takes one `Byte[]`, so a 1.1 GB file would
  have to sit in memory to be hashed.

**Required changes, all in Phase A2:**

- **`HttpsClient->GetToFile(url, path, headers)`** streams to a temporary file in chunks, follows
  redirects, and resumes with `Range: bytes=N-` when the server accepts ranges. It also gets
  `HttpClient` and HTTP/2 counterparts. No size cap: the cap exists to protect memory, and
  streaming removes the reason.
- **The cross-origin `Authorization` strip**, with a regression test against a local server that
  redirects to a second port.
- **An incremental hash:** `Cipher.Hash->Sha256Init/Update/Final`, or a `Hasher` object, over the
  native library behind `Cipher.Hash`. Git-blob SHA-1 for small files uses the existing `SHA1` on
  `"blob {size}\0" + content`.

**The cache:**

```
$OBJECK_HUB_CACHE   (default: ~/.objeck/hub; Windows: %USERPROFILE%\.objeck\hub)
  models--Xenova--distilbert-base-uncased-finetuned-sst-2-english/
    refs/main                       → 0b6928efcb76139cae2c6881d49cda67fe119f42
    snapshots/0b6928ef…/config.json
    snapshots/0b6928ef…/tokenizer.json
    snapshots/0b6928ef…/onnx/model_quantized.onnx
    .incomplete/…                   (resumable partial downloads)
    manifest.json                   (per file: size, sha256 or git-sha1, URL, time)
```

- **The same naming as Python's `huggingface_hub` cache**, but snapshots hold **real files, not
  symlinks**.
  - Python falls back to copies on Windows without developer mode anyway.
  - Symlinks are a known trap in this repository's Windows checkouts.
  - The cost: no de-duplication across revisions. Acceptable for a model cache.
- **Python's cache is read, never written.** With `OBJECK_HUB_CACHE_PYTHON=1`, or when offline, a
  lookup that misses checks `$HF_HUB_CACHE` (or `~/.cache/huggingface/hub`). Models a user already
  downloaded with Python then work offline from Objeck. Decision D3.
- **Integrity:**
  - every file is hashed while it streams, and moved into `snapshots/` only on a match with
    `X-Linked-Etag`;
  - a mismatch deletes the partial file and fails with both hashes.
- **Offline:**
  - `OBJECK_HUB_OFFLINE=1`, or `HF_HUB_OFFLINE=1`, never touches the network;
  - revisions resolve through `refs/`;
  - a missing file fails with the exact command or URL that would fetch it.
- **Pinning:** `Hub->Download(repo, path, revision)`. A commit hash is immutable and skips the
  `refs` lookup. Programs and tests should pin.
- **Tokens:**
  - read from `HF_TOKEN`, or from `OBJECK_HUB_TOKEN`;
  - sent only to `huggingface.co`, never logged.
- **Variant choice:**
  - **default `model.onnx` (fp32)**, for parity with PyTorch;
  - `Variant->QUANTIZED` picks `model_quantized.onnx`, about 4× smaller (111 versus 438 MB for
    bert-base);
  - fp16, q4 and bnb4 are refused by name until a phase tests them. Decision D4.

```objeck
use System.AI.Hub;

hub := HubClient->New();                                  # cache, token and offline mode from the environment
dir := hub->Snapshot("Xenova/distilbert-base-uncased-finetuned-sst-2-english",
                     ["config.json", "tokenizer.json", "tokenizer_config.json", "onnx/model.onnx"]);
if(dir = Nil) { hub->GetError()->ErrorLine(); return; };
```

### 7.4 Tokenizers (`System.NLP`, `nlp.obl`)

**What real `tokenizer.json` files contain** (measured, from the downloaded files):

| Model | `model` | normalizer | pre_tokenizer | post_processor | decoder |
|---|---|---|---|---|---|
| BERT / DistilBERT / MiniLM / NER / QA | WordPiece (`##`, max 100 chars), 28,996–30,522 tokens | `BertNormalizer` (`lowercase` true or false, `strip_accents` null) | `BertPreTokenizer` | `TemplateProcessing` | WordPiece |
| RoBERTa / DistilRoBERTa | BPE, 50,265 tokens, 50,000 merges | none | `ByteLevel` (`add_prefix_space` false, `use_regex` true) | `RobertaProcessing` | ByteLevel |
| DeBERTa-v3 (nli-small, mxbai) | Unigram, 128,000 tokens | `Sequence[Strip, Precompiled, Replace]` | `Metaspace` (`▁`, `prepend_scheme` "always") | `TemplateProcessing` | Metaspace |
| XLM-R | Unigram, 250,002 tokens | `Precompiled` | `Sequence[WhitespaceSplit, Metaspace]` | `TemplateProcessing` | Metaspace |

Some files also carry `truncation` and `padding`: all-MiniLM-L6-v2 pads to a fixed 128, and
mxbai truncates at 500. Those are defaults the loader honours and the caller can override.

**Design.** `TokenizerJson->Load(path)` builds a pipeline object from the four stages. Each stage
is a small class behind an interface, so new component types are added without touching the
others:

- **Normalizers:**
  - `BertNormalizer` (the existing WordPiece text cleaning, moved behind the interface);
  - `Lowercase`, `Strip`, `Replace`, `NFC`/`NFKC`, `Sequence`;
  - **`Precompiled`**: SentencePiece's normalisation table, a base64 blob of 316,720 characters
    in both the DeBERTa and XLM-R files. It holds a double-array trie and its replacement strings.
    Objeck parses the blob itself, about 200 lines. Its parity is tested on a full sweep of the
    Unicode planes, not only on sample text.
- **Pre-tokenizers:**
  - `BertPreTokenizer`, `WhitespaceSplit`, `Metaspace`, `Sequence`;
  - **`ByteLevel`**: the GPT-2 split pattern, letters `\p{L}+`, numbers `\p{N}+`, contractions,
    and space handling. It is written as a hand-coded scanner, not as a regex, so it does not
    depend on the regex library's `\p{…}` support. `WordPieceUnicode` already has generated
    punctuation, control, mark and case tables. A4 adds letter (`L*`) and number (`N*`) category
    tables generated the same way. The bytes→unicode map is GPT-2's fixed table.
- **Models:**
  - `WordPiece` (the existing algorithm);
  - **`BPE`**: merge ranks, a per-word cache, and `continuing_subword_prefix` and `byte_fallback`
    handling;
  - **`Unigram`**: Viterbi over the lattice of vocabulary pieces with their log-probabilities, and
    the `unk` score and `fuse_unk`.
- **Post-processors:** `TemplateProcessing` (single and pair templates, special-token ids),
  `RobertaProcessing`, `BertProcessing`.
- **Decoders:** WordPiece, ByteLevel and Metaspace. Fill-mask needs them to print the token.

**`TokenEncoding` gains** offsets (character start and end per token), word ids, sequence ids
(0, 1, or none for specials), and overflow windows. NER and QA need the offsets to return
character spans. New fields and methods are additive.

**Loading cost (measured):** `Data.JSON.JsonParser` on the real files, under the interpreter, with
the machine otherwise idle:

| File | Size | Parse time |
|---|---|---|
| DistilBERT `tokenizer.json` | 0.71 MB | 0.62 s |
| DistilRoBERTa | 2.0 MB | 0.28 s |
| XLM-R | 15.9 MB | 2.7 s |
| **DeBERTa-v3 (nli-small)** | **8.5 MB** | **15.8 s** |

- **The DeBERTa time is an anomaly.** The file is half XLM-R's size and holds the same 316,720-char
  charsmap string, yet takes 6× as long.
- **Profile it in A5 before designing around it:** run the sampling profiler first, per the
  project's rule. If it is the JSON parser, fix the parser. A binary sidecar cache of the
  vocabulary is the fallback only if the parser cannot be made fast enough.

**Parity tests:**

- **Fixtures:**
  - *small, checked in:* a WordPiece, a BPE and a Unigram `tokenizer.json`, each trained with
    Python `tokenizers` on this repository's `docs/*.md` corpus, with vocabularies of about 2,000
    (roughly 100 KB each);
  - *expected outputs:* ids, tokens, offsets and word ids for the existing parity corpus plus
    multilingual and edge-case lines, as in `programs/regression/data/bert/gen_wordpiece_parity.py`.
- **The test is byte-exact.** Every id and every offset must match, with no tolerance.
- **Optional network job:** the same corpus through the *real* `tokenizer.json` of each model in
  §7.1, against ids stored in a small fixture. It is non-gating, like the other network tests.

### 7.5 Task pipelines (`System.NLP.Onnx`, `nlp_onnx.obl`)

**Shared base.** `TransformerTask` handles everything every task needs:

- it opens a model folder (a local path or a hub snapshot): `config.json`, the tokenizer and the
  ONNX file;
- it reads the session's inputs and outputs, sending only declared inputs, as §7.2 requires;
- it batches, sorting texts by length and padding each batch to its longest text. Truncating to the
  model's `max_position_embeddings` is the default;
- it chooses the execution provider, with CPU fallback, as `BertEmbedder` does.

```objeck
use System.NLP.Onnx;

classifier := TextClassifier->New("Xenova/distilbert-base-uncased-finetuned-sst-2-english");  # a hub id or a folder
results := classifier->Classify(["I loved it", "Not worth the ticket"]);     # Prediction[]: label, score
results[0]->GetLabel()->PrintLine();                                          # POSITIVE

ner := TokenClassifier->New("Xenova/bert-base-NER", TokenClassifier->Aggregation->SIMPLE);
each(e : ner->Extract("Ada Lovelace worked with Charles Babbage in London.")) {
  "{$e->GetGroup()} '{$e->GetWord()}' [{$e->GetStart()}, {$e->GetEnd()}) {$e->GetScore()}"->PrintLine();
};

qa := QuestionAnswerer->New("Xenova/distilbert-base-cased-distilled-squad");
answer := qa->Answer("Where did she work?", long_context);     # Answer: text, score, start, end

mask := FillMask->New("Xenova/bert-base-uncased");
fills := mask->Fill("Paris is the [MASK] of France.", 5);      # MaskFill[]: token, text, score, sequence

embedder := SentenceEmbedder->New("sentence-transformers/all-MiniLM-L6-v2");   # reads modules.json / 1_Pooling
sims := embedder->Similarity(["a cat sits"], ["a kitten is sitting", "stock prices fell"]);

reranker := CrossEncoder->New("cross-encoder/ms-marco-MiniLM-L6-v2");
ranked := reranker->Rank("what is BERT?", passages);         # scored pairs, best first

classify := Pipeline->Create("text-classification", "Xenova/toxic-bert");   # Python pipeline() spelling
```

**How each task reads the model, matching `transformers` behaviour:**

- **Text classification.**
  - Labels come from `config.json` `id2label`; `LABEL_n` when absent.
  - The score uses **softmax**, or **sigmoid** when `problem_type` is `multi_label_classification`
    or there is a single label, which is what `transformers`' `function_to_apply` does by default.
  - `top_k` returns all labels sorted.
- **Token classification.**
  - Per-token softmax over `logits [batch, sequence, labels]`.
  - The aggregation strategies are `NONE`, `SIMPLE`, `FIRST`, `AVERAGE` and `MAX`, with the same
    sub-word and B-/I- merging rules as `transformers`' `TokenClassificationPipeline`.
  - Character spans come from the offsets.
  - The default is `NONE`, as in Python.
- **Question answering.**
  - Pairs are encoded question-first with `only_second` truncation, and `p_mask` excludes the
    question and the specials.
  - Answers are spans with start ≤ end and length ≤ `max_answer_len` (15).
  - The score is the product of the start and end probabilities.
  - `handle_impossible_answer` is supported for SQuAD 2 models.
- **Fill-mask.**
  - Softmax over the vocabulary at each mask position, `top_k` (5), and the decoder for the token
    text.
  - The output is `[batch, sequence, vocabulary]`, so about 15.6 MB of floats per text at length
    128 for BERT. This is the one task where batching is capped by memory.
- **Sentence similarity.**
  - The pooling mode comes from sentence-transformers' `modules.json` and `1_Pooling/config.json`
    (mean, CLS or max), and normalisation from `2_Normalize`.
  - Similarity is cosine.
  - `BertEmbedder` stays as is for existing callers.
- **Cross-encoder reranking.** Pair encoding, then the model's raw logit, with the activation that
  the model's sentence-transformers config names. The parity fixture pins the activation used for
  each model rather than assuming it.
- **Zero-shot** (stretch). The NLI-template method of `transformers`' zero-shot pipeline, over any
  NLI classifier (DeBERTa-v3 NLI).

**Long text:**

- **Truncation** is the default for classification and similarity, with a flag on the result:
  `TokenEncoding` already records `truncated`.
- **Striding:** NER and QA split long input into overlapping windows (`max_length`,
  `stride`/`doc_stride`).
  - NER keeps, for each token, the prediction from the window where the token is furthest from an
    edge, then aggregates.
  - QA takes the best span over all windows.
  - This matches `transformers` when `return_overflowing_tokens` is on.

**Runner interface.** Each task calls an `EncoderRunner`:

```objeck
interface EncoderRunner {
  method : virtual : public : Inputs() ~ String[];
  method : virtual : public : Run(inputs : Vector<API.Models.Tensor>, outputs : String[]) ~ Vector<API.Models.Tensor>;
}
```

`OnnxRunner` is the only implementation in Phase A. Phase B adds `TorchRunner` in its own library,
so a model defined and fine-tuned in Objeck runs through the same `TextClassifier` (§8.1).

### 7.6 Parity tests for the pipelines

Two tiers, following `programs/regression/data/bert/`.

**Tier 1: gating, offline, small checked-in fixtures.**

- **One tiny real-architecture model per task.**
  - It is generated by a script with `transformers` from a scaled-down config: hidden 32, 2 layers,
    2 heads.
  - Its weights are random but fixed by seed, and it is exported to ONNX once.
  - Each is 100–600 KB.
  - It uses the small tokenizers of §7.4.
- **The script also writes the expected outputs.** It runs **PyTorch**, the oracle, and **Python
  ONNX Runtime** on the same file.
- **Tolerances:**

  | Quantity | Against Python ONNX Runtime | Against PyTorch |
  |---|---|---|
  | Logits | ≤ 1e-5 | ≤ 1e-4 |
  | Probabilities | ≤ 1e-6 | — |
  | Labels, entity spans, answer spans, top-k orders | exact | — |
  | Mean-pooled embeddings | ≤ 1e-5 | — |

  `BertEmbedder` already matches Python to within 6e-7 on MiniLM.
- **Like the current tests,** each fixture also includes a case the wrong code would fail:
  - DistilBERT's missing `token_type_ids`;
  - a padded batch;
  - a text longer than the window.

**Tier 2: optional, network.**

- Download the real quantized models of §7.1, about 650 MB in total, pinned by commit.
- Compare against stored Python outputs (a few KB each) for fixed inputs, with the same tolerances.
- Non-gating, and cached in Actions under its own key. This repository has hit the Actions cache
  ceiling before, so the job's budget is stated in the workflow.

## 8. Phase B — the training foundation (libtorch)

### 8.1 Why libtorch, compared fairly

ONNX Runtime's training API is excluded (§3.2). The remaining credible, maintained routes:

| | **libtorch 2.15** | **candle** (Rust, Hugging Face) | **burn** (Rust) |
|---|---|---|---|
| Maintenance (primary evidence) | releases 2026-09-02, 09-30, 10-28 | pushed 2026-10-29; tags only, latest `0.11.0`; **pre-1.0** | `v0.23.0-pre.1` on 2026-10-29; **pre-1.0** |
| Platforms (prebuilt) | Windows x64 and arm64 (CPU zips), Linux x64 (zip), Linux arm64 (from the wheel), macOS arm64 (zip) | built from source by us on every platform | built from source by us |
| GPU | CUDA 13.2 / 13.4 (Windows x64 and Linux x64 as zips; Linux arm64 as wheels), ROCm (Linux x64), Intel XPU (Windows and Linux x64), MPS (macOS: to be confirmed for the C++ zip in B0) | CUDA, Metal | CUDA, ROCm, Metal, wgpu (Vulkan/DX12/Metal) |
| Is it the PyTorch engine? | **yes**: the same kernels and autograd, so parity is natural | no | no (it has a LibTorch backend through `tch`) |
| Runtime size | large: §8.4 | small (no measurement here) | small (no measurement here) |
| Python needed | **no** | no | no |
| Binding effort | C++ shim, linked against libtorch headers, in the same pattern as `libobjk_onnx` | a Rust `cdylib` exposing a C ABI, which adds a Rust toolchain to every platform build | the same as candle |

**Recommendation: libtorch.**

- **It is the only option that is both the reference engine and released on a fixed, frequent
  cadence**, with prebuilt binaries for all five platforms.
- **Parity with PyTorch is by construction** rather than by testing alone.
- **Its one real cost is size (§8.4).** That is solved by packaging, not by choosing a lesser
  engine.
- **Next durable option, if B0 finds a blocker** such as a load conflict, a size that is
  unacceptable, or a license issue: **candle**. It is maintained, Apache-2.0, small, has training
  (backprop and AdamW), and already has BERT, DistilBERT and XLM-R implementations to cross-check
  against. Its costs are a pre-1.0 API and a Rust toolchain in the build.

### 8.2 The native shim (`libobjk_torch`)

- **libtorch has no general C API, so Objeck gets a C++ shim.** It is built per platform against
  that platform's libtorch, exactly as `core/lib/onnx` builds `libobjk_onnx` against ONNX Runtime.
- **The shim exposes the Objeck native-function convention:**
  - tensors are opaque `int64` handles in a handle table;
  - every function returns a status, with the libtorch exception message on failure.
- **Its surface, grown by phase, starting with what BERT needs:**
  - creation: `zeros`, `ones`, `randn`, `arange`, `from_blob`, `from_floats`;
  - dtype and device moves;
  - shape and views;
  - pointwise and reduction math;
  - `matmul`/`bmm`;
  - `softmax`, `log_softmax`, `gelu`, `tanh`, `relu`;
  - `layer_norm`, `embedding`, `dropout`;
  - `cross_entropy`, `binary_cross_entropy_with_logits`;
  - `scaled_dot_product_attention`;
  - autograd: `backward`, `grad`, `requires_grad`, `no_grad`;
  - `AdamW` / `SGD` steps over a parameter list;
  - a seeded RNG;
  - live-handle counts.
- **ABI choice (decision D8).**
  - **Option 1, recommended:** build against the regular C++ API, pinned to one libtorch version
    per Objeck release, and rebuild the shim on each upgrade, the way the ONNX binding pins its
    runtime.
  - **Option 2:** PyTorch's new **Stable ABI** (`torch::stable`, "binary-compatible … across
    PyTorch versions"). It would let users drop in a newer libtorch, CUDA builds included, without
    rebuilding. B0 checks whether its op set covers the surface above. Its documented purpose is
    operator extensions, so the default assumption is that it does not yet cover autograd and
    optimizers.
- **Windows:** MSVC, `/MD`, matching libtorch's CRT.
- **Loading:** the shim is loaded the way the ONNX library is loaded. B0 checks that
  `torch_cpu.dll` and `onnxruntime.dll` coexist in one Objeck process; both bring OpenMP or
  thread-pool runtimes.

### 8.3 Architectures in Objeck, weights from `safetensors`

`System.AI.NN` provides the following building blocks, each with `Forward` and named parameters
in Hugging Face's naming:

- `Module`, `Parameter`;
- `Linear`, `Embedding`, `LayerNorm`, `RMSNorm`, `Dropout`;
- `MultiHeadAttention`, with an optional causal mask and an optional KV cache;
- `Conv1d`, `Conv2d`, `GELU`, `Sequential`.

Because the parameter names match, `module->LoadState(SafeTensors->Read(path))` maps one-to-one
onto a Hugging Face checkpoint.

**BERT, as it is written in `System.AI.Transformers`** (abridged):

```objeck
class BertLayer from Module {
  @attention : MultiHeadAttention;    # attention.self.{query,key,value}, attention.output.dense
  @attn_norm : LayerNorm;             # attention.output.LayerNorm
  @intermediate : Linear;             # intermediate.dense
  @output : Linear;                   # output.dense
  @out_norm : LayerNorm;              # output.LayerNorm
  @dropout : Dropout;

  method : public : Forward(x : Tensor, mask : Tensor) ~ Tensor {
    a := @attn_norm->Forward(x->Add(@dropout->Forward(@attention->Forward(x, mask))));
    h := @intermediate->Forward(a)->Gelu();
    return @out_norm->Forward(a->Add(@dropout->Forward(@output->Forward(h))));
  }
}

class BertForSequenceClassification from Module {
  @bert : BertModel;                  # embeddings + encoder.layer.N + pooler
  @classifier : Linear;

  function : FromPretrained(dir : String) ~ BertForSequenceClassification {
    config := ModelConfig->Read(dir + "/config.json");            # hidden_size, num_attention_heads, id2label, …
    model := BertForSequenceClassification->New(config);
    model->LoadState(SafeTensors->Read(dir + "/model.safetensors"));   # strict: missing or unexpected names fail
    return model;
  }

  method : public : Forward(ids : Tensor, mask : Tensor, types : Tensor) ~ Tensor {
    return @classifier->Forward(@bert->Forward(ids, mask, types)->GetPooled());
  }
}
```

The other two worked examples, abridged, show the same layers serving other models:

```objeck
# A small GPT-style decoder: the same Embedding, Linear and attention, with a causal mask and a KV cache.
class GptBlock from Module {
  method : public : Forward(x : Tensor, cache : KvCache) ~ Tensor {
    h := x->Add(@attn->Forward(@ln1->Forward(x), true, cache));    # causal = true; the cache is updated in place
    return h->Add(@mlp->Forward(@ln2->Forward(h)));
  }
}
next := Sampler->TopP(gpt->Forward(last_token, cache)->Select(1, -1), 0.9, 0.8, rng);

# A ViT: Conv2d patch embedding + the same encoder layers + a CLS head; images from OpenCV with no Objeck-side loop.
patches := @patch_embed->Forward(Tensor->FromImage(mat)->Div(255.0))->Flatten(2)->Transpose(1, 2);
logits := @head->Forward(@encoder->Forward(@cls->Concat(patches, 1)->Add(@pos))->Select(1, 0));
```

**Weights and Python-freedom (measured):**

- `model.safetensors` is present in `google-bert/bert-base-uncased`,
  `distilbert/distilbert-base-uncased`, `sentence-transformers/all-MiniLM-L6-v2`,
  `FacebookAI/roberta-base`, `FacebookAI/xlm-roberta-base`, `dslim/bert-base-NER` and
  `deepset/roberta-base-squad2`.
- `microsoft/deberta-v3-base` has only `pytorch_model.bin` (§3.3).
- With the hub cache (§7.3), `config.json`, `tokenizer.json` and the Objeck architectures, a model
  goes from the Hub to fine-tuning **without Python**.

**Saving.**

- `model->Save(dir)` writes `config.json`, `model.safetensors` and `tokenizer.json` in Hugging
  Face's layout. A Python user can therefore `from_pretrained(dir)` an Objeck fine-tune. That is
  interop both ways, and it is also how parity is checked.
- Inference on the fine-tuned model runs through `TorchRunner` in the same pipeline classes (§7.5).
- ONNX export comes later (Phase E, §9).

### 8.4 Size and packaging (measured)

Runtime libraries in the official 2.15.0 archives, uncompressed. They were read from each zip's
central directory over HTTP ranges, so nothing was downloaded in full:

| Platform | Archive (compressed) | Runtime needed, uncompressed | Largest file |
|---|---|---|---|
| Windows x64 CPU | 204 MB | ≈ 313 MB (the archive has each DLL twice, in `lib/` and `bin/`; `torch_python.dll` is not needed) | `torch_cpu.dll` 309.5 MB |
| Windows arm64 CPU | 109 MB | ≈ 156 MB | `torch_cpu.dll` 116.2 MB, `armpl_lp64.dll` 37.8 MB |
| Linux x64 CPU | 129 MB | ≈ 448 MB | `libtorch_cpu.so` 439.9 MB |
| Linux arm64 CPU (wheel) | 171 MB wheel | ≈ 316 MB (without `libtorch_python.so`) | `libtorch_cpu.so` 265.7 MB, OpenBLAS 24.6 MB, Arm Compute 19 MB |
| macOS arm64 | 114 MB | ≈ 424 MB | `libtorch_cpu.dylib` 421.8 MB |
| CUDA 13.2 (Windows / Linux x64) | 4,167 / 1,965 MB | — | — |
| ROCm (Linux x64) | about 1,965 MB | — | — |

**Today's Objeck release assets (v2026.10.1)** are 20–48 MB per platform, so libtorch cannot go
into the default download.

**Recommendation, decision D5:**

- **A separate, optional "Objeck AI runtime" archive per platform, CPU only.** It holds the shim
  plus the libtorch runtime libraries. The upstream archives are 109–204 MB compressed, and the
  AI runtime archive drops their duplicates and Python libraries. It is built and checksummed
  by the release workflow.
- **It installs over a deploy tree**, and `obc`/`obr` find it the way they find the ONNX runtime.
- **GPU:**
  - the user installs the official CUDA, ROCm or XPU libtorch (2–4 GB) and sets
    `OBJECK_LIBTORCH` to it;
  - the shim is version-locked, so it checks the libtorch version at load and refuses a mismatch
    by name.
  - **A GPU build of the shim per CUDA major version** is decided in B5, after B0 has shown whether
    one shim binary works across CPU and CUDA libtorch builds of the same version.
- **The release-pipeline costs:**
  - one more asset per platform, with its `SHA256SUMS` line and Windows signing;
  - Actions cache space for the libtorch archives, which must stay well under the cache ceiling
    this repository has hit before.

### 8.5 Training time (measured)

PyTorch 2.15.0 CPU, from Python. The Python layer is a thin wrapper here; the kernels and autograd
are libtorch's, so a C++ shim should land close to these numbers. B0 checks that.

- **Hardware:** AMD Ryzen AI 9 HX 370, 12 cores, 12 intra-op threads, 31 GB.
- **Model:** `*ForSequenceClassification` built from configs with random weights; cost does not
  depend on the weights.
- **Step:** sequence length 128, AdamW, a full fine-tuning step (forward, backward, optimizer).
- **Runs:** 2 warm-up steps and 5 timed.
- **Caveat:** a single-threaded Objeck JSON benchmark overlapped the first minute of the MiniLM
  run.

| Model | Params | Train step, batch 32 | per sequence | batch 16 per sequence | Inference per sequence | Train ÷ inference |
|---|---|---|---|---|---|---|
| MiniLM-L6-H384 | 22.7 M | 1,970 ms | **62 ms** | 63 ms | 10 ms | 6.1× |
| MiniLM-L12-H384 | 33.4 M | 3,964 ms | **124 ms** | 139 ms | 16 ms | 7.6× |
| DistilBERT-base | 65.8 M | 4,967 ms | **155 ms** | 130 ms | 23 ms | 6.7× |
| BERT-base | 109.5 M | 7,763 ms | **243 ms** | 274 ms | 49 ms | 4.9× |

**These are slower than #1449's estimates.** That design assumed 3–4× inference and predicted
21–28 ms per sequence for MiniLM-L6 and 150–200 ms for bert-base. The measured ratios are 5–8×,
because small models are dominated by the optimizer and kernel overhead.

**For the lab's grid** (5,000 training rows, 3 epochs, about 15 runs at length 128, the
experiment count from #1449 §5), on this CPU:

| Model | Per run | Whole grid |
|---|---|---|
| MiniLM-L6 | about 16 min | about 4 h |
| DistilBERT | 33–39 min | 8–10 h |
| bert-base | about 61 min | about 15 h |

That is why GPU support matters. The lab itself used Colab's GPU, which was not measured here.
The example defaults to MiniLM-L6 or DistilBERT on CPU, prints its estimate before starting, and
takes `--max-train`.

### 8.6 Training API

```objeck
use System.AI.Tensor;
use System.AI.Transformers;
use System.AI.Optim;
use System.AI.Data;

model := DistilBertForSequenceClassification->FromPretrained(hub->Snapshot("distilbert/distilbert-base-cased"), 2);
model->To(Device->Best());                                    # CUDA / MPS / XPU when present, else CPU

tokenizer := TokenizerJson->Load(dir + "/tokenizer.json");
train := TextDataset->New(texts, labels, tokenizer, 128);     # pads to MAX_SEQ_LENGTH
loader := DataLoader->New(train, 16, true, Generator->New(42));   # batch, shuffle, seed

optimizer := AdamW->New(model->Parameters(), 2e-5, 0.01);     # lr, weight decay; no decay on bias / LayerNorm
schedule := LinearSchedule->New(optimizer, warmup, total);

trainer := Trainer->New(model, optimizer, schedule);
trainer->SetClassWeights([1.0, 99.0]);                        # weighted cross-entropy for the lab's POSRATIO runs
trainer->OnStep(\(StepInfo) ~ Nil : (s) => { s->GetLoss()->PrintLine(); });
trainer->Fit(loader, 3);                                      # epochs

model->Save("malware_distilbert");                            # config.json + model.safetensors + tokenizer.json
classifier := TextClassifier->New(TorchRunner->New(model), tokenizer);   # the Phase A API, the torch backend
```

`FineTuner` is a convenience over these calls, and implements `ScoreModel`.

## 9. ONNX as the export target (Phase E)

Every op an Objeck model runs goes through the tensor API, so the API can **record** a forward
pass and write the ONNX graph:

- `OnnxExporter->Export(model, example_inputs, path)`;
- dynamic batch and sequence axes;
- **each op in the shim's surface maps to one or a few standard ONNX ops**;
- the protobuf is written in Objeck;
- the weights become initializers.

A model fine-tuned in Objeck can then run in `API.Onnx`, on any execution provider ONNX Runtime
has, including the NPUs and GPUs libtorch does not reach (§3.3). That narrows the GPU gaps for
*inference*.

**The test:** export, then run in Objeck's ONNX session, in Python ONNX Runtime and against the
torch-backend logits, within 1e-5.

## 10. The lab, end to end

The user's Lab 1 Part 4 fine-tunes `distilbert-base-cased` as a binary malware classifier over
API-call sequences on Colab: `MAX_SEQ_LENGTH` 128, AdamW, linear warm-up.
`part4_bert_malware.py` evaluates in PyTorch and **does not save the model** (no
`save_pretrained` call).

### 10.1 First target: run the Colab model from Objeck (Phase A1)

1. **In the notebook** (the user's code, Python, once):
   - give the model real labels with `model.config.id2label = {0: "benign", 1: "malware"}`;
   - save it with `model.save_pretrained(out)` and `tokenizer.save_pretrained(out)`;
   - export it with `optimum-cli export onnx --model out --task text-classification out/onnx`.
2. **What the export will look like,** going by the measured DistilBERT classifier signature
   (§7.2):
   - inputs `input_ids` and `attention_mask`, INT64 `[batch, sequence]`;
   - output `logits` FP32 `[batch, 2]`;
   - a cased WordPiece tokenizer (`lowercase` false).
3. **In Objeck:**

   ```objeck
   classifier := TextClassifier->New("malware_distilbert");
   preds := classifier->Classify(test_sequences);
   ```

   Then compute precision, recall and F1 with `System.ML`'s existing metrics.
4. **The check:**
   - Objeck's malware probability matches Python's softmax for the lab's fixed test set within
     1e-5 (ONNX against ONNX) and 1e-4 (against PyTorch);
   - the predicted classes are identical, so the metrics are identical.

### 10.2 Second target: fine-tune it in Objeck (Phase B2)

- The same experiment runs with `Trainer` (§8.6): POSRATIO, unweighted and weighted, then
  `MAX_SEQ_LENGTH`, then learning rate.
- On CPU it takes 33–39 minutes per run for DistilBERT at 5,000 rows (§8.5).
- **The check:** the loss trajectory over the first 20 steps matches PyTorch within 1e-4 relative,
  with the same initial `safetensors`, batch order and seed, dropout off for the parity run, and
  one thread.
- Then the final metrics are within run-to-run noise of the Python lab.

## 11. Phases

Sizes are engineering days for one person, including tests and docs. They are estimates, except
where an earlier phase measures them.

**Phase A: use models (no new native dependency)**

| Phase | Content | Size |
|---|---|---|
| **A1 (first PR)** | `TextClassifier` over a **local folder**: `config.json` labels and `problem_type`; WordPiece from `tokenizer.json`'s WordPiece section or from `vocab.txt` with `tokenizer_config.json`; session-declared inputs; batching. A checked-in tiny DistilBERT-shaped classifier, with a BERT-shaped variant that has `token_type_ids`; parity against Python. Works for the lab's model | 3–4 |
| A2 | Streaming `GetToFile` with resume; the cross-origin `Authorization` strip (security); an incremental SHA-256; `HubClient` and cache, offline mode, pinning, tokens; `TextClassifier->New(hub_id)` | 6–8 |
| A3 | The `TokenizerJson` pipeline (component interfaces, `TemplateProcessing`, decoders); offsets, word ids and windows on `TokenEncoding` | 4–5 |
| A4 | ByteLevel BPE (RoBERTa), with byte-exact parity | 4–5 |
| A5 | Unigram, `Precompiled`, Metaspace (DeBERTa-v3, XLM-R), with byte-exact parity; profile and fix the 15.8 s JSON load | 6–9 |
| A6 | `TokenClassifier` (aggregations, striding) and `QuestionAnswerer` (striding, impossible answers) | 6–8 |
| A7 | `FillMask`, `SentenceEmbedder` (sentence-transformers configs), `CrossEncoder`, zero-shot, `Pipeline->Create` | 4–6 |
| A8 | The Tier 2 network parity job; `docs/AI.md` and examples; `objk_apis.json` | 3 |
| **Total** | | **36–48** |

**Phase B: the training foundation**

| Phase | Content | Size |
|---|---|---|
| **B0 (spike, no Objeck code)** | A C++ program against libtorch 2.15 CPU on Windows x64, then Linux x64 and macOS arm64. It loads bert-base and DistilBERT `safetensors`, writes their forward pass with libtorch ops (mirroring §8.3), runs 20 AdamW steps and compares losses with PyTorch (≤ 1e-4). It also measures: step time against §8.5; coexistence with `onnxruntime.dll` in one process; the per-call overhead through an Objeck native call; whether the Stable ABI covers autograd and optimizers; and whether the macOS zip has MPS | 3–4 |
| B1 | `libobjk_torch` + `System.AI.Tensor`: handles, dtypes, devices, views, math, autograd, `Scope`, zero-copy views of `Float[]`/`Int[]`, live-handle accounting; builds for 5 platforms | 10–14 |
| B2 | `System.AI.NN`, `System.AI.Optim`, `System.AI.Data`; `SafeTensors` read and write; `ModelConfig`; BERT and DistilBERT with classification heads; `Trainer`; the lab `--finetune`; training parity (§10.2) | 10–14 |
| B3 | `TorchRunner`: Phase A pipelines on torch models; `Save` in Hugging Face layout | 3–4 |
| B4 | RoBERTa, XLM-R, DeBERTa-v2 (disentangled attention is the hardest one); token-classification and QA heads | 6–9 |
| B5 | Packaging: the optional AI runtime archive per platform, the release workflow, `OBJECK_LIBTORCH` for GPU builds, version checks | 5–7 |
| **Total** | | **37–52** |

**Later phases, each its own design when it starts**

| Phase | Content |
|---|---|
| C | GPT-style decoder with a KV cache, sampling and BPE; LoRA; int8 weights |
| D | ViT and CNN blocks (Conv2d, BatchNorm), image datasets over OpenCV, `FromImage` |
| E | ONNX export by tracing (§9) |
| F | Deep RL (DQN, PPO) beside `ai_rl.obs`; an optional `DenseNetwork` backend on the core |
| G | Audio front-end (STFT/mel, WAV dataset) |

**Ordering.**

- A1 is first. B0 runs in parallel: it is a spike with no Objeck code.
- A2–A8 and B1–B5 are independent tracks until B3, which joins them.
- The lab's first target needs only A1. Its second target needs B2.

## 12. Decisions for the user

| # | Decision | Recommendation |
|---|---|---|
| D1 | PR #1449 (ORT training design) | **Close it as superseded by this document.** Its lab mapping and class-weight loss design are carried over here (§8.6, §10.2). Its ORT-specific parts violate the no-deprecated rule. Also delete its `probe/ort-training-api` branch |
| D2 | Fine-tuning engine | **libtorch**, with candle as the documented next option if B0 finds a blocker (§8.1) |
| D3 | Hub cache location | **Objeck's own `~/.objeck/hub`** (`OBJECK_HUB_CACHE`), in the same layout with real files; Python's cache **read, never written** |
| D4 | Default ONNX variant | **fp32 `model.onnx`** for parity; quantized as an explicit option; fp16/q4/bnb4 refused until tested |
| D5 | libtorch delivery | **An optional CPU AI-runtime archive per platform** from the release workflow. GPU builds are user-installed with `OBJECK_LIBTORCH` |
| D6 | Binding granularity | **A fine-grained tensor API**, with architectures in Objeck (Python-free, as the goal says). B0 measures call overhead, and fused ops are added only where measured |
| D7 | Bundle names | **`System.AI.Tensor`, `.NN`, `.Optim`, `.Data`, `.Hub`, `.Transformers`**, extending the existing `System.AI` family. Text pipelines **stay in `System.NLP.Onnx`**, so existing programs and docs are unchanged |
| D8 | Shim ABI | **Regular C++ API, pinned per release.** Revisit the Stable ABI after B0 |
| L1 | Operator overloading (a language change) | **Later, its own design**, once real model code exists. Not a prerequisite |
| L2 | Finalizers or a `using` block for native resources (a VM change) | **Its own design, after B1.** `Scope` covers the need until then |
| — | The lab notebook adding `save_pretrained` + export (§10.1) | **Yes.** It is the user's notebook, so the user's call |

## 13. First PR, and follow-ups

**First PR: A1, `TextClassifier` over a local folder.**

- **Files:**
  - `core/compiler/lib_src/nlp_onnx.obs`: `TextClassifier`, `Prediction`, and the `EncoderRunner`
    interface with `OnnxRunner`;
  - `core/compiler/lib_src/nlp.obs`: WordPiece configuration read from `tokenizer.json` /
    `tokenizer_config.json`;
  - rebuilt `nlp.obl` and `nlp_onnx.obl`;
  - `objk_apis.json`.
- **Fixtures,** in `programs/regression/data/bert/`:
  - `gen_tiny_classifier.py`, which writes a DistilBERT-shaped and a BERT-shaped classifier (a few
    KB each, closed-form weights like `gen_tiny_bert.py`, so expected logits are exact);
  - `config.json` with `id2label`;
  - `tokenizer.json`;
  - expected outputs.
- **Test:** `programs/regression/nlp_onnx_text_classifier.obs`, covering:
  - labels;
  - softmax against sigmoid;
  - `top_k`;
  - an export without `token_type_ids`;
  - a padded batch, where the result must not depend on the batch;
  - truncation;
  - a folder with only `vocab.txt`.
- **It is done when:**
  - the test passes on every CI leg;
  - existing `nlp_onnx` and ONNX tests are unchanged;
  - an optional local run against the lab's exported model reproduces Python's predictions
    (§10.1).

**Follow-ups outside this design:**

- **DirectML.** Objeck's Windows ONNX GPU path depends on DirectML, which its README says is in
  maintenance mode (§3.3). It needs its own design for a maintained Windows execution provider.
- **The `HttpsClient` header leak.** Its `Authorization` forwarding on cross-origin redirects
  (§7.3) is a security fix worth landing on its own, before A2, because any current caller that
  sets `Authorization` and hits a redirect is affected.

## Appendix: how the numbers were measured

All on 2026-10-29, on the Windows build box (AMD Ryzen AI 9 HX 370, 12 cores, 31 GB).

- **Hub counts and files:**
  - `GET https://huggingface.co/api/models?filter=onnx&pipeline_tag=…`, paged with `Link`, and
    `?author=Xenova` and `?author=onnx-community`;
  - per repo, `/api/models/{repo}?blobs=true` (sizes, LFS SHA-256) and `config.json`.
- **Redirect behaviour:** raw `HEAD` requests with `http.client`, without following redirects, on
  `Xenova/distilbert-base-uncased-finetuned-sst-2-english`: `config.json` and `tokenizer.json`
  (307, relative), and `onnx/model_quantized.onnx` (302, CDN).
- **Signatures and tokenizer components:**
  - the export files were downloaded, the quantized variants where they exist, since the signature
    does not depend on the variant;
  - each was read with `onnx` 1.23.1 (`load_external_data=False`), listing graph inputs that are
    not initializers;
  - each `tokenizer.json` was parsed with Python's `json`.
- **JSON parse times:** an Objeck program (`JsonParser->New(text)->Parse()`, then `model.vocab`)
  compiled with the local `deploy-x64` `obc` and run with `obr`, after the training benchmark had
  finished.
- **Training times:**
  - `torch` 2.15.0+cpu and `transformers` 5.22.0;
  - `BertConfig` / `DistilBertConfig` at the stated sizes;
  - random ids, an all-ones mask, random labels;
  - `AdamW(lr=2e-5)`;
  - `zero_grad`, forward with labels, `backward`, `step`;
  - 2 warm-up steps and 5 timed;
  - inference timed under `no_grad` in eval mode.
- **libtorch sizes:** `HEAD` for archive sizes and `Last-Modified`; the zip central directory read
  with HTTP `Range` requests (Python `zipfile` over a ranged file object); runtime files are the
  `.dll`/`.so`/`.dylib` entries.
- **Dependency status:**
  - GitHub releases API (`gh api repos/{owner}/{repo}/releases`);
  - PyPI JSON (`/pypi/{name}/json`);
  - `gh search code` for PyTorch's TorchScript deprecation strings;
  - the `microsoft/DirectML` README;
  - the ONNX Runtime v1.20.0 release body.
