# ONNX Models for Objeck

This document lists the ONNX models used by the Objeck ONNX library (`-lib onnx`). Models are too large for the git repository and must be downloaded separately.

## Computer Vision Models

### YOLOv11 (Object Detection)
- **File**: `yolov11.onnx`
- **Source**: [Ultralytics YOLOv11](https://docs.ultralytics.com/models/yolo11/)
- **Export**: `yolo export model=yolo11n.pt format=onnx`
- **Labels**: COCO 80-class labels (`yolo_labels.txt`)

### ResNet-34 (Image Classification)
- **File**: `resnet34.onnx`
- **Source**: [ONNX Model Zoo - ResNet](https://github.com/onnx/models/tree/main/validated/vision/classification/resnet)
- **Labels**: ImageNet 1000-class labels (`resnet_labels.txt`)

### DeepLabV3 (Semantic Segmentation)
- **File**: `deeplabv3.onnx`
- **Source**: [ONNX Model Zoo - DeepLab](https://github.com/onnx/models/tree/main/validated/vision/object_detection_segmentation)
- **Labels**: PASCAL VOC/Cityscapes labels (`deeplab_labels.txt`)

### OpenPose (Human Pose Estimation)
- **File**: `openpose.onnx`
- **Source**: [CMU OpenPose](https://github.com/CMU-Perceptual-Computing-Lab/openpose)
- **Labels**: 17 COCO keypoint labels (`openpose_labels.txt`)

## Phi-3 Text (SLM Text Generation)

### Phi-3 Mini 4K Instruct - DirectML INT4
- **Files**: `model.onnx`, `model.onnx.data`
- **Source**: [microsoft/Phi-3-mini-4k-instruct-onnx](https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-onnx)
- **Variant**: `directml/directml-int4-awq-block-128`
- **Size**: ~2 GB
- **Download**:
  ```bash
  pip install huggingface_hub
  huggingface-cli download microsoft/Phi-3-mini-4k-instruct-onnx \
    --include "directml/directml-int4-awq-block-128/*" \
    --local-dir phi3-mini
  ```
- **Precision**: INT4 AWQ block-128 (weights), FP16 (KV cache)

## Phi-3 Vision (Multimodal Image Understanding)

### Phi-3 Vision 128K Instruct - DirectML INT4
- **Files**:
  - `phi-3-v-128k-instruct-vision.onnx` + `.data` (270 MB, vision encoder)
  - `phi-3-v-128k-instruct-text-embedding.onnx` + `.data` (188 MB, text embedding)
  - `model.onnx` + `.data` (2.0 GB, text decoder)
  - `tokenizer.json`, `genai_config.json`, etc.
- **Source**: [microsoft/Phi-3-vision-128k-instruct-onnx-directml](https://huggingface.co/microsoft/Phi-3-vision-128k-instruct-onnx-directml)
- **Variant**: `directml-int4-rtn-block-32`
- **Total Size**: ~2.5 GB
- **Download**:
  ```bash
  pip install huggingface_hub
  huggingface-cli download microsoft/Phi-3-vision-128k-instruct-onnx-directml \
    --include "directml-int4-rtn-block-32/*" \
    --local-dir phi3v
  ```
- **Precision**: INT4 RTN block-32 (vision + decoder weights), FP16 (embedding + KV cache)

## Expected Directory Layout

```
data/models/
  phi3/
    directml/directml-int4-awq-block-128/
      model.onnx
      model.onnx.data
  phi3v/
    directml-int4-rtn-block-32/
      phi-3-v-128k-instruct-vision.onnx
      phi-3-v-128k-instruct-vision.onnx.data
      phi-3-v-128k-instruct-text-embedding.onnx
      phi-3-v-128k-instruct-text-embedding.onnx.data
      model.onnx
      model.onnx.data
      tokenizer.json
      genai_config.json
```

## Runtime Dependencies

Runtime DLLs are provided in `core/lib/onnx/eq/lib/`:

### Windows (DirectML)
- `onnxruntime.dll` (ONNX Runtime 1.22.1 with DirectML)
- `onnxruntime_providers_shared.dll`
- `DirectML.dll` (DirectML 1.15.4)

Large DLLs are compressed as `.7z` archives. Extract to the application directory or system PATH.

### Linux (CUDA)
- `libonnxruntime.so.1.19.0` (ONNX Runtime with CUDA)
- `libonnxruntime_providers_shared.so`

Requires CUDA toolkit and cuDNN installed on the system.

## Special Tokens (Phi-3 Family)

| Token | ID | Usage |
|---|---|---|
| `<\|endoftext\|>` | 32000 | EOS / padding |
| `<\|assistant\|>` | 32001 | Assistant turn start |
| `<\|system\|>` | 32006 | System prompt |
| `<\|end\|>` | 32007 | End of turn |
| `<\|user\|>` | 32010 | User turn start |
| `<\|image\|>` | 32044 | Image placeholder (Vision only) |
