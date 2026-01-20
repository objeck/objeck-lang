# Objeck Libraries

Native libraries and frameworks that extend Objeck's capabilities. Most libraries consist of an Objeck interface (.obs) paired with a C++ native module (.cpp) that interfaces with external libraries like OpenSSL, SDL2, OpenCV, and OpenAI.

## Library Structure

Each library typically consists of:
- **Objeck source code** (.obs) - High-level API in Objeck
- **C++ native module** (.cpp/.h) - Native code interfacing with external libraries
- **Compiled library** (.obl) - Precompiled Objeck bytecode
- **External dependencies** - Third-party libraries (OpenSSL, SDL2, etc.)

## Categories

### AI & Machine Learning
- **openai.obs** - OpenAI API integration (GPT-4, GPT-5, DALL-E, Whisper, Realtime API)
- **gemini.obs** - Google Gemini API integration with schema support
- **ollama.obs** - Ollama local LLM integration
- **opencv.obs** - OpenCV computer vision library
- **onnx.obs** - ONNX Runtime for cross-platform ML inference
- **ml.obs** - Classic machine learning algorithms (GOFAI)

### Web & Networking
- **net_secure.obs** - HTTPS server and client (OpenSSL-based)
- **net_common.obs** - OAuth and common networking utilities
- **rss.obs** - RSS feed parser

### Data Processing
- **json.obs** - JSON hierarchical parser
- **json_stream.obs** - Streaming JSON parser for large files
- **xml.obs** - XML DOM parser
- **csv.obs** - CSV file reader/writer
- **query.obs** - In-memory SQL-like queries

### Data Storage
- **odbc.obs** - ODBC database connectivity (SQL Server, PostgreSQL, MySQL, etc.)
- **gen_collect.obs** - Generic collections (Map, List, Vector, Hash, etc.)

### Cryptography & Security
- **cipher.obs** - Encryption/decryption (AES, RSA) via OpenSSL

### Utilities
- **regex.obs** - Regular expression support
- **misc.obs** - Miscellaneous utilities
- **lang.obs** - Core language runtime support

### Gaming & Multimedia
- **sdl_game.obs** - 2D game framework (SDL2-based)
- **sdl2.obs** - SDL2 bindings for graphics, audio, input
- **lame.obs** - MP3 audio encoding (LAME)

### Diagnostics
- **diags.obs** - System diagnostics and profiling

## Building Libraries

Libraries are built alongside the compiler. Native modules are compiled into shared libraries:

### Windows
```bash
# Libraries are built with Visual Studio projects
# See core/lib/<library>/vs/*.vcxproj
```

### Linux/macOS
```bash
# Libraries are built via Makefiles
cd core/lib/<library>
make -f Makefile.amd64  # or Makefile.arm64
```

## Using Libraries

Libraries are automatically available to all Objeck programs:

```ruby
use Data.JSON;
use API.OpenAI;
use Collection;

class MyApp {
  function : Main(args : String[]) ~ Nil {
    # Use JSON
    json := JsonParser->Parse('{"name":"Objeck"}');

    # Use collections
    map := Map->New()<String, Int>;
    map->Insert("answer", 42);

    # Use OpenAI
    response := Response->Respond("gpt-4o", "Hello!", token);
  }
}
```

## Adding New Libraries

To add a new library:

1. Create Objeck source in `compiler/lib_src/<name>.obs`
2. Create native module in `lib/<name>/<name>.cpp` (if needed)
3. Add build configuration to appropriate Makefile/VS project
4. Rebuild compiler to include new library

## External Dependencies

Some libraries require external dependencies:
- **OpenSSL** - Used by net_secure, cipher
- **SDL2** - Used by sdl_game, sdl2
- **OpenCV** - Used by opencv
- **ONNX Runtime** - Used by onnx
- **ODBC** - Platform ODBC drivers
- **LAME** - Used by lame for MP3 encoding

## See Also

- [Main README](../../README.md) - Project overview
- [Compiler](../compiler/README.md) - Building and linking libraries
- [API Documentation](https://www.objeck.org) - Complete library reference
- [Library Source](../compiler/lib_src/) - Objeck library source code