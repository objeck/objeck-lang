# Objeck Code Examples

Collection of practical code examples demonstrating Objeck's capabilities.

> **AI developers:** See the full [AI & ML Developer Guide](AI.md) for comprehensive coverage of OpenAI, Gemini, Ollama, ONNX, OpenCV, and NLP.

## Table of Contents
- [Hello World](#hello-world)
- [Web & Networking](#web--networking)
- [AI Integration](#ai-integration)
  - [OpenAI: Realtime, Moderation, Batch, Image Recognition](#ai-integration)
  - [Gemini: Generate, Grounding, Batch Embeddings, Files, Caching](#ai-integration)
- [Computer Vision](#computer-vision)
- [More Examples](#more-examples)

---

<a name="hello-world"></a>
## Hello World

### Basic Hello World
```ruby
class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World"->PrintLine();
  }
}
```

### Unicode Support
```ruby
class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World"->PrintLine();
    "Καλημέρα κόσμε"->PrintLine();
    "こんにちは 世界"->PrintLine();
  }
}
```

---

<a name="web--networking"></a>
## Web & Networking

### HTTP/1.1 HTTPS Client
```ruby
use Web.HTTP;

class HttpsExample {
  function : Main(args : String[]) ~ Nil {
    # Simple GET
    resp := HttpsClient->QuickGet(Url->New("https://httpbin.org/get"));
    if(resp <> Nil & resp->GetCode() = 200) {
      String->New(resp->GetContent())->PrintLine();
    };

    # POST with JSON
    body := "{\"lang\":\"objeck\"}"->ToByteArray();
    resp := HttpsClient->QuickPost(Url->New("https://httpbin.org/post"),
                                   body, "application/json");
    resp->GetCode()->PrintLine();   # 200
  }
}
```

### HTTP/2 Client
```ruby
use Web.HTTP;

class Http2Example {
  function : Main(args : String[]) ~ Nil {
    # Persistent connection — all requests share one TLS session
    client := Http2Client->New("httpbin.org");
    if(<>client->IsConnected()) {
      "HTTP/2 unavailable"->ErrorLine();
      return;
    };

    # GET
    resp := client->Get("/get");
    "GET status: {$resp->GetCode()}"->PrintLine();   # 200

    # POST with JSON
    body := "{\"lang\":\"objeck\"}"->ToByteArray();
    resp := client->Post("/post", body, "application/json");
    "POST status: {$resp->GetCode()}"->PrintLine();  # 200

    # Custom header
    client->AddHeader("x-client", "objeck");
    resp := client->Get("/headers");
    String->New(resp->GetContent())->PrintLine();

    # Multiple requests reuse the same connection automatically
    resp1 := client->Get("/ip");
    resp2 := client->Get("/user-agent");

    client->Close();

    # One-liner for quick fire-and-forget requests
    resp := Http2Client->QuickGet(Url->New("https://httpbin.org/get"));
    resp->GetCode()->PrintLine();   # 200
  }
}
```

### HTTP/3 / QUIC Client
```ruby
use Web.HTTP;

class Http3Example {
  function : Main(args : String[]) ~ Nil {
    # QUIC over UDP — zero round-trip on repeat connections
    client := Http3Client->New("quic.nginx.org");
    if(<>client->IsConnected()) {
      "HTTP/3 unavailable"->ErrorLine();
      return;
    };

    # GET
    resp := client->Get("/");
    "GET status: {$resp->GetCode()}"->PrintLine();   # 200

    # POST
    body := "hello=world"->ToByteArray();
    resp := client->Post("/", body, "application/x-www-form-urlencoded");
    "POST status: {$resp->GetCode()}"->PrintLine();

    # Connection reuse — second request is free (no new handshake)
    resp2 := client->Get("/");

    client->Close();

    # One-liner
    resp := Http3Client->QuickGet(Url->New("https://quic.nginx.org/"));
    resp->GetCode()->PrintLine();   # 200
  }
}
```

---

<a name="ai-integration"></a>
## AI Integration

### OpenAI Realtime API
```ruby
# OpenAI Realtime API - get text AND audio responses
response := Realtime->Respond("How many James Bond movies have been made?",
                              "gpt-4o-realtime-preview-2025-06-03", token);
if(response <> Nil & response->GetFirst() <> Nil & response->GetSecond() <> Nil) {
  # get text
  text := response->GetFirst();
  text_size := text->Size();
  "text: size={$text_size}, text='{$text}'"->PrintLine();

  # get audio
  audio := response->GetSecond();
  audio_bytes := audio->Get();
  audio_bytes_size := audio_bytes->Size();
  "audio: size={$audio_bytes_size}"->PrintLine();

  # play audio
  "---\nplaying..."->PrintLine();
  Mixer->PlayPcm(audio_bytes, 22050, AudioFormat->SDL_AUDIO_S16LSB, 1);
};
```

### Gemini with Schema
```ruby
# Gemini generate w/ schema
content := Content->New("user")->AddPart(TextPart->New("What are the top 5 cities average snowfall in the Eastern US by city for the past 5 years?"));

# set schema
schema_def := ParameterType->New(["year", "name", "inches"], true);
schema_def->AddProp("year", ParameterType->New(ParameterType->Type->STRING));
schema_def->AddProp("name", ParameterType->New(ParameterType->Type->STRING));
schema_def->AddProp("inches", ParameterType->New(ParameterType->Type->INTEGER));
resp_schema := Pair->New("application/json", schema_def)<String, ParameterType>;

# make query
candidates := Model->GenerateContent("models/gemini-2.5-flash-preview-05-20", content, resp_schema, EndPoint->GetApiKey());
if(candidates <> Nil & <>candidates->IsEmpty()) {
  Data.JSON.JsonElement->Decode(candidates->First()->GetAllText()->Trim())->PrintLine();
};
```

### OpenAI Image Recognition
```ruby
# OpenAI image identification
bytes := System.IO.Filesystem.FileReader->ReadBinaryFile("../gemini/thirteen.png");
bytes->Size()->PrintLine();

image := API.OpenAI.ImageQuery->New("What number is this?", bytes, API.OpenAI.ImageQuery->MimeType->PNG);
file_query := Collection.Pair->New("user", image)<String, API.OpenAI.ImageQuery>;

response := Response->Respond("gpt-4o-mini", file_query, token);
if(response = Nil) {
  Response->GetLastError()->ErrorLine();
  return;
};

Data.JSON.JsonElement->Decode(response->GetText())->PrintLine();
```

### OpenAI Content Moderation
```ruby
# Check text for policy violations — returns per-category flags and scores
result := Moderation->Check("I love programming!", token);
if(result <> Nil) {
  "Flagged: {$result->IsFlagged()}"->PrintLine();
  if(result->IsFlagged()) {
    score := result->GetScore("violence");
    "violence score: {$score}"->PrintLine();
  };
};
```

### OpenAI Batch Processing
```ruby
# Submit a batch of requests (up to 50k) for async processing at 50% cost
file_id := "file-abc123";   # upload a .jsonl of requests first
job := Batch->Create(file_id, "/v1/chat/completions", token);
if(job <> Nil) {
  "batch id: {$job->GetId()}"->PrintLine();
  "status: {$job->GetStatus()}"->PrintLine();   # "validating" → "in_progress" → "completed"
};

# Poll later
job := Batch->Get(job->GetId(), token);
if(job->IsComplete()) {
  output_file_id := job->GetOutputFileId();
  "output file: {$output_file_id}"->PrintLine();
};
```

### Gemini Google Search Grounding
```ruby
# Ground the model's answer in live Google Search results
content := Content->New("user")->AddPart(TextPart->New("What are the latest AI breakthroughs?"));
candidates := Model->GenerateContentWithGrounding("models/gemini-2.0-flash", content, token);
if(candidates <> Nil & <>candidates->IsEmpty()) {
  candidates->First()->GetAllText()->PrintLine();
};
```

### Gemini Batch Embeddings
```ruby
# Embed multiple texts in one round-trip
texts := Vector->New()<String>;
texts->AddBack("Objeck is a JIT-compiled language");
texts->AddBack("Python is popular for data science");
texts->AddBack("Rust focuses on memory safety");

embeddings := Model->BatchEmbedContent("models/text-embedding-004", texts, token);
each(i : embeddings) {
  emb := embeddings->Get(i);
  dim := emb->Size();
  "  [{$i}] dim={$dim}"->PrintLine();
};
```

### Gemini Files API
```ruby
# Upload a file for use in model requests
data := FileReader->ReadBinaryFile("document.pdf");
file := FileManager->Upload("my-doc", data, "application/pdf", token);
if(file <> Nil & file->IsActive()) {
  "URI: {$file->GetUri()}"->PrintLine();
};

# List and delete
files := FileManager->List(token);
each(f in files) {
  FileManager->Delete(f->GetName(), token);
};
```

### Gemini Context Caching
```ruby
# Cache large content server-side to avoid re-tokenizing on every request
content := Content->New("user")->AddPart(TextPart->New(large_system_prompt));
item := CachedContent->Create("models/gemini-1.5-pro-001", content, 300, "my-cache", token);
if(item <> Nil) {
  "name: {$item->GetName()}"->PrintLine();
  "tokens cached: {$item->GetTokenCount()}"->PrintLine();
  "expires: {$item->GetExpireTime()}"->PrintLine();
};
```

---

<a name="computer-vision"></a>
## Computer Vision

### OpenCV Face Detection
```ruby
# OpenCV face detection
detector := FaceDetector->New("haarcascade_frontalface_default.xml");
faces := detector->Detect(image);
faces->Size()->PrintLine();  # "5 faces detected"
```

### OpenCV Image Processing
```ruby
# Load and process an image
image := Image->New("photo.jpg");
gray := image->ToGray();
blurred := gray->GaussianBlur(5, 5);
edges := blurred->Canny(50, 150);
edges->Save("edges.jpg");
```

---

<a name="more-examples"></a>
## More Examples

For complete working examples, visit:
- [Examples Directory](https://github.com/objeck/objeck-lang/tree/master/programs/examples)
- [Framework Examples](https://github.com/objeck/objeck-lang/tree/master/programs/frameworks)
- [Test Programs](https://github.com/objeck/objeck-lang/tree/master/programs/tests)

### Additional Resources
- [API Documentation](https://www.objeck.org)
- [Language Features](FEATURES.md)
- [Back to README](../README.md)
