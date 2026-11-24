# Objeck LSP — VS Code

VS Code extension for Objeck: syntax highlighting, diagnostics, completion, hover, go-to-definition, rename, formatting, and more.

## Install

**Automated** (from the extracted LSP release zip):
```cmd
# Windows
scripts\install.cmd C:\Users\you\objeck vscode

# Linux / macOS
./scripts/install.sh ~/objeck vscode
```

**Manual:**
1. Download the `.vsix` from [releases](https://github.com/objeck/objeck-lang/releases/latest)
2. In VS Code: Extensions (`Ctrl+Shift+X`) → drag-and-drop the `.vsix`
3. Open VS Code Settings and set `objk.win.install.dir` (Windows) or `objk.posix.install.dir` (Linux/macOS) to your Objeck install root

## Workspaces

For multi-file projects, add a `build.json` to the project root — see [`build.json.example`](../../build.json.example):

```json
{
  "files": ["main.obs", "helper.obs"],
  "libs": ["gen_collect.obl", "net.obl", "json.obl"],
  "flags": ""
}
```

Open the folder (not just the file) in VS Code to activate workspace mode.

## Inline completion (ghost text), off by default

`objeck.inlineCompletion.provider` is `none` unless you change it, and then **no code is sent anywhere**. Set it to `ollama` to get suggestions from a local [Ollama](https://ollama.com) server (`objeck.inlineCompletion.ollamaHost`, default `http://localhost:11434`; a host that is not your machine receives the code around your cursor). `anthropic`, `openai` and `gemini` send that text to the company's API and need a key already configured the way the Objeck library reads it (`ANTHROPIC_API_KEY` or `anthropic_api_key.dat`, `openai_api_key.dat`, the Gemini key file). `objeck.inlineCompletion.model` overrides the provider's default model and `objeck.inlineCompletion.timeoutMs` (3000) bounds the wait; a slow or failing provider simply produces no suggestion.

## Transport

Each VS Code window starts its own server over **STDIO**, launched and stopped by the language client. The extension runs `obr` from the configured install directory directly (no shell), with `OBJECK_STDIO=binary` set. It used to share one fixed named pipe between windows, so a second window reached the first window's server.

## Debugging (DAP)

The VS Code extension also supports the Debug Adapter Protocol. Set breakpoints, step through code, and inspect variables directly in the editor. See [docs/editors.md](../../../../docs/editors.md) for setup.
