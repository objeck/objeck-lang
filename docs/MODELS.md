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

ONNX models run fully on-device, on the accelerator the platform's build was compiled for (DirectML on Windows x64, QNN on Windows ARM64, CoreML on macOS) or on the CPU. The Linux x64 and ARM64 builds are CPU-only (each links a vendored ONNX Runtime 1.19.0 CPU build). Models must be downloaded separately — they are too large for the repository.

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

### Hugging Face models by name

Text models published on the Hugging Face Hub with an ONNX export download by name, with no Python: `System.AI.Hub.HubClient` (`-lib net,ai_hub`) fetches a repository's files into `~/.objeck/hub` (or `OBJECK_HUB_CACHE`), checks each file's size and SHA-256 against the Hub, and works offline from the cache. `System.NLP.Onnx.TextClassifier` then opens the folder. See [AI.md](AI.md#models-from-the-hugging-face-hub).

```objeck
use System.AI.Hub, System.NLP.Onnx;

folder := HubClient->New()->Snapshot("Xenova/distilbert-base-uncased-finetuned-sst-2-english",
  ["config.json", "tokenizer.json", "tokenizer_config.json", "onnx/model.onnx"]);
classifier := TextClassifier->New(folder);
```

Compile with: `obc -src your.obs -lib models,opencv,cipher,json,onnx,nlp,nlp_onnx,net,ai_hub`. The tests that cover this path are `programs/regression/ai_hub_client.obs` (the cache, hash verification, token handling and offline rules, against two in-process servers that stand in for the Hub and its CDN) and `programs/regression/nlp_onnx_text_classifier.obs` (token ids exactly, logits within 1e-5 of ONNX Runtime and 1e-4 of PyTorch, on three tiny classifiers with fixtures in `programs/regression/data/bert/`).

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

The execution provider is chosen **per session** with the `ep` key: `cpu`, `cuda`, `dml`, `qnn`, `vitisai` or `coreml` (case-insensitive). The provider has to be in the ONNX Runtime the library loaded: that runtime is what carries the accelerator, and the native library only asks for it. A provider the runtime does not carry is refused with an error that lists the providers it does carry, and no session is created. A session is never quietly run on the CPU instead.

Leaving `ep` out uses the build's default provider (`OnnxRuntime->GetDefaultProvider()`):

| Platform | Default accelerator | Shipped runtime carries |
|----------|---------------------|-------------------------|
| Windows x64 | GPU via DirectML (`dml`) | DirectML, CPU |
| Windows ARM64 | Qualcomm GPU via QNN (`qnn`) | QNN, CPU |
| Linux x64, Linux ARM64 | none (`cpu`) | CPU only |
| macOS | Apple Neural Engine / GPU via CoreML (`coreml`) | CoreML, CPU |

The macOS runtime is ONNX Runtime 1.30.0 from Microsoft's NuGet package `Microsoft.ML.OnnxRuntime` (the GitHub release is gone). CoreML evaluates some operators at reduced precision (a BERT pooler's `tanh` came out about 1e-3 off a reference on an M4 Max), so a check against a reference to 1e-6 on Apple Silicon needs `ep=cpu`.

No release ships CUDA. `ep=cuda` works when you provide an ONNX Runtime built with the CUDA provider, plus the NVIDIA driver, CUDA 12 and cuDNN 9 it needs: put that runtime's `onnxruntime` library beside `libobjk_onnx` (Windows: replace `bin\onnxruntime.dll` and add `onnxruntime_providers_cuda.dll` and its dependencies). On Linux `libobjk_onnx` is linked to one runtime version, so rebuild it against yours with `ORT_ROOT=<dir> ./build.sh cpu` (see [core/lib/onnx/README.md](../core/lib/onnx/README.md)). Which providers a runtime carries:

```objeck
use API.Onnx;

OnnxRuntime->GetProviders();            # e.g. ["CUDAExecutionProvider", "CPUExecutionProvider"]
OnnxRuntime->HasProvider("cuda");       # Bool, takes the names "ep" takes
OnnxRuntime->GetDefaultProvider();      # the provider a session gets without "ep"
```

Other keys in the same map are provider options and go to the provider (`device_id`, and for CUDA `gpu_mem_limit`, `cudnn_conv_algo_search`, ...). An option the provider does not know is an error, not ignored.

### Use in Objeck

```objeck
use API.Onnx, Collection;

config := Map->New()<String, String>;
config->Insert("ep", "dml");   # Windows x64; "qnn" or "coreml" on those builds, "cuda" with a CUDA runtime, "cpu" anywhere
session := Phi3Session->New("data/models/phi3/directml/directml-int4-awq-block-128/model.onnx", config);
```

Compile with: `obc -src your.obs -lib models,net,json,cipher,opencv,onnx`

See the [ONNX examples](../programs/frameworks/opencv_onnx/), the [detailed model reference](../core/lib/onnx/MODELS.md), and the [AI Developer Guide](https://www.objeck.org/ai_guide.html).
