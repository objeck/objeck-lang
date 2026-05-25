# AI Models for Objeck

Objeck supports two categories of AI model: **Ollama** (local LLMs via the Ollama server) and **ONNX** (on-device inference via ONNX Runtime). Neither category requires a cloud API key.

---

## Ollama Models

### 1. Install Ollama

Download and install from **https://ollama.com/download** (Windows, macOS, Linux).

After installation the Ollama server starts automatically. Verify it is running:

```
ollama list
```

### 2. Pull a Model

```bash
ollama pull <model-name>
```

### 3. Recommended Models

| Task | Model | Pull Command | Size |
|------|-------|-------------|------|
| General chat / reasoning | `llama3.2` | `ollama pull llama3.2` | ~2 GB |
| Lightweight / fast | `phi3` | `ollama pull phi3` | ~2.3 GB |
| Vision / multimodal | `llava` | `ollama pull llava` | ~4.7 GB |
| Code generation | `qwen2.5-coder` | `ollama pull qwen2.5-coder` | ~1.5 GB |
| Embeddings | `nomic-embed-text` | `ollama pull nomic-embed-text` | ~274 MB |

Browse the full library at **https://ollama.com/library**.

### 4. Verify a Model Works

```bash
ollama run llama3.2 "Say hello in one word"
```

### 5. Use in Objeck

```objeck
use API.Ollama;

response := Completion->Generate("llama3.2", "Why is the sky blue?");
response->PrintLine();
```

Compile with: `obc -src your.obs -lib net,json,cipher,misc,ollama`

See the [Ollama examples](../programs/frameworks/ollama/) and the [AI Developer Guide](https://www.objeck.org/ai_guide.html).

---

## ONNX Models

ONNX models run fully on-device using GPU acceleration (DirectML on Windows, CUDA on Linux, CoreML on macOS) or CPU fallback. Models must be downloaded separately — they are too large for the repository.

### Supported Models

| Session Class | Task | Model | Source |
|---------------|------|-------|--------|
| `Phi3Session` | Text generation (SLM) | Phi-3 Mini 4K INT4 | [Hugging Face](https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-onnx) |
| `Phi3VisionSession` | Multimodal image+text | Phi-3 Vision 128K INT4 | [Hugging Face](https://huggingface.co/microsoft/Phi-3-vision-128k-instruct-onnx-directml) |
| `YoloSession` | Object detection | YOLOv11n | [Ultralytics](https://docs.ultralytics.com/models/yolo11/) |
| `ResNetSession` | Image classification | ResNet-34 | [ONNX Model Zoo](https://github.com/onnx/models/tree/main/validated/vision/classification/resnet) |
| `DeepLabSession` | Semantic segmentation | DeepLabV3 | [ONNX Model Zoo](https://github.com/onnx/models/tree/main/validated/vision/object_detection_segmentation) |
| `OpenPoseSession` | Human pose estimation | OpenPose | [CMU OpenPose](https://github.com/CMU-Perceptual-Computing-Lab/openpose) |
| `FaceSession` | Face detection + recognition | SCRFD + ArcFace R50 | [InsightFace buffalo_l](https://github.com/deepinsight/insightface/releases/tag/v0.7) |

### Download Models

**Phi-3 Mini (text generation, ~2 GB)**

```bash
pip install huggingface_hub
huggingface-cli download microsoft/Phi-3-mini-4k-instruct-onnx \
  --include "directml/directml-int4-awq-block-128/*" \
  --local-dir data/models/phi3
```

**Phi-3 Vision (multimodal, ~2.5 GB)**

```bash
huggingface-cli download microsoft/Phi-3-vision-128k-instruct-onnx-directml \
  --include "directml-int4-rtn-block-32/*" \
  --local-dir data/models/phi3v
```

**Face recognition (~191 MB total)**

```bash
curl -L -o buffalo_l.zip \
  https://github.com/deepinsight/insightface/releases/download/v0.7/buffalo_l.zip
unzip buffalo_l.zip det_10g.onnx w600k_r50.onnx -d data/models/
```

**YOLO object detection**

```bash
pip install ultralytics
yolo export model=yolo11n.pt format=onnx
# rename yolo11n.onnx → data/models/yolov11.onnx
```

**ResNet-34 classification**

Download `resnet34-v1-7.onnx` from the [ONNX Model Zoo](https://github.com/onnx/models/tree/main/validated/vision/classification/resnet), rename to `resnet34.onnx`.

### Expected Directory Layout

```
data/models/
  det_10g.onnx             # face detector (SCRFD)
  w600k_r50.onnx           # face recognizer (ArcFace R50)
  yolov11.onnx
  resnet34.onnx
  deeplabv3.onnx
  openpose.onnx
  phi3/
    directml/directml-int4-awq-block-128/
      model.onnx
      model.onnx.data
      tokenizer.json
      genai_config.json
  phi3v/
    directml-int4-rtn-block-32/
      phi-3-v-128k-instruct-vision.onnx + .data
      phi-3-v-128k-instruct-text-embedding.onnx + .data
      model.onnx + .data
      tokenizer.json
      genai_config.json
```

### Execution Providers

| Platform | Accelerator | Provider flag |
|----------|-------------|---------------|
| Windows | GPU via DirectML | `"ep" → "dml"` |
| Linux | NVIDIA GPU | `"ep" → "cuda"` |
| macOS | Apple Neural Engine | `"ep" → "coreml"` |
| Any | CPU fallback | `"ep" → "cpu"` |

### Use in Objeck

```objeck
use API.Onnx, Collection;

config := Map->New()<String, String>;
config->Insert("ep", "dml");   # or "cuda", "coreml", "cpu"
session := Phi3Session->New("data/models/phi3/directml/directml-int4-awq-block-128/model.onnx", config);
```

Compile with: `obc -src your.obs -lib net,json,cipher,opencv,onnx`

See the [ONNX examples](../programs/frameworks/opencv_onnx/), the [detailed model reference](../core/lib/onnx/MODELS.md), and the [AI Developer Guide](https://www.objeck.org/ai_guide.html).
