# Objeck REPL (obr)

Interactive Read-Eval-Print Loop for rapid prototyping and experimentation with Objeck. The REPL provides an interactive shell where you can write and execute Objeck code immediately without creating source files.

## Features

- **Interactive execution**: Write and run code in real-time
- **In-memory compilation**: Fast compilation with basic optimizations
- **Auto-completion**: Tab completion for classes and methods (where supported)
- **Multi-line support**: Write complex expressions across multiple lines
- **Import support**: Use standard libraries interactively
- **Reduced noise**: Suppresses warnings for unused variables
- **History**: Navigate previous commands with arrow keys

## Usage

```bash
# Start REPL
obr

# Or load and execute a program
obr myprogram.obe

# Run with additional libraries
obr -lib mylib.obl
```

## Interactive Session Example

```ruby
$ obr
Objeck REPL v2026.2.0

> "Hello, REPL!"->PrintLine();
Hello, REPL!

> x := 42;
> y := x * 2;
> y->PrintLine();
84

> use Collection;
> list := List->New()<IntRef>;
> list->AddBack(1); list->AddBack(2); list->AddBack(3);
> list->Size()->PrintLine();
3

> # Multi-line function
> function : Factorial(n : Int) ~ Int {
... if(n <= 1) { return 1; };
... return n * Factorial(n - 1);
... }
> Factorial(5)->PrintLine();
120

> # Use AI libraries
> use API.OpenAI;
> token := System.IO.Filesystem.FileReader->ReadFile("api_key.txt");
> response := Response->Respond("gpt-4o-mini", "What is 2+2?", token);
> response->GetText()->PrintLine();
The answer is 4.

> quit
```

## Commands

| Command | Description |
|---------|-------------|
| `help` | Show help information |
| `clear` | Clear screen |
| `vars` | Show defined variables |
| `quit` or `exit` | Exit REPL |

## Architecture

The REPL consists of:
- **Interactive Parser**: Parses statements and expressions interactively
- **In-Memory Document**: Maintains code context across statements
- **Incremental Compiler**: Compiles new code against existing context
- **VM Integration**: Directly executes compiled bytecode
- **State Management**: Preserves variable values between statements

### Compilation Strategy

The REPL uses a simplified compilation pipeline:
1. **Level 1 optimization**: Basic optimizations only for speed
2. **Warning suppression**: Reduces noise from experimental code
3. **Incremental linking**: Links new code with existing definitions
4. **JIT execution**: Executes code immediately via VM

## Use Cases

### Rapid Prototyping
```ruby
> # Test OpenCV face detection quickly
> use Computer.Vision;
> detector := FaceDetector->New("haarcascade_frontalface_default.xml");
> image := Image->New("photo.jpg");
> faces := detector->Detect(image);
> faces->Size()->PrintLine();
5
```

### API Exploration
```ruby
> # Experiment with Gemini API
> use API.Gemini;
> content := Content->New("user")->AddPart(TextPart->New("Hello!"));
> response := Model->GenerateContent("models/gemini-2.5-flash", content, Nil, key);
> response->First()->GetAllText()->PrintLine();
```

### Quick Calculations
```ruby
> # Complex number operations
> use Collection;
> (1..100)->Reduce(\(a,b) => a + b, 0)->PrintLine();
5050
```

### Learning & Teaching
The REPL is ideal for learning Objeck syntax and exploring libraries interactively.

## Implementation

- **Language**: C++ with STL
- **JIT Support**: Direct machine code generation via VM
- **Line Editing**: Platform-specific terminal support
- **Compilation**: Shared infrastructure with main compiler

## Limitations

- Some optimizations are disabled for compilation speed
- Complex multi-file programs should use regular compilation
- Debug symbols are not generated in REPL mode
- Some platform-specific features may have limited support

## See Also

- [Main README](../../README.md) - Project overview
- [Compiler](../compiler/README.md) - Full compilation options
- [Virtual Machine](../vm/README.md) - VM architecture
- [API Documentation](https://www.objeck.org) - Library reference