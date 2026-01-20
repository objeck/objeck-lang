# Objeck Documentation

Comprehensive documentation, guides, and resources for Objeck development.

## Contents

### Language Documentation
- **[FEATURES.md](FEATURES.md)** - Detailed language features with examples (OOP, Functional, Platform)
- **[EXAMPLES.md](EXAMPLES.md)** - Practical code examples and tutorials
- **[API Reference](https://www.objeck.org/doc/api/index.html)** - Complete API documentation

### Getting Started
- **[README.md](../README.md)** - Main project README with quick start
- **[CHANGELOG.md](../CHANGELOG.md)** - Version history and release notes
- **Programmer's Guide** - Available at [objeck.org](https://www.objeck.org)

### Editor Integration

Syntax highlighting and IDE support for multiple editors:

| Editor | Location | Features |
|--------|----------|----------|
| **VSCode** | [syntax/vscode](syntax/vscode) | Syntax highlighting, snippets |
| **Sublime Text** | [syntax/sublime](syntax/sublime) | Syntax highlighting |
| **Kate** | [syntax/kate](syntax/kate) | Syntax highlighting |
| **Geany** | [syntax/geany](syntax/geany) | Syntax highlighting |
| **jEdit** | [syntax/jedit](syntax/jedit) | Syntax highlighting |
| **Lite-XL** | [syntax/lite-xl](syntax/lite-xl) | Syntax highlighting |
| **TextAdept** | [syntax/textadept](syntax/textadept) | Syntax highlighting |

For LSP support (autocomplete, go-to-definition, etc.), see [objeck-lsp](https://github.com/objeck/objeck-lsp).

### Architecture References

The [arch](arch) directory contains CPU architecture documentation and opcode references used for implementing the JIT compilers:

- **x64 (AMD64)** - Intel/AMD 64-bit architecture
- **ARM64 (AArch64)** - ARM 64-bit architecture
- **Instruction Sets** - Assembly language references
- **Calling Conventions** - Platform-specific ABI documentation

These references were essential for building the JIT compilers in the Objeck VM.

## Online Resources

- **Website**: [objeck.org](https://www.objeck.org)
- **API Docs**: [objeck.org/doc/api](https://www.objeck.org/doc/api/index.html)
- **Getting Started**: [objeck.org/getting_started](https://www.objeck.org/getting_started.html)
- **GitHub**: [github.com/objeck/objeck-lang](https://github.com/objeck/objeck-lang)
- **Discussions**: [github.com/objeck/objeck-lang/discussions](https://github.com/objeck/objeck-lang/discussions)

## Images & Diagrams

The [images](images) directory contains architectural diagrams and visual documentation:
- Compiler pipeline diagrams
- VM architecture diagrams
- Memory management visualizations
- System design flowcharts

## Contributing Documentation

When adding or updating documentation:
1. Use clear, concise language
2. Include code examples where applicable
3. Update table of contents if adding new sections
4. Follow existing formatting conventions
5. Test all code examples before committing

## Documentation Format

- **Markdown** (.md) for text documentation
- **SVG** for diagrams (preferred for scalability)
- **PNG/JPG** for screenshots and images

## See Also

- [Main README](../README.md) - Project overview
- [Compiler README](../core/compiler/README.md) - Compiler documentation
- [VM README](../core/vm/README.md) - Virtual Machine documentation
- [Programs README](../programs/README.md) - Example programs
