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
| Windows | DirectML | GPU (any DX12 adapter) |
| Linux | CPU / CUDA | CPU or NVIDIA GPU |
| macOS | CoreML | Apple Neural Engine / GPU |

Build with `eq/build.sh <cpu|cuda|coreml>` on Linux/macOS.  
Windows uses the Visual Studio solution `onnx.sln` (DML) or `eq/dml/onnx_dml.sln`.

## Quick Start

```objeck
use API.OpenCV, API.Onnx;

# --- Object detection ---
session := YoloSession->New("yolo11n.onnx");
img := Image->Load("photo.jpg")->Convert(Image->Format->JPEG);
result := session->Inference(img, 640, 640, 0.5, labels);
each(cls in result->GetClassifications()) {
    "{$cls->GetName()}: {$cls->GetConfidence()}"->PrintLine();
};
session->Close();

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

### Windows (DirectML)
Open `onnx.sln` or `eq/dml/onnx_dml.sln` in Visual Studio 2022 and build Release x64.

### Linux / macOS
```sh
cd eq
./build.sh cpu       # Linux CPU
./build.sh cuda      # Linux CUDA
./build.sh coreml    # macOS CoreML
```

Requires `pkg-config`, `opencv4`, and `libonnxruntime` on the library path.
