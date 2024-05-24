<h1 align="center">Fully Programmable with Instructions</h1>

<p align="center">
  <a href="https://www.objeck.org"><img src="docs/images/gear_wheel_256.png""  width="300" alt="An Objeck"/></a>
</p>

<hr/>

<p align="center">
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml/badge.svg" alt="GitHub CodeQL"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml/badge.svg" alt="GitHub CI"></a>
  <a href="https://scan.coverity.com/projects/objeck"><img src="https://img.shields.io/coverity/scan/10314.svg" alt="Coverity SCA"></a>
</p>

Objeck is a general-purpose, cross-platform, object-oriented, and functional programming language geared towards machine learning development.

## Releases

* v2024.6.x
  * LLaMa 3 local model support
  * Unified ML framework across OpenAI, Gemini and LLaMa
 
* v2024.6.1
  * Common framework for tuning OpenAI and Gemini models
  
* [v2024.6.0](https://github.com/objeck/objeck-lang/tree/v2024.5.1/core)
    * Model tuning
      * OpenAI [done]
      * Gemini
    * Image generation
      * OpenAI [enqueue]
      * Gemini
      
* [v2024.5.0](https://github.com/objeck/objeck-lang/tree/master/core) (current)
  * Gemini support for function calls
  * JSON Scheme support for function modeling for OpenAI, Gemini, and LLaMa
  * Enhancements
    * Added Collection 'Reduce' methods
    * Boxing/unboxing support for the '<' and '>' operators (legacy missing feature)
  * Bug fixes
    * Fixed Collection 'Filter' methods
    * Resolved ODBC refactor bug

## Code Examples

```ruby
# create an image from a prompt
use API.OpenAI, API.OpenAI.FineTuning, System.IO.Filesystem, Data.JSON;

class CreateImage {
  function : Main(args : String[]) ~ Nil {
    image := Image->Create("Create an image of two old steel gears with a transparent background", token);
    if(image <> Nil) {
      urls := image->GetUrls();
      each(url in urls) {
        url->ToString()->PrintLine();
      };
    };
  }
 }
```

```ruby
# image identification 
use API.Google.Gemini, System.IO.Filesystem;

class IdentifyImage {
  function : Main(args : String[]) ~ Nil {
    content := Content->New("user")->AddPart(TextPart->New("What number is this image showing?"))
      ->AddPart(BinaryPart->New(FileReader->ReadBinaryFile("thirteen.png"), "image/png"))
      ->AddPart(TextPart->New("Format output as JSON"));

    candidates := Model->GenerateContent("models/gemini-pro-vision", content, EndPoint->GetApiKey());
    if(candidates->Size() > 0) {
      candidates->First()->GetAllText()->Trim()->PrintLine();
    };
  }
}
```

```ruby
# turn an ML model
use Collection, API.OpenAI, System.IO.Filesystem, Data.JSON, Data.CSV;

class CreateImage {
  function : Main(args : String[]) ~ Nil {
    if(args->Size() = 1) {
      tuning_file := args[1];
      file := API.OpenAI.File->LoadOrCreate(tuning_file, "fine-tune", token);

      name := file->Gettuning_file();
      id := file->GetId();
      "file='{$name}', id='{$id}'"->PrintLine();

      tuning_job := Tuning->Create("gpt-3.5-turbo", id, token);
      tuning_job->ToString()->PrintLine();
    }
  }
}
```

```ruby
# vector embeddings similarities
use Web.HTTP, Collection, Data.JSON, API.Google.Gemini.Corpus;

class Embaddings {
  function : Main(args : String[]) ~ Nil {
    if(args->Size() = 1 & args[0]->Equals("list")) {
      corpuses := Corpus->List()<Corpus>;
      each(corpus in corpuses) {
        corpus_id := corpus->GetId();
        corpus_create_str := corpus->GetCreateTime()->ToShortString();
        "corpus: id='{$corpus_id}', created={$corpus_create_str}"->PrintLine();      
        
        documents := Document->List(corpus->As(Corpus))<Document>;
        each(document in documents) {
          doc_id := document->GetId();
          doc_create_str := document->GetCreateTime()->ToShortString();
          "\tdocument='{$doc_id}', created={$doc_create_str}"->PrintLine();

          chunks := Chunk->List(document);
          each(chunk in chunks) {
            chunk_id := chunk->GetId();
            chunk_state := chunk->GetState();
            chunk_create_str := corpus->GetCreateTime()->ToShortString();
            "\t\tchunk='{$chunk_id}', state={$chunk_state}, created={$chunk_create_str}"->PrintLine();          
          };
        };
      };
    }
    else if(args->Size() = 1 & args[0]->Equals("rebuild")) {
      # clean up
      corpuses := Corpus->List();
      each(corpus in corpuses) {
        documents := Document->List(corpus);
        each(document in documents) {
          chunks := Chunk->List(document);
          each(chunk in chunks) {
            chunk->Delete()->PrintLine();
          };
          document->Delete()->PrintLine();
        };
        corpus->Delete()->PrintLine();
      };

      # corups
      corpus := Corpus->Create("Corpus 1");

      metadata := Map->New()<String, String>;
      metadata->Insert("about", "fruit, vegetable, vehicle, human, and animal");
      document := Document->Create("Document 1", metadata, corpus);

      metadata := Map->New()<String, String>;
      metadata->Insert("category", "fruit");
      Chunk->Create("Nature's candy! Seeds' sweet ride to spread, bursting with colors, sugars, and vitamins. Fuel for us, future for plants. Deliciously vital!", metadata, document)->ToString()->PrintLine();

      metadata := Map->New()<String, String>;
      metadata->Insert("category", "vegetable");
      Chunk->Create("Not just leaves! Veggies sprout from roots, stems, flowers, and even bulbs. Packed with vitamins, minerals, and fiber galore, they fuel our bodies and keep us wanting more.", metadata, document)->ToString()->PrintLine();

      metadata := Map->New()<String, String>;
      metadata->Insert("category", "vehicle");
      Chunk->Create("Metal chariots or whirring steeds, gliding on land, skimming seas, piercing clouds. Carrying souls near and far, vehicles weave paths for dreams and scars.", metadata, document)->ToString()->PrintLine();

      metadata := Map->New()<String, String>;
      metadata->Insert("category", "human");
      Chunk->Create("Walking contradictions, minds aflame, built for laughter, prone to shame. Woven from stardust, shaped by clay, seeking answers, paving the way.", metadata, document)->ToString()->PrintLine();

      metadata := Map->New()<String, String>;
      metadata->Insert("category", "animal");
      Chunk->Create("Sentient dance beneath the sun, from buzzing flies to whales that run. Flesh and feather, scale and claw, weaving instincts in nature's law. ", metadata, document)->ToString()->PrintLine();

      metadata := Map->New()<String, String>;
      metadata->Insert("category", "other");
      Chunk->Create("Except for fruit, vegetable, vehicle, human, and animal", metadata, document)->ToString()->PrintLine();
    }
    else if(args->Size() = 3 & args[0]->Equals("query")) {
      id := args[1];
      query := args[2];

      document := Document->Get(id);
      document->ToString()->PrintLine();

      results := document->Query(query);
      each(result in results) {
        relevance := result->GetFirst()->As(FloatRef);
        metadata := result->GetThird()->As(Map<String, String>);

        relevance->PrintLine();
        metadata->ToString()->PrintLine();
        "---"->PrintLine();
      }
    };
  }
}
```

## [Architecture](https://github.com/objeck/objeck-lang/tree/master/core)

* Object-oriented and functional
* Cross-platform: Linux, macOS, Windows
* JIT-compiled runtimes (ARM64 and AMD64)
* REPL shell
* [LSP pulgins](https://github.com/objeck/objeck-lsp) for VSCode, Sublime, Kate, and more
* API [documentation](https://www.objeck.org)

## Libraries

* Machine learning (OpenAI, Gemini, GOFAI)
* Web (server, client, OAuth)
* Data serialization
  * JSON (hierarchical, streaming)
  * XML (parsing, RSS)
  * Binary
* Collections
* Data access
  * Relational SQL
  * In-memory
* Encryption
* Regular expressions
* 2D gaming
* 

## Glance of Features

### [Object-oriented](https://en.wikipedia.org/wiki/Object-oriented_programming)
  
#### Inheritance
```ruby
class Triangle from Shape {
  New() {
    Parent();
  }
}
```

#### Interfaces
```ruby
class Triangle from Shape implements Color {
  New() {
    Parent();
  }

  method : public : GetRgb() ~ Int {
    return 0xadd8e6;
  }
}

interface Color {
  method : virtual : public : GetRgb() ~ Int;
}
```

#### Type Inference
```ruby
value := "Hello World!";
value->Size()->PrintLine();
```

#### Anonymous Classes
```ruby
interface Greetings {
  method : virtual : public : SayHi() ~ Nil;
}

class Hello {
  function : Main(args : String[]) ~ Nil {
    hey := Base->New() implements Greetings {
      New() {}

      method : public : SayHi() ~ Nil {
        "Hey..."->PrintLine();
      }
    };
}
```

#### Reflection
```ruby
klass := "Hello World!"->GetClass();
klass->GetName()->PrintLine();
klass->GetMethodNumber()->PrintLine();
```

#### Dependency Injection
```ruby
value := Class->Instance("System.String")->As(String);
value += "510";
value->PrintLine();
```
#### Generics
```ruby
map := Collection.Map->New()<IntRef, String>;
map->Insert(415, "San Francisco");
map->Insert(510, "Oakland");
map->Insert(408, "Sunnyvale");
map->ToString()->PrintLine();
```

#### Type Boxing
```ruby
list := Collection.List->New()<IntRef>;
list->AddBack(17);
list->AddFront(4);
(list->Back() + list->Front())->PrintLine();
```

#### Static import
```ruby
use function Int;

class Test {
  function : Main(args : String[]) ~ Nil {
    Abs(-256)->Sqrt()->PrintLine();
  }
}
```

#### Serialization
```ruby
serializer := System.IO.Serializer->New();
serializer->Write(map);
serializer->Write("Fin.");
bytes := serializer->Serialize();
bytes->Size()->PrintLine();
```

### [Functional](https://en.wikipedia.org/wiki/Functional_programming)
#### Closures and Lambda Expressions
```ruby
funcs := Vector->New()<FuncRef<IntRef>>;
each(i : 10) {
  funcs->AddBack(FuncRef->New(\() ~ IntRef : () 
    => System.Math.Routine->Factorial(i) * funcs->Size())<IntRef>);
};

each(i : funcs) {
  value := funcs->Get(i)<FuncRef>;
  func := value->Get();
  func()->Get()->PrintLine();
};
```

#### First-Class Functions
```ruby
@f : static : (Int) ~ Int;
@g : static : (Int) ~ Int;

function : Main(args : String[]) ~ Nil {
  compose := Composer(F(Int) ~ Int, G(Int) ~ Int);
  compose(13)->PrintLine();
}

function : F(a : Int) ~ Int {
  return a + 14;
}

function : G(a : Int) ~ Int {
  return a + 15;
}

function : native : Compose(x : Int) ~ Int {
  return @f(@g(x));
}

function : Composer(f : (Int) ~ Int, g : (Int) ~ Int) ~ (Int) ~ Int {
  @f := f;
  @g := g;
  return Compose(Int) ~ Int;
}
```

### Host Support
#### Unicode
```ruby
"Καλημέρα κόσμε"->PrintLine();
```

#### File System
```ruby
content := Sytem.IO.Filesystem.FileReader->ReadFile(filename);
content->Size()->PrintLine();
Sytem.IO.Filesystem.File->Size(filename)->PrintLine();
```

#### Sockets
```ruby
socket->WriteString("GET / HTTP/1.1\nHost:google.com\nUser Agent: Mozilla/5.0 (compatible)\nConnection: Close\n\n");
line := socket->ReadString();
while(line <> Nil & line->Size() > 0) {
  line->PrintLine();  
  line := socket->ReadString();
};
socket->Close();
```

#### Named Pipes
```ruby
pipe := System.IO.Pipe->New("foobar", Pipe->Mode->CREATE);
if(pipe->Connect()) {
  pipe->ReadLine()->PrintLine();
  pipe->WriteString("Hi Ya!");
  pipe->Close();
};
```

#### Threads
```ruby
class CaculateThread from Thread {
  ...
  @inc_mutex : static : ThreadMutex;

  New() {
    @inc_mutex := ThreadMutex->New("inc_mutex");
  }
  
  method : public : Run(param : System.Base) ~ Nil {
    Compute();
  }

  method : native : Compute() ~ Nil {
    y : Int;

    while(true) {
      critical(@inc_mutex) {
        y := @current_line;
        @current_line+=1;
      };
      ...
    };
  }
}
```
#### Date/Times
```ruby
yesterday := System.Time.Date->New();
yesterday->AddDays(-1);
yesterday->ToString()->PrintLine();
```

## Screenshots
| <sub>[VS Code](https://github.com/objeck/objeck-lsp)</sub> | <sub>[Debugger](https://github.com/objeck/objeck-lang/tree/master/core/debugger)</sub> | <sub>[Dungeon Crawler](https://github.com/objeck/objeck-dungeon-crawler)</sub> | <sub>[Platformer](https://github.com/objeck/objeck-lang/blob/master/programs/deploy/2d_game_13.obs)</sub> | <sub>[Windows Utility](https://github.com/objeck/objeck-lang/tree/master/core/release/WindowsLauncher)</sub> |
| :---: | :----: | :---: | :---: | :---: |
![alt text](docs/images/web/comp.png "Visual Studio Code") | ![alt text](docs/images/web/debug.jpg "Command line debugger") | ![alt text](docs/images/web/crawler.png "Web Crawler") | ![alt text](docs/images/web/2d_game.jpg "Platformer") | ![alt text](docs/images/web/launch.png "Windows Launcher") |
