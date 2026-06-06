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
- [Machine Learning (System.ML)](#machine-learning-systemml)
  - [Regression](#regression)
  - [Classification](#classification)
  - [Clustering & Decomposition](#clustering--decomposition)
  - [Neural Network](#neural-network)
  - [Model Persistence](#model-persistence)
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

## Machine Learning (System.ML)

Classical machine learning, pure standard library — no native model runtimes needed. Every estimator follows the same API: `Fit`, `Predict` (and `PredictClass` for classifiers), `Score`, `IsFitted`, and `Store`/`Load` for model persistence. Stochastic algorithms take a seed for reproducible results.

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

booster := AdaBoost->New(16);          # boosted decision stumps
booster->Fit(data);
```

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

### Model Persistence

Every estimator round-trips through `Store`/`Load`; loaded models reproduce the original predictions exactly.

```ruby
model->Store("model.dat");
loaded := LinearRegression->Load("model.dat");
loaded->Predict(X);
```

---

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
| `@ml` | `gemini`, `openai`, `net_server`, `misc` | hosted LLM APIs |
| `@ai` | `ai`, `ml`, `gen_collect`, `csv` | local System.ML / System.AI work |
| `@game` | `sdl2`, `sdl_game` | SDL games |

Aliases and explicit names mix freely (`-lib @ai,json`). Groups are user-editable: add a section to `configobjk.ini` and reference it as `@yourname`. An unknown alias fails with `Unknown library alias` — check the spelling and that `OBJECK_LIB_PATH` points at the library directory.

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
| Linear / regularized regression | `LinearRegression`, `RidgeRegression`, `LassoRegression`, `ElasticNet` | `ml` | None |
| Linear classifiers | `LogisticRegression`, `SVM`, `Perceptron` | `ml` | None |
| Probabilistic classifiers | `GaussianNaiveBayes`, `NaiveBayes` | `ml` | None |
| Trees & ensembles | `DecisionTree`, `RandomForest`, `AdaBoost` | `ml` | None |
| Nearest neighbors | `KNearestNeighbors`, `KDTree` | `ml` | None |
| Clustering | `KMeans`, `DBSCAN`, `GaussianMixture` | `ml` | None |
| Decomposition | `PCA` | `ml` | None |
| Neural network | `NeuralNetwork` | `ml` | None |
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

ollama/   ollama_chat.obs     ollama_vision.obs

opencv_onnx/  face_recognition.obs  yolo_detect.obs  phi3_chat.obs
```
