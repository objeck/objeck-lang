# FastSAM-s segmentation for ONNX — design

Add promptable instance segmentation to `API.Onnx` by wrapping FastSAM-s, with
the model handling, decode and mask assembly in C++ and a small Objeck surface
that looks like the model families already there.

The central fact that shapes the whole design: **FastSAM does not condition the
network on a prompt.** One forward pass produces every instance mask in the
image, and a point or box prompt merely *selects* among masks already computed.
So a session runs once and can answer many prompts for free, and the API should
make that obvious rather than inviting a re-run per prompt.

---

## Why

`API.Onnx` covers detection (YOLOv11), classification (ResNet-34), semantic
segmentation (DeepLabV3), pose (OpenPose), faces (SCRFD + ArcFace) and text
(Phi-3). The gap is **instance** segmentation that is not tied to a fixed label
set.

- DeepLabV3 labels every pixel with one of a fixed set of classes. It cannot
  separate two adjacent instances of the same class, and it cannot segment
  anything outside its vocabulary.
- YOLOv11 gives boxes, not masks.

FastSAM-s gives per-instance masks for arbitrary objects, at a size and speed
that suit a CPU or DirectML box. For research use that is the difference between
"which pixels are road" and "give me this object, whatever it is".

The small variant is the right first target: roughly 23 MB, which keeps the
download in line with the other vision models rather than with Phi-3's 2 GB.

---

## Ground truth

Measured in this tree on 2026-09-24, not assumed.

| | |
|---|---|
| `core/lib/onnx/eq/onnx.cpp` | 322 lines; thin dispatch |
| `core/lib/onnx/eq/common.h` | 3113 lines; the actual implementation |
| native entry points | 12, of which 8 are `onnx_<family>_*_inf` |
| session lifecycle | `onnx_new_session` / `onnx_close_session`, handle is an `Ort::Session*` cast to Int |
| threading | `ORT_PARALLEL` + `SetIntraOpNumThreads(hardware_concurrency())`, 8 call sites, one per provider |
| thread-count config keys | **0** — hard-coded |
| explicit locks in the native layer | **0** |
| reusable post-processing | `iou_rect` and `nms` at `common.h:494-526` |
| OpenCV | available (`opencv2/opencv.hpp`, `cv::dnn::blobFromImage`) |
| types marshalled back to Objeck | `API.OpenCV.Image`, `API.OpenCV.Rect` |

The Objeck side follows one shape per family: a private `Proxy` holding a static
`DllProxy("libobjk_onnx")`, a `<Model>Session` owning an opaque `@session : Int`,
a `<Model>Result`, and per-item record classes. Native function names are cached
in `static : String` fields because a string literal allocates on every
evaluation.

**FastSAM-s needs no new marshalling machinery.** Masks are single-channel
images, and `API.OpenCV.Image` already crosses the boundary.

---

## The model

FastSAM-s is YOLOv8-seg with one class. Two outputs:

```
output0  [1, 37, 8400]      4 box + 1 score + 32 mask coefficients, per anchor
output1  [1, 32, 160, 160]  32 mask prototypes
```

Decode is the YOLOv8-seg recipe:

1. Transpose to `[8400, 37]`, drop anchors below the score threshold.
2. NMS on the boxes — **`nms()` in `common.h` already does this**.
3. For each survivor, mask = `sigmoid(coeffs · protos)` — a 32-long dot product
   against `[32, 160, 160]`, giving one 160×160 map.
4. Crop the map to the detection box, resize to the letterboxed input size, undo
   the letterbox to original image coordinates, threshold at 0.5.

Step 3 is a matrix multiply, which is why it belongs in C++: 100 detections
against 32×160×160 is 81 M multiply-adds, and `cv::Mat` does it with the
optimised path already linked in.

---

## Shape

One native entry point, following the existing naming:

```
onnx_fastsam_image_inf(VMContext& context)
```

Arguments mirror `onnx_yolo_image_inf`: result slot, session handle, image,
thresholds. It returns an `API.Onnx.FastSamResult` holding an array of
`API.Onnx.FastSamMask`.

### Objeck surface

```objeck
class FastSamSession {
  New(model : String);
  New(model : String, config : Map<String, String>);

  method : public : Segment(image : Image) ~ FastSamResult;
  method : public : Segment(image : Image, conf : Float, iou : Float) ~ FastSamResult;
  method : public : Close() ~ Nil;
}

class FastSamResult {
  method : public : GetMasks() ~ FastSamMask[];
  method : public : GetCount() ~ Int;

  # Prompting: selection over masks ALREADY computed. No inference.
  method : public : AtPoint(x : Int, y : Int) ~ FastSamMask;
  method : public : InBox(box : Rect) ~ FastSamMask[];
  method : public : Largest() ~ FastSamMask;
}

class FastSamMask {
  method : public : GetImage() ~ Image;      # single channel, 0 or 255
  method : public : GetBox() ~ Rect;
  method : public : GetScore() ~ Float;
  method : public : GetArea() ~ Int;
}
```

`Segment` is the only call that runs the network. Everything on `FastSamResult`
is selection over what that call produced, which is the property the API exists
to expose.

---

## What goes in C++, and why

In the native layer:

- Letterbox preprocessing and `blobFromImage`, as the other families do.
- Session run.
- Decode, NMS, the prototype matmul, sigmoid, crop, resize, threshold.
- Building the `FastSamMask` objects and the result array.

In Objeck:

- Session construction and configuration.
- Prompt selection (`AtPoint`, `InBox`, `Largest`), which is a loop over a
  handful of already-materialised masks and costs nothing.
- Composition with the rest of `API.OpenCV`.

