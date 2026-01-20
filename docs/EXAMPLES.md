# Objeck Code Examples

Collection of practical code examples demonstrating Objeck's capabilities.

## Table of Contents
- [Hello World](#hello-world)
- [AI Integration](#ai-integration)
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
