<p align="center">
  <img src='https://github.com/objeck/objeck-lang/blob/master/core/lib/code_doc/templates/resources/objeck-logo-alt.png' width="50%" height="50%"/>
</p>

<p align="center">
Another programming language
</p>

<hr/>

<p align="center">
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/codeql.yml/badge.svg" alt="GitHub CodeQL"></a>
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml/badge.svg" alt="GitHub CI"></a>
  <a href="https://scan.coverity.com/projects/objeck"><img src="https://img.shields.io/coverity/scan/10314.svg" alt="Coverity SCA"></a>
</p>

## Updates

v2025.7.0
  * Added ``Hash->Dict(..)``, ``Map->Dict(..)`` and ``Vector->Zip(..)``
  * Updated style (docs, logos, etc.)
  * Bug fixes
    
<ins>v2025.6.3</ins>
  * Support for user-provided HTTPS PEM files
  * Added multi-statement pre/update support for ``for`` loops
      
v2025.6.2
  * New [API documentation](https://www.objeck.org/doc/api/index.html) system
  * Added support for OpenAI's <a href='https://platform.openai.com/docs/api-reference/responses'>Responses API</a>
  * Updated Windows launcher
  * Improved JSON scheme support
    
## Code Examples

```ruby
# hello world
class Hello {
  function : Main(args : String[]) ~ Nil {
    "Hello World" → PrintLine();
    "Καλημέρα κόσμε" → PrintLine();
    "こんにちは 世界" → PrintLine();
  }
}
```

```ruby
# openai response
query := Collection.Pair->New("user", "What is the most common espresso drink ordered in Columbia?")<String, String>;
response := Response->Respond("gpt-4o-mini", query, token);
if(response = Nil) {
  Response->GetLastError()->ErrorLine();
  return;
};
response->ToString()->PrintLine();
```

```ruby
# gemini generate w/ schema
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

```ruby
# openai image identification 
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

## Design <sub><a href='https://github.com/objeck/objeck-lang/tree/master/core'>[1]</a></sub>

* Object-oriented and functional
* Cross-platform support for Linux, macOS, and Windows (including Docker and RPI 3/4/5)
* JIT-compiled runtimes (ARM64 and AMD64)
* REPL shell
* LSP [plugins](https://github.com/objeck/objeck-lsp) for VSCode, Sublime, Kate, and more
* API [documentation](https://www.objeck.org)

## Libraries <sub><a href='https://github.com/objeck/objeck-lang/tree/master/core/compiler/lib_src'>[2]</a></sub>

* Machine learning ([OpenAI](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/openai.obs), [Gemini](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gemini.obs), [Ollama](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/ollama.obs), [GOFAI](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/ml.obs))
* Web ([server](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs), [client](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs), [OAuth](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_common.obs))
* Data exchange
  * JSON ([hierarchical](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/json.obs), [streaming](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/json_stream.obs))
  * [XML](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/xml.obs)
  * [CSV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/csv.obs)
  * [Binary](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/lang.obs)
* [RSS](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/rss.obs)
* [Collections](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gen_collect.obs)
* Data access
  * [Relational SQL](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/odbc.obs)
  * [In-memory](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/query.obs)
* [Encryption](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/cipher.obs)
* [Regular expressions](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/regex.obs)
* [2D gaming](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/sdl_game.obs)

## Features

### Object-oriented
  
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

### Functional
#### Closures and Lambda Expressions
```ruby
funcs := Vector->New()<FuncRef<IntRef>>;
each(i : 10) {
  funcs->AddBack(FuncRef->New(\() ~ IntRef : () 
    => i->Factorial() * funcs->Size())<IntRef>);
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
