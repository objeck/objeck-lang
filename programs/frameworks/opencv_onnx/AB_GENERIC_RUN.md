# The generic run, checked against the per-family paths

`API.Onnx.Session->Run` is the chokepoint added in P1 of
[`docs/INFERENCE_ENGINES_PLAN.md`](../../../docs/INFERENCE_ENGINES_PLAN.md):
tensors in, tensors out, with no model-family knowledge in the native library.
The claim that needs checking is that it drives ONNX Runtime the same way the
eight per-family entry points do.

Neither program below can live in `programs/regression`, because both need a
model and the regression tree carries none — `onnx_runtime_test.obs` says why.
`programs/regression/onnx_generic_run.obs` covers the half that needs no asset
(every refusal path, and the flattening the Objeck side does); these two cover
the half that needs a model.

## `ab_resnet.obs` — the same answer as `ResNetSession`

Replicates `resnet_preprocess` in Objeck (resize, BGR→RGB, /255, ImageNet
mean/std, CHW), pushes the tensor through `Session->Run`, softmaxes the logits in
Objeck, and requires the class, the label and the confidence to match what
`ResNetSession->Inference` reports for the same bytes.

```bash
obc -src ab_resnet.obs -lib onnx,opencv,cipher,json -dest ab_resnet.obe
obr ab_resnet.obe data/media/bar.jpg data/models/resnet34.onnx data/models/resnet_labels.txt
```

Measured 2026-10-07: both paths `id=830 name=stretcher conf=0.080005`,
confidence gap `0.000000`.

**Feed it the original file bytes, not `image->Convert(JPEG)`.** Convert
re-encodes, JPEG is lossy, and the per-family path then decodes different pixels
from the ones you preprocessed. That showed up as a 0.0025 confidence gap which
looked like an arithmetic difference and was not. (It also means `demo_resnet.obs`
has been classifying a re-encoded image all along: 0.077514 rather than
0.080005.)

## `ab_phi3.obs` — a real SLM prefill

The case the chokepoint exists for. Phi-3 takes **67 inputs**: `input_ids`,
`position_ids` and `attention_mask` as `INT64[-1,-1]`, plus 32 layers × (key,
value) as `FP16[-1,32,-1,96]`, all **empty** on the first forward pass. Every
dimension is symbolic, so the caller chooses all of them.

`phi3_text_inf` is ~270 lines of C++ plus `discover_slm_model` and
`extract_last_logits` to do this. Here it is from Objeck with none of that.

```bash
obc -src ab_phi3.obs -lib onnx,opencv,cipher,json -dest ab_phi3.obe
obr ab_phi3.obe <deploy>/examples/data/models/phi3 "The capital of France is"
```

Measured 2026-10-07: `logits: [1,5,32064]`, `next token: id=3681 text='Paris'`.

It exercises in one call: 67 inputs, INT64 conversion, FP16 conversion,
zero-element tensors, and reading an FP16 output back as `Float`. The empty cache
is what found the one real bug in this work — an `extent < 1` check that refused
`[1,32,0,96]` as "not concrete", which would have rejected the first forward pass
of every SLM.
