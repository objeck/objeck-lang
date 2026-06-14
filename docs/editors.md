# Editor & IDE Support

Objeck has syntax highlighting, build integration, LSP support, and DAP debugging across multiple editors.

---

## VS Code (Recommended)

Full IDE experience with syntax highlighting, LSP autocomplete, and DAP debugging. Tested on Windows, Linux, and macOS.

### 1. Install the Extension

Download the latest `.vsix` from the [objeck-lsp releases](https://github.com/objeck/objeck-lsp/releases), then (same command on all three platforms):

```
code --install-extension objeck-lsp-*.vsix
```

Or in VS Code: **Extensions** (Ctrl+Shift+X on Windows/Linux, Cmd+Shift+X on macOS) > **...** menu > **Install from VSIX...**

### 2. Configure Install Path

Open VS Code Settings (Ctrl+, / Cmd+,) and set the install directory that matches your platform:

| Platform | Setting | Example |
|----------|---------|---------|
| **Windows** | `objk.win.install.dir` | `C:\Program Files\Objeck` |
| **Linux** | `objk.posix.install.dir` | `/usr/local/objeck-lang` |
| **macOS** | `objk.posix.install.dir` | `/usr/local/objeck-lang` or `/opt/homebrew/Cellar/objeck/<version>` |

### 3. Debugging with DAP

Compile with debug symbols:

```bash
obc -src myprogram.obs -debug
```

Create `.vscode/launch.json` in your workspace:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "objeck",
      "request": "launch",
      "name": "Debug Objeck Program",
      "program": "${workspaceFolder}/myprogram.obe",
      "sourceDir": "${workspaceFolder}"
    }
  ]
}
```

Press **F5** to start debugging. Features:

- Breakpoints (click gutter or F9)
- Conditional breakpoints (right-click breakpoint > Edit Condition, e.g. `i > 3`)
- Function breakpoints (Breakpoints panel > **+**, e.g. `Main->Factorial`, `Main.Factorial`, or a bare `Factorial`)
- Logpoints (right-click gutter > Add Logpoint — log a `{expr}`-interpolated message and keep running instead of stopping)
- Exception breakpoints (Breakpoints panel > **Uncaught Runtime Errors** — stop on an uncaught Objeck runtime error such as a `Nil` dereference)
- Step Over (F10), Step Into (F11), Step Out (Shift+F11)
- Variable inspection in the **Variables** panel; edit an `Int`/`Char`/`Float` local in place (double-click the value)
- Call stack in the **Call Stack** panel
- Restart (Ctrl+Shift+F5) re-runs the program in-process without relaunching the adapter
- Expression evaluation in the **Debug Console**
- Program output (`PrintLine`, `Print`, stderr) appears in the **Debug Console** as DAP `output` events — `obd --dap` redirects the running program's stdout/stderr through capture pipes so the protocol stream stays clean.

### 4. LSP Features

The extension provides:

- Syntax highlighting
- Code completion
- Go to definition
- Hover information
- Diagnostics (compile errors)

---

## Sublime Text

Tested on Windows, Linux, and macOS with Sublime Text 4.

### Automated Setup (Recommended)

Download the [objeck-lsp release](https://github.com/objeck/objeck-lsp/releases) and run the install script for your platform:

```cmd
:: Windows (Command Prompt)
scripts\install.cmd "C:\Program Files\Objeck" sublime
```

```bash
# Linux
./scripts/install.sh /usr/local/objeck sublime

