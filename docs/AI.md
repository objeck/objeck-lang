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
- [Anthropic (Claude)](#anthropic-claude)
  - [Chat and system prompts](#chat-and-system-prompts)
  - [Tool use](#tool-use)
  - [Structured JSON output](#structured-json-output)
  - [Streaming](#streaming)
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
  - [Any Model (generic run)](#any-model-generic-run)
- [Remote Inference Servers](#remote-inference-servers)
  - [One interface over both](#one-interface-over-both)
- [Computer Vision (OpenCV)](#computer-vision-opencv)
- [Natural Language Processing](#natural-language-processing)
  - [BERT tokens and sentence embeddings](#bert-tokens-and-sentence-embeddings)
  - [Text classification with a fine-tuned model](#text-classification-with-a-fine-tuned-model)
  - [Models from the Hugging Face Hub](#models-from-the-hugging-face-hub)
  - [Topic modeling and BSON (System.NLP.Topics)](#topic-modeling-and-bson-systemnlptopics)
- [Machine Learning (System.ML)](#machine-learning-systemml)
  - [Regression](#regression)
  - [Classification](#classification)
  - [Clustering & Decomposition](#clustering--decomposition)
  - [Neural Network](#neural-network)
  - [Model Persistence](#model-persistence)
  - [Local vector search (System.ML.VectorSearch)](#local-vector-search-systemmlvectorsearch)
  - [Replicates and reports (System.ML.Report)](#replicates-and-reports-systemmlreport)
  - [Face verification: FAR, FRR, EER and CLIP embeddings (System.ML.Verification, System.ML.Clip)](#face-verification-far-frr-eer-and-clip-embeddings-systemmlverification-systemmlclip)
  - [Convolutional networks (System.ML.Cnn)](#convolutional-networks-systemmlcnn)
  - [Statistics (System.Stats)](#statistics-systemstats)
  - [Plotting (System.Plot)](#plotting-systemplot)
    - [In a window, or as a PNG (System.Plot.SDL)](#in-a-window-or-as-a-png-systemplotsdl)
- [Classic AI (System.AI)](#classic-ai-systemai)
  - [Graph Search](#graph-search)
  - [Game Playing](#game-playing)
  - [Optimization](#optimization)
  - [Reinforcement Learning](#reinforcement-learning)
- [Library Aliases](#library-aliases)
- [Quick Reference](#quick-reference)

---

## Authentication

### OpenAI
Create `openai_api_key.dat` in your working directory containing your API key. The static helper reads it:

```ruby
use API.OpenAI;

token := EndPoint->GetApiKey();
```

### Anthropic
Set the `ANTHROPIC_API_KEY` environment variable, or create `anthropic_api_key.dat` in your working directory, or call `EndPoint->SetApiKey(key)`. `Chat->New(model, Nil)` reads the key with `EndPoint->GetApiKey()`; pass a key as the second argument to override it for one chat.

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
use API.OpenAI.Responses, Collection;

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

response := Response->Respond("gpt-4o-mini", messages, Nil, token);   # Nil: no output schema
response->GetText()->PrintLine();
```

```ruby
# With reasoning control
response := Response->Respond("o3-mini",
    Pair->New("user", "Prove Fermat's Last Theorem.")<String, String>,
    ReasoningEffort->HIGH,
    Verbosity->HIGH,
    Nil, token);
response->GetText()->PrintLine();
```

**Output cap.** The `max_tokens` argument of `Response->Respond(...)` limits the generated tokens (reasoning tokens included) and is sent as the Responses API field `max_output_tokens`; `-1` sends no cap. `Completion->Complete(...)` (Chat Completions) sends it as `max_completion_tokens`, because `max_tokens` is deprecated there and reasoning models reject it. `Response->BuildRespondRequest(...)` and `Completion->BuildCompleteRequest(...)` return the request body as a `JsonElement` without any network call.

---

### Vision

```ruby
use API.OpenAI, API.OpenAI.Responses, System.IO.Filesystem, Collection;

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
use API.OpenAI, Game.SDL2, System.IO.Filesystem;

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
audio_bytes := FileReader->ReadBinaryFile("question.pcm16");   # raw PCM 16-bit LE, 24kHz mono
response := Realtime->Respond(audio_bytes, "gpt-4o-realtime-preview", "alloy", token);
```

---

### Image Generation

```ruby
use API.OpenAI, System.IO.Filesystem;

# DALL-E 3, 1024×1024
image := Image->Create("A photorealistic Objeck logo on a circuit board",
    "dall-e-3", Image->Size->DALLE3_1024_1024, token);
if(image <> Nil) {
    urls := image->GetUrls();
    each(url in urls) {
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
use API.OpenAI;

# 1. Upload a .jsonl file of requests (or reuse the one already uploaded)
file := File->LoadOrCreate("requests.jsonl", "batch", token);

# 2. Create the batch job
job := Batch->Create(file->GetId(), "/v1/chat/completions", token);
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

## Anthropic (Claude)

`API.Anthropic` is a client for the Anthropic Messages API: chat with a system prompt, tool use, streaming over server-sent events, structured output, and extended-thinking blocks. Compile with `-lib anthropic`. A failed call returns `Nil`, and `EndPoint->GetLastError()` says why, as in the other clients. Requests go over HTTPS; `EndPoint->SetBaseUrl` points the client at a gateway and `EndPoint->SetCertificate` pins its certificate.

### Chat and system prompts

A `Chat` keeps the message history. `SetSystem`, `SetMaxTokens` (default 4096) and `SetTemperature` return the chat, so they chain.

```ruby
use API.Anthropic;

chat := Chat->New("claude-sonnet-5-5", Nil);
chat->SetSystem("Answer in one sentence.");
chat->Send("Why is the sky blue?")->PrintLine();
chat->Send("And at sunset?")->PrintLine();
```

`SendReply(message, tools)` returns the whole `Reply`: `GetText()`, `GetThinking()`, `GetStopReason()`, `GetInputTokens()`, `GetOutputTokens()`, and the tool calls. Pass `Nil` as the message to send the history as it stands.

`SetThinking(budget_tokens)` turns on extended thinking. The API needs a budget of at least 1024 tokens and a `SetMaxTokens` above it. While thinking is on the temperature is not sent, and `SendJson` leaves thinking off for its one request, because the API rejects a forced tool with thinking.

What joins the history, so a chat never wedges: a reply that fails is dropped together with the message that asked for it; a refusal (`stop_reason` `refusal`) and an empty reply are returned to you but not kept, as the API advises; an empty text block is removed from a turn before it is replayed; thinking blocks, including redacted ones, are replayed whole.

### Tool use

Describe each tool with a name, a description and a JSON schema. When a reply has tool uses, run them, return each result with `AddToolResult(use_id, text)` and send again. Results for one reply share one user message, as the API requires. Mark a failed tool with `AddToolResult(id, text, true)`.

```ruby
use API.Anthropic;
use Collection;
use Data.JSON;

schema := JsonParser->TextToElement("{\"type\":\"object\",\"properties\":{\"city\":{\"type\":\"string\"}},\"required\":[\"city\"]}");
tools := Vector->New()<Tool>;
tools->AddBack(Tool->New("get_weather", "Get the weather for a city", schema));

chat := Chat->New("claude-sonnet-5-5", Nil);
reply := chat->SendReply("What is the weather in Paris?", tools);
while(reply <> Nil & reply->HasToolUses()) {
  uses := reply->GetToolUses();
  each(i : uses) {
    call := uses->Get(i);
    city := call->GetArgument("city");
    chat->AddToolResult(call->GetId(), "18C and sunny in {$city}");
  };
  reply := chat->SendReply(Nil, tools);
};
reply->GetText()->PrintLine();
```

`Tool->New(func)` also takes the `FunctionType` the other clients use.

### Structured JSON output

`SendJson(question, name, schema)` makes the model call a tool whose input schema is the one you give, and returns that input as JSON. It is one-shot: neither the question nor the answer joins the history.

```ruby
schema := JsonParser->TextToElement("{\"type\":\"object\",\"properties\":{\"city\":{\"type\":\"string\"},\"temp\":{\"type\":\"integer\"}},\"required\":[\"city\",\"temp\"]}");
answer := chat->SendJson("Name a city where it is 18 degrees.", "answer", schema);
answer->Get("city")->GetString()->PrintLine();
```

### Streaming

`Stream(message, tools)` returns a `ReplyStream`. Read it piece by piece; the exchange joins the history when the stream ends, whether or not you ask for the whole reply, and `GetReply()` can be called more than once. `Close()` before the end, an error event and a tool input that was cut off by `max_tokens` all drop the exchange from the history.

```ruby
stream := chat->Stream("Write a haiku about rivers.", Nil);
while(stream <> Nil & stream->HasNext()) {
  stream->Next()->Print();
};
reply := stream->GetReply();
```

Streaming works over HTTPS only. A refused request (a bad key, a rate limit) returns `Nil` with the status and what it usually means in `GetLastError()`; the body of a refused stream is not read, so the API's own message is not shown there.

**Tested against a local mock, not the live service.** `anthropic_parse_test` replays recorded response bodies and event streams, and `anthropic_http_test` runs the client against a local HTTPS server that speaks the same message shapes, checking the `x-api-key` and `anthropic-version` headers, errors, a tool round trip, structured output and streaming. Neither calls api.anthropic.com. If a field or event the live API sends is not handled, the parse test is where to add a recorded case.

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
bytes := System.IO.Filesystem.FileReader->ReadBinaryFile("chart.png");
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
use API.Google.Gemini, Data.JSON.Scheme, Collection;

content := Content->New("user")
    ->AddPart(TextPart->New("Top 3 programming languages by popularity."));

# Define schema: array of {name: string, rank: integer}
schema := ParameterType->New(["name", "rank"], true);
schema->AddProp("name", ParameterType->New(ParameterType->Type->STRING));
schema->AddProp("rank", ParameterType->New(ParameterType->Type->INTEGER));
resp_schema := Pair->New("application/json", schema)<String, ParameterType>;

candidates := Model->GenerateContent("models/gemini-2.5-flash", content, resp_schema, token);
if(candidates <> Nil & <>candidates->IsEmpty()) {
    candidates->First()->GetAllText()->Trim()->PrintLine();
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
use API.Google.Gemini, Collection;

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
use API.Google.Gemini, System.IO.Filesystem;

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
image := System.IO.Filesystem.File->New("photo.jpg");
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
    m->GetPulled()->GetName()->PrintLine();
};
Model->Pull("phi3");
```

---

## ONNX Local Inference

Run ML models locally using the ONNX Runtime. The execution provider is a per-session choice (`ep`: `cpu`, `cuda`, `dml`, `qnn`, `vitisai`, `coreml`) among those the loaded ONNX Runtime carries. Without `ep` a session uses the build's default: DirectML on Windows x64, QNN on Windows ARM64, CoreML on macOS, and the CPU on Linux x64 and ARM64 (each linked with a vendored ONNX Runtime 1.19.0 CPU-only build). No release ships CUDA; with an ONNX Runtime you supply that carries it, `ep=cuda` runs on an NVIDIA GPU. See [execution providers](MODELS.md#execution-providers) for the setting and [core/lib/onnx/README.md](../core/lib/onnx/README.md) for how to set CUDA up.

A provider the runtime does not carry is refused with an error that lists the ones it does, and the session does not open; it is never quietly run on the CPU. Ask first:

```objeck
use API.Onnx, Collection;

each(provider in OnnxRuntime->GetProviders()) {       # what the runtime reports, e.g. CUDAExecutionProvider
  provider->PrintLine();
};
OnnxRuntime->GetDefaultProvider()->PrintLine();       # "dml", "qnn", "coreml" or "cpu": what a session gets without ep

config := Map->New()<String, String>;
if(OnnxRuntime->HasProvider("cuda")) {
  config->Insert("ep", "cuda");                       # other keys, such as "device_id", are provider options
};
session := YoloSession->New("yolo11n.onnx", config);
if(<>session->IsOpen()) {
  "session did not open, the reason was printed above"->PrintLine();
};
```

**Compile:** `obc -src your.obs -lib models,net,json,cipher,opencv,onnx`

### Face Recognition

Uses [InsightFace buffalo_l](https://github.com/deepinsight/insightface): SCRFD 10G-KPS detector + ArcFace R50 (512-dim embeddings).

```ruby
use API.Onnx, System.IO.Filesystem;

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
use API.Onnx, System.IO.Filesystem;

labels := String->New[80];   # COCO labels
labels[0] := "person"; labels[1] := "bicycle"; # ... fill all 80

session := YoloSession->New("yolov8n.onnx");
img := FileReader->ReadBinaryFile("street.jpg");

result := session->Inference(img, 640, 640, 0.5, labels);
detections := result->GetClassifications();
each(d in detections) {
    name := d->GetName();
    conf := d->GetConfidence();
    "{$name}: {$conf}"->PrintLine();
};

session->Close();
```

---

### Phi-3 / Phi-3 Vision

Run Microsoft's Phi-3 SLM locally for text or vision tasks.

```ruby
use API.Onnx, System.IO.Filesystem;

tokenizer := Phi3Tokenizer->New("tokenizer.json");
session   := Phi3Session->New("phi3-mini-4k-instruct.onnx");

prompt     := "<|user|>\nWhat is the capital of France?<|end|>\n<|assistant|>\n";
token_ids  := tokenizer->Encode(prompt);
eos_tokens := Int->New[1]; eos_tokens[0] := 32007;

result  := session->Generate(token_ids, 200, 0.7, eos_tokens);
decoded := tokenizer->Decode(result->GetTokens());
decoded->PrintLine();

session->Close();
```

```ruby
# Phi-3 Vision (image + text): the image goes between the prefix and suffix tokens
vision := Phi3VisionSession->New(
    "phi3v-vision.onnx",
    "phi3v-embed.onnx",
    "phi3v-decoder.onnx");

img    := FileReader->ReadBinaryFile("diagram.png");
prefix := tokenizer->Encode("<|user|>\n");
suffix := tokenizer->Encode("Describe this diagram.<|end|>\n<|assistant|>\n");

result := vision->Generate(img, prefix, suffix, 300, 0.7, eos_tokens);
tokenizer->Decode(result->GetTokens())->PrintLine();

vision->Close();
```

---

### Image Classification (ResNet)

```ruby
use API.Onnx, System.IO.Filesystem;

labels := FileReader->ReadFile("imagenet_labels.txt")->Split("\n");
session := ResNetSession->New("resnet50.onnx");
img     := FileReader->ReadBinaryFile("cat.jpg");

result := session->Inference(img, 224, 224, labels);
top  := result->GetName();
conf := result->GetConfidence();
"Predicted: {$top} ({$conf})"->PrintLine();

session->Close();
```

### Any Model (generic run)

`YoloSession`, `ResNetSession`, `DeepLabSession`, `OpenPoseSession`, `Phi3Session`,
`Phi3VisionSession` and `FaceSession` each handle one model family. For anything they do not
cover -- a model with several inputs, a text or audio model, or a model whose
output you want to decode yourself -- `Session` runs any ONNX graph. Ask it what
the model wants, build the tensors, run it:

```objeck
use API.Onnx, API.Models, Collection;

session := Session->New("model.onnx");
if(<>session->IsOpen()) {
  return;
};

# what does this model take and return?
info := session->GetModelInfo();
specs := info->GetInputs();
each(spec in specs) {
  spec->ToString()->PrintLine();      # e.g.  data: FP32[-1,3,224,224]
};

inputs := Vector->New()<Tensor>;
inputs->AddBack(Tensor->New("data", [1, 3, 224, 224], pixels));

outputs := session->Run(inputs);
each(tensor in outputs) {
  tensor->ToString()->PrintLine();
};

session->Close();
```

A dimension the model leaves to you is reported as `-1` -- a batch size or a
sequence length. You choose it; nothing guesses.

**You never state an element type.** The model's own declaration decides what
each input becomes, so FP32, FP16, INT64, UINT8 and BOOL models are all driven
the same way.

An integer tensor keeps its integers. The conversion to `Float` happens only at
the native crossing, which is the one place that needs it, and refuses a value
past 2^53 rather than rounding it -- 9,007,199,254,740,992, so every token id and
class index is far inside it. A server takes integers as JSON integers and has no
such limit, which is why the conversion lives at the boundary that cares rather
than in the tensor.

Preprocessing and decoding are yours, in Objeck. That is the trade: more work
than `YoloSession`, and it works for models no per-family class exists for.
`programs/frameworks/opencv_onnx/ab_phi3.obs` drives Phi-3 through it with 67
inputs, which is what the shape is for.

---

## Remote Inference Servers

`API.Inference` talks to a model server over HTTP instead of loading a model
in-process. Use it when the model is not an ONNX file you have locally: a model
served from a GPU machine, a TensorFlow model you would rather not convert, or
anything already deployed behind Triton, KServe, TorchServe or TF Serving.

Two wire protocols, because no single one covers the field:

| Protocol | Served by | Shape |
|---|---|---|
| `OPEN_INFERENCE` (default) | Triton, KServe, OpenVINO Model Server | KServe v2: flat data, with `shape` and `datatype` stated |
| `TF_SERVING` | TensorFlow Serving | v1 `:predict`: nesting carries the shape, no datatype |

```objeck
use API.Inference, API.Models, Collection;

client := Client->New("http://gpu-box:8000");

inputs := Vector->New()<Tensor>;
inputs->AddBack(Tensor->New("input", [1, 4], [5.1, 3.5, 1.4, 0.2]));

outputs := client->Predict("iris", inputs);
if(outputs = Nil) {
  EndPoint->GetLastError()->PrintLine();
  return;
};

each(tensor in outputs) {
  tensor->ToString()->PrintLine();
};
```

A failed call returns `Nil` and the reason is in `EndPoint->GetLastError()`.
Servers report errors in a JSON body rather than only in the status line, so a
body that will not parse means something else answered -- a proxy's error page,
a truncated response -- and the body itself is reported.

Ask the server what a model expects before guessing at names and shapes:

```objeck
if(client->IsReady("iris")) {
  client->Metadata("iris")->ToString()->PrintLine();
};
```

And when a request is rejected, print the body rather than reading the error:

```objeck
client->DescribeRequest(inputs)->PrintLine();
```

The two protocols shape the same tensors very differently and a tensor nested to
the wrong depth looks identical in Objeck, so seeing what was sent is usually
faster than interpreting what came back.

```
v2, shape [4]     {"inputs":[{"data":[...],"datatype":"FP32","name":"input","shape":[4]}]}
v1, shape [4]     {"inputs":{"input":[1.0,2.0,3.0,4.0]}}
v1, shape [2,2]   {"inputs":{"input":[[1.0,2.0],[3.0,4.0]]}}
```

`-lib models,inference,net,json,cipher,gen_collect`. Neither is part of any `@`
alias; name the libraries, or add a group to `configobjk.ini`.

### One interface over both

`Tensor`, `EndPoint` and the `Engine` interface live in `API.Models`, which both
backends link. So calling code can name a backend once and not again:

```objeck
use API.Models, API.Onnx, API.Inference, Collection;

engine : API.Models.Engine;
if(use_gpu_box) {
  engine := HttpEngine->New("http://gpu-box:8000", "iris");
}
else {
  engine := OnnxEngine->New("iris.onnx");
};

if(<>engine->IsOpen()) {
  EndPoint->GetLastError()->PrintLine();
  return;
};

outputs := engine->Run(inputs);
engine->Close();
```

`IsOpen` differs in kind between them and the difference is worth knowing:
`OnnxEngine` reports whether a file loaded, once; `HttpEngine` asks the server
and the answer can change between calls, so it is not cached.

What the interface deliberately leaves out is anything only one backend has --
`GetModelInfo` on ONNX, `Metadata` and `DescribeRequest` on HTTP. Reach them
through `GetSession()` or `GetClient()`; an interface that carried the union of
two backends would describe neither.

---

## Computer Vision (OpenCV)

**Compile:** `obc -src your.obs -lib json,cipher,opencv`

```ruby
use API.OpenCV;

# Load, process, save
image   := Image->Load("photo.jpg");
gray    := image->CvtColor(ColorConversionCodes->BGR2GRAY);
blurred := gray->GaussianBlur(5, 5, 0.0);   # sigma 0: derived from the kernel size
edges   := blurred->Canny(50, 150);
edges->Save("edges.jpg");
```

```ruby
use API.OpenCV, API.Onnx, System.IO.Filesystem;

# Face detection: the OpenCV bundle has no cascade classifier, so detect faces
# with the ONNX FaceSession (add onnx to -lib) and draw the boxes with OpenCV
bytes   := FileReader->ReadBinaryFile("group.jpg");
session := FaceSession->New("det_10g.onnx");
faces   := session->Detect(bytes, 0.5);
count   := faces->GetSize();
"Faces: {$count}"->PrintLine();

image := Image->Load(bytes);
green := Scalar->New(0.0, 255.0, 0.0);
detections := faces->GetDetections();
each(face in detections) {
    image := image->DrawRectangle(face->GetBounds(), green, 2);
};
image->Save("faces.jpg");
session->Close();
```

```ruby
# Resize and color conversion
resized := image->Resize(Size->New(320, 240));
hsv     := resized->CvtColor(ColorConversionCodes->BGR2HSV);
hsv->Save("output.jpg");
```

---

## Natural Language Processing

**Compile:** `obc -src your.obs -lib gen_collect,nlp`

```ruby
use System.NLP;

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
tokens := Tokenizer->WordTokenize("The quick brown fox");
each(t in tokens) {
    t->PrintLine();
};
```

```ruby
# Text similarity (cosine over the TF-IDF vectors above)
sim := TextSimilarity->CosineSimilarity(tfidf->Transform("cats are pets"), tfidf->Transform("dogs are pets"));
"Similarity: {$sim}"->PrintLine();
```

### N-grams, spelling and style features

These are the features of text-classification chapters such as fake-review and fake-news
detection. All are in `nlp.obl`; none needs a model.

**TF-IDF over word n-grams.** `TF_IDF->New(min_n : 1, max_n : 2)` counts words and word pairs
as scikit-learn's `TfidfVectorizer(ngram_range=(1, 2))` does; `(1, 3)` adds triples and
`(2, 2)` is pairs only. Smoothed IDF and unit-length vectors follow scikit-learn (the
regression test compares every IDF and vector entry with it). `min_df` drops terms found in
fewer documents and `max_features` keeps the most frequent ones. `Fit` returns `true` when it
built a vocabulary. An invalid setting (`min_n` below 1, `max_n` below `min_n`), a Nil or empty
corpus, or a corpus with no term left returns `false`: `IsValid()` is false for a bad setting
and `GetError()` says why (it is `""`, never Nil, when there is nothing to report). Existing
calls such as `TF_IDF->New(stem : true)` and a statement `tfidf->Fit(docs);` still compile.

`TransformAll(texts)` and `FitTransform(texts)` give a `Float[,]`, a row per text, ready for a
classifier, so no study writes the loop. The matrix is dense: 8 bytes times the vocabulary times
the documents (40,000 documents at 20,000 terms is 6.4 GB). `EstimateDenseBytes(documents)`
tells you before you ask, and `TransformAll` returns Nil with a reason rather than allocate
more than `GetMaxDenseBytes()` (2 GiB; `SetMaxDenseBytes` changes it). Limit the vocabulary
with `max_features` and `min_df`.

```ruby
tfidf := TF_IDF->New(min_n : 1, max_n : 2, min_df : 2);
X_train := tfidf->FitTransform(training_texts);   # Nil, with tfidf->GetError(), when Fit fails
X_test  := tfidf->TransformAll(test_texts);       # the test texts use the training vocabulary
bytes   := tfidf->EstimateDenseBytes(100000);
```

**Misspelling rate.** `SpellChecker` counts the tokens a word list does not know. No
dictionary ships with Objeck: load a newline-delimited list yourself, for example
`/usr/share/dict/words` on Linux, the SCOWL / aspell lists (http://wordlist.aspell.net) or
`words_alpha.txt`, and check its licence. Tokens are `Tokenizer->WordTokenize`'s, matching is
case-insensitive, tokens with digits are never misspelled, and the rate is misspelled tokens
over all tokens. `Suggest` returns words within a Levenshtein distance, nearest first. A checker
with no words and a Nil text give -1 instead of failing.

```ruby
checker := SpellChecker->New("words.txt");   # checker->GetError() is "" when fine
if(checker->IsLoaded()) {
    n    := checker->CountMisspelled("teh product is gud");
    rate := checker->MisspellingRate("teh product is gud");
    near := checker->Suggest("gud", 1, 3);
};
```

**Stylometric features.** `TextFeatures->Extract(text)` returns eight numbers (names in
`TextFeatures->Names()`): characters, tokens, average word length, type-token ratio,
punctuation rate, uppercase ratio, sentence count and average sentence length. Each is also
a function (`TokenCount`, `TypeTokenRatio`, ...). Empty text gives zeros and Nil gives
Nil / -1. `ExtractAll(texts)` takes a `Vector<String>` or `String[]` and gives a `Float[,]`, a
row per text.

For a fake-review study, `ExtractWith(checker, text)` returns the eight features followed by
`misspelled_count` and `misspelling_rate` from a loaded `SpellChecker` (`NamesWith()` names
the ten; `ExtractAllWith` gives the matrix). `Extract` is unchanged. A Nil text, or a checker
that is not loaded, returns Nil, never a -1 entry that would enter a regression as data.

```ruby
f := TextFeatures->Extract("Great product! Works perfectly. HIGHLY recommended!!!");
X := TextFeatures->ExtractAllWith(checker, reviews);   # [reviews, 10]
```

### BERT tokens and sentence embeddings

`WordPieceTokenizer` is BERT's tokenizer (Hugging Face's `BertTokenizer`, checked id for id on
a 13,000-line corpus); it reads a model's `vocab.txt` and gives the `input_ids`,
`attention_mask` and `token_type_ids` a BERT-family ONNX model takes. `ApiSequenceTokenizer`
builds a vocabulary of API names from malware traces. `BpeTokenizer` is RoBERTa's and GPT-2's
byte-level BPE (checked id for id and offset for offset against Hugging Face's `tokenizers`,
with DistilRoBERTa's own 50,265-token vocabulary). Both implement `SubwordTokenizer`, and
their encodings carry each token's character offsets (`GetOffsets`, BPE). `BertEmbedder` (library `nlp_onnx`) runs
an exported BERT through ONNX Runtime and pools its output into one vector per text — CLS,
mean over the real tokens, or the model's pooler. The encoder is frozen: train a classifier on
the embeddings. Design and limits: [docs/ML_BERT_DESIGN.md](ML_BERT_DESIGN.md).

**Compile:** `obc -src your.obs -lib models,opencv,cipher,json,onnx,nlp,nlp_onnx` (or `-lib @vision`)

```ruby
use System.NLP;
use System.NLP.Onnx;

tokenizer := WordPieceTokenizer->New("vocab.txt");
tokenizer->Tokenize("unaffable")->Size()->PrintLine();   # 3: un ##aff ##able
encoding := tokenizer->Encode("He's unaffable.", 16);    # [CLS] ... [SEP] [PAD]...

embedder := BertEmbedder->New("model.onnx", "vocab.txt", 128);
if(embedder->IsOpen()) {
    embedder->SetPooling(BertEmbedder->Pooling->MEAN);
    X := embedder->Embed(["CreateFileW ReadFile CloseHandle", "RegOpenKeyExA RegSetValueExA"]);
    embedder->Close();
};
```

**Layers.** By default the final layer is pooled. `SetLayers` (or the three-argument `Embed`) can
pool the sum of the last four hidden layers (`SUM_LAST_4`, `Embed` gives `hidden` columns) or
concatenate them (`CONCAT_LAST_4`, `4 x hidden` columns, oldest layer first), each with CLS or
mean pooling. A model has to export its intermediate layers for this: four or more FP32 outputs
named `hidden_states.N` (also `hidden_states_N`, `hidden_state_N`), or one FP32
`[layer, batch, sequence, hidden]` output named `hidden_states`. A standard export does not have
them (export with `output_hidden_states=True` and name the outputs). When the model does not,
`HasHiddenLayers()` is false and `Embed` with another layer strategy returns Nil and `GetError()`
says so. `POOLER` pooling is the final layer's only.

```ruby
embedder->SetLayers(BertEmbedder->Layers->CONCAT_LAST_4);
embedder->SetPooling(BertEmbedder->Pooling->MEAN);
X := embedder->Embed(texts);   # 4 x hidden columns, or Nil with GetError() when the model lacks the layers
```

### Text classification with a fine-tuned model

`TextClassifier` (library `nlp_onnx`) runs a fine-tuned BERT-family classifier the way
Python's `pipeline("text-classification")` does. Point it at the model's folder: what
`save_pretrained` writes, with an ONNX export beside it, or a Hub repository's files as
downloaded.
- **Model:** `model.onnx` or `onnx/model.onnx`.
- **Tokenizer:** `tokenizer.json` (WordPiece), or `vocab.txt` with `tokenizer_config.json`.
- **Labels:** `config.json`'s `id2label`.
- **Scores:** softmax, or sigmoid for multi-label and one-label models, as `transformers`
  chooses them.

The parity test checks ids, logits, scores and labels against `transformers` on three model
shapes. Design: [docs/AI_FOUNDATION_DESIGN.md](AI_FOUNDATION_DESIGN.md) §7.5.

```ruby
use System.NLP.Onnx;

classifier := TextClassifier->New("distilbert-malware");     # the model folder
if(classifier->IsOpen()) {
    each(p in classifier->Classify(["CreateFileW WriteFile CloseHandle", "RegSetValueExW"])) {
        p->GetLabel()->PrintLine();      # e.g. malware, from config.json's id2label
        p->GetScore()->PrintLine();      # its probability
    };
    classifier->Close();
};
```

### Models from the Hugging Face Hub

`HubClient` (library `ai_hub`) downloads a model repository's files by name into a local
cache, as Python's `from_pretrained` does, and returns the snapshot folder the model classes
open.
- **Pinned to one commit:** every file of a snapshot comes from the same commit.
- **Verified:** each file is checked against the Hub's SHA-256 (large files) or git blob hash
  (small files), and its size. A mismatch is not kept.
- **Streamed:** downloads go to disk, so file size is limited only by disk space.
- **Cached:** a second run downloads nothing, and `OBJECK_HUB_OFFLINE=1` works from the cache
  alone. Python's cache is read as a fallback, never written.
- **Tokens stay with the Hub:** a token (`HF_TOKEN`) is sent to the Hub only, never to its file
  CDN.

The default cache is `.objeck/hub` in your home folder; set `OBJECK_HUB_CACHE` to move it.
Design: [docs/AI_FOUNDATION_DESIGN.md](AI_FOUNDATION_DESIGN.md) §7.3.

**Compile:** `obc -src your.obs -lib gen_collect,cipher,json,net,ai_hub`

```ruby
use System.AI.Hub;

hub := HubClient->New();
folder := hub->Snapshot("Xenova/distilbert-base-uncased-finetuned-sst-2-english",
    ["config.json", "tokenizer.json", "tokenizer_config.json", "onnx/model.onnx"]);
if(folder = Nil) {
    hub->GetError()->ErrorLine();
};
```

### Topic modeling and BSON (System.NLP.Topics)

LDA topic modeling for text, with the steps of a Gensim workflow: decode a MongoDB `.bson` dump, tokenize, drop stopwords, build a dictionary and filter infrequent tokens, train, read the topics, measure their quality and draw them. It is one library, `nlp_topics.obl` (`-lib nlp_topics,json,plot,gen_collect`, or `-lib @ai`; it needs `json` for the BSON documents and `plot` for the picture). It was written for the topic-modeling lab of "Extracting Cyber Threat Intelligence from X Using AI" (book chapter 11), but nothing in it is specific to that data.

```ruby
use System.NLP.Topics;

texts := BsonReader->ReadStrings("CyberTweets.bson", "text");       # step 1: the "text" field of every document
docs := TopicTokenizer->New(TopicStopwords->English()->Union(TopicStopwords->Cyber()))->TokenizeAll(texts);

dict := TokenDictionary->New();                                       # step 2: Gensim's Dictionary
dict->AddDocuments(docs);
dict->FilterExtremes(5, 0.5, 0)->ToString()->PrintLine();             # size and % of token occurrences removed
corpus := dict->ToCorpus(docs);

lda := Lda->FitBest(corpus, dict, 10, 0.0, 0.0, 20, 50, 2000, 1, 5);  # step 3: 10 topics, best of 5 seeds
lda->GetTopic(0, 10)->ToString()->PrintLine();                        # step 4: top words and mixtures
mix := lda->GetDocumentTopics(corpus, 0);                             # Float[] that sums to 1
"bound {$lda->LogPerplexityBound(corpus)}, perplexity {$lda->Perplexity(corpus)}"->PrintLine();

cv := TopicMetrics->Mean(TopicMetrics->CV(lda->GetTopics(10), docs, 110));
TopicPlots->SaveReport(lda, corpus, 0, 0.6, 15, "topics.svg");        # step 5: a static counterpart of pyLDAvis
```

- **BSON.** `BsonReader` decodes a `.bson` file (documents back to back, each starting with its own little-endian length) into `Data.JSON` elements, one at a time with `HasNext`/`Next` or all at once; `ExtractStrings("user.name")` takes one string field, by dotted path, from every document and counts the ones it skipped. Every type is mapped: strings, numbers, documents and arrays as themselves, an ObjectId as its 24 hex digits, a datetime as an ISO-8601 string, binary, timestamp, regular expression and min/max key as small `$`-objects, and decimal128 is kept as its 16 raw bytes (`{"$decimal128": hex}`) and not decoded. Bad input does not crash or hang the reader: every length is checked against the bytes left, nesting deeper than 100 levels (MongoDB's own limit) is refused, and a truncated or hostile file gives `Nil` and a message with the byte offset in `GetError()`. The class documentation lists the mapping in full.
- **Tokens and stopwords.** `TopicTokenizer` lowercases, keeps runs of `a-z` and digits (so it suits English text; accented and non-Latin characters end a token), drops URLs and `@mentions`, keeps a hashtag's word and drops tokens that are too short, all digits or on the stopword list. It is its own tokenizer, and does not use `TextPreprocessor`, because tweets need the URL and mention handling and `TextPreprocessor` would add a dependency on `nlp.obl`; there is no stemming. The three stopword strategies of the lab are data: `TopicStopwords->New()` (none), `->English()` (the 179 words of NLTK's English list, embedded, so nothing is downloaded) and `->English()->Union(->Cyber())`. `Cyber()` is a small starting point, 28 words of Twitter noise and of words found in nearly every cybersecurity tweet, not a researched list; build your own with `FromArray`.
- **Dictionary.** `TokenDictionary` is Gensim's `Dictionary`: token to id, document and collection frequencies, `FilterExtremes(no_below, no_above, keep_n)` with Gensim's semantics (`no_above` times the number of documents, truncated; `keep_n` keeps the most frequent, ties to the smaller id; ids are compacted in their old order) and `ToCorpus` for the bag-of-words `TopicCorpus`. `FilterExtremes` returns a `FilterReport`: the dictionary size before and after and the percentage of token occurrences removed (and of distinct tokens).
- **LDA.** `Lda` is online variational Bayes, the algorithm of Hoffman, Blei and Bach and the one Gensim's `LdaModel` uses, with Gensim's parameters: `num_topics`, `alpha` (a number, or `SetAlphaMode("asymmetric")` or `"auto"`), `eta`, `passes`, `iterations`, a seed, plus `SetChunkSize`, `SetLearningRate` (offset and decay) and `SetGammaThreshold`. Passing 0 for `alpha` or `eta` means Gensim's default of `1/num_topics`. `GetTopic`/`GetTopics` give the top words with their probabilities, `GetDocumentTopics` the mixture of one document (a probability vector), `GetDocumentTopicMatrix` all of them, `GetLambda` the topic-word pseudo-counts in Gensim's layout, `Save` (or `Store`, the `System.ML` spelling) and `Load` the model in a file with a checksum, and `GetDocumentTopicMatrix2D`/`GetLambda2D` the same numbers as `Float[,]` matrices. **It is comparable in kind to Gensim, not numerically identical**: the random generator differs (so a seed gives another, equally valid, model), the E step starts each document from gamma = 1 instead of a random draw, and summation order differs. The same seed gives the same model on every run and platform here.
- **Local optima.** Variational Bayes starts from a random point and can end with two real topics merged and another split. On the planted-topic data of the tests roughly half of the seeds did, and the per-word bound of such a run is clearly lower. So train from several seeds and keep the best: `Lda->FitBest` does that, and `TopicMetrics->Stability` measures how alike two runs are.
- **Bound and perplexity.** `LogPerplexityBound(corpus)` is Gensim's `log_perplexity`: a variational lower bound on the log likelihood **per word**. It is negative and higher is better. It is **not** perplexity; `Perplexity(corpus)` returns `exp(-bound)`, lower is better. The lab table in `topic_lab.obs` prints both and says which is which.
- **Coherence.** `TopicMetrics` implements three measures after Roeder, Both and Hinneburg (2015), each the mean over a topic's word pairs, on the top-N words you pass in: `UMass` (document co-occurrence over the bag-of-words corpus, `log((D(wi,wj)/D + 1e-12) / (D(wj)/D))` over pairs with `j < i`; Gensim's `u_mass`), `CNpmi` (normalized PMI over sliding windows, Gensim's `c_npmi`, whose default window is 10) and `CV` (the NPMI context-vector cosine, Gensim's default `c_v`, window 110). The windows are cut from the token lists you pass in; a text no longer than the window is one window, an empty text one empty window, as in Gensim. Gensim's sliding window has a quirk that is reproduced on purpose so the numbers match: when it slides it removes the token that left even if the same word is still inside the window, so a word repeated within a window is under-counted (a plain per-window set count differed from Gensim by up to 0.18 on a test corpus). `Diversity` (unique words among all topics' top-k, divided by topics times k), `Overlap` (mean pairwise Jaccard of the top-k sets) and `Stability` (mean best-match cosine between two models' topic-word vectors, taken both ways) complete the lab's metrics.
- **Picture.** `TopicPlots` writes one SVG with an intertopic distance map (topics placed by classical MDS of the square roots of their Jensen-Shannon divergences, circle area proportional to prevalence) and the bar chart of a topic's most relevant terms (relevance with a lambda you choose, grey bar for the corpus frequency, colored bar for the part the topic explains). It is a simple static counterpart of pyLDAvis: no slider and no interactivity; the term ranking uses pyLDAvis's relevance formula. `Relevance` and `SalientTerms` return the numbers behind it.

Measured on a Ryzen 9 7950X3D (Windows, JIT on) with a synthetic corpus of 20,000 documents of 12 tokens, a vocabulary of 10,000 and 15 topics: tokenizing 0.5 s, dictionary, filter and corpus 0.3 s, `Fit` 0.28 s per pass (2.8 s for 10 passes, 8.2 s for 30, since the E step stops early once documents converge), `LogPerplexityBound` 0.3 s, c_v coherence 0.06 s. A corpus of 50,000 such documents trained 5 passes in 3.5 s. Memory is a few arrays of vocabulary times topics doubles.

Limits: one thread; text is English (ASCII tokens); `num_topics` times the vocabulary may not exceed 50 million; no live data was used, so the lab numbers in the example come from a planted synthetic corpus. What was checked against independent values is listed in the test headers: the model against a numpy/scipy port of the same algorithm, the BSON decoder against hand-encoded `struct.pack` fixtures and against bytes written by pymongo's `bson`, and the coherence measures against Gensim 4.4.0's `CoherenceModel` (`u_mass`, `c_npmi`, `c_v`; they agree to 1e-16, `programs/regression/nlp_topics_gensim_check.py`). Against Gensim's `LdaModel` on two planted-topic corpora (`tools/ml/lda_gensim_compare`) the per-word bounds agree to about 0.01 to 0.1 for fits of the same quality, a fit with merged topics scores clearly lower in both, and when both end in the same optimum the topics and document mixtures agree (cosine 1.000); the two systems do not land in the same local optimum from the same seed number.

The whole lab, with the five experiments of the assignment (topics, minimum frequency, stopword strategy, passes, alpha) printed as `System.ML.Lab` tables on a synthetic corpus with planted topics, is [`programs/examples/ml/topic_lab.obs`](../programs/examples/ml/topic_lab.obs).

---

## Machine Learning (System.ML)

Classical machine learning, pure standard library — no native model runtimes needed. The estimators share a common shape: `Fit`, `Predict` (and `PredictClass` for classifiers), `Score`, `IsFitted`, and, for most of them, `Store`/`Load` for model persistence (see [Model Persistence](#model-persistence)). Stochastic algorithms take a seed for reproducible results.

```
obc -src program.obs -lib ml,gen_collect,csv -dest program.obe
```

### Regression

```ruby
use System.ML;

# ordinary least squares with an intercept: y = 2x + 1
X := [[1.0], [2.0], [3.0], [4.0]];
y := [[3.0], [5.0], [7.0], [9.0]];

model := LinearRegression->New();
model->Fit(X, y, true);
"R²={$model->GetRSquared()} RMSE={$model->GetRMSE()}"->PrintLine();

preds := model->Predict([[5.0]]);
preds[0,0]->PrintLine();    # ~11.0
```

```ruby
# ridge (L2): closed form, shrinks coefficients as alpha grows
ridge := RidgeRegression->New(0.1);
ridge->Fit(X, y);

# lasso (L1): drives uninformative feature coefficients exactly to zero
lasso := LassoRegression->New(0.5);
lasso->Fit(Xs, ys);
coeffs := lasso->GetCoefficients();   # sparse

# elastic net mixes both penalties
enet := ElasticNet->New(0.1, 0.5);    # alpha, l1_ratio
enet->Fit(X, y);
```

### Classification

```ruby
# logistic regression with L2 regularization
log := LogisticRegression->New(0.5, 2000, 0.001);
log->Fit(X, y);                        # y holds 0.0 / 1.0 labels
labels := log->PredictClass(X);        # Bool[]
acc := log->Score(X, y);

# linear SVM (hinge loss) and perceptron share the same shape
svm := SVM->New(0.01, 0.1, 500);       # lambda, learning rate, epochs
svm->Fit(X, y);
margins := svm->Predict(X);            # signed distances from the plane

# kernel SVM (C-SVC): scikit-learn's SVC parameters, LIBSVM's solver
kernel_svm := KernelSVM->New(SvmKernel->RBF, 10.0, 0.1);   # kernel, C, gamma
kernel_svm->Fit(Xs, labels);           # Bool[] labels; scale features first
scores := kernel_svm->DecisionFunction(Xs_test);           # signed margins
recall := Metrics->RecallAtFpr(scores, labels_test, 0.001);
# LINEAR, RBF, POLY, SIGMOID; gamma 'scale' (default) or SetGammaAuto();
# SetClassWeightBalanced(), SetCacheSize(mb), GetSupportVectorCount()

# one-class SVM: fit on normal rows, flag what falls outside
detector := OneClassSVM->New(SvmKernel->RBF, 0.05);       # kernel, nu
detector->Fit(Xs_normal);
anomaly := detector->PredictScore(Xs_test);   # -decision: higher = stranger
flags := detector->IsOutlier(Xs_test);        # Bool[]; Predict gives +1/-1

# multiclass Gaussian naive Bayes over integer labels 0..C-1
gnb := GaussianNaiveBayes->New();
gnb->Fit(X, y);
classes := gnb->PredictClass(X);       # Int[]
```

```ruby
# trees and ensembles work on Bool[,] rows: features..., label LAST
data := [
  [false, false, false],
  [false, true,  false],
  [true,  false, false],
  [true,  true,  true]];    # label = f0 AND f1

tree := DecisionTree->New(4, 1);       # max depth, min samples
tree->Fit(data);
tree->Predict([true, true])->PrintLine();

forest := RandomForest->New(16);       # bootstrap + majority vote
forest->Fit(data);
forest->Score(data)->PrintLine();
scores := forest->PredictScore(data);  # votes_true / ntrees per row, for Metrics->AucRoc and RecallAtFpr

booster := AdaBoost->New(16);          # boosted decision stumps
booster->Fit(data);
```

```ruby
# continuous-feature forest: Float[,] rows, Bool[] labels, scores to threshold
forest := RandomForestClassifier->New(100, -1, 1, 42);   # trees, max_depth (-1 = None), min_samples, seed
forest->SetMinSamplesLeaf(5);          # scikit-learn's min_samples_leaf
forest->Fit(X, labels);
scores := forest->PredictScore(X_test);
importances := forest->GetFeatureImportances();   # mean decrease in Gini, sums to 1
seconds := forest->GetFitSeconds();
```

Runnable lab sweeps (SVM kernel / C / gamma, forest trees / depth / leaf size, on
NSL-KDD or synthetic data of its shape) are in `programs/examples/ml/`.

```ruby
# k-nearest neighbors with an exact KDTree index
tree := KDTree->New(matrix);
nearest := tree->Nearest(3, [57.0, 170.0]);   # row indexes, closest first
```

### Clustering & Decomposition

```ruby
# DBSCAN discovers the cluster count and labels outliers -1
scanner := DBSCAN->New(0.8, 3);        # eps, min points
scanner->Fit(X);
labels := scanner->GetLabels();
count := scanner->GetNumClusters();

# Gaussian mixture: soft clustering by EM, reproducible per seed
gmm := GaussianMixture->New(2, 7);     # components, seed
gmm->Fit(X);
resp := gmm->Predict(X);               # responsibilities
hard := gmm->PredictClass(X);

# PCA: project to the top components, reconstruct, explain variance
pca := PCA->New(1);
pca->Fit(X);
reduced := pca->Transform(X);
ratios := pca->GetExplainedVarianceRatio();
```

### Neural Network

```ruby
use System.ML, Collection;

# feed-forward network with hidden/output bias: learns XOR
inputs := Vector->New()<FloatMatrixRef>;
inputs->AddBack(FloatMatrixRef->New([[0.0], [0.0]]));
inputs->AddBack(FloatMatrixRef->New([[0.0], [1.0]]));
inputs->AddBack(FloatMatrixRef->New([[1.0], [0.0]]));
inputs->AddBack(FloatMatrixRef->New([[1.0], [1.0]]));

targets := Vector->New()<FloatMatrixRef>;
targets->AddBack(FloatMatrixRef->New([[0.01]]));
targets->AddBack(FloatMatrixRef->New([[0.99]]));
targets->AddBack(FloatMatrixRef->New([[0.99]]));
targets->AddBack(FloatMatrixRef->New([[0.01]]));

# inputs, hidden factor, outputs, rate, iterations
network := NeuralNetwork->Train(2, inputs, 8, 1, targets, 0.5, 12500);
network->Confidence(FloatMatrixRef->New([[0.0], [1.0]]))->PrintLine();  # ~0.99
```

### Classifier head over embeddings

`EmbeddingClassifier` trains a logistic or multi-layer ReLU head with binary cross-entropy by
minibatch Adam or SGD, at a configurable learning rate, with class weights for imbalance and a
per-epoch training-loss history. It is the trainable half of a frozen-encoder classifier: feed
it `BertEmbedder` rows (see Natural Language Processing above) or any fixed-size features.

```ruby
use System.ML;

head := EmbeddingClassifier->New([64], 0.001, 20, 32, 42);   # hidden sizes, rate, epochs, batch, seed
head->SetBalancedWeights();                                 # or SetClassWeights(negative, positive)
head->Fit(X_train, y_train);                                # Float[,], Bool[]
loss := head->GetLossHistory();                             # one value per epoch, for a loss plot
probabilities := head->PredictProba(X_test);
labels := head->PredictClass(X_test, 0.5);
```

### Model Persistence

`Store`/`Load` round-trip the linear models (`LinearRegression`, `RidgeRegression`, `ElasticNet`, `LassoRegression`, `LogisticRegression`, `Perceptron`, `SVM`), `GaussianNaiveBayes`, `GaussianMixture`, `PCA`, `NeuralNetwork`, `DecisionTree`, `RandomForest`, `AdaBoost`, `RegressionTree` and `GradientBoostedTrees`; loaded models reproduce the original predictions exactly. `KernelSVM`, `OneClassSVM`, `RandomForestClassifier`, `EmbeddingClassifier`, `KMeans`, `DBSCAN`, `KNearestNeighbors` and the outlier detectors have no `Store`/`Load`: refit them (they are seeded, so a refit is reproducible). `LassoRegression` loads back as an `ElasticNet`.

```ruby
model->Store("model.dat");
loaded := LinearRegression->Load("model.dat");
loaded->Predict(X);
```

`Load` answers Nil for a file that is missing, not a zlib stream (a truncated download, a file from another tool) or not a model of that type, rather than ending the program. Check for Nil, as above. `System.IO.Deserializer` has the same two pieces for your own formats: `IsOk()` is false when the bytes were Nil, damaged or not zlib, and every read then answers its zero value; `Byte->TryUncompressZlib(bytes)` is `UncompressZlib` that returns Nil instead of ending the program.

### Bad input

`Fit`, `Predict`, `Transform` and `Score` do not end the program on a Nil, an empty or a wrong-width matrix, or on targets that do not match its rows. They answer a failure value instead, as scikit-learn raises a `ValueError` you can catch. What that value is depends on the method:

| Call | On bad input |
|---|---|
| `Fit` of the matrix models (the linear models, `GaussianNaiveBayes`, `PCA`, `FeatureScaler`, `GaussianMixture`, `DBSCAN`, the tree and boosting classes, `KernelSVM`, `OneClassSVM`, `AdaBoost`) | `false` |
| `DenseNetwork->Fit` | a Float below 0.0 (the loss otherwise) |
| `DecisionTree->Fit` and `RandomForest->Fit` (the `Bool[,]` ones) | Nil, as always: the failure shows in `IsFitted()` |
| `Predict`, `PredictScore`, `PredictAll`, `Transform` | Nil |
| `Score` | `0.0` |
| `Predict(row)` of `RegressionTree` and `DecisionTree`, whose result has no room for an error | `0.0` / `false`, for a row of another width than the tree was fitted on |

`Score` returns `0.0` for bad input, which is also a real accuracy or R-squared. To tell them apart, call `Predict` on the same matrix first (it is Nil for bad input), or check `IsFitted()`. `Metrics->ThresholdAtFpr` has the same limit: it returns `0.0` for bad input and for a real threshold of 0.0; `Metrics->RecallAtFpr` returns -1.0 for the same input, so check that first.

A model remembers the width it was fitted on, so a feature matrix with a different column count at predict time (the usual mistake, and the one a changed CSV produces) gets Nil. A `Bool[,]` model takes the width of its training matrix, label column included, for `Score` and either that width or one less for `Predict`. A tree or forest read back from a file has no width on record, so only the shape is checked there.

A `Fit` that is refused for its arguments leaves most models as they were: the linear models, the trees and boosters, `GaussianNaiveBayes`, `GaussianMixture`, `PCA`, `FeatureScaler`, `DBSCAN`, `AdaBoost`, the `Bool[,]` trees and forests and `DenseNetwork` (which has no fitted flag, only its weights). `IsFitted()` stays true and `Predict` answers from the earlier fit, as scikit-learn leaves the old model when `fit` raises. `KernelSVM`, `OneClassSVM`, `EmbeddingClassifier` and the outlier detectors mark themselves unfitted at the start of `Fit`, so a refused refit leaves them unfitted, and so does a `Fit` that fails inside the solver (a singular system). Check the result of `Fit`, or `IsFitted()` on a model you meant to fit for the first time, rather than assuming either. `programs/regression/ml_input_validation.obs` runs every model against Nil, empty, wrong-width and mismatched-target inputs.

```ruby
if(<>model->Fit(X, y)) {
  "could not fit: check that X and y have the same number of rows"->ErrorLine();
};
preds := model->Predict(X_new);
if(preds = Nil) {
  "X_new does not have the columns the model was fitted on"->ErrorLine();
};
```

### Lab support (System.ML.Lab)

The plumbing around a parameter sweep, in `ml_lab.obl` (`-lib ml,ml_lab,csv,json` or `-lib @ai`): a deterministic stratified hold-out split, class-ratio resampling of the training set against a fixed test set, a wall-clock stopwatch, a sweep runner that collects accuracy, precision, recall, F1, FPR, false negatives and fit time per value (mean and standard deviation over seeds), a result table that renders as text, Markdown, CSV and JSON, and standalone SVG charts. Bad input raises `LabError` with a message naming the problem. Design: [ML_LAB_SUPPORT_DESIGN.md](ML_LAB_SUPPORT_DESIGN.md); a complete run is [`programs/examples/ml/lab_sweeps.obs`](../programs/examples/ml/lab_sweeps.obs).

```ruby
use System.ML, System.ML.Lab;

# 70/30 stratified split from a seed; every stratum lands in both halves
split := Partition->TrainTest(X, y, 0.3, 42, true);
scaler := FeatureScaler->New();
scaler->Fit(split->GetTrainX());            # training half only
train_z := scaler->Transform(split->GetTrainX());
test_z := scaler->Transform(split->GetTestX());

# the class-imbalance experiment: resample the TRAINING half, the test set is untouched
train := Resample->ToPositiveRatio(train_z, split->GetTrainY(), 0.05, 42);
```

```ruby
# a sweep: the Trial builds, fits under a Stopwatch and predicts for one value
class ForestTrial implements Trial {
  @s : DataSplit;
  New(s : DataSplit) { @s := s; }
  method : public : Run(value : Float, name : String, seed : Int) ~ TrialResult {
    forest := RandomForestClassifier->New(value->As(Int), 6, 2, seed);
    sw := Stopwatch->New();
    sw->Start();
    forest->Fit(@s->GetTrainX(), @s->GetTrainY());
    ms := sw->Stop();
    return TrialResult->New(forest->Predict(@s->GetTrainX()), @s->GetTrainY(),
      forest->Predict(@s->GetTestX()), @s->GetTestY(), ms);
  }
}

exp := Experiment->New("Random forest: number of trees", "n_estimators");
exp->SetValues([10.0, 50.0, 100.0, 200.0, 500.0]);
exp->SetSeeds([1, 2, 3]);                        # mean ± std per cell
exp->AddSetting("max_depth", "6");               # the footnote
table := exp->Run(ForestTrial->New(split));

table->ToText()->PrintLine();
table->Save("rf_trees.md");                      # .md, .csv, .json or text by extension
table->ToChart(["test_accuracy", "precision", "recall", "f1"])->Save("rf_trees.svg");
table->ToChart(["train_accuracy", "test_accuracy"])->Save("overfitting.svg");
```

```ruby
# charts on their own: lines with error bars, a log x axis, grouped bars, a loss curve
c := Chart->New("Recall vs C", "C", "recall");
c->SetLogX(true);
c->AddLine("rbf", cs, recall_rbf, recall_std);
c->Save("recall_vs_c.svg");

loss := Chart->LossCurve("Training loss", ["lr=0.1", "lr=0.01"], losses);  # rows = series, columns = epochs
```

A cell, a deviation or a chart point that is NaN or infinite is a missing value, not a number: `System.Stats` returns NaN by design (a degenerate fit has no p-value), and it can flow straight into a `ResultTable`. `ToText` and `ToMarkdown` write `n/a`, `ToCsv` writes an empty field, `ToJson` writes `null` (so the document is valid JSON), and `FromCsv` and `FromJson` read an empty field or `null` back as NaN (an infinity does not come back: it becomes NaN). A `Chart` skips a point whose x or y is not finite, breaks the line there, and draws no error bar for a non-finite error; a series with no finite point draws nothing. Finite values are written exactly as before. `ResultTable->IsFinite(v)` is the test.

**Number formats.** A column is "fixed" (its decimals, the default and the only form before) unless you ask: `AddIntegerColumn(name)` writes a count as `176` instead of `176.0000`, `AddPValueColumn(name)` writes `0.0471` but `<0.0001` for anything smaller (a tiny p-value no longer reads as `0.0000`), `AddScientificColumn(name, decimals)` writes `1.23e-05`, and `AddColumn(name, decimals, "fixed" | "sci" | "p" | "int")` is the general form. A format changes only how `ToText` and `ToMarkdown` (and so a `System.ML.Report` table) spell a cell, including the deviation of `mean ± std`; the number itself is unchanged. `ToCsv` and `ToJson` write at most 10 decimals, which would turn a p-value of 1e-12 into 0, so in a `"sci"` or `"p"` column `ToCsv` writes a value below 1e-4 in exponent form with its full digits (`1.23e-12`) and `FromCsv` reads it back; `ToJson` keeps its 10 decimals (the JSON reader does not read exponents) and stores the formats so `FromJson` restores them. `GetColumnPrecision(name)` and `GetColumnFormat(name)` read a column's settings back (`-1` and `""` for an unknown column). `ResultTable->FormatValue(value, decimals, format)` is the formatter itself. Scientific text matches Python's `'%.*e'` (checked on 600 random values).

---

### Local vector search (System.ML.VectorSearch)

A local index for retrieval over your own documents, with no hosted vector store: keep an embedding per document under a string id and ask for the closest ones to a query vector. It is `ml_vector.obl` (`-lib ml_vector,gen_collect` or `-lib @ai`) and needs nothing from `System.ML`. The vectors can come from anywhere that gives a `Float[]`: `BertEmbedder` (see [BERT tokens and sentence embeddings](#bert-tokens-and-sentence-embeddings)), the OpenAI, Gemini and Ollama embedding calls, or a file.

```ruby
use System.ML.VectorSearch;

index := VectorIndex->New(384, VectorMetric->COSINE);   # DOT and L2 too
index->Add("doc-1", embedding_1);      # Float[] of exactly 384 values; false if refused
index->Add("doc-2", embedding_2);

hits := index->Search(query_embedding, 5);     # the 5 closest, closest first
each(i : hits) {
  "{$hits->Get(i)->GetId()}  {$hits->Get(i)->GetScore()}"->PrintLine();
};

index->Remove("doc-1");
index->Store("docs.vix");
loaded := VectorIndex->Load("docs.vix");       # Nil if the file is not a good index
```

- **Metrics.** `COSINE` and `DOT` score higher-is-closer (the cosine similarity or the dot product); `L2` scores the Euclidean distance, so lower is closer. `Search` returns the hits closest first for all three.
- **Exact.** A search scans every vector, so it always returns the true top k; there is no approximate mode. Ties on the score are broken by id, smallest first, so the answer does not depend on the order vectors were added or removed in. That holds for bit-identical scores: vectors that tie only mathematically (parallel vectors of different length under `COSINE`, say) can differ in the last bit and then rank by that bit, not by id. `COSINE` scores are clamped to [-1, 1]. `Add` of an id that is already there replaces its vector, and `Get` and `Add` copy, so the caller's arrays never alias the index.
- **Bad input returns a value.** A wrong-length, `Nil`, NaN or infinite vector, a `Nil` or empty id, `k` below 1 and an index created with a dimension below 1 never end the program: `Add`, `Remove` and `Store` return `false`, `Search` returns an empty `Vector` (never `Nil`) and `Get` returns `Nil`, and the index's `GetError()` says why. `Load` is a function with no index to ask, so it returns `Nil` and gives no reason. Cosine scores a zero-length vector as 0.
- **Value range.** A nonzero value must have a magnitude between 1e-100 and 1e100 (zero is fine); `Add` and `Search` refuse anything else, and say so. Past that range a squared length overflows or underflows, so a cosine would silently read 0 and a dot product could turn NaN and break the ranking. Rescale such data first. Inside the range every score is finite.
- **Files.** `Store` writes a small header (a format tag and a CRC32 of the body) and then the body through `System.IO.Serializer`, the path the `System.ML` models use. `Load` checks the tag, the three reserved header bytes (zero) and the CRC32 *before* it decompresses anything, so a missing, truncated, damaged or foreign file gives `Nil`. (The `System.ML` model loaders cannot do that yet: see [LIBRARY_BACKLOG_2026_11.md](LIBRARY_BACKLOG_2026_11.md) item 1.)
- **Memory.** Objeck's `Float` is 64 bits, so a vector of 384 values takes 3 KB: 100,000 of them take about 307 MB, plus the ids. The first `Add` at the largest dimension (1,048,576) allocates a 128 MB block, and `Load` holds the file, its body and the decoded vectors at once, about three times the file size at its peak.
- **Threads.** An index is not thread-safe: `Search` records a refusal in the index, so concurrent calls, or a search beside `Add` or `Remove`, need the caller's lock.

Measured on a Ryzen 9 7950X3D (Windows, JIT on, `-opt s3`), random vectors of 384 values, one thread, 20 queries after a warm-up, top 10:

| Vectors | Search (cosine, dot or L2) | Build by `Add` | `Store` / file | `Load` |
|---|---|---|---|---|
| 20,000 | 8.8 ms | 0.27 s | | |
| 100,000 | 44 ms (about 23 queries/s) | 1.4 s | 7.0 s / 295 MB | 1.5 s |

The three metrics cost the same, and a four-accumulator unrolled inner loop was no faster: at 100,000 x 384 a search streams 307 MB, so it is limited by memory bandwidth (about 7 GB/s) and not by arithmetic. An approximate index (HNSW) was not built: 44 ms per query is fast enough for retrieval over a document collection of this size, and an approximate answer brings recall to measure, a second file format and a build step with it. If you need far more vectors or queries per second, the next step is HNSW or an inverted-file index, not a faster scan.

A complete run (a small corpus, embedded either with a BERT model or, with nothing to download, a hashed bag of words, then queried, saved and loaded) is [`programs/examples/ml/vector_search.obs`](../programs/examples/ml/vector_search.obs). It is tested by `ml_vector_exact.obs` (against a brute-force reference, ties, `k` past the end, removals), `ml_vector_edges.obs` (value range, block boundaries, emptying an index, near-ties), `ml_vector_validation.obs` and `ml_vector_persist.obs` (round trip and corrupt files).

### Replicates and reports (System.ML.Report)

`ml_report.obl` (`-lib @ai`, or `ml_report,ml_lab,ml,stats,plot,csv,json,gen_collect`) turns a study into a hand-in: a single document with numbered sections, results tables, figures, code and notes, that opens in a browser and prints to PDF. It is pure Objeck, so it runs in the web playground. No call ends the program on bad input: a call that cannot do its job returns `false` (or `Nil`) and `GetError()` says why, `""` when there is nothing to report. Output is deterministic (nothing reads a clock), so a test can compare exact strings.

`Replicates` collects the value of each metric per seed and per setting (group). A statistic leaves out a value that is not finite (NaN, an infinity, or the marker given to `SetMissing`, for the `-1.0` that `Metrics->AucRoc` returns) and `GetMissing` counts them; a statistic that needs more values than there are is NaN, so one seed has a mean and an undefined standard deviation.

```ruby
use System.ML.Report;
use System.ML.Lab;
use System.Stats;

rep := Replicates->New();                         # 95% level; SetConfidence(0.99) changes it
each(seed : 5) {
  rep->AddRun("depth 2", seed, ["test_accuracy", "f1"], [acc2[seed], f12[seed]]);
  rep->AddRun("depth 8", seed, ["test_accuracy", "f1"], [acc8[seed], f18[seed]]);
};
rep->GetMean("depth 2", "test_accuracy")->PrintLine();
rep->GetStd("depth 2", "test_accuracy")->PrintLine();        # sample (n - 1) standard deviation
rep->GetCiLow("depth 2", "test_accuracy")->PrintLine();      # t interval of the mean, n - 1 df
t := rep->Paired("depth 8", "depth 2", "test_accuracy");     # a TTestResult, paired by seed
"{$t->GetT()} {$t->GetP()} {$t->GetCohenD()}"->PrintLine();
```

Seeds pair the settings, not their position: add them in any order. `Paired` uses the seeds both settings share (`GetPairedCount`), `Welch` does not need shared seeds. Also `GetCount`, `GetStdError`, `GetMin`, `GetMax`, `GetCiHigh`, `GetValues` and `GetSeeds`. A repeated seed under the same setting and metric is refused.

`ResultTables` converts to and from a `System.ML.Lab` `ResultTable` (the Lab library is not changed): `FromReplicates(rep, metric, title, parameter)` (a row per setting: `mean`, `std`, `ci_low`, `ci_high`, `n`, `missing`), `MeansFromReplicates` (a column per metric, `mean ± std`), `FromComparison` (a row per metric: both means, the difference, its interval, t, df, p, Cohen's d, pairs), `FromOls`, `FromTTests`, and `ToCells` / `FromCells` for a grid of text.

`ResultTables->FromCsv(text)` reads a CSV written by `ResultTable->ToCsv` and, unlike `ResultTable->FromCsv`, which raises a `LabError`, returns `Nil` with the reason in `GetError()` for text that is empty, not CSV, or not in that layout (heading `<parameter>,<parameter>_value,...`, optional `<column>_std` columns, unique column names). `ResultTables->CsvToCells(text)` turns any CSV into a grid of text (heading row first) for `AddTable`, again `Nil` with a reason on failure, and `ResultTables->CsvColumnKinds(grid)` says per column whether it is `"integer"`, `"number"`, `"text"` or `"empty"`. Note that `CsvTable` counts its header as row 0 (`Size()` is records plus one; data starts at `Get(1)`), and a CSV with no records or a ragged record is not parsed at all.

`Report` builds the document:

```ruby
report := Report->New("Assignment 3");
report->SetAuthor("A. Student");
report->SetDate("2026-11-19");                    # text you choose: the library never reads the clock
report->AddSection(1, "Chapter 4", "Clustering");            # a label, or AddSection(1, "heading") for "1", "1.1", ...
report->AddSection(2, "Experiment 1", "Choosing k");         # shown as "Chapter 4 / Experiment 1"
report->AddParagraph("Five seeds per setting.");
report->AddTable("Accuracy by depth", ResultTables->MeansFromReplicates(rep, "Means", "depth"));
report->AddResultTableChart("Mean accuracy", summary, ["mean"], "bar");   # System.Plot, embedded
report->AddFigure("A scatter plot", fig);                     # a System.Plot Figure, or SVG text
report->AddFigureFile("A PNG", "plot.png");                   # embedded as a base64 data URI
report->AddCode("obc -src study.obs ...");
report->AddNote("Synthetic data.");
report->Save("report.html");                                  # or report.md
```

`AddCsvTable(caption, csvText)` adds a table from CSV text: a Lab-layout CSV becomes a formatted `ResultTable` table, any other CSV a grid, and text that is not CSV is refused. For a study whose results arrive over time, `AddPlaceholderTable(caption, parameter, columns, rows)` adds a table whose cells all read `not run yet`, `AddFigureSlot(caption, "results/fig1")` adds `fig1.svg` (or `.png`) if it exists and otherwise a note that says which file to drop in (`GetError()` is `""` for a file that is merely absent, and says why for one that was refused), and `AddRunStatus(caption, files)` adds a table of expected files with `present` or `not run yet` and returns how many exist. `AddResultTableChart(caption, table, columns, kind, title, skipRows)` also sets the title drawn in the chart (`""` uses the table's title; the caption is the figure's caption below it) and leaves out the rows whose names you list, so a total row or a row with no value yet neither draws nor makes the chart refuse a value that is not finite.

Also `AddBullets`, `AddNumbered`, `AddChart` (a Lab `Chart`), `AddPageBreak`, `SetContents(false)` and `SetPaper("A4")` or `"Letter"`. Tables and figures are numbered and captioned automatically, and a table's cells are spelled as `ResultTable->ToMarkdown()` spells them (the column's decimals, `mean ± std`, NaN as `n/a`). Text is plain text: `<`, `&`, quotes, backticks, pipes, asterisks and a line that starts like Markdown are escaped in both formats, so cell text cannot inject markup; an SVG with a script, an event handler or a `javascript:` link is refused.

The HTML (`ToHtml()`) is one file with its styles inline and no external request: a table of contents, inline SVG that scales to the page width, tables whose rows do not split across printed pages and whose header repeats, and a print stylesheet. Open it in a browser and choose Print, Save as PDF. `ToMarkdown()` writes CommonMark with pipe tables; `Save("report.md")` writes the SVG figures beside it (`report-figure-1.svg`).

Word: Word opens an HTML file but does not draw its inline SVG reliably. For a `.docx`, print the HTML to PDF from a browser, or convert the Markdown with pandoc if you have it, or use PNG figures (`PlotPng->Save` from `System.Plot.SDL` needs the SDL2 natives; `AddFigureFile` embeds the result). This library was checked in Chromium-based Edge and by parsing, and not in Word. Example: `programs/examples/ml/study_report.obs`; tests `ml_report_replicates` (statistics against numpy, scipy and statsmodels), `ml_report_build` and `ml_report_golden`.
### Face verification: FAR, FRR, EER and CLIP embeddings (System.ML.Verification, System.ML.Clip)

Two libraries for a verification study such as "does a CLIP image embedding separate same-person pairs from different-person pairs on Labeled Faces in the Wild". `ml_verify.obl` (in `@ai`, needs only `gen_collect`, pure Objeck, so it runs in the web playground) holds the metrics. `ml_clip.obl` (in `@vision`; needs `onnx`, `opencv`, `models`, `ml_verify`) holds the CLIP image encoder and the pair reader. No call in either ends the program on bad input: a call that cannot do its job returns a failure value (`Nil`, `false`, or a result whose `IsValid()` is false) and `GetError()` says why, `""` when there is nothing to report. A scalar with no value is `NaN`.

**Metrics.** A pair is accepted when its similarity score is greater than or equal to the threshold. FAR (false acceptance rate) is impostor pairs accepted over impostor pairs, FRR (false rejection rate) is genuine pairs rejected over genuine pairs, and the EER (equal error rate) is where the two are equal. `Verification->Evaluate(scores, same)` takes one score per pair (higher means more alike, finite) and a `Bool[]` that is true for a same pair, and returns a `VerificationResult`:

```ruby
use System.ML.Verification;

r := Verification->Evaluate(scores, same);
if(<>r->IsValid()) {
  r->GetError()->ErrorLine();            # "no impostor (different) pair: FAR is undefined", "score 3 is not finite", ...
  return;
};
"EER {$r->GetEer()} at threshold {$r->GetEerThreshold()}"->PrintLine();
far := r->FarAt(0.65);                   # FAR, FRR and accuracy at any threshold
frr := r->FrrAt(0.65);
acc := r->AccuracyAt(0.65);
best := r->GetBestAccuracyThreshold();   # the threshold with the best accuracy (ties: the lowest)
frr_1 := r->GetFrrAtFar(0.01);           # FRR when FAR may not exceed 1%
t_1 := r->GetThresholdAtFar(0.01);
```

The curve has one operating point per distinct score plus a last point at +Infinity that accepts nothing (FAR 0, FRR 1), thresholds ascending: `GetThresholds`, `GetFarCurve`, `GetFrrCurve`, or all three as rows with `GetCurve`. Scores that are equal share one point, so a tie is never split. The EER rule: `d = FAR - FRR` is 1 at the first point and -1 at the last and never increases; the EER is read at the first point where `d <= 0`. If `d` is exactly 0 the EER is that point's FAR and the threshold is its score; otherwise the two rates cross between two points and both the EER and the threshold are interpolated linearly along that segment (`lambda = d(j-1) / (d(j-1) - d(j))`), which is what interpolating the ROC polyline with scipy gives; when the crossing is after the highest score, the threshold reported is the highest score. `GetRocCurve` returns `[threshold, FAR, 1 - FRR]` rows in the layout `Metrics->RocCurve` returns, so `Axes->RocCurve("name", r->GetRocCurve())` from `System.Plot` draws it; `GetDetCurve` returns `[FAR, FRR]` rows for a DET plot (`Axes->SetLogX(true)` after dropping the rows where a rate is 0). `Verification->Far`, `Frr` and `Accuracy` give one number at one threshold in O(n) without building the curve, and `Verification->Check(scores, same)` returns the reason an input would be refused. `Similarity->Cosine(a, b)` and `Similarity->Normalize(v)` are the embedding helpers.

**CLIP encoder.** `ClipEncoder` runs an exported CLIP image tower (input `pixel_values` `[N, 3, 224, 224]`, output the embedding `[N, D]`; export the Hugging Face model with `torch.onnx.export` of `CLIPModel.get_image_features`, the text tower is not needed):

```ruby
use System.ML.Clip;
use System.ML.Verification;

encoder := ClipEncoder->New("clip-vit-base-patch32-image.onnx");
if(<>encoder->IsOpen()) {
  encoder->GetError()->ErrorLine();
  return;
};
pairs := FacePairs->ReadLfw("lfw_home/pairsDevTest.txt", "lfw_home/lfw_funneled");   # also pairs.txt, pairsDevTrain.txt, or ReadCsv
scores := encoder->ScorePairs(pairs);                    # cosine per pair; each distinct image is embedded once
r := Verification->Evaluate(scores, pairs->GetLabels());
"EER {$r->GetEer()}"->PrintLine();
```

`Embed(path, normalize)`, `EmbedImage(image, normalize)` and `EmbedBatch(paths, normalize)` give raw or L2-normalised embeddings (the batch size is `SetBatchSize`, 16 by default). `ClipPreprocessor` is what CLIPImageProcessor does: resize the shortest side to 224 with a bicubic filter, centre-crop 224 x 224, RGB, scale to 0..1, normalise with the CLIP mean (0.48145466, 0.4578275, 0.40821073) and standard deviation (0.26862954, 0.26130258, 0.27577711), CHW. Its default resize, `ClipPreprocessor->Resize->PIL_BICUBIC`, is Pillow's bicubic written in Objeck, with Pillow's antialiasing (the filter support grows with the shrink factor), 22-bit fixed-point coefficients, 8-bit intermediate rounding and the order of the two passes Pillow chooses (that order is an empirical rule, fitted on 1,478 random sizes and not read from Pillow's source). `ClipPreprocessor->Resize->OPENCV_CUBIC` is `cv::resize` with `INTER_CUBIC` instead. A model that was exported with a different input size takes `ClipPreprocessor->New(size, mode)` and `ClipEncoder->New(path, config, pre)`.

**Why Pillow's resize and not OpenCV's.** OpenCV's `INTER_CUBIC` does not antialias when it shrinks an image (it reads four pixels per output pixel, whatever the scale) and uses the kernel with `a = -0.75`; Pillow uses `a = -0.5` and widens the kernel by the shrink factor. On 12 test images (10 LFW faces, one 400 x 250 and one 180 x 320 image, so the aspect-ratio resize and the centre crop are exercised) the Objeck preprocessing with `PIL_BICUBIC` differs from `CLIPImageProcessor`'s `pixel_values` by at most 2.8e-7 (the reference is float32; the difference is float32 rounding), and with `OPENCV_CUBIC` by at most 0.26 in normalised units (about 18 grey levels, at one pixel); the embeddings differ from PyTorch's by at most 1.5e-5 (ViT-B/32), 1.1e-5 (ViT-B/16) and 4.8e-5 (ViT-L/14) with `PIL_BICUBIC`, and by up to 0.20, 0.28 and 0.38 with `OPENCV_CUBIC` (cosine to the PyTorch embedding at worst 0.9974, 0.9972 and 0.9945). The ONNX towers themselves agree with PyTorch to 1.1e-5, 1.1e-5 and 1.6e-5 on the same pixels. Two more differences from a Pillow pipeline: OpenCV decodes the image, so a JPEG decoder difference of a grey level is possible (none was seen on LFW), and it applies an EXIF orientation tag, which Pillow does not.

**LFW results.** CLIP image encoders, cosine similarity of the L2-normalised embeddings, LFW funneled images, the standard pair lists. EER and the best accuracy are read from the whole curve; FRR@FAR is the false rejection rate at the highest threshold whose FAR is at most 1% or 0.1%. The first table is `pairs.txt`, 6,000 pairs (3,000 same, 3,000 different, 7,701 distinct images, 10 folds), the second `pairsDevTest.txt`, 1,000 pairs (500 and 500, 1,549 images). Times are the whole of `ScorePairs` (image decoding, the preprocessing in Objeck, the model on ONNX Runtime's CPU provider, restricted to 8 logical processors of a Ryzen 9 7950X3D); the preprocessing is about 15 ms per image, so the three models cost about the same.

| model (6,000 pairs) | EER | EER threshold | best accuracy | FRR @ FAR 1% | FRR @ FAR 0.1% | seconds |
|---|---|---|---|---|---|---|
| ViT-B/32 (512) | 5.467% | 0.6862 | 94.633% | 18.533% | 40.633% | 115 |
| ViT-B/16 (512) | 4.267% | 0.7163 | 95.767% | 13.000% | 31.100% | 121 |
| ViT-L/14 (768) | 2.700% | 0.6571 | 97.350% | 7.033% | 25.933% | 143 |

| model (1,000 pairs) | EER | EER threshold | best accuracy | FRR @ FAR 1% | FRR @ FAR 0.1% | seconds |
|---|---|---|---|---|---|---|
| ViT-B/32 | 4.800% | 0.6889 | 95.400% | 11.400% | 49.000% | 21 |
| ViT-B/16 | 4.200% | 0.7126 | 95.900% | 10.400% | 54.800% | 23 |
| ViT-L/14 | 2.800% | 0.6442 | 97.500% | 4.000% | 19.600% | 28 |

These were checked twice from outside Objeck. The metrics: scikit-learn's `roc_curve` and the interpolation rule above, applied in Python to the scores Objeck wrote, give the same numbers to the digits shown (the regression test compares the library with the same independent code to 1e-12). The encoder: a pure-Python run (`CLIPImageProcessor` and the PyTorch `CLIPModel`, `tools/ml/clip_lfw_reference.py`) over the same pairs gives the same EER, EER threshold, best accuracy and FRR at FAR as Objeck's, to the digits shown, for all three models on the 1,000-pair list and for ViT-B/32 on the 6,000-pair list. Per pair, Objeck's cosine and PyTorch's differ by at most 2.1e-6 (B/32), 1.9e-6 (B/16) and 5.1e-6 (L/14) on the 1,000-pair list and 2.6e-6 (B/32) on the 6,000-pair list. The FRR at a very low FAR rests on a handful of impostor pairs (3 impostor pairs at FAR 0.1% of 3,000), so it moves a lot from one list or model to another; the EER is the steadier number.

Example: [`programs/examples/ml/clip_face_verification.obs`](../programs/examples/ml/clip_face_verification.obs) (`obr clip_face_verification.obe MODELS PAIRS IMAGES_DIR [STEP [SCORES_CSV [ROC_SVG]]]`) prints the table above for one or several models, and writes the scores and a ROC figure; [`clip_embed_check.obs`](../programs/examples/ml/clip_embed_check.obs) is the preprocessing and embedding comparison against Python (`tools/ml/clip_reference.py`). Tests: `ml_verify_metrics` (14 score sets against scikit-learn and numpy, `ml_verify_golden/gen_expected.py`: ties, one score for every pair, perfect and inverted separation, tiny inputs, invalid input), `ml_clip_pairs` (the pair readers), `ml_clip_preprocess` (seven synthetic images against CLIPImageProcessor at 32 x 32) and `ml_clip_encoder` (a tiny ONNX tower, generated by `data/clip/gen_tiny_clip.py`, against onnxruntime; the last two skip when the ONNX or OpenCV native library is not deployed). What was not run: Linux, macOS and ARM64.

### Convolutional networks (System.ML.Cnn)

Convolutional networks for image classification, on the CPU (and, optionally, on an NVIDIA GPU: see [the CUDA backend](#the-optional-cuda-backend) below): `Conv`, `MaxPool`, `ReLU`, `Flatten`, `Dense` and `Dropout` layers with forward and backward passes, the Adam and SGD optimizers, binary cross-entropy on logits, an image dataset with a seeded train / validation / test split and on-the-fly augmentation, and a training loop that records loss, accuracy and wall time for every epoch. The layers are native (`libobjk_ml`, plain C++ and Eigen, nothing platform-specific: no intrinsics, no OpenMP) and the library is `ml_cnn.obl`: `obc -lib ml_cnn,ml,ml_lab,opencv,csv,json,cipher,gen_collect` or `-lib @cnn,gen_collect`. It reads image files through `opencv`, scores with `System.ML.Metrics` and writes its history as a `System.ML.Lab.ResultTable`.

```ruby
use System.ML.Cnn;

data := ImageDataset->New(224, 3);              # every image is resized to 224 x 224
data->AddDirectory("real", 0);                  # class 0
data->AddDirectory("fake", 1);                  # class 1
splits := data->Split(0.7, 0.15, 42);           # train, validation, test: seeded, repeatable
aug := Augmentation->New();
aug->SetFlip(0.5);                              # half the training images are mirrored
aug->SetRotation(0.3, 10.0);                    # 30% are turned by up to +-10 degrees
aug->SetBrightness(0.3, 0.8, 1.2);              # 30% are scaled by 0.8 .. 1.2
splits[0]->SetAugmentation(aug);                # augments the training subset only

# the study's baseline: three Conv 3x3 + ReLU + MaxPool blocks (16, 32, 64 filters),
# Dense 128, dropout 0.1, Adam at 1e-4; 5,561,633 parameters at 224 x 224
model := CnnModel->Baseline(224, 3, 0.1, 128, 0.0001, 42);

trainer := CnnTrainer->New(model, splits[0], splits[1], 64, 42);   # batch size 64
trainer->SetVerbose(true);
history := trainer->Fit(10);                    # per epoch: train/validation loss and accuracy, seconds
history->SaveCsv("history.csv");
test := trainer->Evaluate(splits[2]);           # accuracy, precision, recall, F1, confusion matrix
test->ToString()->PrintLine();
model->Save("model.bin");
```

A different architecture is built layer by layer (each `Add` answers `false` and `GetError()` says why if the layer does not fit):

```ruby
model := CnnModel->New(3, 64, 64, 42);          # channels, height, width, seed
model->AddConv(32, 3, 1, Padding->SAME);        # filters, kernel, stride, padding
model->AddReLU();
model->AddMaxPool(2);
model->AddFlatten();
model->AddDense(64);
model->AddDropout(0.3);
model->AddDense(1);                             # the last layer is one logit per image
model->Compile(0.001);                          # Adam; CompileSgd(lr, momentum) for SGD
loss := model->TrainBatch(images, labels);      # Float[] of whole images (NCHW) and 0.0 / 1.0 labels
probabilities := model->Predict(images);        # sigmoid of the logit: P(class 1)
```

**What it is, exactly.**

| | |
|---|---|
| Layout | NCHW, `float32`; a batch is `Float[]` of whole images, each channels x height x width (planes, not interleaved), values 0 to 1. Weights are `[filters][channels][kh][kw]` for a convolution and `[units][inputs]` for a dense layer, the layouts of PyTorch |
| `Conv` | cross-correlation, square kernel, stride, bias; `VALID` (no padding) or `SAME` (output `ceil(size / stride)`, an even kernel's extra pixel at the bottom/right, as TensorFlow does). im2col and one matrix multiply per image, the column buffer reused |
| `MaxPool` | square window, stride defaults to the window, no padding, floor; a tie goes to the first maximum in scan order |
| `Dropout` | inverted: survivors are scaled by `1 / (1 - rate)` in training, evaluation is the identity. The mask is a function of the model seed, the forward-pass counter and the element index, so a run repeats and `ResetDropout()` replays a mask |
| Loss | binary cross-entropy on the logit, `max(z, 0) - z y + log(1 + e^-|z|)`, stable for logits of any size (a logit of 30,000 gives 30,000 or 0, not NaN) |
| Optimizers | Adam as `torch.optim.Adam` (bias-corrected moments, `eps` outside the square root), and SGD with optional momentum |
| Initial weights | Glorot (Xavier) uniform, limit `sqrt(6 / (fan_in + fan_out))`, the Keras default; `SetInitializer(Initializer->KAIMING)` for He uniform, limit `sqrt(6 / fan_in)`. Biases start at 0. Drawn from the model seed with a portable generator, so a seed gives the same weights on every platform |
| Threads | the batch is spread over a thread pool, default the hardware threads, at most 8 (`CnnRuntime->SetThreads(n)`). Work is split statically, so a run repeats bit for bit at a given thread count; different counts agree to rounding. The setting is process-wide (all models share one pool), and the default is the machine's hardware threads, so call `CnnRuntime->SetThreads(n)` with a fixed `n` when a result must be the same on another machine (the examples use 8) |
| Save / Load | weights only, in a small binary file with a digest of the architecture; `Load` is bit-exact and refuses a file from another architecture, a truncated file or a foreign one, leaving the model unchanged |
| Scope | binary classification (one logit per image). The default and the fallback is the CPU; an optional GPU backend (`model->SetDevice("cuda")`, below) implements the same layers. Layers are an interface over contiguous NCHW tensors (`core/lib/matrix/cnn.h`) |

Nothing here ends the program on bad input: an empty batch, a Nil array, a wrong size, a model that is not compiled or a layer that does not fit answers `false`, `-1.0`, `0` or `Nil`, and `GetError()` gives the reason. A model holds native memory, so call `Close()` when done (twice is harmless; `IsOpen()` says whether the model is alive). `GetError()` returns `""`, never `Nil`, when nothing went wrong. A model is not thread-safe: one thread at a time.

**Images.** `ImageDataset` holds file paths (decoded and resized when a batch needs them, or kept with `SetCache(true)`) and in-memory pixels. `AddDirectory` takes the `.jpg`, `.jpeg`, `.png` and `.bmp` files of a folder in name order, so the same folder always gives the same indexes; `Check()` decodes every file once and counts the ones that fail. `Split(train, validation, seed)` deals three `ImageSubset`s by a seeded shuffle (the rest is the test set); when the data is already in folders, make one dataset per folder and use `All(augment)`. Augmentation (`Augmentation`: flip, rotation by a random angle with bilinear interpolation, brightness factor) is applied as an image is staged for a batch and only on a subset made for training; whether each fires, and its value, is a function of the seed and the image's position in the epoch, so training repeats exactly. `ImageDataset->Synthetic(size, per_class, seed)` makes a procedural two-class set (low against high spatial frequency) for demos and tests with no files.

**Metrics.** `CnnTrainer->Fit(epochs)` returns a `TrainingHistory`: per epoch the training loss and accuracy (running means over the epoch's batches in training mode, as Keras reports them), the validation loss and accuracy (evaluation mode, after the epoch) and the wall time; `ToCsv()`, `SaveCsv(path)` and `ToResultTable(title)` write it out, with the column names of the `System.ML.Lab` experiment tables (`train_loss`, `train_accuracy`, `val_loss`, `val_accuracy`, `epoch_seconds`). `Evaluate(subset)` returns a `CnnEvaluation` with loss, accuracy, precision, recall, F1 (`System.ML.Metrics`, class 1 positive, threshold 0.5) the confusion matrix, `GetScores()` (the class-1 probability of each image) with `GetActuals()` for `Metrics->AucRoc` and ROC curves, and `ToResultTable(title)`. `GetParameterCount()` gives the parameter count: for a convolution `filters x (channels x k x k) + filters`, for a dense layer `units x inputs + units`. For the baseline at 224 x 224 that is 448 + 4,640 + 18,496 for the three convolutions, a flatten of 26 x 26 x 64 = 43,264 values, 128 x 43,264 + 128 = 5,537,920 for the hidden dense layer and 129 for the output: **5,561,633**. The baseline takes `CnnModel->Baseline(image_size, conv_blocks, dropout, dense_units, learning_rate, seed)`, so the experiments of the study (2, 3 or 4 blocks; dropout 0, 0.1, 0.3, 0.5; image size 64, 128, 224; learning rate 1e-3, 1e-4, 1e-5) are one-argument changes.

**Complete runs.** [`programs/examples/ml/cnn_synthetic.obs`](../programs/examples/ml/cnn_synthetic.obs) trains the baseline on a generated 64 x 64 set in seconds, with no data. [`programs/examples/ml/cnn_images.obs`](../programs/examples/ml/cnn_images.obs) reads image folders (`--data ROOT` for the `train/`, `valid/`, `test/` layout of the 140k Real and Fake Faces dataset, or `--real DIR --fake DIR` for a seeded split) and takes the study's settings as options: `--blocks`, `--dropout`, `--lr`, `--size`, `--batch`, `--epochs` and `--augment`. It prints the parameter count, one line per epoch and the test metrics, and writes the history to a CSV file.

**How it was checked.** Five regression tests (`ml_cnn_*.obs`) and a PyTorch reference.

*Gradients.* `ml_cnn_gradcheck.obs` compares backpropagation with central finite differences for every layer type, alone and stacked, and for a three-block network. It runs on the float64 build of the same code (`CnnModel->NewReference`), because float32 differences cannot resolve a gradient better than about 1e-3: with a step of 1e-5 a correct backward pass agrees to better than 1e-7. Measured over about 2,400 parameters and 3,100 input pixels, with the error defined as `|analytic - numeric| / max(|analytic| + |numeric|, 1e-4)`, the worst was 9.1e-8 for a parameter and 5.7e-8 for an input pixel, against a bound of 1e-6. Dropout is checked in training mode with the mask replayed.

*PyTorch parity.* `tools/ml/cnn_parity.py` (PyTorch, CPU, float32) draws weights, inputs and labels from a fixed seed, runs forward and backward, and writes text files (`programs/regression/data/cnn_parity/`, about 2.4 MB). `ml_cnn_parity.obs` loads the same weights and compares the loss, the logits, every parameter gradient, the gradient with respect to the input and the output of each layer. Each difference is `max |ours - torch| / max |torch|` over the tensor:

| Case | Parameters | loss | logits | parameter gradients | input gradient | layer outputs |
|---|---|---|---|---|---|---|
| Conv 3x3 valid | 157 | 1.1e-7 | 3.8e-7 | 2.4e-7 | 2.3e-7 | 3.8e-7 |
| Conv 3x3 same | 175 | 1.0e-7 | 1.2e-7 | 3.2e-7 | 2.0e-7 | 2.3e-7 |
| Conv 4x4 same (even kernel) | 95 | 0.4e-7 | 1.2e-7 | 1.9e-7 | 2.1e-7 | 1.2e-7 |
| Conv 5x5 stride 2 | 265 | 0.6e-7 | 8.3e-7 | 5.0e-7 | 1.4e-7 | 8.3e-7 |
| Conv 1x1 | 97 | 0.5e-7 | 1.0e-7 | 2.8e-7 | 1.4e-7 | 1.0e-7 |
| Conv + ReLU | 79 | 0.3e-7 | 1.2e-7 | 1.5e-7 | 0.9e-7 | 1.6e-7 |
| MaxPool 2, odd input | 39 | 0.4e-7 | 2.0e-7 | 1.8e-7 | 1.5e-7 | 2.0e-7 |
| MaxPool 3, stride 2 | 25 | 0.8e-7 | 0.0e-7 | 0.8e-7 | 0.5e-7 | 0.0e-7 |
| Dense + ReLU | 251 | 0.1e-7 | 0.0e-7 | 1.3e-7 | 1.5e-7 | 0.0e-7 |
| Dropout (evaluation mode) | 109 | 0.0e-7 | 0.7e-7 | 2.9e-7 | 2.0e-7 | 0.7e-7 |
| **Baseline at 32 x 32** | 56,609 | 0.1e-7 | 3.8e-7 | 8.3e-7 | 1.4e-7 | 7.5e-7 |

Both sides are float32 but sum in different orders, which costs a few units of 1.2e-7 per accumulation (up to 2,304 products here), so the differences are a few ulps. The test allows 2e-5 per layer and 5e-5 for the whole network. Dropout is compared in evaluation mode only, since PyTorch's random numbers are not ours; its mask, scaling and backward are checked by the gradient check and by exact-value tests (`ml_cnn_edges.obs`).

*Training curve.* `train_baseline32` starts PyTorch and `ml_cnn` from the same weights, feeds both the same batches in the same order and takes 20 Adam steps (learning rate 1e-3). The loss falls from 0.721750 to 0.156777 in both; the worst per-step difference is 3.8e-7 (relative), and the weights after the 20 steps differ by 2.0e-5 relative to their scale (Adam divides by `sqrt(v)`, so entries with a tiny gradient move by more than the gradient difference). The test allows 1e-4 for the loss and 1e-3 for the weights. At 64 x 64 and above a float32 curve is not a fair comparison, because Adam amplifies rounding: in an experiment with identical weights and batches, PyTorch float32 differed from PyTorch float64 by 5e-4 after 20 steps at 64 x 64 and by 6% at 128 x 128, while this engine's float32 run differed from float64 by 4e-3 and 2e-4 respectively, and this engine's float64 build matched PyTorch float64 to 4e-16. So `ml_cnn_parity64.obs` compares the float64 build against PyTorch float64 (any wrong formula shows), with a loose bound for float32.

*Edge cases.* `ml_cnn_edges.obs`: parameter counts against the closed form (including the 5,561,633 above and variants at 64, 96, 128 and 224 pixels, 2 to 4 blocks, `SAME` padding), odd sizes, 1 and 3 channels, a batch of 1, loss values worked out by hand, dropout (same seed same masks, scaling, rate, identity in evaluation, replay), max-pool ties, one SGD step and the first Adam step, initializer limits, bit-exact `Save` / `Load`, and a list of bad inputs that must answer a failure value. `ml_cnn_dataset.obs`: the dataset, split, staging and augmentation (a flip mirrors, brightness multiplies, a rotation fills the corners with zeros, each fires at about its probability and repeats for a seed and key), BMP files, a corrupt file, and the trainer's history and metrics. `ml_cnn_train.obs`: the baseline on the synthetic set reaches at least 95% on the validation and the test sets (it reaches 100%).

**Speed.** `TrainBatch` (forward, backward and an Adam step) on a batch of 64 images, 3 blocks, Dense 128, 8 threads, Ryzen 9 7950X3D, Windows, MSVC `/O2` without `/arch` (SSE2: the release binaries must run on any x64 CPU), measured from Objeck over 3 to 5 batches after a warm-up:

| Image size | Parameters | 1 thread | 8 threads | Forward only, 8 threads |
|---|---|---|---|---|
| 64 x 64 | 318,753 | 0.136 s / batch (469 images/s) | 0.027 s (2,385 images/s) | 0.011 s (5,979 images/s) |
| 224 x 224 | 5,561,633 | 2.12 s (30 images/s) | 0.447 s (143 images/s) | 0.182 s (352 images/s) |

At 143 images/s an epoch of 100,000 images at 224 x 224 takes about 11.7 minutes of training, plus about a minute to validate 20,000 images; ten epochs are about 2.1 hours. At 64 x 64 an epoch of 100,000 images is 42 seconds. The same code compiled with `/arch:AVX2` ran 1.7 times faster on one thread and about 1.3 times faster on eight (a standalone harness, not a shipped option), so the convolutions are limited by memory traffic (im2col, ReLU and pooling over tensors of tens of megabytes) as much as by arithmetic; a CUDA backend is the way to a large step.

**Platforms.** Built and tested on Windows x64 (MSVC). The sources also compile cleanly with `g++ 13 -std=c++17 -O3 -Wall -Wextra` on Ubuntu 24.04 (WSL2), where a standalone harness gives the same losses to six digits as the Windows build (0.686856 at 64 x 64, 0.698541 at 224 x 224) and a gradient error of 2e-8. Linux and macOS (x64, arm64) and Windows arm64 were not built or run as part of this work; the code is standard C++14 with Eigen and `std::thread` and is built by the existing `libobjk_ml` project files and scripts, which is what CI exercises.

#### The optional CUDA backend

The same model can run on an NVIDIA GPU. It is **optional and is not shipped**: Objeck's releases and CI build no CUDA code, `libobjk_ml` links nothing from CUDA and does not need it to load, and without the extra library every model simply stays on the CPU, which is the default. The GPU code is a separate shared library, `libobjk_ml_cuda` (`core/lib/matrix/cuda/`: the kernels and the cuBLAS calls in `objk_ml_cuda.cu`, the C interface in `objk_ml_cuda.h`), that `libobjk_ml` loads on demand with `LoadLibrary` / `dlopen` the first time a `"cuda"` device is asked for.

```ruby
use System.ML.Cnn;

CnnRuntime->GetDevices()->PrintLine();      # "cpu", "cuda:0 NVIDIA GeForce RTX 4090 (24563 MB, compute capability 8.9)"
model := CnnModel->Baseline(224, 3, 0.1, 128, 0.0001, 42);
if(model->SetDevice("cuda")) {              # or "cuda:1"; "cpu" moves it back
  "on {$model->GetDevice()}"->PrintLine();
}
else {
  "staying on the CPU: {$model->GetError()}"->PrintLine();   # no library, no GPU, out of memory ...
};
trainer := CnnTrainer->New(model, train, validation, 64, 42);  # unchanged: the same trainer, history and metrics
```

`CnnRuntime->HasCuda()`, `CudaDeviceCount()`, `CudaStatus()` (why not), `CudaFreeMemory(index)` and `GetDevices()` describe what is there; `CnnModel->SetDevice(name)`, `GetDevice()` and `GetDeviceMemory()` (the GPU bytes the model holds) place it. A refused `SetDevice` answers `false`, says why in `GetError()` and leaves the model where it was; nothing crashes. A model can move at any time and keeps its weights (the Adam moments restart from zero). `Save` and `Load` write the same bytes on both devices, so a model trained on the GPU loads on the CPU and the other way round. `programs/examples/ml/cnn_images.obs` takes `--device cuda`, and `programs/examples/ml/cnn_speed.obs` measures images per second.

**What it needs.** An NVIDIA GPU of compute capability 7.5 or newer (Turing and later) with a current driver, and the CUDA Toolkit 12.x or 13.x to build it (nvcc and cuBLAS; **cuDNN is not used**). At run time `libobjk_ml_cuda` needs `cublas64_*.dll` (Windows, found through `CUDA_PATH`) or `libcublas.so` (Linux, on the loader path).

**Building it.** By hand, never as part of the Objeck build:

```
core\lib\matrix\cuda\build_cuda.cmd [output-dir]     (Windows)
core/lib/matrix/cuda/build_cuda.sh [output-dir]      (Linux)
```

then copy `libobjk_ml_cuda.dll` / `.so` next to `libobjk_ml` (for a release tree, `lib/native`), or set the environment variable `OBJK_CUDA_LIB` to its full path. The script builds machine code for compute capabilities 7.5 to 9.0 and 12.0 and PTX for newer GPUs (`CUDA_ARCHS` changes the list). Host compiler: CUDA 13.1 accepts only the Visual Studio 2019 and 2022 compilers (`_MSC_VER` below 1950) and refuses the v145 toolset of Visual Studio 2026 (`unsupported Microsoft Visual Studio version`). That IDE still installs the v143 toolset beside it, which is what `build_cuda.cmd` selects (`vcvarsall.bat amd64 -vcvars_ver=14.4`), so `-allow-unsupported-compiler` is not needed; `VC_TOOLSET` and `VCVARSALL` override the choice.

**How it works.** The model keeps its weights, activations and gradients on the GPU between layers; per batch only the images and labels go in and the loss and logits come out. Convolution is an im2col kernel and `cublasSgemmStridedBatched` (one matrix multiply per image, launched as a batch), its input gradient a batched multiply and a gather (`col2im`), its weight gradient a batched multiply into per-image partial sums that a kernel adds in a fixed order; dense layers are `cublasSgemm`; max pooling, ReLU, bias, dropout, the loss and the Adam and SGD updates are small CUDA kernels. Nothing uses atomic additions on floats, so a run repeats bit for bit on the same GPU and driver. Dropout draws the CPU engine's counter-based mask (same seed, same forward-pass counter, same element index, compared in double), so a given seed drops the same activations on both devices. Everything is full FP32: cuBLAS runs in its default math mode, which does not use Tensor Cores. `SetDevice("cuda", true)` allows TF32 instead; it was measured to change the logits by 2e-3 and the gradients by up to 7e-2 (relative to the largest entry) and to give no speed-up on this network (the matrix multiplies are not the bottleneck), so it is not recommended.

**Memory.** A model holds its parameters, gradients and optimizer state plus buffers for the largest batch it has seen: the baseline at 224 x 224 takes 1.1 GB at batch 32, 2.1 GB at 64, 4.2 GB at 128 and 8.2 GB at 256 (`GetDeviceMemory()`). A batch that does not fit answers `-1.0` and "out of GPU memory: a batch of N needs about X MB and Y MB are free" and leaves the model usable with a smaller one. `Close()`, `SetDevice("cpu")` or the end of the program give the memory back; a model left open is released when the library unloads.

**Speed.** `TrainBatch` and `Predict` from Objeck on random images, batch 64, the baseline (3 blocks, Dense 128), RTX 4090, full FP32, Windows, CUDA 13.1; the CPU is the 8-thread engine above. PyTorch 2.11 (CUDA 12.8 build, cuDNN, TF32 off, Adam) on the same GPU and the same network is the reference. Images per second:

| Image size | CPU train | GPU train (best batch) | PyTorch GPU train | GPU predict | PyTorch GPU predict |
|---|---|---|---|---|---|
| 64 x 64 | 2,393 | 50,181 (57,181 at batch 128) | 45,671 (90,287 at batch 128) | 99,585 | 191,913 |
| 128 x 128 | 492 | 11,433 (11,907 at batch 32) | 27,244 | 20,461 | 81,671 |
| 224 x 224 | 114 (143 in the table above) | 3,412 (batch 64) | 7,792 | 5,891 | 22,565 |

Training 100,000 images takes about 2.0 s at 64 x 64, 8.7 s at 128 x 128 and 29 s at 224 x 224 on the GPU (batch 64), against 42 s, 3.4 minutes and 14.6 minutes on the CPU: 21 times faster at 64 x 64, 23 times at 128 and 30 times at 224. Training is 1.1 times as fast as PyTorch's cuDNN at 64 x 64 and about 2.4 times slower at 128 and 224 pixels, prediction 2 to 4 times slower; the layer timings show the three convolutions taking about 80% of a 224 x 224 step, which is the price of an im2col matrix multiply that writes a column tensor 9 times the size of the image, where cuDNN convolves directly. Prediction from `Float[]` is limited by the host: converting and copying 38 MB of doubles per batch of 64 takes about as long as the GPU does the forward pass, and training from 8-bit staged pixels is within 1% of training from `Float[]` at 224 x 224 (the "staged train" figure of `cnn_speed.obs`). Decoding and augmenting real image files (OpenCV) stays on the CPU and bounds a real epoch; `cnn_images.obs --cache` keeps decoded images in memory.

**How the GPU backend was checked.** Three tests that print `SKIP` and exit 0 where there is no CUDA library or device (they are inert on CI), and one that runs only there.

*Against PyTorch and the CPU engine.* `ml_cnn_cuda_parity.obs` runs the cases of `ml_cnn_parity.obs` (every layer type alone, the baseline at 32 x 32, 20 Adam steps) on the GPU and on the CPU engine and compares each with PyTorch and with each other, on the loss, the logits, every parameter gradient, the input gradient and each layer's output. Largest difference over the cases, `max |a - b| / max |b|` per tensor, in units of 1e-7 (float32 rounding is 1.2e-7):

| | loss | logits | parameter gradients | input gradient | layer outputs |
|---|---|---|---|---|---|
| GPU vs PyTorch (CPU fixtures) | 0.8 | 4.6 | 7.2 | 2.3 | 6.5 |
| GPU vs PyTorch on the GPU (`cnn_parity.py --device cuda`) | 0.9 | 3.8 | 7.0 | 2.0 | 5.4 |
| GPU vs the CPU engine | 1.4 | 10.0 | 5.6 | 2.0 | 10.0 |
| CPU engine vs PyTorch (for scale) | 1.1 | 8.3 | 8.1 | 2.3 | 8.3 |

The GPU is as close to PyTorch as the CPU engine is. The test allows 2e-5 per layer and 5e-5 for the network, the tolerances of the CPU test, which are 100 to 400 times the measurement: float32 sums of up to 2,304 products in a different order (cuBLAS, Eigen, PyTorch) differ by a few ulps, and the GPU also fuses a multiply and an add that the CPU does not. The 20 Adam steps of `train_baseline32` agree with PyTorch to 9e-7 per step (loss) and 5e-5 in the final weights, and with the CPU engine to 1e-6 and 7e-5. At larger sizes (`cnn_parity.py --large --float64`, `ml_cnn_cuda_parity.obs <dir> large`, not committed: megabytes) the baseline at 64 x 64 agrees with PyTorch to 8e-7 (gradients) and at 224 x 224 to 2.1e-6 (gradients) and 1.4e-7 (input gradient). Against a float32 PyTorch reference the 224 x 224 gradients differ by 2.9e-3 and the input gradient by 6e-2, **for the CPU engine and the GPU alike**: PyTorch's own float32 run differs from its float64 run by exactly that much (a ReLU input within rounding of zero flips sign with the summation order, and over 800,000 activations per image a few do), so the float64 reference is the one to compare with. In a 20-step Adam run at 64 x 64 the GPU follows PyTorch to 8e-5 per step and 5e-3 in the weights, and the CPU engine to 4.2e-3 and 3.9e-2: Adam amplifies the float32 noise of near-zero gradients, and it is the CPU engine's trajectory that drifts there (not investigated further; it does not depend on the thread count).

*Unit tests.* `ml_cnn_cuda.obs` compares the GPU with a CPU twin of the same seed for: the device list, selection, refusal and the move back; odd and non-square sizes, 1, 2 and 3 channels, a 1 x 1 image, a kernel as large as the image, stride 3, an even `SAME` kernel, overlapping pooling windows, batches of 1, 3 and 6 (worst 1.1e-6); dropout (the same seed gives the same mask on two GPU models and on the CPU, element for element, another seed another mask, `ResetDropout` replays, the rate and the scaling are right, training-mode gradients agree); 20 Adam steps from the same weights and batches following the CPU loss curve step by step (worst 8e-8 relative), SGD with momentum, `SetLearningRate`; two GPU models trained alike being identical bit for bit; `Save` and `Load` between devices in both directions (weights bit-identical, predictions equal to 1e-7); four models alive and trained in turn, each equal to its twin; memory returned to its baseline after repeated create, train and close cycles and after `SetDevice("cpu")`; a batch of 400 that needs 55 GB answering `-1.0` with a reason and leaving the model working; bad arguments refused as on the CPU; and a 20-epoch `CnnTrainer` run on the synthetic texture set that reaches 100% validation and test accuracy on both devices (3.0 s on the CPU, 0.4 s on the GPU). `ml_cnn_cuda_fallback.obs` is the other half and runs wherever CUDA is *absent*, CI included: no device is counted, the reason is given, `SetDevice("cuda")` is refused, and a model refused the GPU trains exactly as one that never asked.

*Not verified.* The whole path through Objeck (tests, speed, parity) was run on Windows x64 with an RTX 4090 (compute capability 8.9, driver 621.24, CUDA 13.1) only. On Linux (Ubuntu 24.04 under WSL2, g++ 13, CUDA 13.1, the same GPU) `build_cuda.sh` builds the library and a standalone C++ program that loads it with `dlopen` trains the baseline through it next to the CPU engine, agreeing to 1.2e-4 over 20 Adam steps on random labels; `libobjk_ml`'s own loader compiles there but was not run, and macOS has no CUDA. No other GPU generation, no multi-GPU machine and no older toolkit were tried; TF32 was measured on one batch only.

### Statistics (System.Stats)

Classical statistics for the analysis that comes before (or instead of) a model: descriptive statistics, t-tests with confidence intervals and effect sizes, Pearson correlation with a p-value, and an ordinary least squares fit with the full `statsmodels`-style summary. It is `stats.obl` (`-lib stats,gen_collect` or `-lib @ai`) and needs nothing from `System.ML`. No call ends the program on bad input: a `Float` function returns NaN, and a result object comes back with `IsValid()` false and `GetError()` saying why (Nil, a single value, zero variance, mismatched lengths, NaN or infinity, a confidence level outside (0, 1), a rank-deficient design).

| Class | What it does |
|---|---|
| `Stats` | `Mean`, `Variance` (n - 1), `PopulationVariance`, `StdDev`, `StdError`, `Covariance`, `Median`, `Sum` |
| `TTest` | `OneSample(x, mu)`, `TwoSample(x, y)` (Student, pooled variance), `Welch(x, y)`, `Paired(x, y)`; each takes an optional confidence level (default 0.95) and returns a `TTestResult` |
| `TTestResult` | `GetT`, `GetDf`, `GetP` (two-sided), `GetMeanDiff`, `GetCiLow`, `GetCiHigh`, `GetCohenD`, `GetHedgesG`, `GetN1`, `GetN2`, `IsSignificant(alpha)`, `ToString` |
| `Correlation` | `Pearson(x, y)` with a p-value (Student t, n - 2 df) and a Fisher-z interval, as a `CorrelationResult` |
| `OLS` | `Fit(X, y, add_intercept)` and `Fit(X, y, add_intercept, names)` by Householder QR, as an `OLSResult` |
| `OLSResult` | `GetCoefficients`, `GetStdErrors`, `GetTStats`, `GetPValues`, `GetConfInt(level)`, `GetRSquared`, `GetAdjustedRSquared`, `GetResidualStdError`, `GetFStat`, `GetFPValue`, `GetAIC`, `GetBIC`, `GetLogLikelihood`, `GetResiduals`, `GetFitted`, `GetCovariance`, `Predict(X)`, `ToString` (the summary table) |
| `Design` | `AddIntercept`, `Column`, `HStack`, `Levels`, `Dummies` (0/1 columns, first sorted level is the reference) and `DummiesFor` (explicit level order) |
| `Dist` | the building blocks: `StudentTwoSidedP`, `StudentCdf`, `StudentQuantile`, `NormalCdf`, `NormalQuantile`, `ChiSquareCdf`/`Sf`, `FCdf`/`Sf`, `BetaInc`, `GammaInc`, `LogGamma` |

```ruby
use System.Stats;

class Demo {
  function : Main(args : String[]) ~ Nil {
    genuine := [52.0, 61.5, 48.2, 70.1, 55.3, 49.9, 66.0, 58.4];
    fake := [40.2, 45.8, 52.1, 38.9, 47.5, 44.0, 50.3, 41.7];

    r := TTest->Welch(genuine, fake);          # unequal variances; the safer default
    r->ToString()->PrintLine();                # t, df, p, mean diff, 95% CI, Cohen d, Hedges g
    if(r->IsValid() & r->IsSignificant(0.05)) {
      "difference {$r->GetMeanDiff()} (95% CI {$r->GetCiLow()} to {$r->GetCiHigh()}), g = {$r->GetHedgesG()}"->PrintLine();
    };

    # regression with a categorical predictor: an intercept, a numeric column and 0/1 columns
    length := [10.0, 25.0, 31.0, 12.0, 40.0, 22.0, 35.0, 18.0, 29.0, 44.0];
    kind := ["a", "b", "c", "a", "b", "c", "a", "b", "c", "a"];
    rating := [2.0, 3.1, 4.2, 2.4, 3.9, 3.8, 3.3, 3.0, 4.6, 4.1];
    X := Design->HStack(Design->Column(length), Design->Dummies(kind));
    fit := OLS->Fit(X, rating, true, ["length", "kind_b", "kind_c"]);
    fit->ToString()->PrintLine();              # coef, std err, t, P>|t|, 95% CI, R-squared, F, AIC, BIC
    fit->GetPValues()[1]->PrintLine();         # a Float[], one p-value per coefficient
  }
}
```

Conventions worth knowing. The mean difference is the first group minus the second, and the confidence interval is for that difference (for a one-sample test, mean minus the hypothesised mean). Cohen's d divides by the pooled standard deviation (one-sample: the sample's; paired: that of the differences), and Hedges' g multiplies it by the exact correction `Gamma(df/2) / (sqrt(df/2) * Gamma((df-1)/2))` (`Dist->HedgesJ`, about `1 - 3 / (4 df - 1)`); the Welch test reports the same pooled-sd d and g as the Student test. A regression with an intercept uses the centred total sum of squares and tests against the intercept-only model; without one it uses the uncentred sum and `df_model = k`, as `statsmodels` does, and AIC and BIC count one parameter per coefficient, also as `statsmodels` does. Put every level of a categorical variable beside an intercept and the fit is refused as rank deficient (the dummy trap); `Design->Dummies` drops the first level for that reason.

The p-values come from the regularized incomplete beta function (continued fraction, Lentz), so they stay accurate in the far tail (`p = 7e-18` is `7e-18`, not 0) and for large degrees of freedom. Every distribution function is tested against `scipy` at small and large df.

A complete run -- the review-length t-test at n = 100, 500 and 2000 per group on synthetic data, then the same comparison as a regression -- is [`programs/examples/ml/fake_review_ttest.obs`](../programs/examples/ml/fake_review_ttest.obs).

### Plotting (System.Plot)

A matplotlib-shaped plotting library (scatter, histogram, line, bars, heatmap, confusion matrix, box plot, ROC and precision-recall curves, annotations, subplots) that writes standalone SVG 1.1: no display, no native library, no script, no font file. It is `plot.obl` (`-lib plot,gen_collect` or `-lib @ai`) and needs nothing from `System.ML`, so a figure can come from any `Float[]`. A `Figure` holds one or more `Axes`; `fig->Save("a.svg")` writes the file and `fig->ToSvg()` returns the text. Every coordinate is written with one decimal and nothing depends on the clock, a locale or a font, so the same data gives the same bytes on every platform and a chart can be compared against a golden file in a test. `System.ML.Lab.Chart` (the line and bar writer behind `ResultTable->ToChart`) is unchanged and still the one-call API for experiment tables; `System.Plot` is the general one. Design: [`docs/PLOT_DESIGN.md`](PLOT_DESIGN.md).

```ruby
use System.Plot;

# a scatter plot, one series per class; the legend lists the named series
fig := Figure->New(800, 480);
ax := fig->GetAxes();
ax->SetTitle("Two classes");
ax->SetXLabel("feature 1");
ax->SetYLabel("feature 2");
ax->Scatter("class A", xa, ya);
ax->Scatter("class B", xb, yb);
fig->Save("scatter.svg");

# color the points by a third column (a color bar appears) and size them by a fourth
ax->Scatter("", xs, ys, score, weight);       # Nil->As(Float[]) leaves either one out

# a histogram: 20 bins, or 0 for Sturges' rule; the fourth argument true gives density
ax->Hist("residuals", errors, 20);

# lines, bars (grouped, over named categories), log axes, fixed limits
ax->Plot("loss", epochs, loss);
ax->SetCategories(["A", "B", "C"]);
ax->Bar("2026", [4.0, 4.5, 6.0]);
ax->SetLogX(true);
ax->SetYLim(0.0, 1.0);

# a heatmap with its values printed in the cells (row 0 is at the top); a color bar is added
ax->Heatmap(values, ["low", "mid", "high"], ["w", "x", "y", "z"], true, 1);

# a confusion matrix: Metrics->ConfusionMatrix feeds straight in; true shows fractions per row
ax->ConfusionMatrix(Metrics->ConfusionMatrix(predictions, actuals), ["negative", "positive"], false);

# a box plot: one Box call per group, left to right; the whiskers reach the furthest value
# within 1.5 x the interquartile range and the rest are circles; the quartiles are linear
# interpolation between ranks (numpy's default), so Q1 of 1 .. 4 is 1.75
ax->Box("control", control_values);
ax->Box("treated", treated_values);

# ROC and precision-recall from the curve matrices System.ML returns; the legend carries
# the area under the curve (trapezoid rule) or the average precision
ax->RocCurve("model", Metrics->RocCurve(scores, actuals));     # [threshold, FPR, TPR] rows
ax->PrCurve("model", Metrics->PrCurve(scores, actuals));       # [threshold, recall, precision] rows

# text in data coordinates, text with a line pointing at a point, a dashed reference line
ax->Text(1.2, 5.5, "a label");
ax->Annotate("peak", 6.0, 6.0, 4.2, 6.4);                      # text at (4.2, 6.4) pointing at (6, 6)
ax->Reference(0.0, 3.5, 7.0, 3.5);
ax->SetLegendCorner("lower right");

# several plots on one canvas: a rows x cols grid, left to right and top to bottom
board := Figure->New(900, 640);
board->SetTitle("Dashboard");
board->Subplots(2, 2);
board->GetAxes(0, 0)->Scatter("A", xa, ya);
board->GetAxes(0, 1)->Hist("", residuals, 12);
board->Save("dashboard.svg");
```

Bad input never produces a broken picture: a mismatched length, a NaN or infinite value, a non-positive value on a log axis, bars without categories, `Hist` or `Bar` on a log y axis (an empty bin has height 0, which has no log) or an empty axes raise `PlotError`, with a message that says what was wrong; control characters other than tab and line breaks are dropped from titles and labels so the SVG stays valid XML; numbers are always written with a decimal point whatever the C locale says; tick labels carry the decimals their step needs (so a 1-second range of epoch seconds reads 1700000000.2, not float noise) and switch to scientific form below 1e-6 (`try { } on(e : PlotError) { }`). A `Figure` draws into a `PlotScene`, an ordered list of pixel-space primitives (lines, polylines, polygons, rects, circles, text with an anchor), and `SvgWriter` turns that into text; `fig->ToScene()` is the hook a second backend would use. The library is allowed in the web playground, since SVG needs no display. For a window or a raster file there is a second library, `plot_sdl.obl` (below).

A heatmap owns its axes and a box plot cannot share axes with other plots; both refuse otherwise. A confusion matrix's colors run from 0 (the smallest count is only the darkest when it is 0), and a heatmap value that rounds to zero prints as 0.00, never -0.00. `PrCurve` of scores that are all tied is a single point, which is refused with that reason. `RocCurve` and `PrCurve` take the matrix as it is, so `System.Plot` does not link `System.ML` (compile with `-lib ml,plot,gen_collect,csv` only when you call `Metrics`; `PrCurve` is in `System.ML` from the scores PR on). Two complete runs: [`programs/examples/ml/plot_demo.obs`](../programs/examples/ml/plot_demo.obs) writes four files (scatter, colored scatter, histogram, a dashboard), and [`programs/examples/ml/plot_evaluation.obs`](../programs/examples/ml/plot_evaluation.obs) writes the confusion matrix, the ROC curve and a three-panel classifier report. The tests are `plot_svg.obs` (scatter, histogram, line, bars, log axes, subplots) and `plot_svg_more.obs` (box statistics against a hand-worked sample, the legend's AUC against `Metrics->AucRoc`, annotation text, refusals). They check structure (one circle per point, one bar per bin, one box per group, a color bar only when points are colored), the histogram counts against hand-counted bins, the area of a density histogram, geometry (every coordinate inside the viewport) and a byte-for-byte golden per chart under `programs/regression/plot_golden/`; each golden has a control showing that one changed value changes the bytes.

#### In a window, or as a PNG (System.Plot.SDL)

`plot_sdl.obl` draws the same `PlotScene` with SDL2 (`-lib @plot,gen_collect`, which is `plot`, `plot_sdl` and `sdl2`). It is separate from `plot.obl` so that SVG-only programs, the web playground and headless CI never need SDL.

```ruby
use System.Plot;
use System.Plot.SDL;

# an interactive window: the wheel zooms about the pointer, dragging with the left button pans,
# the pointer reads out the data position, R resets the view, Escape or closing ends it
PlotWindow->New(fig, "my plot")->Run();      # false when there is no display

# a PNG, with no window and no display: it draws on a software surface
PlotPng->Save(fig, "plot.png");
```

Zoom and pan change the axes' limits (`Axes->Zoom`, `Axes->Pan`, `Axes->DataAt`, `Axes->ResetLimits`) and the scene is rebuilt, so ticks and labels stay sharp; a log axis zooms in log space, so a decade stays a decade; categorical axes and heatmaps do not zoom; a zoom stops short of the float limit (a range narrower than 1e-200, or a log axis reaching 1e-300 or 1e300) instead of failing; `R` returns to the limits the figure chose (`Axes->ResetView`: a ROC plot's -0.02 .. 1.02), not to the data's range, while `Axes->ResetLimits` forgets the limits altogether; a figure that cannot be drawn (a `PlotError` in a frame) resets the view and shows the reason at the bottom of the window instead of ending the program. Everything is clipped to the canvas and a margin before it reaches SDL2_gfx, which takes 16-bit coordinates, so a 200000-pixel line or a billion-pixel dashed line draws correctly and quickly. A rect with no width or no height draws nothing, as in SVG. Text uses SDL_ttf with the shipped `lib/sdl/fonts/lato.ttf` (or `SdlCanvas->SetFont`) and falls back to SDL2_gfx's 8x8 bitmap font when no font file is found. The font is looked for under `OBJECK_LIB_PATH/sdl/fonts/lato.ttf`, then at `../lib/sdl/fonts/lato.ttf` relative to the current directory (the toolchain's `bin` directory under the regression runner); a font that fails to open is not retried on every text. A PNG is not byte-identical across SDL, FreeType and SDL2_gfx versions the way the SVG is, which is why the SVG is the format the golden tests use; the PNG is tested by reading pixels back. A window needs a display; `SDL_VIDEODRIVER=dummy` runs the whole backend without one, which is how it is tested. A complete run is [`programs/examples/ml/plot_window.obs`](../programs/examples/ml/plot_window.obs), tested by `plot_sdl.obs` (exact pixel colors for each primitive, a clip, text, a figure's marker, PNG signature and size, a hidden window driven through zoom, pan, hover, reset and a user close event) and `plot_view.obs` (the zoom and pan arithmetic, with no SDL).

## Classic AI (System.AI)

Search, game playing, optimization and tabular reinforcement learning — `-lib ai` (or the `@ai` alias). All stochastic algorithms are seeded for reproducible runs.

```
obc -src program.obs -lib ai,gen_collect -dest program.obe
```

### Graph Search

```ruby
use System.AI;

graph := Graph->New(6);                # nodes 0..5
graph->AddEdge(0, 1, 1.0);             # directed; add both ways if undirected
graph->AddEdge(1, 2, 1.0);
graph->AddEdge(2, 5, 1.0);
graph->AddEdge(0, 5, 100.0);

# minimum cost
result := Dijkstra->FindPath(graph, 0, 5);
"cost={$result->GetCost()}"->PrintLine();          # 3.0
path := result->GetPath();                          # [0, 1, 2, 5]

# A* with a per-node heuristic (admissible -> same cost, fewer expansions)
heuristic := [3.0, 2.0, 1.0, 0.0, 0.0, 0.0];
result := AStar->FindPath(graph, 0, 5, heuristic);

# fewest hops / reachability
result := BreadthFirst->FindPath(graph, 0, 5);      # takes the direct edge
result := DepthFirst->FindPath(graph, 0, 5);
```

### Game Playing

Implement the `GameState` interface (`GetMoves`, `Apply`, `IsTerminal`, `Evaluate`, `IsMaximizing` — evaluation is always from the maximizing player's perspective), then search it:

```ruby
# perfect play with alpha-beta pruning
searcher := Minimax->New(9);            # depth limit
best := searcher->FindBestMove(state);

# Monte Carlo tree search for bigger branching factors
mcts := MonteCarloTreeSearch->New(2000, 1.414, 7);  # playouts, UCB1 c, seed
best := mcts->FindBestMove(state);
```

### Optimization

```ruby
# genetic algorithm over Bool[] chromosomes: implement FitnessFunction
class OneMax implements FitnessFunction {
  New() {}
  method : public : Fitness(genes : Bool[]) ~ Float {
    count := 0;
    each(i : genes) { if(genes[i]) { count += 1; }; };
    return count->As(Float);
  }
}

ga := GeneticAlgorithm->New(40, 24, 0.02, 0.9, 7);  # pop, genes, mutate, cross, seed
best := ga->Run(OneMax->New(), 80);
"fitness: {$ga->GetBestFitness()}"->PrintLine();    # 24.0
```

```ruby
# simulated annealing / hill climbing over Float[] states: implement EnergyFunction
sa := SimulatedAnnealing->New(10.0, 0.995, 0.5, 7); # temp, cooling, step, seed
best := sa->Run(energy, [-8.0, 9.0], 4000);

hc := HillClimbing->New(0.5, 50, 5.0, 7);           # step, patience, restart range, seed
best := hc->Run(energy, start, 3000);
```

### Reinforcement Learning

Implement the `Environment` interface (`GetNumStates`, `GetNumActions`, `Reset`, `Step`), then train:

```ruby
agent := QLearning->New(0.2, 0.95, 0.2, 7);  # alpha, gamma, epsilon, seed
agent->Train(env, 400, 60);                  # episodes, max steps
best := agent->BestAction(state);

# on-policy variant
sarsa := Sarsa->New(0.2, 0.95, 0.2, 7);
sarsa->Train(env, 400, 60);
```

```ruby
# explicit MDP solved by value iteration
mdp := MarkovDecisionProcess->New(6, 2);     # states, actions
mdp->AddTransition(0, 1, 1, 1.0, -1.0);      # state, action, next, prob, reward
mdp->AddTransition(4, 1, 5, 1.0, 10.0);
mdp->Solve(0.95, 0.000001, 1000);            # gamma, tolerance, max iterations
policy := mdp->GetPolicy();
values := mdp->GetValues();
```

---

## Library Aliases

`-lib` accepts `@`-prefixed aliases that expand to groups of libraries, defined in `lib/configobjk.ini` next to the installed `.obl` files:

```
obc -src program.obs -lib @ai -dest program.obe
```

| Alias | Expands to | Use for |
|---|---|---|
| `@std` | `json`, `json_stream`, `net`, `cipher` | everyday networked apps |
| `@web` | `net_h2`, `net_quic`, `web_server`, `json_rpc`, `net_server`, `net`, `json`, `cipher` | HTTP/2, HTTP/3, the embedded server, JSON-RPC |
| `@data` | `xml`, `regex`, `csv`, `query`, `rss`, `misc`, `net`, `json`, `cipher` | parsing and querying structured data |
| `@ml` | `gemini`, `openai`, `ollama`, `net_server`, `misc`, `net`, `json`, `cipher` | LLM clients: OpenAI, Gemini, Ollama |
| `@ai` | `ai`, `ml`, `ml_lab`, `ml_vector`, `ml_verify`, `stats`, `plot`, `nlp`, `nlp_topics`, `csv`, `json` | local System.ML / System.ML.Lab / System.Stats / System.Plot / System.AI / System.NLP / System.NLP.Topics work |
| `@plot` | `plot`, `plot_sdl`, `sdl2` | charts in a window or as PNG (SVG alone needs only `plot`) |
| `@vision` | `opencv`, `onnx`, `nlp_onnx`, `ml_clip`, `ml_verify`, `models`, `nlp`, `json`, `cipher` | OpenCV and ONNX inference, BERT text embeddings, CLIP image embeddings and face verification |
| `@cnn` | `ml_cnn`, `ml`, `ml_lab`, `opencv`, `csv`, `json`, `cipher` | `System.ML.Cnn` convolutional networks, image loading and history tables |
| `@game` | `sdl2`, `sdl_game`, `sdl_gl`, `json`, `gen_collect` | SDL games, 2D and OpenGL |

Aliases and explicit names mix in any order, for either `--target` (`-lib @ai,json` and `-lib json,@ai` are the same). Groups are user-editable: add a section to `configobjk.ini` and reference it as `@yourname`.

A group must be **closed under dependencies** — the linker loads exactly what the group lists and does not chase a member's own dependencies, so a group that omits one fails on every use, even from a program that never touches it.

An unknown alias names the config file and lists the groups it does define. An unresolvable class names the library that would supply it:

```
Error: Unable to resolve external library class: 'Web.HTTP.Server.Request'.
        Add it with '-lib net_server'
```

See [cli_options.md](cli_options.md#library-groups) for the same table alongside the rest of `obc`'s options.

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
| Any ONNX model | `Session`, `Tensor` | `models`, `onnx` | None |
| Model served over HTTP | `Client`, `Tensor` | `models`, `inference` | None |
| Either, behind one interface | `Engine`, `OnnxEngine`, `HttpEngine` | `models` + one backend | None |
| Computer vision | `Image`, `VideoCapture` | `opencv` | None |
| Sentiment / TF-IDF | `SentimentAnalyzer`, `TF_IDF` | `nlp` | None |
| Spelling and style features | `SpellChecker`, `TextFeatures` | `nlp` | None |
| BERT tokenization | `WordPieceTokenizer`, `ApiSequenceTokenizer` | `nlp` | None |
| Sentence embeddings (BERT) | `BertEmbedder` | `nlp_onnx` + `models`, `onnx`, `nlp` | None |
| Text classification (fine-tuned BERT) | `TextClassifier`, `Prediction` | `nlp_onnx` + `models`, `onnx`, `nlp` | None |
| Image embeddings (CLIP) and LFW pair scoring | `ClipEncoder`, `ClipPreprocessor`, `FacePairs` | `ml_clip` + `onnx`, `opencv`, `models`, `ml_verify` | A CLIP image ONNX model |
| FAR, FRR, EER, ROC and DET points for a verifier | `Verification`, `VerificationResult`, `Similarity` | `ml_verify` | None |
| Hugging Face Hub downloads | `HubClient` | `ai_hub` + `net`, `json`, `cipher` | Network (cache works offline) |
| Linear / regularized regression | `LinearRegression`, `RidgeRegression`, `LassoRegression`, `ElasticNet` | `ml` | None |
| Linear classifiers | `LogisticRegression`, `SVM`, `Perceptron` | `ml` | None |
| Kernel SVMs | `KernelSVM` (linear, RBF, poly, sigmoid), `OneClassSVM` | `ml` | None |
| Probabilistic classifiers | `GaussianNaiveBayes`, `NaiveBayes` | `ml` | None |
| Trees & ensembles | `DecisionTree`, `RandomForest`, `AdaBoost` | `ml` | None |
| Nearest neighbors | `KNearestNeighbors`, `KDTree` | `ml` | None |
| Clustering | `KMeans`, `DBSCAN`, `GaussianMixture` | `ml` | None |
| Decomposition | `PCA` | `ml` | None |
| Neural network | `NeuralNetwork` | `ml` | None |
| Classifier head over embeddings | `EmbeddingClassifier` | `ml` | None |
| Graph search | `Dijkstra`, `AStar`, `BreadthFirst`, `DepthFirst` | `ai` | None |
| Game playing | `Minimax`, `MonteCarloTreeSearch` | `ai` | None |
| Metaheuristics | `GeneticAlgorithm`, `SimulatedAnnealing`, `HillClimbing` | `ai` | None |
| Reinforcement learning | `QLearning`, `Sarsa`, `MarkovDecisionProcess` | `ai` | None |

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

ollama/   test_0.obs (generate)   test_1.obs (vision)
          test_2.obs (chat)       test_3.obs (models)

opencv_onnx/  demo_face.obs    demo_yolo.obs    demo_resnet.obs
              demo_phi3_*.obs  demo_phi3v_*.obs
```

[`programs/examples/ml/bert_malware_lab.obs`](../programs/examples/ml/bert_malware_lab.obs) runs a
BERT malware-classification lab's three experiments — class imbalance with a fixed test set,
MAX_SEQ_LENGTH, and learning rate with per-epoch loss curves — on a frozen BERT encoder and a
trained `EmbeddingClassifier` head, from a CSV of API-call traces or a synthetic corpus
(`--synthetic`). Its header says how this differs from the lab's end-to-end fine-tuning.

A complete `System.ML.Lab` run -- two parameter sweeps with tables, CSVs and SVG charts -- is [`programs/examples/ml/lab_sweeps.obs`](../programs/examples/ml/lab_sweeps.obs).

### How to check

The libraries in this guide are covered by the regression suite in `programs/regression/`:

| Area | Tests |
|---|---|
| `System.Plot`, `System.Plot.SDL` | `plot_svg.obs`, `plot_svg_more.obs` (with goldens in `plot_golden/`), `plot_view.obs`, `plot_sdl.obs` |
| `System.ML.Cnn` | `ml_cnn_gradcheck.obs` (finite differences, float64), `ml_cnn_parity.obs` (against PyTorch, data from `tools/ml/cnn_parity.py`), `ml_cnn_parity64.obs` (float64 engine against PyTorch float64 at 64 x 64, 20 Adam steps), `ml_cnn_edges.obs`, `ml_cnn_dataset.obs`, `ml_cnn_train.obs`, and for the optional GPU backend `ml_cnn_cuda.obs`, `ml_cnn_cuda_parity.obs` (both skip without a CUDA device) and `ml_cnn_cuda_fallback.obs` (runs only without one) |

| `System.Stats` | `stats_dist.obs`, `stats_ttest.obs`, `stats_corr_ols.obs`, `stats_degenerate.obs` (expected values from scipy / numpy, generated by `tools/stats/gen_stats_reference.py`) |
| `System.ML.Verification`, `System.ML.Clip` | `ml_verify_metrics.obs` (against scikit-learn and numpy, generated by `ml_verify_golden/gen_expected.py`), `ml_clip_pairs.obs`, `ml_clip_preprocess.obs`, `ml_clip_encoder.obs` (data from `data/clip/gen_tiny_clip.py`) |
| `System.ML.VectorSearch` | `ml_vector_exact.obs`, `ml_vector_edges.obs`, `ml_vector_validation.obs`, `ml_vector_persist.obs` |
| `System.ML` | `ml_*.obs`, e.g. `ml_kernel_svm_reference.obs` and `ml_one_class_svm_reference.obs` (against an independent reference, generated by `tools/ml/svm_reference/gen_reference_test.py`), `ml_outlier_*.obs`, `ml_lab_*.obs` |
| `System.AI` | `ai_search_test.obs`, `ai_game_test.obs`, `ai_optimize_test.obs`, `ai_rl_test.obs` |
| `System.NLP` | `nlp_*.obs`, e.g. `nlp_wordpiece_parity.obs`, `nlp_bpe_tokenizer.obs` |
| `System.NLP.Topics` | `nlp_topics_bson.obs`, `nlp_topics_bson_pymongo.obs`, `nlp_topics_dictionary.obs`, `nlp_topics_lda.obs`, `nlp_topics_metrics.obs`, `nlp_topics_plots.obs`; the reference values come from `nlp_topics_reference.py`, `nlp_topics_bson_fixtures.py`, `nlp_topics_bson_pymongo.py` and `nlp_topics_gensim_check.py` |
| ONNX, remote inference, Hub | `onnx_*.obs`, `nlp_onnx_*.obs`, `inference_client_test.obs`, `ai_hub_client.obs` |

A test passes when it exits 0 and is compiled with the libraries its `# EXTRA_LIBS:` line names; a test that needs a model or a native library that is not deployed prints `SKIP:` instead. To run one:

```
cd programs/regression
obc -src ml_kernel_svm_reference.obs -lib ml,gen_collect,csv -dest ml_kernel_svm_reference.obe
obr ml_kernel_svm_reference.obe
```

`./run_regression.sh x64` (`run_regression.cmd x64` on Windows) runs the whole suite.
