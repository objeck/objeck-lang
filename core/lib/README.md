# Objeck Libraries

Native libraries and frameworks that extend Objeck's capabilities. Most libraries are written in Objeck alone (.obs); the rest pair an Objeck interface with a C++ native module (.cpp) that interfaces with external libraries like mbedTLS, SDL2, OpenCV, and ONNX Runtime.

## Library Structure

Each library typically consists of:
- **Objeck source code** (.obs) - High-level API in Objeck
- **C++ native module** (.cpp/.h), when needed - Native code interfacing with external libraries
- **Compiled library** (.obl) - Precompiled Objeck bytecode
- **External dependencies** - Third-party libraries (mbedTLS, SDL2, etc.)

## Categories

### AI & Machine Learning
- **openai.obs** - OpenAI API integration (GPT-4, GPT-5, DALL-E, Whisper, Realtime API)
- **gemini.obs** - Google Gemini API integration with schema support
- **ollama.obs** - Ollama local LLM integration
- **opencv.obs** - OpenCV computer vision library
- **onnx.obs** - ONNX Runtime for cross-platform ML inference
- **ml_core.obs** - ML core: NeuralNetwork, Matrix2D, Random, MatrixReader
- **ml_linear.obs** - Linear models: LinearRegression, Ridge, Lasso, ElasticNet, LogisticRegression, Perceptron, SVM
- **ml_tree.obs** - Tree models: DecisionTree, RandomForest, AdaBoost, RegressionTree, GradientBoostedTrees
- **ml_bayes.obs** - Bayesian classifiers: NaiveBayes, GaussianNaiveBayes
- **ml_neighbors.obs** - Nearest neighbors: KNearestNeighbors, KDTree
- **ml_cluster.obs** - Clustering: KMeans, DBSCAN, GaussianMixture
- **ml_data.obs** - Data utilities: FeatureScaler, Metrics, CrossValidation, PCA
- **ai_search.obs** - Graph search: Dijkstra, A*, BFS, DFS
- **ai_game.obs** - Adversarial game search: Minimax, Monte Carlo Tree Search
- **ai_optimize.obs** - Optimization: GeneticAlgorithm, SimulatedAnnealing, HillClimbing
- **ai_rl.obs** - Reinforcement learning: QLearning, Sarsa, MDP value iteration
- **nlp.obs** - Natural language processing

### Web & Networking
- **net_secure.obs** - HTTPS client and secure WebSockets (TLS via mbedTLS)
- **net_common.obs** - Common networking utilities: URLs, cookies, downloads, server-sent events
- **net.obs** - HTTP/1.1 client (`HttpClient`) and WebSocket client (`Web.HTTP`)
- **net_h2.obs** - HTTP/2 client (`Http2Client`)
- **net_quic.obs** - HTTP/3 client over QUIC (`Http3Client`)
- **net_server.obs** - HTTP server framework: `WebServer` routes, query and multipart parsing, OAuth token exchange, and an MCP server (`Web.HTTP.Server`)
- **web_server.obs** - Lightweight embedded web server with typed requests and responses (`Web.Server`)
- **json_rpc.obs** - JSON-RPC server and client (`Data.JSON.RPC`)
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
- **cipher.obs** - AES-256 encryption/decryption, SHA/MD5/RIPEMD-160 hashing and Base64, via mbedTLS

### Utilities
- **regex.obs** - Regular expression support
- **concurrent.obs** - Structured concurrency (`TaskScope`, `Task`) and a runtime `Monitor` (`System.Concurrency`)
- **misc.obs** - Miscellaneous utilities
- **lang.obs** - Core language runtime support

### Gaming & Multimedia
- **sdl_game.obs** - 2D game framework (SDL2-based)
- **sdl_gl.obs** - 3D framework: OpenGL 3.3 core over SDL2 (`Game.OpenGL`)
- **sdl2.obs** - SDL2 bindings for graphics, audio, input
- **lame.obs** - MP3 audio encoding (LAME)

### Diagnostics
- **diags.obs** - Static analysis and language-server support (diagnostics, symbols, completions)

## Building Libraries

Libraries are built alongside the compiler. Native modules are compiled into shared libraries:

### Windows
```bash
# Libraries are built with Visual Studio projects
# See core/lib/<library>/vs/*.vcxproj
```

### Linux
```bash
# Each native module has a build script; core/release/deploy_posix.sh runs them all
cd core/lib/<library>
./build_linux.sh <library>  # ONNX: cd core/lib/onnx/eq && ./build.sh cpu
```

### macOS
`core/release/deploy_macos_arm64.sh` builds each native module from its Xcode
project under `core/lib/<library>/macos`; OpenCV and ONNX build with CMake from
`core/lib/opencv/macos/CMakeLists.txt`.

## Using Libraries

Only `lang` and `gen_collect` are linked by default; name any other library with `-lib` when compiling, e.g. `obc -src my_app.obs -lib net,net_server,json,cipher,misc,openai`:

```ruby
use Data.JSON;
use API.OpenAI.Responses;
use Collection;

class MyApp {
  function : Main(args : String[]) ~ Nil {
    # Use JSON
    json := JsonParser->TextToElement("{\"name\":\"Objeck\"}");

    # Use collections
    map := Map->New()<String, IntRef>;
    map->Insert("answer", 42);

    # Use OpenAI
    response := Response->Respond("gpt-4o", Pair->New("user", "Hello!")<String, String>, token);
  }
}
```

## Adding New Libraries

To add a new library:

1. Create Objeck source in `compiler/lib_src/<name>.obs`
2. Create native module in `lib/<name>/<name>.cpp` (if needed)
3. Add build configuration for the native module (a `build_linux.sh`, a VS project, the macOS project)
4. Add the library to `core/compiler/build_libs.sh`, which compiles each library's `.obl`, and to the doc-build lists that `tools/cicd/check_doc_lists.py` checks

## External Dependencies

Some libraries require external dependencies:
- **mbedTLS** - Used by net_secure (TLS, built into the VM) and cipher
- **SDL2** - Used by sdl_game, sdl_gl, sdl2
- **OpenGL** - Used by sdl_gl (3.3 core; a system framework on macOS,
  opengl32 on Windows, the driver's libGL on Linux)
- **OpenCV** - Used by opencv
- **ONNX Runtime** - Used by onnx
- **ODBC** - Platform ODBC drivers
- **LAME** - Used by lame for MP3 encoding

## See Also

- [Main README](../../README.md) - Project overview
- [Compiler](../compiler/README.md) - Building and linking libraries
- [API Documentation](https://www.objeck.org) - Complete library reference
- [Library Source](../compiler/lib_src/) - Objeck library source code