# Objeck Programs

Collection of example programs, frameworks, tools, and tests demonstrating Objeck's capabilities. From simple "Hello World" to complex AI applications and games.

## Directory Structure

| Directory | Description | Examples |
|-----------|-------------|----------|
| **[examples](examples)** | Rosetta Code examples and language demonstrations | Algorithms, data structures, classic problems |
| **[frameworks](frameworks)** | Framework integration prototypes and demos | OpenAI, Gemini, OpenCV, ONNX, SDL2, GTK, ODBC |
| **[deploy](deploy)** | Tools and utilities shipped with distribution | Installers, deployment scripts, documentation generators |
| **[langs](langs)** | Mini languages and interpreters written in Objeck | Expression evaluators, DSLs, simple interpreters |
| **[tests](tests)** | Unit tests, integration tests, and benchmarks | Language feature tests, performance benchmarks |

## Featured Examples

### AI & Machine Learning
- **[OpenAI Examples](frameworks/openai/)** - GPT-4, GPT-5, DALL-E, Whisper, Realtime API
- **[Gemini Examples](frameworks/gemini/)** - Google Gemini with structured output
- **[Ollama Examples](frameworks/ollama/)** - Local LLM integration
- **[OpenCV Examples](frameworks/opencv_onnx/)** - Computer vision, face detection, image processing
- **[ONNX Examples](frameworks/opencv_onnx/)** - ML model inference

### Web & Networking
- **[Web Framework](frameworks/web/)** - HTTP server, REST APIs, web scraping
- **[OAuth Examples](frameworks/)** - Authentication and API integration
- **[RSS Examples](frameworks/)** - Feed parsing and aggregation

### Games & Graphics
- **[SDL2 Games](frameworks/sdl/)** - 2D games using SDL2
- **[Game Examples](examples/games/)** - Classic games reimplemented in Objeck

### Data Processing
- **[JSON Examples](examples/json/)** - JSON parsing, generation, streaming
- **[XML Examples](examples/xml/)** - XML processing
- **[Database Examples](frameworks/odbc/)** - SQL queries, ODBC connectivity

### Algorithms
- **[Rosetta Code](examples/)** - Implementation of Rosetta Code tasks
- **[Sorting Algorithms](examples/algorithms/)** - Various sorting implementations
- **[Data Structures](examples/data_structures/)** - Trees, graphs, heaps, etc.

## Running Examples

### Basic Examples
```bash
# Compile and run
cd examples/hello
obc -s hello.obs -d hello.obe
obr hello.obe
```

### Framework Examples
```bash
# OpenAI example (requires API key)
cd frameworks/openai
echo "your-api-key" > openai_api_key.dat
obc -s openai_chat.obs -d openai_chat.obe
obr openai_chat.obe

# OpenCV example
cd frameworks/opencv_onnx
obc -s face_detection.obs -d face_detection.obe
obr face_detection.obe photo.jpg
```

### Game Examples
```bash
# SDL2 game
cd frameworks/sdl/simple_game
obc -s game.obs -d game.obe
obr game.obe
```

## Testing

Run the test suite to verify your Objeck installation:

```bash
cd tests
./run_tests.sh    # Linux/macOS
run_tests.cmd     # Windows
```

## Benchmarks

Performance benchmarks are in the `tests` directory:
- **Fibonacci** - Recursive and iterative implementations
- **Sorting** - QuickSort, MergeSort, HeapSort
- **Memory** - Allocation and GC performance
- **Threading** - Multi-threaded performance

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

1. **Basic Syntax** - Start with `examples/hello` and `examples/basics`
2. **Object-Oriented** - Move to `examples/oop`
3. **Collections** - Explore `examples/collections`
4. **File I/O** - Check `examples/io`
5. **Networking** - Try `frameworks/web` examples
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