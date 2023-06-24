<p align="center">
  <a href="https://www.objeck.org"><img src="docs/images/gear_wheel_256.png"  width="192" height="192" alt="An Objeck"/></a>
</p>

<p align="center">
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/c-cpp.yml/badge.svg" alt="C/C++ CI"></a>
  <a href="https://discord.gg/qEaCGWR7nb"><img src="https://badgen.net/badge/icon/discord?icon=discord&label" alt="Discord"></a>
  <a href="https://scan.coverity.com/projects/objeck"><img src="https://img.shields.io/coverity/scan/10314.svg" alt="Coverity Scan Build Status"></a>
</p>

<h1 align="center">Intuitive, Fast & Efficient</h1>

```ruby
class Hello {
   function : Main(args : String[]) ~ Nil {
      hiya := Collection.Vector->New()<String>
      hiya->AddBack("Hello World")
      hiya->AddBack("Καλημέρα κόσμε")
      hiya->AddBack("こんにちは 世界")
      hiya->Each(\^(h) => h->PrintLine())
   }
}
```

More Rosetta Code [examples](https://github.com/objeck/objeck-lang/tree/master/programs/tests/rc) and [IDE](https://github.com/objeck/objeck-lsp) support.

## Key Features
* Object-oriented
  * [Inheritance](https://en.wikipedia.org/wiki/Inheritance_(object-oriented_programming))
  * [Interfaces](https://en.wikipedia.org/wiki/Interface_(object-oriented_programming))
  * [Type Inference](https://en.wikipedia.org/wiki/Type_inference)
  * [Reflection](https://en.wikipedia.org/wiki/Reflective_programming)
  * [Dependency injection](https://en.wikipedia.org/wiki/Dependency_injection)
  * [Generics](https://en.wikipedia.org/wiki/Generic_programming)
  * [Type boxing](https://en.wikipedia.org/wiki/Boxing_(computer_science))
  * [Serialization](https://en.wikipedia.org/wiki/Serialization)
* Functional 
  * [Closures](https://en.wikipedia.org/wiki/Closure_(computer_programming))
  * [Lambda expressions](https://en.wikipedia.org/wiki/Anonymous_function)
  * [First-class functions](https://en.wikipedia.org/wiki/First-class_function)
* [Unicode](https://en.wikipedia.org/wiki/Unicode)
* OS support
  * [File systems](https://en.wikipedia.org/wiki/File_system)
  * [Sockets](https://en.wikipedia.org/wiki/Network_socket)
  * [Named pipes](https://en.wikipedia.org/wiki/Named_pipe)
  * [Threads](https://en.wikipedia.org/wiki/Thread_(computing))
  * [Date/times](https://en.wikipedia.org/wiki/C_date_and_time_functions)
  * [Extension libraries](https://en.wikipedia.org/wiki/Library_(computing))
* [Garbage collection](https://en.wikipedia.org/wiki/Tracing_garbage_collection)
* [JIT compilation](https://en.wikipedia.org/wiki/Just-in-time_compilation)
  * [arm64](https://github.com/objeck/objeck-lang/tree/master/core/vm/arch/jit/arm64): Linux (Raspberry Pi 4), macOS (Apple silicon)
  * [x86-64](https://github.com/objeck/objeck-lang/tree/master/core/vm/arch/jit/amd64): Windows (10/11), Linux and macOS
* [LSP support](https://github.com/objeck/objeck-lsp)
* Documentation
  * [Tutorial](https://www.objeck.org/getting_started.html)
  * [APIs](https://www.objeck.org/doc/api/index.html)

## Screenshots
| <sub>[VS Code](https://github.com/objeck/objeck-lsp)</sub> | <sub>[Debugger](https://github.com/objeck/objeck-lang/tree/master/core/debugger)</sub> | <sub>[Dungeon Crawler](https://github.com/objeck/objeck-dungeon-crawler)</sub> | <sub>[Platformer](https://github.com/objeck/objeck-lang/blob/master/programs/deploy/2d_game_13.obs)</sub> | <sub>[Windows Utility](https://github.com/objeck/objeck-lang/tree/master/core/release/WindowsLauncher)</sub> |
| :---: | :----: | :---: | :---: | :---: |
![alt text](docs/images/web/comp.png "Visual Studio Code") | ![alt text](docs/images/web/debug.jpg "Command line debugger") | ![alt text](docs/images/web/crawler.png "Web Crawler") | ![alt text](docs/images/web/2d_game.jpg "Platformer") | ![alt text](docs/images/web/launch.png "Windows Launcher") |

## Libraries
  * [HTTPS](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net_secure.obs) and [HTTP](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/net.obs) server and client APIs
  * [JSON](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/json.obs), [XML](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/xml.obs) and [CSV](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/csv.obs) parsers
  * [Regular expression](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/regex.obs) library
  * Encryption and hashing
  * In memory [query framework](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/query.obs) with SQL-like syntax
  * Database access
  * [2D Gaming framework](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/sdl_game.obs) via SDL2
  * [Collections](https://github.com/objeck/objeck-lang/blob/master/core/compiler/lib_src/gen_collect.obs) (caches, vectors, queues, trees, hashes, etc.)
  * GTK windowing support [(work-in-progress)](core/lib/experimental/gtk)
  
