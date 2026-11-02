# Editor setup and the language server

Objeck ships a language server (LSP) and a debug adapter (DAP) that work in VS Code, Sublime Text,
Neovim, Emacs, Kate, ecode and Helix. Every editor gets the same server, so the features below do
not depend on which one you use.

## Install

Download `objeck-lsp_<version>.zip` from the
[releases page](https://github.com/objeck/objeck-lang/releases/latest) and unzip it. Then, from the
unzipped folder, run the install script, giving the folder Objeck is installed in and the editor
(`vscode`, `sublime`, `neovim`, `emacs` or `all`):

```text
scripts\install.cmd "C:\Program Files\Objeck" vscode
./scripts/install.sh /usr/local/objeck vscode
```

The first line is for Windows and the second for Linux and macOS. VS Code can also install the
extension directly:

```text
code --install-extension objeck-lsp.vsix
```

Then set the install folder in the editor's settings (`objk.win.install.dir` on Windows,
`objk.posix.install.dir` elsewhere). The per-editor steps, including Vim, Kate and the editors that
only have syntax highlighting, are in **[`docs/editors.md`](../editors.md)**, and
[`tools/lsp/README.md`](../../tools/lsp/README.md) covers the server itself.

For a project of several files, put a `build.json` in the project folder that lists them:

```json
{
  "files": ["main.obs", "helper.obs"],
  "libs": ["gen_collect.obl", "net.obl", "json.obl"],
  "flags": ""
}
```

## What you get

Diagnostics as you type, completion with signature help, hover, go to definition, declaration,
type definition and implementation, find references, rename, code actions, formatting, document
and workspace symbols, call and type hierarchy, inlay hints, semantic tokens, folding and
document highlight. The full list is in [`docs/editors.md`](../editors.md#4-lsp-features).

Debugging needs a program compiled with debug symbols, or no breakpoint will bind:

```text
obc -src myprogram.obs -debug
```

then start a debug session from the editor (F5 in VS Code). Breakpoints, conditional breakpoints,
logpoints, stepping, variable inspection and the call stack all work.

## What changed in v2026.10.3

- **Edits analyze faster.** The diagnostics library inflates each library once per process instead
  of on every edit, and a folder's analysed program is kept until a newer one replaces it. On
  Windows a hover after an edit in a small workspace went from 165 ms to 53 ms, and a hover in a
  second folder from about 600 ms to about 60 ms
  ([#1531](https://github.com/objeck/objeck-lang/pull/1531),
  [#1532](https://github.com/objeck/objeck-lang/pull/1532)). The cached and uncached servers gave
  identical answers for every file in more than 6,000 edited texts. Setting
  `OBJECK_LSP_NO_CACHE=1` runs the uncached server, which is useful if you suspect a stale answer.
- **A broken library no longer takes the server down.** A fatal compiler error is returned instead
  of ending the host process, so a library that fails to load leaves the server running
  ([#1507](https://github.com/objeck/objeck-lang/pull/1507)).
- **The debug adapter keeps output whole.** Partial UTF-8 is carried across reads, messages are
  ordered, an unknown request fails instead of being ignored, you can pause, and breakpoints keep
  their file and have their own ids
  ([#1487](https://github.com/objeck/objeck-lang/pull/1487),
  [#1495](https://github.com/objeck/objeck-lang/pull/1495)).

The design of the incremental analysis, and the two phases that are not started, is in
[`docs/INCREMENTAL_ANALYSIS_DESIGN.md`](../INCREMENTAL_ANALYSIS_DESIGN.md).