# macOS (Intel or Apple Silicon)
./scripts/install.sh /usr/local/objeck sublime
```

This installs the LSP server, syntax highlighting, the DAP adapter plugin, and configures Sublime automatically. Requires the [LSP](https://packagecontrol.io/packages/LSP) package (installed automatically on Linux/macOS if git is available).

After install: **Tools > LSP > Enable Language Server Globally > select "objeck"**.

### Debugging with DAP

The install script drops `objeck_dap_adapter.py` and a `.python-version` file (containing `3.8`) into `Packages/Objeck/`, and writes `Packages/User/Objeck.sublime-settings` with the path to `obd`. The `.python-version` is required: the Sublime [Debugger](https://packagecontrol.io/packages/Debugger) package targets Python 3.8 and Sublime Text 4 defaults to Python 3.3 for plugins, so without it the adapter fails to load. See [API Environments](https://www.sublimetext.com/docs/api_environments.html).

To enable debugging:

1. Install the [Debugger](https://packagecontrol.io/packages/Debugger) package via Package Control.
2. Restart Sublime so the adapter is registered with Debugger's adapter registry.
3. Compile your source with debug symbols: `obc -src myprog.obs -debug`.
4. Add a `debugger_configurations` block to your `.sublime-project`:

```json
{
    "folders": [{ "path": "." }],
    "debugger_configurations": [{
        "name": "Debug current Objeck file",
        "type": "objeck",
        "request": "launch",
        "program": "${folder}/${file_base_name}.obe",
        "sourceDir": "${folder}"
    }]
}
```

5. Open the project, then **Debugger > Start** (or via the command palette: "Debugger: Start"). Set breakpoints by clicking the gutter; step with the Debugger panel buttons; inspect locals in the **Variables** view.

The adapter spawns `obd --dap` over stdio with `OBJECK_LIB_PATH` set per `Objeck.sublime-settings`. The full example lives at `clients/sublime/dap/objeck.sublime-project.example` in the objeck-lsp release.

### Manual Setup

Copy syntax files from `docs/syntax/sublime/` to your Sublime packages directory:

| Platform | Path |
|----------|------|
| **Windows** | `%APPDATA%\Sublime Text\Packages\Objeck\` |
| **macOS** | `~/Library/Application Support/Sublime Text/Packages/Objeck/` |
| **Linux** | `~/.config/sublime-text/Packages/Objeck/` |

Files:
- `objeck.sublime-syntax` — syntax definitions
- `objeck.tmPreferences` — indentation rules
- `objeck.sublime-build` — build system (edit paths to match your install)

For LSP, add to **Preferences > Package Settings > LSP > Settings**:

```json
{
  "clients": {
    "objeck": {
      "enabled": true,
      "command": ["~/.objeck-lsp/bin/obr", "~/.objeck-lsp/objeck_lsp.obe", "~/.objeck-lsp/objk_apis.json", "stdio"],
      "env": {
        "OBJECK_LIB_PATH": "~/.objeck-lsp/lib",
        "OBJECK_STDIO": "binary"
      },
      "selector": "source.objeck-obs"
    }
  }
}
```

---

## Vim / GVim (9.0+)

Full IDE setup with syntax highlighting, LSP (autocomplete / go-to-def / hover / diagnostics) via [yegappan/lsp](https://github.com/yegappan/lsp), and DAP debugging via [vimspector](https://github.com/puremourning/vimspector). Requires Vim 9.0+ with `+python3` (the gvim "Huge" build on Windows, `vim-gtk3` / `vim-gnome` on Linux, `brew install macvim` on macOS).

Tested on Windows (gvim "Huge" build), Linux, and macOS (MacVim).

### Automated Setup (Recommended)

Download the [objeck-lsp release](https://github.com/objeck/objeck-lsp/releases) and run the installer for your platform:

```cmd
:: Windows (Command Prompt)
scripts\install.cmd "C:\Program Files\Objeck" vim
```

```bash
# Linux
./scripts/install.sh /usr/local/objeck vim