The split follows the brief: session management and the numerically heavy work
in C++, a thin idiomatic surface in Objeck. Prompt selection is deliberately on
the Objeck side because it is cheap, needs no session, and is the part a
researcher will want to vary.

---

## Session management and threading

The existing plumbing is reused unchanged: `onnx_new_session` takes the model
path plus a `Map<String, String>` config, `ep` selects CPU or the compiled-in
provider, and the handle is closed by `onnx_close_session`.

Two threading points, one of which is a pre-existing gap:

**Thread counts are hard-coded.** `SetIntraOpNumThreads(hardware_concurrency())`
is applied at all 8 provider sites with no way to override it. On a machine
doing other work — a CI runner, or a box running several sessions — that is the
wrong number and there is no way to say so. Proposal: honour `intra_op_threads`
and `inter_op_threads` config keys, falling back to the current behaviour when
absent. This is not FastSAM-specific and would apply to every model family, so
it should land as its own change rather than riding along.

**Concurrent `Run` on one session is UNVERIFIED.** ONNX Runtime documents
`Session::Run` as thread-safe, and the native layer holds no locks, but that is
not the same as having checked that *this* code path is re-entrant — the decode
buffers are per-call today, and that needs confirming before the API promises
anything. Until it is verified the documentation should say: one session per
thread. See **Open questions**.

---

## What must not happen

- **No re-inference per prompt.** If `AtPoint` ever runs the network, the design
  has been lost. One `Segment`, many prompts.
- **No silent provider substitution.** The existing code refuses a provider it
  was not compiled with rather than quietly falling back, because a request for
  a provider you are not getting must never look like success. FastSAM inherits
  that and must not soften it.
- **No mask returned in letterbox coordinates.** Masks come back in the original
  image's coordinate space or they are a trap for every caller.
- **No new marshalling type for masks.** `API.OpenCV.Image` already crosses the
  boundary and composes with the rest of the library.
- **No text prompting in phase 1.** FastSAM's text mode needs CLIP, a second
  model with its own download and preprocessing. It is a separate piece of work,
  not a flag.

---

## Phasing

**1 — segment everything.** `onnx_fastsam_image_inf`, `FastSamSession`,
`FastSamResult`, `FastSamMask`. Decode, NMS, prototype matmul, mask assembly.
No prompting. Proves the model loads, the masks land in the right coordinates,
and the session lifecycle behaves. This is the bulk of the work.

**2 — prompt selection.** `AtPoint`, `InBox`, `Largest` on the result. Pure
Objeck, no native change. Small.

**3 — threading configuration.** `intra_op_threads` / `inter_op_threads` config
keys across all families, plus a verified answer on concurrent `Run`. Separable
from 1 and 2 and useful on its own.

**4 — documentation and a sample.** `MODELS.md` entry with the export recipe,
and a `programs/deploy` sample that segments an image and writes the composite,
matching how the other vision models are demonstrated.

Text prompting via CLIP is explicitly out of scope and would be its own design.

---

## Testing

The other ONNX families are hard to regression-test because the models are large
and not in the repository, and that constraint applies here too. What can be
tested without the weights:

- **Decode arithmetic in isolation.** `core/lib/onnx/eq/test/` already holds
  `test_scrfd_decode.cpp` and `test_phi3_sampling.cpp`, which test decode logic
  against synthetic tensors. Their header is explicit: **no ONNX Runtime, no
  OpenCV, no model.** A `test_fastsam_decode.cpp` in the same shape is the right
  home for the prototype matmul, the crop and the letterbox reversal — the parts
  most likely to be silently wrong.

  This constrains the implementation, not just the test. To be reachable from
  that harness the coordinate math and the prototype combination have to be free
  functions over plain buffers, with `cv::Mat` used only at the edges for the
  resize and the final `Image`. Writing the matmul directly against `cv::Mat`
  because it is convenient puts the arithmetic somewhere nothing can test it,
  and this is precisely the arithmetic worth testing. Decide that before writing
  it; it is painful to unpick afterwards.
- **Coordinate round-trip.** Construct a known box in original coordinates,
  letterbox it, reverse it, assert it returns. This is the defect this kind of
  code actually ships.
- **Objeck-side prompt selection** against hand-built `FastSamMask` objects,
  which needs no model at all and belongs in `programs/regression`.

A mask that is plausible but wrong — offset by the letterbox padding, or
correct at one aspect ratio and not another — will pass any eyeball check on a
square test image. The synthetic coordinate round-trip is the test that catches
it, so it should exist before the visual sample does.

---

## Open questions for review

1. **Input size.** FastSAM-s exports at 640×640 by default and supports 1024.
   Larger gives better small-object masks at roughly 2.5× the cost. Fix at 640,
   or read it from the model's input shape and adapt? Reading it is more work in
   the letterbox path but avoids a silent mismatch when someone exports at 1024.
2. **Mask resolution returned.** Full original resolution is the obvious contract
   but costs a resize per instance, and a scene with 100 masks at 4K is 100
   full-frame allocations. Offer a `GetImage()` at original size and keep the
   160×160 map available for callers who only need coarse coverage?
3. **Score and IoU defaults.** YOLO uses 0.45 IoU in this tree. FastSAM's
   reference implementation uses 0.9 confidence / 0.7 IoU (SAM-style, which
   wants heavy overlap suppression). Match the reference, or match the house
   default and document the divergence?
4. **Concurrent `Run`.** Verify re-entrancy before documenting it either way.
   If it is not safe, one session per thread is a fine answer — but it must be
   stated, not left for someone to discover.
5. **Where the variant is named.** `FastSamSession` implies FastSAM generally.
   FastSAM-x has the same outputs and would work unchanged. Keep the class name
   variant-neutral and let the model file decide?
