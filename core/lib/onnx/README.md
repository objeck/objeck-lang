# ONNX Support

Cross-platform ONNX inference library for Objeck (`-lib models,opencv,cipher,json,onnx`). Wraps [ONNX Runtime](https://onnxruntime.ai/) with OpenCV for image preprocessing and exposes computer vision, language, and face recognition pipelines.

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
| Linux ARM64 | CPU | CPU only: the vendored aarch64 ONNX Runtime 1.19.0 is a CPU-only build (shipped since v2026.10.0, `fc454eced8`) |
| macOS | CoreML | Apple Neural Engine / GPU |

### Choosing the provider

Every shipped library is built from the one source, `eq/onnx.cpp`. The `ONNX_EP_*` define it is compiled with (none for the CPU build) only sets the **default** provider, the one a session gets when the program names none. The program chooses per session with the `ep` option, `cpu`, `cuda`, `dml`, `qnn`, `vitisai` or `coreml`:

```objeck
use API.Onnx;

config := Map->New()<String, String>;
if(OnnxRuntime->HasProvider("cuda")) {
  config->Insert("ep", "cuda");
  config->Insert("device_id", "0");     # other keys are provider options
};
session := YoloSession->New("yolo11n.onnx", config);
if(<>session->IsOpen()) {
  "no session, see the message above"->PrintLine();
};
```

The accelerator itself lives in the ONNX Runtime library loaded next to `libobjk_onnx`, so the native library asks ONNX Runtime what it was built with (`Ort::GetAvailableProviders()`) instead of assuming. A provider that is not there is **refused**: the session does not open and the message lists what is available, for example `ep=cuda` against the CPU-only runtime:

```
>>> ONNX: execution provider 'cuda' was requested, but the ONNX Runtime this library loaded does not provide it. Available: CPUExecutionProvider. Use ep=cpu, or put an ONNX Runtime that carries 'cuda' beside libobjk_onnx. <<<
```

A session never silently runs on the CPU in place of a provider that was asked for. (The one fallback left is macOS with no `ep` at all, where CoreML failing announces itself and uses the CPU; `ep=coreml` is strict.) A provider the runtime does carry can still fail to start, for instance CUDA without cuDNN or an unknown provider option, and that error from ONNX Runtime is printed with the runtime's provider list.

The Objeck side, in `API.Onnx.OnnxRuntime`:

| Call | Returns |
|---|---|
| `GetProviders()` | what the loaded runtime reports, as ONNX Runtime spells it (`CUDAExecutionProvider`, `DmlExecutionProvider`, `CPUExecutionProvider`, ...) |
| `HasProvider(name)` | `Bool`; takes the short names `ep` takes, case-insensitive |
| `GetDefaultProvider()` | the short name this build uses when `ep` is not set |

### Builds

- Windows: `deploy_windows.cmd` builds `onnx.sln` (`vs/vs.vcxproj`) as `Release-DML|x64` or `Release-QNN|ARM64`. `Release-CUDA|x64` is not part of any release; it builds the same library with `cuda` as the default (set `OnnxRuntimeLibDir` to link against a `Microsoft.ML.OnnxRuntime.Gpu` package).
- Linux: `deploy_posix.sh` runs `eq/build.sh cpu`; the MSYS2 deploy scripts run the same.
- macOS: `deploy_macos_arm64.sh` builds it with `core/lib/opencv/macos/CMakeLists.txt` (`ONNX_EP_COREML`).

The per-provider copies of the source (`eq/dml`, `eq/cuda/onnx_cuda.cpp`, `eq/qnn`, `eq/vitis`) are gone; one of them appended `"DML"` under a CUDA name. What stays under those directories is vendored runtime files and headers.

### Using CUDA (not shipped)

No release ships CUDA. It needs an ONNX Runtime built with the CUDA provider, the NVIDIA driver, CUDA 12 and cuDNN 9, all supplied by you.

- **Windows x64:** copy a `Microsoft.ML.OnnxRuntime.Gpu` package's `onnxruntime.dll`, `onnxruntime_providers_cuda.dll` and `onnxruntime_providers_shared.dll` over the ones in `bin`, with the CUDA 12 runtime, cuBLAS, cuFFT and cuDNN 9 DLLs on the same path, then pass `ep=cuda`. The GPU runtime has no DirectML, so a program that leaves `ep` out (default `dml`) is refused rather than moved to the CPU.
- **Linux x64:** the shipped library is linked to the vendored 1.19.0 symbol version (`VERS_1.19.0`), so it will not load a different runtime. Rebuild against yours: `ORT_ROOT=/opt/onnxruntime-gpu/lib ./build.sh cpu` (`cuda` also makes it the default provider), and run with that directory first on `LD_LIBRARY_PATH`.

Measured on an RTX 4090 (YOLO11 fine-tune, 768x768, 40 MB, one 1080p photo, `ep=cpu` vs `ep=cuda`, see `docs/ONNX_CUDA_PLAN.md`): about 460-500 ms per image on the CPU and 42 ms on CUDA on Windows (roughly 11x), 340-510 ms and 44 ms on Linux under WSL2. The time includes JPEG decoding, preprocessing and decoding the detections, which stay on the CPU.

### IO binding (keeping tensors on the device)

`Session->Run` copies every input to the device and every output back on each call. `DeviceTensor` and `BoundSession` (`device_tensor.h`, `onnx.obs`) allocate tensors once in the provider's memory (`ep=cuda`, `ep=tensorrt`; host memory for `ep=cpu`), bind them to the model's inputs and outputs by name, and run on them in place: ResNet-34 at batch 32 on an RTX 4090 went from 48.9 ms to 6.1 ms per run with the data resident. A bound run is bit-identical to `Session->Run` on the same provider. The CUDA runtime calls it needs (a copy, a stream) are looked up from the cudart library the CUDA provider already loaded, so building the library still needs no CUDA. A session closed while tensors still use it is released after the last of them (`session_registry.h`). See `docs/ai_sections/onnx_gpu.md`.

### Half precision and 8-bit

`Session->Run` converts to whatever the model declares (FP32, FP16, BF16, FP64, INT8 to INT64, UINT8 to UINT64, BOOL). The FP16 and BF16 conversions round to nearest, ties to even, straight from the `Float`; integers saturate at the type's range. `Half`, `BFloat16` and `Quantization` (`onnx.obs`) expose the conversions, `DeviceTensor->UploadBits` / `DownloadBits` move raw element patterns, and `programs/tests/onnx_gpu/make_models.py` produces FP16 and INT8 (QDQ) variants of an FP32 model. See `docs/ai_sections/onnx_gpu.md`.

### TensorRT, session options and CUDA graphs

`ep=tensorrt` (`session_options.h`) appends the TensorRT provider, then the CUDA provider for what TensorRT cannot take, with the engine cache on by default in a per-user directory. Session-level keys in the config map (`intra_op_threads`, `inter_op_threads`, `graph_optimization`, `execution_mode`, `mem_pattern`, `cpu_mem_arena`, `session_config.<key>`) are checked and refused when unreadable; every other key is a provider option. `enable_cuda_graph=1` (CUDA) and `cuda_graph=1` (TensorRT) replay the recorded kernels for a bound run. A runtime that lacks the provider refuses the session. Keys, defaults and measurements: `docs/ai_sections/onnx_gpu.md`.

### Threads

`Session->Run`, a bound run and the `DeviceTensor` copies park the calling thread for the length of the native work, so a stop-the-world collection started by another Objeck thread does not wait for an inference (the size of the gain depends on the workload; see `docs/ai_sections/onnx_gpu.md`). A `Session` may be shared by threads; give each thread its own `BoundSession` and tensors. See `docs/ai_sections/onnx_gpu.md`.

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
obc -src demo_face.obs -lib models,opencv,cipher,json,onnx
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

Both modes link the vendored ONNX Runtime in `eq/cuda/lib/<arch>/lib` (`x64` or `arm64`, chosen from `uname -m`), which is a
CPU-only build, so a `cuda` library refuses every session that does not ask for
`ep=cpu`. Running on CUDA needs a CUDA-enabled ONNX Runtime linked in its place
(see [MODELS.md](MODELS.md)). Both x64 and arm64 have a vendored
runtime. Requires `pkg-config` and an OpenCV development package (module `opencv4`,
`opencv5` or `opencv`).

### macOS
`core/release/deploy_macos_arm64.sh` builds `libobjk_onnx.dylib` (CoreML) with
`core/lib/opencv/macos/CMakeLists.txt`, against the static OpenCV and the prebuilt
ONNX Runtime from `tools/deps/build_macos_deps.sh`. `eq/build.sh` refuses to run on
macOS.