# macOS
./scripts/install.sh /usr/local/objeck vim
```

The script:

- Installs syntax + ftdetect into the Vim runtime dir — `%USERPROFILE%\vimfiles\` on Windows, `~/.vim/` on Linux/macOS
- Clones `yegappan/lsp` and `puremourning/vimspector` into `<vim-runtime>/pack/objeck/start/`
- Drops `objeck.vim` into the auto-loaded `plugin/` dir with the LSP server registration and keybindings

After install, open any `.obs` file in gvim / vim / MacVim — the LSP server starts automatically and these keybindings are active in Objeck buffers:

| Key          | Action                |
|--------------|-----------------------|
| `gd`         | Go to definition      |
| `gr`         | Show references       |
| `K`          | Hover documentation   |
| `<leader>rn` | Rename symbol         |

### Debugging with DAP

1. **Compile with debug symbols**:

   ```bash
   obc -src myprog.obs -debug
   ```

2. **Drop a `.vimspector.json` next to your sources**. Vimspector walks up from the file being debugged looking for `.vimspector.json` — place it at your project root (not `~/`). It must contain **both** an `adapters` block (pointing at `obd`) and a `configurations` block:

   **Windows** — note the doubled backslashes in JSON:

   ```json
   {
       "adapters": {
           "objeck": {
               "name": "objeck",
               "command": [
                   "C:\\Program Files\\Objeck\\bin\\obd.exe",
                   "--dap"
               ],
               "env": {
                   "OBJECK_LIB_PATH": "C:\\Program Files\\Objeck\\lib"
               }
           }
       },
       "configurations": {
           "Debug current Objeck file": {
               "adapter": "objeck",
               "configuration": {
                   "request": "launch",
                   "type": "objeck",
                   "program": "${workspaceRoot}/${fileBasenameNoExtension}.obe",
                   "sourceDir": "${workspaceRoot}"
               }
           }
       }
   }
   ```

   **Linux / macOS**:

   ```json
   {
       "adapters": {
           "objeck": {
               "name": "objeck",
               "command": [
                   "/usr/local/objeck/bin/obd",
                   "--dap"
               ],
               "env": {
                   "OBJECK_LIB_PATH": "/usr/local/objeck/lib"
               }
           }
       },
       "configurations": {
           "Debug current Objeck file": {
               "adapter": "objeck",
               "configuration": {
                   "request": "launch",
                   "type": "objeck",
                   "program": "${workspaceRoot}/${fileBasenameNoExtension}.obe",
                   "sourceDir": "${workspaceRoot}"
               }
           }
       }
   }
   ```

   Adjust `command` and `OBJECK_LIB_PATH` to match your install prefix. On Homebrew macOS, that's typically `/opt/homebrew/Cellar/objeck/<version>/bin/obd` and `/opt/homebrew/Cellar/objeck/<version>/lib`.

3. **Open the `.obs` file in gvim**, press `<F9>` on a line to set a breakpoint, then `<F5>`. Vimspector opens its tab layout (code / variables / watches / stack / output / console) and starts execution.

Vimspector "HUMAN" key bindings:

| Key        | Action                |
|------------|-----------------------|
| `<F5>`     | Continue / start      |
| `<F9>`     | Toggle breakpoint     |
| `<F10>`    | Step over             |
| `<F11>`    | Step into             |
| `<F12>`    | Step out              |
| `<S-F5>`   | Stop debugging        |
| `<leader>di` | Inspect variable under cursor (hover) |

In the Variables window, press `<CR>` on `Scope: Locals`, `Scope: Instance`, or `Scope: Class` to expand. Hovering a local object with `<leader>di` shows `ClassName { field=val, ... }` (one-level field expansion).

Program output (`PrintLine`, `Print`, stderr) appears in the vimspector output window — `obd --dap` redirects the running program's stdout/stderr through capture pipes so the protocol stream stays clean.

> **Troubleshooting** — if `<F5>` reports *"The specified adapter 'objeck' is not available. Did you forget to run 'Vimspector Install'?"*, your `.vimspector.json` is missing the `adapters` block shown above. Vimspector only reads adapter definitions from project-local `.vimspector.json` files (or `g:vimspector_adapters`); a `~/.vimspector.json` in your home directory will be ignored unless the file being debugged lives directly under `~/` with no closer project file.

### Manual Setup (syntax only, no LSP/DAP)

Copy from `docs/syntax/vim/`:

```bash
# Linux / macOS
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp docs/syntax/vim/objeck.vim ~/.vim/syntax/
cp docs/syntax/vim/ftdetect/objeck.vim ~/.vim/ftdetect/
```

```cmd
:: Windows (gvim)
mkdir "%USERPROFILE%\vimfiles\syntax" "%USERPROFILE%\vimfiles\ftdetect"
copy docs\syntax\vim\objeck.vim "%USERPROFILE%\vimfiles\syntax\"
copy docs\syntax\vim\ftdetect\objeck.vim "%USERPROFILE%\vimfiles\ftdetect\"
```

For build-from-vim without LSP, add to `.vimrc` / `_vimrc`:

```vim
autocmd FileType objeck setlocal makeprg=obc\ -src\ %\ -dest\ %:r.obe
autocmd FileType objeck setlocal errorformat=%f:(%l\\,%c):\ %m
```

Use `:make` to compile and `:copen` to see errors. `obc` must be on your `PATH`.

---

## Neovim (0.11+)

### Automated Setup (Recommended)

Download the [objeck-lsp release](https://github.com/objeck/objeck-lsp/releases) and run:

```bash
# Linux / macOS
./scripts/install.sh /usr/local/objeck neovim
```

This installs syntax highlighting, ftdetect, and the LSP client config to your Neovim config directory automatically.

### Manual Setup

Copy from the [objeck-lsp](https://github.com/objeck/objeck-lsp) repo's `clients/neovim/`:

```bash
cp clients/neovim/objeck.lua ~/.config/nvim/lsp/
cp clients/neovim/objeck.vim ~/.config/nvim/ftdetect/
```

Or for syntax-only (no LSP), use the standalone files from `docs/syntax/vim/`:

```bash
mkdir -p ~/.config/nvim/syntax ~/.config/nvim/ftdetect
cp docs/syntax/vim/objeck.vim ~/.config/nvim/syntax/
cp docs/syntax/vim/ftdetect/objeck.vim ~/.config/nvim/ftdetect/
```

---

## Emacs (29+)

### Automated Setup with LSP (Recommended)

Download the [objeck-lsp release](https://github.com/objeck/objeck-lsp/releases) and run:

```bash
./scripts/install.sh /usr/local/objeck emacs
```

This installs `objeck-mode.el` (with LSP integration via eglot) to your Emacs load-path.

### Manual Setup (syntax only)

Copy from `docs/syntax/emacs/`:

```bash
cp docs/syntax/emacs/objeck-mode.el ~/.emacs.d/
```

Add to `~/.emacs` or `~/.emacs.d/init.el`:

```elisp
(load "~/.emacs.d/objeck-mode.el")
```

Provides: syntax highlighting, auto-indentation (2-space), comment support, `.obs` file auto-detection.

### Compile from Emacs

```
M-x compile RET obc -src myprogram.obs RET
```

Errors are parsed and navigable with `M-x next-error`.

---

## Kate

Copy `docs/syntax/kate/objeck.xml` to:

| Platform | Path |
|----------|------|
| **Linux** | `~/.local/share/katepart5/syntax/` |
| **macOS** | `~/Library/Application Support/katepart5/syntax/` |

LSP support available via Kate's built-in LSP client — see [objeck-lsp](https://github.com/objeck/objeck-lsp) for configuration.

---

## Other Editors

| Editor | Files | Location |
|--------|-------|----------|
| **Geany** | `docs/syntax/geany/filetypes.Objeck.conf` | Copy to Geany config directory |
| **Notepad++** | `docs/syntax/notepadxx/objeck.xml` | Import via Language > User Defined Language |
| **JEdit** | `docs/syntax/jedit/objeck.xml` | Copy to `~/.jedit/modes/` |
| **Lite-XL** | `docs/syntax/lite-xl/language_objeck.lua` | Copy to Lite-XL plugins |
| **TextAdept** | `docs/syntax/textadept/objeck.lua` | Copy to TextAdept lexers |

---

## Command-Line Debugger

For terminal-based debugging without an IDE, see the [Debugger Guide](https://www.objeck.org/getting_started.html#debugger) or `core/debugger/README.md`.

```bash
obc -src myapp.obs -debug       # compile with debug symbols
obd -bin myapp.obe -src_dir .   # start debugger
```
