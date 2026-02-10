# Images & Diagrams

Various images including architecture diagrams, benchmarks, icons, and screenshots.

## Architecture Diagrams

### Modern Documentation (Recommended)
**📖 [../architecture.md](../architecture.md)** - Comprehensive Mermaid-based architecture documentation
- Interactive diagrams that render on GitHub
- Version-controlled and easy to update
- Detailed technical views of all subsystems
- 10+ detailed diagrams covering complete architecture

### Legacy Diagrams (SVG/PNG)

Historical architecture diagrams (still useful for visual reference):

| File | Description | Format | Status |
|------|-------------|--------|--------|
| **design4.svg/.png** | Main system architecture | Visio SVG | Legacy (use architecture.md instead) |
| **design3.svg/.png** | Runtime components focus | Visio SVG | Legacy |
| **design2.svg/.png** | Compiler pipeline focus | Visio SVG | Legacy |
| **design.png** | Original architecture (v1) | PNG | Historical |
| **jit_design.svg/.png** | JIT frame structure detail | Visio SVG | Still relevant |
| **toolchain.svg** | Build & distribution | SVG | Still relevant |
| **compiling2.svg/.png** | Compilation flow | Visio SVG | Legacy |

**Note:** The `.svg` files are editable sources created in Microsoft Visio. They can be edited with Inkscape or any SVG editor.

## Other Images

### Development Tools
- **debugger.png** - Debugger UI screenshot (1MB)
- **guide.png** - Development guide
- **the_lab.png** - IDE screenshot (1MB)
- **vcr.png** - Visual Component Recorder (1MB)

### Performance & Benchmarks
- **fannkuch-redux-web.png** - Benchmark comparison
- **primes.png** - Prime number benchmark
- **tiny_m.png** - TinyM language comparison

### System Internals
- **exe_stack.png** - Execution stack structure
- **x86_frame.png** - x86 stack frame layout
- **mem_mgr_usage.png** - Memory manager usage
- **reg_alloc.png** - Register allocation diagram

### Build & Setup
- **woa_installer.png** - Windows on ARM installer screenshot
- **vs-nuget-pckgs.png** - Visual Studio NuGet packages
- **inline_doc_mockup.png** - Documentation UI mockup

### Branding
- **gear_types.png** - Logo variations
- **gear_wheel_256.png** - Logo 256x256
- **Gear.ico** - Windows icon
- **Gear.icns** - macOS icon
- **modern_objeck.jpg** - Modern branding
- **xml_code.png** - Code example

### Web Assets
- **web/** - Website assets directory

## Diagram Maintenance

**For new diagrams:**
- Use Mermaid format in Markdown files (preferred)
- Commit to version control (text-based, reviewable)
- Automatically renders on GitHub

**For updating legacy diagrams:**
- Edit .svg files with Inkscape (free) or Visio
- Export to both .svg and .png
- Commit both formats

**Recommendation:** Create new diagrams in [architecture.md](../architecture.md) using Mermaid rather than adding more SVG/PNG files. This keeps documentation maintainable and version-controllable.
