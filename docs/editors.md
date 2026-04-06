# Editor & IDE Support

Objeck has syntax highlighting, build integration, LSP support, and DAP debugging across multiple editors.

---

## VS Code (Recommended)

Full IDE experience with syntax highlighting, LSP autocomplete, and DAP debugging.

### 1. Install the Extension

Download the latest `.vsix` from the [objeck-lsp releases](https://github.com/objeck/objeck-lsp/releases), then:

```
code --install-extension objeck-lsp-*.vsix
```

Or in VS Code: **Extensions** (Ctrl+Shift+X) > **...** menu > **Install from VSIX...**

### 2. Configure Install Path

Open VS Code Settings (Ctrl+,) and set:

- **Windows:** `objk.win.install.dir` → e.g. `C:\Program Files\Objeck`
- **macOS/Linux:** `objk.posix.install.dir` → e.g. `/usr/local/objeck-lang`

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
- Step Over (F10), Step Into (F11), Step Out (Shift+F11)
- Variable inspection in the **Variables** panel
- Call stack in the **Call Stack** panel
- Expression evaluation in the **Debug Console**

### 4. LSP Features

The extension provides:

- Syntax highlighting
- Code completion
- Go to definition
- Hover information
- Diagnostics (compile errors)

---

## Sublime Text

### Automated Setup (Recommended)

Download the [objeck-lsp release](https://github.com/objeck/objeck-lsp/releases) and run the install script:

```bash
# Windows
scripts\install.cmd "C:\Program Files\Objeck" sublime

# Linux / macOS
./scripts/install.sh /usr/local/objeck sublime
```

This installs the LSP server, syntax highlighting, and configures Sublime's LSP client automatically. Requires the [LSP](https://packagecontrol.io/packages/LSP) package (installed automatically on Linux/macOS if git is available).

After install: **Tools > LSP > Enable Language Server Globally > select "objeck"**.

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

## Vim / GVim

### Syntax Highlighting

Copy from `docs/syntax/vim/`:

```bash
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp docs/syntax/vim/objeck.vim ~/.vim/syntax/
cp docs/syntax/vim/ftdetect/objeck.vim ~/.vim/ftdetect/
```

Provides highlighting for keywords, types, strings, comments, numbers, and operators. `.obs` files are auto-detected.

### Build from Vim

Add to your `.vimrc`:

```vim
autocmd FileType objeck setlocal makeprg=obc\ -src\ %\ -dest\ %:r.obe
autocmd FileType objeck setlocal errorformat=%f:(%l\\,%c):\ %m
```

Then use `:make` to compile and `:copen` to see errors.

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
