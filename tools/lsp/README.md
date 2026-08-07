<p align="center">
<strong>Objeck LSP</strong><br>
Language Server Protocol support for <a href="https://github.com/objeck/objeck-lang">Objeck</a><br>
Code intelligence for 7 editors across Windows, Linux, and macOS
</p>

<hr/>

<p align="center">
  <a href="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml"><img src="https://github.com/objeck/objeck-lang/actions/workflows/release-build.yml/badge.svg" alt="Release Build"></a>
  <a href="https://github.com/objeck/objeck-lang/releases"><img src="https://img.shields.io/github/v/release/objeck/objeck-lang?sort=date" alt="Latest Release"></a>
</p>

The Objeck LSP server brings code intelligence to the [Objeck](https://github.com/objeck/objeck-lang) programming language &mdash; diagnostics, completion, hover docs, go-to-definition, rename, formatting, and more. It runs on **Windows**, **Linux**, and **macOS** (AMD64 and ARM64).

## Quick Start

**1. Install Objeck** from [github.com/objeck/objeck-lang](https://github.com/objeck/objeck-lang/releases/latest)

On **Linux and macOS**, the Objeck runtime dynamically links a few system libraries (mbedTLS for crypto/TLS; nghttp2, ngtcp2, and nghttp3 for HTTP/2 and HTTP/3). Install them first — the install script checks for them and stops with the same hint if they're missing (Windows releases bundle their DLLs):
```sh
# Debian / Ubuntu
sudo apt-get install libmbedtls-dev libnghttp2-dev libngtcp2-dev \
    libngtcp2-crypto-gnutls-dev libnghttp3-dev libgnutls28-dev

# macOS
brew install mbedtls nghttp2 ngtcp2 nghttp3 gnutls
```

**2. Run the install script** from the extracted release directory:
```sh
# Windows - user install (no admin required)
scripts\install.cmd C:\Users\you\objeck vscode

# Windows - system-wide install
scripts\install.cmd "C:\Program Files\Objeck" vscode

# Linux / macOS - user install
./scripts/install.sh ~/objeck vscode

# Linux / macOS - system-wide install
./scripts/install.sh /usr/local/objeck vscode
```
This creates a self-contained deployment at `~/.objeck-lsp/` and configures your editor. Replace `vscode` with `sublime`, `neovim`, `emacs`, or `all`. The first argument is wherever you installed Objeck.

**3. Or configure manually** &mdash; pick your editor below, then see the [Install Guide](docs/install_guide.html) for step-by-step instructions. Set environment variables (required for STDIO transport):
```sh
export OBJECK_LIB_PATH=<objeck_install_dir>/lib
export OBJECK_STDIO=binary
```

**4. Create a workspace** &mdash; add a `build.json` to your project root for multi-file projects:
```json
{
  "files": ["main.obs", "helper.obs"],
  "libs": ["gen_collect.obl", "net.obl", "json.obl"],
  "flags": ""
}
```

Open the folder in your editor and the LSP server handles the rest.

**5. To debug** &mdash; compile with debug symbols first, or no breakpoint will bind:
```sh
obc -src myprog.obs --debug -dest myprog.obe
```
Then add a launch configuration. In **VS Code** the extension contributes an `objeck` debug type,
so `Run > Add Configuration...` writes a `launch.json` entry for you. In **Sublime** put a
`debugger_configurations` block in a `.sublime-project`, or a `global_debugger_configurations`
block in `Packages/User/Debugger.sublime-settings` to debug without a project at all &mdash; see
[`clients/sublime/README.md`](clients/sublime/README.md) for both routes and their example files.
The `.obe` is what runs, so recompile after every edit.

## Supported Editors

| Editor | Transport | Setup |
|--------|-----------|-------|
| **VS Code** | Named pipe | Install the [`.vsix` extension](https://github.com/objeck/objeck-lang/releases/latest), set install path in settings |
| **Sublime Text** | STDIO | Add config from [`clients/sublime/`](clients/sublime/) to LSP settings |
| **Kate** | STDIO | Add server entry in LSP Client settings ([instructions](README.txt)) |
| **ecode** | STDIO | Add server to [`lspclient.json`](README.txt) |
| **Neovim** (0.11+) | STDIO | Copy [`objeck.lua`](clients/neovim/) + [`objeck.vim`](clients/neovim/) to nvim config |
| **Emacs** (29+) | STDIO | Copy [`objeck-mode.el`](clients/emacs/) to your load-path (includes syntax highlighting) |
| **Helix** | STDIO | Merge [`clients/helix/languages.toml`](clients/helix/) into your config |

Install scripts (`scripts/install.cmd` and `scripts/install.sh`) automate setup for VS Code, Sublime, Neovim, and Emacs. Use `scripts/update_lsp` to refresh the runtime after rebuilding Objeck.

## Features

- **Diagnostics** &mdash; Real-time error and warning reporting
- **Code Completion** &mdash; Variables, methods, and functions with trigger characters (`@`, `.`, `>`)
- **Signature Help** &mdash; Method/function parameter hints
- **Hover** &mdash; Bundle documentation on hover
- **Go to Definition / Declaration** &mdash; Navigate to variables, classes, and methods
- **Find References** &mdash; Locate all usages of a symbol
- **Rename** &mdash; Project-wide variable and method renaming
- **Document & Workspace Symbols** &mdash; Outline and cross-file search
- **Code Actions** &mdash; Quick fixes (add `use` statements, qualify references)
- **Formatting** &mdash; Document and range formatting
- **Multi-root Workspaces** &mdash; JSON-configured project support via `build.json`

## Debugging Features

The debug adapter (`obd --dap`) is shared by every editor that speaks DAP.

- **Structured variable inspection** &mdash; objects expand into their instance fields, arrays into indexed elements, and `Vector`, `Map` and `Hash` into their contents. A `Map` shows `key → value` in key order rather than an address, and each child expands recursively
- **Readable values** &mdash; strings show their text, boxed scalars (`IntRef`, `FloatRef`, …) show their value, and collections show `Vector(size=3)`. These are deliberately terminal: drilling into them would only expose backing storage
- **Watch and hover** &mdash; expressions resolving to an object or collection expand exactly like the Variables pane
- **Variable paging** &mdash; large arrays and collections report `indexedVariables` and honour `start`/`count`, so a client can page instead of pulling everything
- **Breakpoint validation** &mdash; `breakpointLocations` reports only lines that carry an instruction, so editors stop offering breakpoints that cannot bind
- **Data breakpoints** &mdash; break when a value *changes* rather than when a line is reached, which is how you find the one write among many that corrupts a field. Right-click a variable and choose "Break on Value Change"; the stop names the watch and reports the old &rarr; new transition. Write/change only: the watch compares values after each instruction, so a read that leaves the value alone cannot be seen
- **Debug console completion** &mdash; `completions` suggests the variables visible in the selected frame
- **Also supported** &mdash; conditional breakpoints, function breakpoints, logpoints, exception breakpoints with `exceptionInfo`, `setVariable`/`setExpression`, `restart`, `terminate`, `modules` and `loadedSources`

Not supported: stepping backwards, and `goto`/`restartFrame` &mdash; the last two would need the VM's interpreter loop to expose its program counter, which it currently passes to the debugger by value.

## Architecture

```mermaid
graph LR
    subgraph Editors
        VSCode[VS Code]
        Sublime[Sublime Text]
        Kate[Kate]
        ecode[ecode]
        Neovim[Neovim]
        Emacs[Emacs]
        Helix[Helix]
    end

    subgraph Transport
        STDIO[STDIO]
        Pipe[Named Pipe]
        Socket[TCP Socket]
    end

    subgraph Runtime["Objeck Runtime"]
        Workspace[In-Memory Workspace]
        Compiler[Analysis Compiler]
        Detection[Issue Detection]
        Navigation[Code Navigation & Refactoring]
        Docs[API Documentation]

        Workspace --> Compiler
        Compiler --> Detection
        Compiler --> Navigation
        Compiler --> Docs
    end

    VSCode --> Pipe
    Sublime --> STDIO
    Kate --> STDIO
    ecode --> STDIO
    Neovim --> STDIO
    Emacs --> STDIO
    Helix --> STDIO

    STDIO --> Workspace
    Pipe --> Workspace
    Socket --> Workspace
```

<details>
<summary><strong>LSP Protocol Coverage</strong></summary>

### Notifications

| Event | Method |
|-------|--------|
| Initialized | `initialized` |
| Cancel Request | `$/cancelRequest` |
| File Open | `textDocument/didOpen` |
| File Changed | `textDocument/didChange` |
| File Save | `textDocument/didSave` |
| File Close | `textDocument/didClose` |
| Exit | `exit` |

### Requests

| Feature | Method |
|---------|--------|
| Initialize | `initialize` |
| Shutdown | `shutdown` |
| Completion | `textDocument/completion` |
| Document Symbol | `textDocument/documentSymbol` |
| Workspace Symbol | `workspace/symbol` |
| Signature Help | `textDocument/signatureHelp` |
| References | `textDocument/references` |
| Definition | `textDocument/definition` |
| Declaration | `textDocument/declaration` |
| Rename | `textDocument/rename` |
| Hover | `textDocument/hover` |
| Code Action | `textDocument/codeAction` |
| Format Document | `textDocument/formatting` |
| Format Selection | `textDocument/rangeFormatting` |

### Workspace

| Feature | Method |
|---------|--------|
| Watch File Changes | `workspace/didChangeWatchedFiles` |
| Workspace Folder Changes | `workspace/didChangeWorkspaceFolders` |
| Find Symbol | `workspace/symbol` |

</details>

## Development

**Building the VS Code extension:**
```sh
npm install -g yo generator-code typescript @vscode/vsce
cd clients/vscode && npm run compile
```

**Building the LSP server** (requires [Objeck](https://github.com/objeck/objeck-lang)):
```sh
cd server
obc -src frameworks.obs,proxy.obs,server.obs,format_code/scanner.obs,format_code/formatter.obs \
    -lib diags,net,json,regex,cipher -dest objeck_lsp.obe
```

## Resources

- [Install Guide](docs/install_guide.html) &mdash; detailed setup for all editors
- [README.txt](README.txt) &mdash; quick-reference setup instructions
- [Objeck Language](https://github.com/objeck/objeck-lang) &mdash; compiler, runtime, and documentation
- [Issues](https://github.com/objeck/objeck-lang/issues) &mdash; bug reports and feature requests
