# Objeck AI & ML Developer Guide

All AI and ML capabilities are **standard library** — no third-party packages, no pip install, no package.json. Import the relevant bundle and go.

## Table of Contents
- [Authentication](#authentication)
- [OpenAI](#openai)
  - [Chat & Text](#chat--text)
  - [Vision](#vision)
  - [Realtime (Audio)](#realtime-audio)
  - [Image Generation](#image-generation)
  - [Embeddings](#embeddings)
  - [Moderation](#moderation)
  - [Batch Processing](#batch-processing)
- [Gemini](#gemini)
  - [Generate Content](#generate-content)
  - [Structured Output](#structured-output)
  - [Search Grounding](#search-grounding)
  - [Embeddings](#embeddings-1)
  - [Files API](#files-api)
  - [Context Caching](#context-caching)
- [Ollama (Local Models)](#ollama-local-models)
- [ONNX Local Inference](#onnx-local-inference)
  - [Face Recognition](#face-recognition)
  - [Object Detection (YOLO)](#object-detection-yolo)
  - [Phi-3 / Phi-3 Vision](#phi-3--phi-3-vision)
  - [Image Classification (ResNet)](#image-classification-resnet)
- [Computer Vision (OpenCV)](#computer-vision-opencv)
- [Natural Language Processing](#natural-language-processing)
- [Quick Reference](#quick-reference)

---

## Authentication

### OpenAI
Create `openai_api_key.dat` in your working directory containing your API key. The library reads it automatically:

```ruby
use API.OpenAI, System.IO.Filesystem;

token := FileReader->ReadFile("openai_api_key.dat")->Trim();
```

### Gemini
Create `gemini_api_key.dat` in your working directory. Use the static helper:

```ruby
use API.Google.Gemini;

token := EndPoint->GetApiKey();
```

### Ollama
No authentication needed. Ollama runs locally at `http://localhost:11434` by default. Start it with `ollama serve`.

---

## OpenAI

### Chat & Text

**Compile:** `obc -src your.obs -lib net,net_server,json,cipher,misc,openai`

```ruby
use API.OpenAI;

# Single-turn
response := Response->Respond("gpt-4o-mini",
    Pair->New("user", "Explain JIT compilation in one sentence.")<String, String>,
    token);
response->GetText()->PrintLine();
```

```ruby
# Multi-turn conversation
messages := Vector->New()<Pair<String, String>>;
messages->AddBack(Pair->New("system", "You are a helpful assistant.")<String, String>);
messages->AddBack(Pair->New("user", "What is Objeck?")<String, String>);
messages->AddBack(Pair->New("assistant", "Objeck is a JIT-compiled OO language.")<String, String>);
messages->AddBack(Pair->New("user", "What platforms does it support?")<String, String>);

response := Response->Respond("gpt-4o-mini", messages, token);
response->GetText()->PrintLine();
```

```ruby
# With reasoning control
response := Response->Respond("o3-mini",
    Pair->New("user", "Prove Fermat's Last Theorem.")<String, String>,
    Response->ReasoningEffort->HIGH,
    Response->Verbosity->VERBOSE,
    Nil, token);
response->GetText()->PrintLine();
```

---

### Vision

```ruby
use API.OpenAI, System.IO.Filesystem;

bytes := FileReader->ReadBinaryFile("photo.jpg");
image := ImageQuery->New("What is in this image?", bytes, ImageQuery->MimeType->JPEG);
query := Pair->New("user", image)<String, ImageQuery>;

response := Response->Respond("gpt-4o", query, token);
response->GetText()->PrintLine();
```

---

### Realtime (Audio)

Sends audio or text and receives both a text transcript and PCM audio response over a WebSocket connection.

**Compile:** add `-lib sdl2` for audio playback

```ruby
use API.OpenAI, Game.SDL2;

# Text → text + audio
response := Realtime->Respond("What time is it in Tokyo?",
    "gpt-4o-realtime-preview", token);
if(response <> Nil) {
    text := response->GetFirst();
    "Response: {$text}"->PrintLine();

    # Play audio (PCM 16-bit LE, 24kHz mono)
    audio := response->GetSecond();
    Mixer->PlayPcm(audio->Get(), 24000, AudioFormat->SDL_AUDIO_S16LSB, 1);
};
```

```ruby
# Audio file → text + audio
audio_bytes := FileReader->ReadBinaryFile("question.wav");
response := Realtime->Respond(audio_bytes, "gpt-4o-realtime-preview", "alloy", token);
```

---

### Image Generation

```ruby
use API.OpenAI;

# DALL-E 3, 1024×1024
image := Image->Create("A photorealistic Objeck logo on a circuit board",
    "dall-e-3", Image->Size->DALLE3_1024_1024, token);
if(image <> Nil) {
    each(url in image->GetUrls()) {
        url->GetUrl()->PrintLine();
    };
};
```

```ruby
# Edit with a mask
original := FileReader->ReadBinaryFile("room.png");
mask     := FileReader->ReadBinaryFile("mask.png");
image := Image->Edit("room.png", original,
    "Add a bookshelf to the left wall",
    "mask.png", mask, "dall-e-2", 1,
    Image->Size->DALLE2_1024_1024, "url", "", token);
```

---

### Embeddings

```ruby
use API.OpenAI;

values := Embedding->Create("Objeck is a JIT-compiled language",
    "text-embedding-3-small", token);
if(values <> Nil) {
    dim := values->Size();
    "Dimensions: {$dim}"->PrintLine();   # 1536
};
```

```ruby
# Reduced dimensions
values := Embedding->Create("search query text",
    "text-embedding-3-large", 256, token);
```

---

### Moderation

```ruby
use API.OpenAI;

result := Moderation->Check("I want to hurt someone.", token);
if(result <> Nil) {
    flagged := result->IsFlagged();
    "Flagged: {$flagged}"->PrintLine();

    if(flagged) {
        score := result->GetScore("violence");
        "violence score: {$score}"->PrintLine();
    };
};
```

Categories: `harassment`, `harassment/threatening`, `hate`, `hate/threatening`, `self-harm`, `self-harm/instructions`, `self-harm/intent`, `sexual`, `sexual/minors`, `violence`, `violence/graphic`

---

### Batch Processing

Process up to 50,000 requests asynchronously at 50% of standard API cost. Responses are available within 24 hours.

```ruby
use API.OpenAI, System.IO.Filesystem;

# 1. Upload a .jsonl file of requests
data := FileReader->ReadBinaryFile("requests.jsonl");
File->Create("requests.jsonl", "batch", data, token);

# 2. Create the batch job
job := Batch->Create(file_id, "/v1/chat/completions", token);
"Batch ID: {$job->GetId()}"->PrintLine();
"Status: {$job->GetStatus()}"->PrintLine();   # "validating"

# 3. Poll for completion
job := Batch->Get(job->GetId(), token);
if(job->IsComplete()) {
    output_id := job->GetOutputFileId();
    "Output file: {$output_id}"->PrintLine();
};

# 4. List recent jobs
jobs := Batch->List(token);
each(j in jobs) {
    id := j->GetId();
    status := j->GetStatus();
    "{$id}: {$status}"->PrintLine();
};
```

**Request format** (`requests.jsonl` — one JSON object per line):
```json
{"custom_id":"r1","method":"POST","url":"/v1/chat/completions","body":{"model":"gpt-4o-mini","messages":[{"role":"user","content":"Hello"}]}}
```

---

## Gemini

**Compile:** `obc -src your.obs -lib net,net_server,json,cipher,misc,gemini`

### Generate Content

```ruby
use API.Google.Gemini;

token := EndPoint->GetApiKey();

# Simple text
content := Content->New("user")->AddPart(TextPart->New("Why is the sky blue?"));
candidates := Model->GenerateContent("models/gemini-2.0-flash", content, token);
if(candidates <> Nil & <>candidates->IsEmpty()) {
    candidates->First()->GetAllText()->PrintLine();
};
```

```ruby
# With system instruction
system := Content->New("system")->AddPart(TextPart->New("Reply only in haiku."));
candidates := Model->GenerateContent("models/gemini-2.0-flash",
    content, system, token);
```

```ruby
# Image + text
bytes := FileReader->ReadBinaryFile("chart.png");
content := Content->New("user")
    ->AddPart(TextPart->New("Summarize this chart."))
    ->AddPart(BinaryPart->New(bytes, "image/png"));
candidates := Model->GenerateContent("models/gemini-2.0-flash", content, token);
```

```ruby
# Multi-turn chat session
chat := Chat->New("models/gemini-2.0-flash", token);
chat->SetSystemInstruction(Content->New("system")->AddPart(TextPart->New("You are a coding assistant.")));

r1 := chat->SendPart(TextPart->New("What is a closure?"), "user");
r1->GetAllText()->PrintLine();

r2 := chat->SendPart(TextPart->New("Show me an example in Objeck."), "user");
r2->GetAllText()->PrintLine();
```

---

### Structured Output

Force the model to return valid JSON matching a schema.

```ruby
use API.Google.Gemini, Data.JSON.Scheme;

content := Content->New("user")
    ->AddPart(TextPart->New("Top 3 programming languages by popularity."));

# Define schema: array of {name: string, rank: integer}
schema := ParameterType->New(["name", "rank"], true);
schema->AddProp("name", ParameterType->New(ParameterType->Type->STRING));
schema->AddProp("rank", ParameterType->New(ParameterType->Type->INTEGER));
resp_schema := Pair->New("application/json", schema)<String, ParameterType>;

candidates := Model->GenerateContent("models/gemini-2.5-flash", content, resp_schema, token);
if(candidates <> Nil & <>candidates->IsEmpty()) {
    Data.JSON.JsonElement->Decode(candidates->First()->GetAllText()->Trim())->PrintLine();
};
```

---

### Search Grounding

Connects the model to live Google Search for up-to-date answers.

```ruby
use API.Google.Gemini;

content := Content->New("user")
    ->AddPart(TextPart->New("What major AI models were released this month?"));

candidates := Model->GenerateContentWithGrounding("models/gemini-2.0-flash", content, token);
if(candidates <> Nil & <>candidates->IsEmpty()) {
    candidates->First()->GetAllText()->PrintLine();
};
```

---

### Embeddings

```ruby
use API.Google.Gemini;

# Single embedding
content := Content->New("user")->AddPart(TextPart->New("machine learning"));
values := Model->EmbedContent(content, token);   # uses models/embedding-001
dim := values->Size();
"Dimensions: {$dim}"->PrintLine();   # 768
```

```ruby
# Batch — multiple texts in one round-trip
texts := Vector->New()<String>;
texts->AddBack("Objeck is JIT-compiled");
texts->AddBack("Python uses an interpreter");
texts->AddBack("Rust is memory-safe");

embeddings := Model->BatchEmbedContent("models/text-embedding-004", texts, token);
each(i : embeddings) {
    emb := embeddings->Get(i);
    dim := emb->Size();
    "  [{$i}] dim={$dim}"->PrintLine();
};

# Cosine similarity between first two
a := embeddings->Get(0)->Get();
b := embeddings->Get(1)->Get();
```

```ruby
# Task-typed embedding (improves retrieval quality)
content := Content->New("user")->AddPart(TextPart->New("What is Objeck?"));
values := Model->EmbedContent(content, "FAQ title", Model->TaskType->RETRIEVAL_DOCUMENT, token);
```

---

### Files API

Upload files once and reference them across multiple requests without re-uploading.

```ruby
use API.Google.Gemini, System.IO.Filesystem;

# Upload
data := FileReader->ReadBinaryFile("report.pdf");
file := FileManager->Upload("Q1 Report", data, "application/pdf", token);
if(file <> Nil & file->IsActive()) {
    "URI: {$file->GetUri()}"->PrintLine();
    "Name: {$file->GetName()}"->PrintLine();   # "files/abc123"
};

# List
files := FileManager->List(token);
each(f in files) {
    name := f->GetName();
    state := f->GetState();
    "{$name}: {$state}"->PrintLine();
};

# Delete
FileManager->Delete("files/abc123", token);
```

---

### Context Caching

Cache large, reused content server-side to avoid paying re-tokenization costs on every request.

```ruby
use API.Google.Gemini;

# Cache a large system prompt or document for 5 minutes
large_context := FileReader->ReadFile("legal_document.txt");
content := Content->New("user")->AddPart(TextPart->New(large_context));

item := CachedContent->Create(
    "models/gemini-1.5-pro-001", content, 300, "legal-doc-cache", token);
if(item <> Nil) {
    "Name: {$item->GetName()}"->PrintLine();
    tokens := item->GetTokenCount();
    "Tokens cached: {$tokens}"->PrintLine();
    "Expires: {$item->GetExpireTime()}"->PrintLine();
};

# List active caches
items := CachedContent->List(token);
each(item in items) {
    name := item->GetName();
    expires := item->GetExpireTime();
    "{$name} — expires {$expires}"->PrintLine();
};

# Clean up
CachedContent->Delete("cachedContents/abc123", token);
```

---

## Ollama (Local Models)

Run open-source models locally. No API key, no data leaves your machine.

**Prerequisites:** [Install Ollama](https://ollama.com) and pull a model: `ollama pull llama3.2`

**Compile:** `obc -src your.obs -lib net,json,cipher,misc,ollama`

```ruby
use API.Ollama;

# One-shot generation
response := Completion->Generate("llama3.2", "What is 2 + 2?");
response->PrintLine();
```

```ruby
# With temperature control
opts := Options->New()->SetTemperature(0.2);
response := Completion->Generate("llama3.2", "List 3 capitals of Europe.", opts);
response->PrintLine();
```

```ruby
# Multi-turn chat
chat := Chat->New("llama3.2");
r1 := chat->Send("My name is Alice.");
r2 := chat->Send("What is my name?");
r2->PrintLine();   # "Your name is Alice."
```

```ruby
# Vision (multimodal models)
image := File->New("photo.jpg");
response := Completion->Generate("llava", "Describe this image.", image);
response->PrintLine();
```

```ruby
# Local embeddings
values := Model->Embeddings("nomic-embed-text", "machine learning");
dim := values->Size();
"Dimensions: {$dim}"->PrintLine();
```

```ruby
# Model management
models := Model->List();
each(m in models) {
    m->GetName()->PrintLine();
};
Model->Pull("phi3");
```

---

## ONNX Local Inference

Run ML models locally using the ONNX Runtime. Supports DirectML (Windows), CUDA (Linux), and CoreML (macOS) — no GPU required for CPU inference.

**Compile:** `obc -src your.obs -lib net,json,cipher,opencv,onnx`

### Face Recognition

Uses [InsightFace buffalo_l](https://github.com/deepinsight/insightface): SCRFD 10G-KPS detector + ArcFace R50 (512-dim embeddings).

```ruby
use API.Onnx;

session := FaceSession->New("det_10g.onnx", "w600k_r50.onnx");

img1 := FileReader->ReadBinaryFile("person_a.jpg");
img2 := FileReader->ReadBinaryFile("person_b.jpg");

r1 := session->Recognize(img1, 0.5);
r2 := session->Recognize(img2, 0.5);

if(r1->GetSize() > 0 & r2->GetSize() > 0) {
    emb1 := r1->GetResults()[0]->GetEmbedding();
    emb2 := r2->GetResults()[0]->GetEmbedding();

    sim := FaceSession->Compare(emb1, emb2);
    "Similarity: {$sim}"->PrintLine();
    same := sim > 0.35;
    "Same person: {$same}"->PrintLine();
};

session->Close();
```

```ruby
# Detection only (no embedding)
session := FaceSession->New("det_10g.onnx");
result := session->Detect(img_bytes, 0.5);
count := result->GetSize();
"Faces detected: {$count}"->PrintLine();
```

---

### Object Detection (YOLO)

```ruby
use API.Onnx;

labels := String->New[80];   # COCO labels
labels[0] := "person"; labels[1] := "bicycle"; # ... fill all 80

session := YoloSession->New("yolov8n.onnx");
img := FileReader->ReadBinaryFile("street.jpg");

result := session->Inference(img, 640, 640, 0.5, labels);
detections := result->GetClassifications();
each(d in detections) {
    label := d->GetLabel();
    conf  := d->GetConfidence();
    "{$label}: {$conf}"->PrintLine();
};

session->Close();
```

---

### Phi-3 / Phi-3 Vision

Run Microsoft's Phi-3 SLM locally for text or vision tasks.

```ruby
use API.Onnx;

tokenizer := Phi3Tokenizer->New("tokenizer.json");
session   := Phi3Session->New("phi3-mini-4k-instruct.onnx");

prompt     := "<|user|>\nWhat is the capital of France?<|end|>\n<|assistant|>\n";
token_ids  := tokenizer->Encode(prompt);
eos_tokens := Int->New[1]; eos_tokens[0] := 32007;

result  := session->Generate(token_ids, 200, 0.7, eos_tokens);
decoded := tokenizer->Decode(result->GetTokenIds());
decoded->PrintLine();

session->Close();
```

```ruby
# Phi-3 Vision (image + text)
vision := Phi3VisionSession->New(
    "phi3v-vision.onnx",
    "phi3v-embed.onnx",
    "phi3v-decoder.onnx");

img    := FileReader->ReadBinaryFile("diagram.png");
prefix := tokenizer->Encode("<|user|>\n<|image_1|>\n");
suffix := tokenizer->Encode("Describe this diagram.<|end|>\n<|assistant|>\n");

result := vision->Generate(img, prefix, suffix, 300, 0.7, eos_tokens);
tokenizer->Decode(result->GetTokenIds())->PrintLine();

vision->Close();
```

---

### Image Classification (ResNet)

```ruby
use API.Onnx;

labels := FileReader->ReadFile("imagenet_labels.txt")->Split("\n");
session := ResNetSession->New("resnet50.onnx");
img     := FileReader->ReadBinaryFile("cat.jpg");

result := session->Inference(img, 224, 224, labels);
top := result->GetTopLabel();
conf := result->GetTopConfidence();
"Predicted: {$top} ({$conf})"->PrintLine();

session->Close();
```

---

## Computer Vision (OpenCV)

**Compile:** `obc -src your.obs -lib json,cipher,opencv`

```ruby
use API.OpenCV;

# Load, process, save
image   := Image->New("photo.jpg");
gray    := image->ToGray();
blurred := gray->GaussianBlur(5, 5);
edges   := blurred->Canny(50, 150);
edges->Save("edges.jpg");
```

```ruby
# Haar cascade face detection
detector := FaceDetector->New("haarcascade_frontalface_default.xml");
image    := Image->New("group.jpg");
faces    := detector->Detect(image);
count    := faces->Size();
"Faces: {$count}"->PrintLine();
```

```ruby
# Resize and color conversion
resized := image->Resize(320, 240);
hsv     := resized->ToHsv();
hsv->Save("output.jpg");
```

---

## Natural Language Processing

**Compile:** `obc -src your.obs -lib gen_collect,nlp`

```ruby
use API.ML.NLP;

# Sentiment analysis
text      := "This product is absolutely wonderful!";
sentiment := SentimentAnalyzer->Classify(text);
sentiment->PrintLine();   # "positive"
```

```ruby
# TF-IDF vectorization
docs := String->New[3];
docs[0] := "cats are pets";
docs[1] := "dogs are pets";
docs[2] := "birds can fly";

tfidf := TF_IDF->New();
tfidf->Fit(docs);

vector := tfidf->Transform("cats and dogs");
each(v in vector) {
    v->PrintLine();
};
```

```ruby
# Tokenization
tokens := Tokenizer->Tokenize("The quick brown fox");
each(t in tokens) {
    t->PrintLine();
};
```

```ruby
# Text similarity (cosine)
sim := TextSimilarity->Cosine("hello world", "hello there");
"Similarity: {$sim}"->PrintLine();
```

---

## Quick Reference

| Capability | Class | Library | Key |
|---|---|---|---|
| Chat completion | `Response` | `openai` | OpenAI |
| Vision (image input) | `Response` + `ImageQuery` | `openai` | OpenAI |
| Realtime audio | `Realtime` | `openai` | OpenAI |
| Image generation | `Image` | `openai` | OpenAI |
| Text embeddings | `Embedding` | `openai` | OpenAI |
| Content moderation | `Moderation` | `openai` | OpenAI |
| Batch processing | `Batch` | `openai` | OpenAI |
| Generate (text/vision) | `Model::GenerateContent` | `gemini` | Gemini |
| Structured JSON output | `Model::GenerateContent` + schema | `gemini` | Gemini |
| Search grounding | `Model::GenerateContentWithGrounding` | `gemini` | Gemini |
| Gemini embeddings | `Model::EmbedContent` | `gemini` | Gemini |
| Batch embeddings | `Model::BatchEmbedContent` | `gemini` | Gemini |
| File upload | `FileManager` | `gemini` | Gemini |
| Context caching | `CachedContent` | `gemini` | Gemini |
| Local text gen | `Completion` / `Chat` | `ollama` | None |
| Local embeddings | `Model::Embeddings` | `ollama` | None |
| Face recognition | `FaceSession` | `onnx` | None |
| Object detection | `YoloSession` | `onnx` | None |
| Local SLM | `Phi3Session` | `onnx` | None |
| Image classification | `ResNetSession` | `onnx` | None |
| Pose estimation | `OpenPoseSession` | `onnx` | None |
| Segmentation | `DeepLabSession` | `onnx` | None |
| Computer vision | `Image`, `FaceDetector` | `opencv` | None |
| Sentiment / TF-IDF | `SentimentAnalyzer`, `TF_IDF` | `nlp` | None |

### Model Recommendations

| Task | OpenAI | Gemini | Ollama |
|---|---|---|---|
| General chat | `gpt-4o-mini` | `gemini-2.0-flash` | `llama3.2` |
| Reasoning | `o3-mini` | `gemini-2.5-pro` | `qwen2.5` |
| Vision | `gpt-4o` | `gemini-2.0-flash` | `llava` |
| Realtime audio | `gpt-4o-realtime-preview` | — | — |
| Embeddings | `text-embedding-3-small` | `text-embedding-004` | `nomic-embed-text` |
| Image gen | `dall-e-3` | — | — |
| Fast + cheap | `gpt-4o-mini` | `gemini-2.5-flash` | `phi3` |

### Example Programs
Full working examples are in [`programs/frameworks/`](../programs/frameworks/):

```
openai/   openai_chat.obs     openai_images.obs   openai_moderation.obs
          openai_batch.obs    openai_tune.obs      openai_responses.obs

gemini/   gemini_image.obs    gemini_audio.obs     gemini_files.obs
          gemini_cache.obs    gemini_grounding.obs gemini_embed.obs

ollama/   ollama_chat.obs     ollama_vision.obs

opencv_onnx/  face_recognition.obs  yolo_detect.obs  phi3_chat.obs
```
