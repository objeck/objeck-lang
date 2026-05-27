# Objeck for Vim 9.2+ / gvim

Adds syntax highlighting, LSP (autocomplete / go-to-def / hover / diagnostics), and DAP debugging to plain Vim and gvim. Requires Vim 9.0+ with `+python3` (the gvim Huge build has it).

## Automated install

From the extracted `objeck-lsp` release:

```cmd
:: Windows
scripts\install.cmd "C:\Program Files\Objeck" vim

:: Linux / macOS
./scripts/install.sh /usr/local/objeck vim
```

The script:
- Copies syntax + ftdetect from `objeck-lang/docs/syntax/vim/` into `~/vimfiles/syntax/` and `~/vimfiles/ftdetect/` (POSIX: `~/.vim/`)
- Clones [yegappan/lsp](https://github.com/yegappan/lsp) into `~/vimfiles/pack/objeck/start/lsp/`
- Clones [puremourning/vimspector](https://github.com/puremourning/vimspector) into `~/vimfiles/pack/objeck/start/vimspector/`
- Drops `objeck_vimrc.vim` into `~/vimfiles/plugin/objeck.vim` so it auto-loads
- Writes `~/.vimspector.json` with the Objeck DAP adapter pointing at `obd --dap`

## Manual install

1. **Syntax** — copy from `<objeck>/docs/syntax/vim/`:
   ```cmd
   mkdir "%USERPROFILE%\vimfiles\syntax" "%USERPROFILE%\vimfiles\ftdetect"
   copy objeck.vim "%USERPROFILE%\vimfiles\syntax\"
   copy ftdetect\objeck.vim "%USERPROFILE%\vimfiles\ftdetect\"
   ```

2. **LSP plugin**:
   ```bash
   git clone --depth 1 https://github.com/yegappan/lsp \
       ~/vimfiles/pack/objeck/start/lsp
   ```

3. **DAP plugin**:
   ```bash
   git clone --depth 1 https://github.com/puremourning/vimspector \
       ~/vimfiles/pack/objeck/start/vimspector
   ```

4. **Vim config** — append the contents of `objeck_vimrc.vim` to your `_vimrc` (Windows) or `.vimrc` (POSIX), or `:source` it from there.

5. **vimspector adapter** — copy `vimspector.json` to `~/.vimspector.json`.

## Usage

### LSP

Open any `.obs` file. The LSP server (`objeck_lsp.obe`) auto-starts. Built-in keybindings (active in Objeck buffers):

| Key       | Action                |
|-----------|-----------------------|
| `gd`      | Go to definition      |
| `gr`      | Show references       |
| `K`       | Hover documentation   |
| `<leader>rn` | Rename symbol      |

### Debugging

1. Compile with debug symbols:
   ```bash
   obc -src myprog.obs -debug
   ```

2. Add a launch config to your project — copy `vimspector.project.json.example` to `<your-project>/.vimspector.json`.

3. Open your `.obs` file in gvim, set a breakpoint with `<F9>`, then start debugging with `<F5>`.

   Vimspector "HUMAN" key bindings:

   | Key       | Action                |
   |-----------|-----------------------|
   | `<F5>`    | Continue / start      |
   | `<F9>`    | Toggle breakpoint     |
   | `<F10>`   | Step over             |
   | `<F11>`   | Step into             |
   | `<F12>`   | Step out              |
   | `<S-F5>`  | Stop debugging        |

   Program output (`PrintLine`, etc.) appears in the vimspector output window.

## Troubleshooting

- **`E117: Unknown function: g:LspAddServer`** — yegappan/lsp not installed or not in `~/vimfiles/pack/objeck/start/`. Verify with `:echo &runtimepath`.
- **`Vimspector unavailable: Requires Vim compiled with +python3`** — install the gvim Huge build, or `:echo has('python3')` to confirm.
- **`E10: \ should be followed by /, ? or &`** — your vimrc is in `compatible` mode; make sure `set nocompatible` runs before any `\`-continued lines.
