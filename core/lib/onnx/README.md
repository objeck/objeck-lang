# ONNX Support

Cross-platform ONNX inference library for Objeck (`-lib onnx`). Wraps [ONNX Runtime](https://onnxruntime.ai/) with OpenCV for image preprocessing and exposes computer vision, language, and face recognition pipelines.

## Supported Models

| Class | Model | Task |
|---|---|---|
| `YoloSession` | YOLOv11/v12 | Object detection |
| `ResNetSession` | ResNet-34 | Image classification |
| `DeepLabSession` | DeepLabV3 | Semantic segmentation |
| `OpenPoseSession` | OpenPose | Human pose estimation |
| `Phi3Session` | Phi-3 Mini 4K | Text generation (SLM) |
| `Phi3VisionSession` | Phi-3 Vision 128K | Multimodal image understanding |
| `FaceSession` | SCRFD + ArcFace R50 | Face detection + recognition |

See [MODELS.md](MODELS.md) for download links, file layouts, and sizes.

## Platform / Execution Provider

| Platform | Execution Provider | Backend |
|---|---|---|
| Windows x64 | DirectML | GPU (any DX12 adapter) |
| Windows ARM64 | QNN | Qualcomm GPU (the default `backend_type`) |
| Linux x64 | CPU | CPU only: the vendored ONNX Runtime is a CPU-only build |
| Linux ARM64 | none | not shipped: only an x64 ONNX Runtime is vendored |
| macOS | CoreML | Apple Neural Engine / GPU |

Every shipped library is built from `eq/onnx.cpp`, and the `ONNX_EP_*` define it is compiled with (none for the CPU build) fixes its provider. The `ep` session option can name only that provider or `cpu`; any other name is refused and no session is created (see [execution providers](../../../docs/MODELS.md#execution-providers)).

- Windows: `deploy_windows.cmd` builds `onnx.sln` (`vs/vs.vcxproj`) as `Release-DML|x64` or `Release-QNN|ARM64`.
- Linux: `deploy_posix.sh` runs `eq/build.sh cpu`; the MSYS2 deploy scripts run the same.
- macOS: `deploy_macos_arm64.sh` builds it with `core/lib/opencv/macos/CMakeLists.txt` (`ONNX_EP_COREML`).

No deploy script or workflow builds the per-provider sources in `eq/dml`, `eq/cuda`, `eq/qnn` and `eq/vitis`, or their solutions such as `eq/dml/onnx_dml.sln`.

## Quick Start

```objeck
use API.OpenCV, API.Onnx;

# --- Object detection ---
labels := ["person", "bicycle", "car"];   # the model's class names, in order
yolo := YoloSession->New("yolo11n.onnx");
img := Image->Load("photo.jpg")->Convert(Image->Format->JPEG);
classes := yolo->Inference(img, 640, 640, 0.5, labels)->GetClassifications();
each(cls in classes) {
    "{$cls->GetName()}: {$cls->GetConfidence()}"->PrintLine();
};
yolo->Close();

# --- Face recognition ---
session := FaceSession->New("det_10g.onnx", "w600k_r50.onnx");
img := Image->Load("face.jpg")->Convert(Image->Format->JPEG);
result := session->Recognize(img, 0.5);
faces := result->GetResults();
emb1 := faces[0]->GetEmbedding();

img2 := Image->Load("face2.jpg")->Convert(Image->Format->JPEG);
result2 := session->Recognize(img2, 0.5);
faces2 := result2->GetResults();
emb2 := faces2[0]->GetEmbedding();

sim := FaceSession->Compare(emb1, emb2);
"Similarity: {$sim}"->PrintLine();   # >0.35 = same person
session->Close();
```

## Demo Programs

Located in `programs/frameworks/opencv_onnx/`:

| File | Description |
|---|---|
| `demo_yolo.obs` | Object detection with bounding boxes |
| `demo_resnet.obs` | ImageNet classification |
| `demo_openpose.obs` | Human pose keypoints |
| `demo_phi3_*.obs` | Text generation examples |
| `demo_phi3v_*.obs` | Vision + language examples |
| `demo_face.obs` | Face detection + recognition accuracy test |

Compile any demo:
```
obc -src demo_face.obs -lib lang,collect,opencv,onnx,json
obr demo_face.obe
```

## Building the Native Library

### Windows
`deploy_windows.cmd` restores the NuGet packages (`nuget restore onnx.sln`: ONNX
Runtime DirectML 1.22.1 and DirectML 1.15.4, listed in `vs/packages.config`) and
builds `onnx.sln` as `Release-DML|x64` (DirectML) or `Release-QNN|ARM64` (QNN,
against the runtime in `eq/qnn/win/onnx/arm64`). To build by hand, open `onnx.sln`
in Visual Studio and pick the same configuration.

### Linux
```sh
cd eq
./build.sh cpu       # what deploy_posix.sh builds and ships
./build.sh cuda      # compiles the CUDA path only; see below
```

Both modes link the vendored ONNX Runtime in `eq/cuda/lib/x64/lib`, which is a
CPU-only build, so a `cuda` library refuses every session that does not ask for
`ep=cpu`. Running on CUDA needs a CUDA-enabled ONNX Runtime linked in its place
(see [MODELS.md](MODELS.md)). Only x64 has a vendored runtime, so `build.sh` stops
on ARM64. Requires `pkg-config` and an OpenCV development package (module `opencv4`,
`opencv5` or `opencv`).

### macOS
`core/release/deploy_macos_arm64.sh` builds `libobjk_onnx.dylib` (CoreML) with
`core/lib/opencv/macos/CMakeLists.txt`, against the static OpenCV and the prebuilt
ONNX Runtime from `tools/deps/build_macos_deps.sh`. `eq/build.sh` refuses to run on
macOS.
