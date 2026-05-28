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

## Transport

VS Code uses a **named pipe** connection on all platforms, avoiding the need for `OBJECK_STDIO`.

## Debugging (DAP)

The VS Code extension also supports the Debug Adapter Protocol. Set breakpoints, step through code, and inspect variables directly in the editor. See [docs/editors.md](../../../../docs/editors.md) for setup.
