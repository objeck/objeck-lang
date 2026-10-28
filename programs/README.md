# Objeck Programs

Collection of example programs, frameworks, tools, and tests demonstrating Objeck's capabilities. From simple "Hello World" to complex AI applications and games.

## Directory Structure

| Directory | Description | Examples |
|-----------|-------------|----------|
| **[examples](examples)** | Rosetta Code examples and language demonstrations | Algorithms, data structures, classic problems |
| **[frameworks](frameworks)** | Framework integration prototypes and demos | OpenAI, Gemini, OpenCV, ONNX, SDL2, GTK, ODBC |
| **[deploy](deploy)** | Example programs shipped in every release's `examples/` directory, plus release utilities | Numbered examples indexed in [deploy/README.md](deploy/README.md), release file renaming (`util/`) |
| **[langs](langs)** | Mini languages and interpreters written in Objeck | Expression evaluators, DSLs, simple interpreters |
| **[regression](regression)** | Regression test suite, run by CI on every platform | About 300 tests, listed in [regression/TESTS.md](regression/TESTS.md) |
| **[tests](tests)** | Unit tests, integration tests, and benchmarks | Language feature tests, performance benchmarks |
| **[web-playground](web-playground)** | The browser playground at [playground.objeck.org](https://playground.objeck.org) | FastAPI backend, sandboxed runner image, frontend, demo programs |

## Featured Examples

### AI & Machine Learning
- **[OpenAI Examples](frameworks/openai/)** - GPT-4, GPT-5, DALL-E, Whisper, Realtime API
- **[Gemini Examples](frameworks/gemini/)** - Google Gemini with structured output
- **[Ollama Examples](frameworks/ollama/)** - Local LLM integration
- **[OpenCV Examples](frameworks/opencv_onnx/)** - Computer vision, face detection, image processing
- **[ONNX Examples](frameworks/opencv_onnx/)** - ML model inference

### Web & Networking
- **[Web Framework](frameworks/web/)** - HTTP server, REST APIs, web scraping
- **[HTTP Examples](examples/)** - `http.obs`, `https.obs`, `http_server.obs`
- **[HTTPS Client](deploy/http_client_22.obs)**, **[Web Server](deploy/web_server_23.obs)** and **[RSS Reader](deploy/rss_reader_24.obs)** - the shipped networking examples

### Games & Graphics
- **[SDL2 Games](frameworks/sdl/)** - 2D games using SDL2
- **[Gem Collector](deploy/game_31.obs)** - The shipped SDL2 game: a game loop, a state machine, sprites and a saved best score; the [OpenGL examples](examples/) include `gl_fps.obs`

### Data Processing
- **[JSON Examples](frameworks/json/)** - JSON parsing, generation, streaming
- **[XML Examples](examples/)** - XML processing (`xml_*.obs`)
- **[Database Examples](frameworks/odbc/)** - SQL queries, ODBC connectivity

### Algorithms
- **[Rosetta Code](examples/)** - Implementation of Rosetta Code tasks
- **[Sorting Algorithms](examples/)** - `quick_sort.obs`, `shell_sort.obs`, `heap.obs` (heapsort) and more
- **[Data Structures](examples/)** - `queue.obs`, `double_list.obs`, `hash.obs`, `vlist.obs`

## Running Examples

### Basic Examples
```bash
# Compile and run
cd deploy
obc -s hello_0.obs -d hello_0.obe
obr hello_0.obe
```

Each program in `deploy/` opens with a comment giving its exact compile and run lines; [deploy/README.md](deploy/README.md) lists the libraries each one needs.

### Framework Examples
```bash
# OpenAI example (requires API key)
cd frameworks/openai
echo "your-api-key" > api_key.txt
obc -s openai_chat.obs -lib openai,csv,net,json,misc -d openai_chat.obe
obr openai_chat.obe create    # creates an assistant over files/*.json
obr openai_chat.obe           # chats with it
```

The OpenCV and ONNX examples in `frameworks/opencv_onnx/` need model files that are not in the repository; see [docs/MODELS.md](../docs/MODELS.md).

### Game Examples
```bash
# SDL2 game
cd deploy
obc -src game_31.obs -lib sdl2,sdl_game,json
obr game_31.obe
```

## Testing

To run the regression suite against a deploy tree built under `core/release`:

```bash
cd regression
./run_regression.sh x64     # Linux/macOS (or arm64)
run_regression.cmd x64      # Windows (or arm64)
```

[TESTING.md](TESTING.md) describes every test suite and how CI runs them.

## Benchmarks

Performance benchmarks are in the `tests` directory:
- **[clbg](tests/clbg/)** - Computer Language Benchmarks Game programs: binarytrees, fannkuchredux, fasta, mandelbrot, nbody, spectralnorm
- **[perf](tests/perf/)** - Micro-benchmarks for the optimizer, method dispatch, array work and GC churn

## Contributing Examples

When adding new examples:
1. Place in appropriate directory (examples, frameworks, etc.)
2. Include a README explaining what the example demonstrates
3. Add clear comments in the code
4. Test on multiple platforms if possible
5. Update this index

### Example Template
```ruby
#
# Example: [Name]
# Description: [What this demonstrates]
# Usage: obr example.obe [args]
#

class Example {
  function : Main(args : String[]) ~ Nil {
    # Your code here
    "Hello, Objeck!"->PrintLine();
  }
}
```

## Learning Path

Recommended order for learning Objeck:

1. **Basic Syntax** - The shipped examples in [deploy/](deploy/README.md) are numbered in learning order: start with `hello_0.obs` through `closures_8.obs`
2. **Object-Oriented** - `deploy/classes_4.obs` and `deploy/records_enums_5.obs`, then error handling in `deploy/errors_6.obs` and generics in `deploy/generics_7.obs`
3. **Collections** - Explore `examples/queue.obs`, `examples/hash.obs` and `examples/double_list.obs`
4. **File I/O** - Check `deploy/files_13.obs`, `deploy/serialize_20.obs` and `examples/file.obs`
5. **Networking** - Try `deploy/http_client_22.obs` and `deploy/web_server_23.obs`, then the `frameworks/web` examples
6. **AI/ML** - Experiment with `frameworks/openai` or `frameworks/gemini`
7. **Advanced** - Dive into `frameworks/opencv_onnx` or games

## External Resources

- **[Rosetta Code](https://rosettacode.org/wiki/Category:Objeck)** - More Objeck examples
- **[API Documentation](https://www.objeck.org)** - Complete library reference
- **[Language Guide](https://www.objeck.org/getting_started.html)** - Comprehensive tutorial

## See Also

- [Main README](../README.md) - Project overview
- [EXAMPLES.md](../docs/EXAMPLES.md) - Curated code examples
- [FEATURES.md](../docs/FEATURES.md) - Language features with examples
- [API Documentation](https://www.objeck.org) - Complete reference