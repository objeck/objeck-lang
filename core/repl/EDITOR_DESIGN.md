# Full-screen editor for `obi` — design

`obi` today edits by line. `Document`/`Line` hold the buffer and `DoInsertLine`,
`DoDeleteLine`, `DoReplaceLine`, `DoGotoLine`, `DoList` operate on it through an
`ed`-style command loop that reads whole lines from `std::wcin`. This note
describes adding a full-screen mode with a run pane, without disturbing that.

## What already exists

Three things make this smaller than it looks:

- **ANSI output already works on all three platforms.** `core/debugger/color.h`
  enables `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on Windows and falls back to
  plain text when it cannot (`color_enabled`). The same approach carries the
  editor's rendering — no new rendering strategy, and no curses dependency.
- **The buffer model is already there.** `Document` owns the lines and their
  read-only/read-write types, plus load and save. The editor is a new *view*
  over it, not a new model.
- **Compile and run are already in-process.** `DoExecute` calls
  `lang.Compile(...)` then `lang.Execute(...)`. A run pane does not need a
  subprocess.

## What is missing

Everything on the input and layout side:

1. **Raw-mode input.** Nothing in `core/` puts a terminal into raw mode. Needs
   `termios` (POSIX) and `SetConsoleMode` (Windows).
2. **Key decoding.** Arrows, Home/End, PgUp/PgDn, Delete, and modified keys
   arrive as escape sequences on POSIX and as `INPUT_RECORD`s on Windows.
3. **A screen model.** Direct writes flicker. Needs a back buffer diffed against
   the front buffer so only changed cells are emitted.
4. **Size and resize.** `ioctl(TIOCGWINSZ)` + `SIGWINCH` on POSIX;
   `GetConsoleScreenBufferInfo` and polling on Windows (no resize signal).
5. **Display width.** Objeck source is UTF-8 and the buffer is `wstring`. CJK
   and emoji occupy two columns; combining marks occupy zero. Cursor placement
   and wrapping are wrong without a `wcwidth`-equivalent. This is the single
   biggest threat to "looks right on all platforms".
6. **A non-TTY fallback.** `obi --file` and `obi --inline` are scripted and used
   in CI. Full-screen mode must engage only on an interactive TTY.

## Architecture

Four new units under `core/repl/`, each independently testable:

```
term.h/.cpp      raw mode, size, cursor, colours, key decoding -> Key events
screen.h/.cpp    cell grid, back/front buffer, damage diff, wcwidth handling
tui_editor.h/.cpp viewport, cursor, selection, undo, over the existing Document
keymap.h/.cpp    named actions bound to keys; two profiles (see below)
```

`term` is the only unit with `#ifdef _WIN32`. Everything above it is portable.

**Key events, not characters.** `term` yields a `Key { code, ch, ctrl, alt,
shift }` so the editor never parses escape sequences. This is what keeps the
Windows and POSIX paths from leaking upward.

**Damage-diff rendering.** `screen` holds two cell grids; `Flush()` walks the
diff and emits the minimum cursor moves and text. Full repaints only on resize.
Without this, editing feels laggy over SSH and flickers on Windows.

## Bindings

Bindings are *data*, not control flow: a `keymap` maps a `Key` to a named action
(`move-word-left`, `delete-line`, `run`). Two profiles ship, selected by a REPL
command and persisted:

- **`simple` (default)** — the notepad model. Arrows, Home/End, PgUp/PgDn,
  Shift+arrows to select, Ctrl+S save, Ctrl+Q quit, Ctrl+C/X/V, Ctrl+Z undo,
  F5 run. Discoverable, with a persistent hint bar.
- **`vi`** — modal, normal/insert/visual, `hjkl`, `dd`, `yy`, `p`, `/`, `:w`,
  `:q`. Implemented as a second keymap plus a mode field; the editing primitives
  are shared, so this is bindings and a mode, not a second editor.

The default is `simple` deliberately: a user who wants `vi` will look for it,
whereas a user who gets modal editing unexpectedly is stuck.

## Run pane

Horizontal split, buffer above and output below, toggled and resizable.
`Run` calls the existing `DoExecute` path. The one new requirement is capturing
what the compiler and VM write to stdout/stderr so it lands in the pane instead
of corrupting the screen — the same redirect problem `obd --dap` already solved
(see `dap_stdio_redirect`). Compiler diagnostics carry line numbers, so errors
in the pane should be selectable and jump the cursor to the offending line;
that is most of the value of having the pane at all.

## Phasing

Each phase is independently useful and shippable:

1. `term` + `screen` with a scratch program that echoes decoded keys and
   repaints — proves raw mode, decoding, resize and width handling on all three
   platforms before any editing logic exists.
2. `tui_editor` over `Document`: movement, insert, delete, save, quit,
   `simple` keymap. Line editing keeps working; the new mode is opt-in.
3. Run pane and error navigation.
4. `vi` keymap, selection, undo/redo.

## Risks, in order

- **Display width.** Get `wcwidth` behaviour wrong and the cursor drifts on any
  non-ASCII line. Decide early whether to vendor a width table or use
  `wcwidth`/`wcswidth` where available; note this codebase already hit a
  UTF-8/locale defect (`utf8_locale_bug`), so do not assume the locale helps.
- **Windows console variance.** Windows Terminal, conhost, and older Windows 10
  differ in VT support. `color.h`'s degradation pattern must be followed, and a
  no-VT fallback path decided rather than discovered.
- **Non-TTY regressions.** Guard the mode behind an `isatty` check, and keep a
  regression test that `obi --inline`/`--file` still behave headlessly.
- **Scope.** This is the largest single item in the REPL. Phase 1 alone is worth
  landing before committing to the rest.
