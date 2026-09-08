# `System.Terminal` — styled output, progress, and interactive controls for the command line: design

> **Status (2026-09-08):** design only, nothing built. Decided: all three phases,
> shipping together as the headline of the release **after** v2026.9.1, under the
> namespace **`System.Terminal`** (library file `term.obs` → `term.obl`).
> The phasing section at the end is the work plan; the API sketch is the
> contract to review before the first line of library code.

## 1. Why

Objeck has no terminal support at all. There is no ANSI helper in any library,
no way to ask whether stdout is a terminal, no terminal size, no single-key
input (`System.IO.Console->ReadLine` is the only primitive: line-buffered,
echoing, backed by the `STD_IN_STRING` trap), and on Windows nothing enables
virtual-terminal processing. The debugger prints in colour, but from C++.

Every command-line tool written in Objeck therefore prints plain lines. The
rest of the ecosystem converged years ago on the same small toolkit — styling
that degrades honestly, progress bars with rate and ETA, spinners, tables,
panels, and prompts (chalk and inquirer in Node, Rich in Python, Charm's Lip
Gloss / Bubbles / Huh in Go, indicatif and dialoguer in Rust, Spectre.Console
in .NET). This library brings that toolkit to Objeck in the language's own
idiom, and adds the five VM primitives it cannot be built without.

## 2. Goals and non-goals

**Goals**

- Styled text (16 / 256 / truecolor, bold, dim, italic, underline, strike,
  inverse) that renders to a `String` and degrades to plain text when it must.
- Display components that redraw in place: progress bars (single and multi,
  with rate, ETA and colour thresholds), spinners, a checklist, tables, panels,
  rules.
- Interactive components: checkbox (multi-select), select (radio), confirm,
  text input with validation, password, composed into a `Form`.
- Every component testable **without a terminal**: rendering is a pure
  function of state, input is a scripted key sequence.
- Correct behaviour when piped, when `NO_COLOR` is set, in Windows Terminal,
  in legacy conhost, and over SSH.
- The terminal is always restored — on normal exit, `Runtime->Exit`, Ctrl-C,
  and a crash.

**Non-goals (this release)**

- A full-screen application framework (alternate screen, layouts, mouse). The
  component model is chosen so one could be built on it later; none is built.
- East-Asian wide-character and grapheme-cluster width. Widths are code-point
  counts; a `DisplayWidth` hook exists so this can be corrected in one place.
- Windows before 10 build 1511 (no virtual-terminal support at all). The
  library degrades to plain output there; it does not emulate.

## 3. Architecture

Five layers, each usable without the ones above it:

```
  Form / Live            runs a component loop: render -> ReadKey -> handle -> redraw
  Components             Render() ~ String, Handle(key) ~ Bool, Value()
  Style / Color / Text   pure functions: String in, String out
  Ansi / Capabilities    escape-sequence builder; what this terminal can do
  Terminal (VM traps)    IsTty, Size, EnableAnsi, RawMode, ReadKey
```

Everything above the bottom layer is pure Objeck in `core/compiler/lib_src/term.obs`.
The bottom layer is five traps in the VM. Phase 1 builds the top four layers
against a `Capabilities` object that can be constructed by hand, which is how
the tests run and how the library behaves when the traps say "not a terminal".

## 4. API sketch

Signatures are the contract; bodies are illustrative. All classes are in
`System.Terminal`.

### 4.1 `Terminal` — the VM boundary

```objeck
class Terminal {
  function : IsTty() ~ Bool;                 # stdout is an interactive terminal
  function : IsInputTty() ~ Bool;            # stdin is one (ReadKey reads keys, not a pipe)
  function : Size() ~ Size;                  # columns x rows; 0x0 when unknown
  function : EnableAnsi() ~ Bool;            # Windows: ENABLE_VIRTUAL_TERMINAL_PROCESSING; true elsewhere
  function : RawMode(on : Bool) ~ Bool;      # no line buffering, no echo; restored automatically
  function : ReadKey() ~ Key;                # blocks for one key; decoded (arrows, enter, ...)
  function : Capabilities() ~ Capabilities;  # detected once, cached; overridable for tests
}
```

`Capabilities` is a value: `color_depth` (`NONE`, `ANSI16`, `ANSI256`, `TRUECOLOR`),
`unicode : Bool`, `is_tty : Bool`, `columns : Int`. Detection order, matching
what every other library does so scripts behave the same across tools:

1. `NO_COLOR` set (any value) → `NONE`.
2. `FORCE_COLOR` set → at least `ANSI16` even when piped.
3. not a TTY → `NONE`.
4. `COLORTERM` = `truecolor`/`24bit` → `TRUECOLOR`; `TERM` contains `256color` → `ANSI256`;
   `TERM` = `dumb` → `NONE`; otherwise `ANSI16`. Windows with `EnableAnsi()` succeeding → `TRUECOLOR`
   (Windows Terminal and conhost since 1511 both accept 24-bit sequences).
5. `unicode` = stdout encoding is UTF-8 (POSIX `LANG`/`LC_*` containing `UTF-8`;
   Windows: output code page 65001 or `--objeck-stdio=utf8`). When false every
   glyph table has an ASCII column.

### 4.2 `Color`, `Style`, `Text`

```objeck
class Color {                                  # a value; never emits escapes itself
  function : Named(name : ColorName) ~ Color;  # BLACK..WHITE, BRIGHT_*
  function : Index(n : Int) ~ Color;           # 0-255
  function : Rgb(r : Int, g : Int, b : Int) ~ Color;
  function : Hex(text : String) ~ Color;       # "#ff8800"
  method : public : Downgrade(depth : ColorDepth) ~ Color;   # nearest 256 / 16 colour
}

class Style {                                  # immutable, chainable, cheap to copy
  New();
  method : public : Fg(c : Color) ~ Style;   method : public : Bg(c : Color) ~ Style;
  method : public : Bold() ~ Style;  Dim();  Italic();  Underline();  Strike();  Inverse();
  method : public : Apply(text : String) ~ String;          # under current Capabilities
  method : public : Apply(text : String, caps : Capabilities) ~ String;
}

class Text {                                   # width-aware string helpers
  function : DisplayWidth(text : String) ~ Int;             # escape-free width (code points for now)
  function : Strip(text : String) ~ String;                 # remove escapes
  function : PadRight(text, width) ~ String;  PadLeft; Center;
  function : Truncate(text : String, width : Int) ~ String; # keeps escapes balanced, adds "…"/"..."
}
```

`Apply` is the only place escapes are emitted for styling. It consults the
capability depth: truecolor emits `38;2;r;g;b`, 256 emits `38;5;n`, 16 emits
`3x`/`9x`, `NONE` returns the text unchanged. A `Style` never knows what
terminal it is on; the same object renders differently under different
capabilities, which is what makes the tests deterministic.

### 4.3 Display components

```objeck
interface Renderable { method : virtual : public : Render() ~ String; }

class ProgressBar implements Renderable {
  New(total : Int);
  method : public : Label(text : String) ~ ProgressBar;
  method : public : Width(columns : Int) ~ ProgressBar;   # default: terminal width minus decorations
  method : public : Thresholds(warn : Float, ok : Float) ~ ProgressBar;   # colour bands: <warn red, <ok yellow, else green
  method : public : Style(fill : Style, empty : Style) ~ ProgressBar;
  method : public : Glyphs(fill : Char, empty : Char) ~ ProgressBar;      # default █/░, ASCII #/-
  method : public : ShowRate(on : Bool);  ShowEta(on : Bool);  ShowPercent(on : Bool);
  method : public : Update(current : Int) ~ Nil;   method : public : Advance(by : Int) ~ Nil;
  method : public : Finish() ~ Nil;                # clamps to total, freezes the line
  method : public : Render() ~ String;             # "[label] ████████░░░░  67%  1.2k/s  eta 0:14"
}

class MultiProgress implements Renderable {        # N bars, one redraw
  method : public : Add(bar : ProgressBar) ~ Nil;
  method : public : Render() ~ String;             # one line per bar
}

class Spinner implements Renderable {              # frames advance on Tick(); Render() is the current frame
  New(label : String);  method : public : Tick() ~ Nil;  method : public : Done(text : String) ~ Nil;
}

class Checklist implements Renderable {            # "☐ pending  ⟳ running  ☑ done  ☒ failed", colour-coded
  method : public : Add(label : String) ~ Int;     # returns the item's index
  method : public : Set(index : Int, state : ItemState) ~ Nil;
  method : public : Render() ~ String;
}

class Table implements Renderable {
  New(headers : String[]);
  method : public : Add(row : String[]) ~ Nil;
  method : public : Align(column : Int, a : Alignment) ~ Nil;
  method : public : Border(kind : BorderKind) ~ Nil;      # NONE, ASCII, LIGHT, HEAVY, ROUNDED; Unicode falls back to ASCII
  method : public : CellStyle(column : Int, s : Style) ~ Nil;
  method : public : Render() ~ String;
}

class Panel implements Renderable { New(body : String); Title(text); Border(kind); Padding(n); Render(); }
class Rule  implements Renderable { New(); Title(text); Render(); }   # ──── title ────
```

Rate and ETA come from `System.Time.Timer->GetElapsedTime()` (seconds as
`Float`), started on the first `Update`. ETA is smoothed over the last N
updates, never printed before two samples exist, and prints `--:--` when the
rate is zero, so a stalled bar does not announce "eta 0:00".

### 4.4 In-place redraw: `Live`

```objeck
class Live {                                   # owns the region below the cursor that it redraws
  New(component : Renderable);
  method : public : Refresh() ~ Nil;           # render; cursor up N; clear to end; write frame; one write
  method : public : Finish() ~ Nil;            # leaves the last frame, moves below it
}
```

The redraw rule is: remember how many lines the previous frame occupied,
emit `ESC[<n>A` then `ESC[J`, write the whole new frame as one string, flush.
One write per frame is what prevents flicker; the line count comes from the
frame's own newlines plus wrapping computed from `Capabilities.columns`, which
is why every component clamps to the width instead of trusting the terminal to
wrap. When `IsTty()` is false `Live` prints each frame on its own line without
cursor movement (a log file gets a readable progression, not escape garbage).

### 4.5 Keys and interactive components

```objeck
class Key {                                    # what ReadKey returns
  method : public : GetKind() ~ KeyKind;       # CHAR, ENTER, ESCAPE, BACKSPACE, DELETE, TAB, UP, DOWN, LEFT, RIGHT,
                                               # HOME, END, PAGE_UP, PAGE_DOWN, F1..F12, CTRL_C, EOF
  method : public : GetChar() ~ Char;          # for CHAR
  method : public : IsCtrl() ~ Bool;  IsAlt() ~ Bool;  IsShift() ~ Bool;
}

interface Component from Renderable {
  method : virtual : public : Handle(key : Key) ~ Bool;   # true when the component is finished
  method : virtual : public : IsCancelled() ~ Bool;       # Escape / Ctrl-C
}

class Confirm  implements Component { New(prompt : String); Default(yes : Bool); method : public : Value() ~ Bool; }
class Select   implements Component { New(prompt : String, options : String[]); method : public : Value() ~ Int; }
class Checkbox implements Component { New(prompt : String, options : String[]); Preselect(indexes : Int[]);
                                      MinMax(min : Int, max : Int); method : public : Value() ~ Int[]; }
class Input    implements Component { New(prompt : String); Placeholder(text); Validate(f : (String) ~ String);  # Nil = ok, else message
                                      method : public : Value() ~ String; }
class Password implements Component { New(prompt : String); method : public : Value() ~ String; }   # renders •••, never echoes

class Form {                                   # runs components in order; one RawMode on/off around the whole run
  method : public : Add(c : Component) ~ Form;
  method : public : Run() ~ Bool;              # false when cancelled; values stay readable on the components
}
```

Keyboard conventions, the same in every component so users learn them once:
arrows or `j`/`k` move, space toggles, enter accepts, escape cancels, `Ctrl-C`
cancels *and* the form returns false (the VM's own Ctrl-C handling stays
active — see §5.4). A component that is finished re-renders as a one-line
summary (`? Continue?  yes`), which is what makes a form's transcript readable
afterwards and in a log.

## 5. The VM side — five traps

Appended **after** `HTTP3_REQUEST_HDRS` in `core/shared/traps.h` (the enum is
unnumbered; the ordinal is the wire id, so append-only), named
`TERM_IS_TTY`, `TERM_SIZE`, `TERM_ENABLE_ANSI`, `TERM_RAW_MODE`, `TERM_READ_KEY`.
Each new trap touches the same places every earlier one did:

- `core/compiler/scanner.cpp` — `_SYSTEM`-only keyword in `ident_map`;
  `core/compiler/parser.cpp` — both intrinsic switch sites (~2451, ~4571);
  `core/compiler/intermediate.cpp` — emission. `lang.obs` is not involved;
  `term.obs` is compiled with the ordinary compiler, so the `Terminal` class
  calls the intrinsics from a small `_SYSTEM`-compiled shim in `lang.obs`
  (`System.IO.ConsoleIO`, which already owns `ReadString`), exactly as the
  network library reaches its traps through `System.IO.Net`.
- `core/vm/common.cpp` — the trap switch; per-platform bodies in
  `arch/win32/win32.h` and `arch/posix/posix.h` behind the existing
  `#ifdef _WIN32`.
- **Both JIT trap parameter tables** (`core/vm/arch/jit/amd64`, `arm64`): a
  trap's entry says how many working-stack values the callback consumes. A
  missing entry is the frame-trap class of bug — compiled code passes the
  wrong count. The equivalence fixture in `run_vm_flag_tests.py` therefore
  gains a probe that calls every new trap, so interpreter and `--jit=1`
  output are compared on every leg.
- `core/compiler/linker.cpp`'s bytecode reader is unaffected (traps are
  `TRAP` instructions with an integer operand; no new opcode).

### 5.1 `TERM_IS_TTY` / `TERM_SIZE` / `TERM_ENABLE_ANSI`

POSIX: `isatty(1)`, `isatty(0)`; `ioctl(1, TIOCGWINSZ)`; `EnableAnsi` is a
no-op returning true. Windows: `GetConsoleMode` on the handle succeeds only
for a console; `GetConsoleScreenBufferInfo` for size (window rectangle, not
buffer); `EnableAnsi` ORs `ENABLE_VIRTUAL_TERMINAL_PROCESSING` into the
output mode and reports whether `SetConsoleMode` accepted it (it fails on
pre-1511 conhost, which is the honest "no ANSI here" signal). These interact
with nothing else: `--objeck-stdio` changes how *text* is encoded, and escape
sequences are ASCII under every mode.

### 5.2 `TERM_RAW_MODE`

POSIX: `tcgetattr` once (saved), then `ICANON | ECHO` off, `VMIN=1`, `VTIME=0`.
**`ISIG` stays on** — Ctrl-C still raises `SIGINT`, which the VM already
handles (`StackProgram::SignalHandler`, `common.cpp:184`); the handler
restores the terminal before the process dies. Windows: input mode minus
`ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT`, plus `ENABLE_VIRTUAL_TERMINAL_INPUT`
so arrow keys arrive as the same escape sequences POSIX sends — one decoder
serves both platforms. `ENABLE_PROCESSED_INPUT` stays on for the same reason
`ISIG` does.

**Restore guarantee.** The first `RawMode(true)` saves the original mode and
registers an `atexit` restore; `RawMode(false)` restores and is idempotent;
the signal handler calls the same restore; `Runtime->Exit` goes through
`exit()` and so through `atexit`. `Form->Run` wraps its loop so a thrown
error still restores. The debugger host (`obd`, which embeds the VM) restores
before showing its own prompt after a stop, and never enters raw mode in
`--dap` mode where stdin is the protocol channel — `IsInputTty()` is false
there and `ReadKey` reads the stream (§5.3), so a program that prompts under
the debugger does not corrupt the DAP stream.

### 5.3 `TERM_READ_KEY`

Blocks for one key and returns it decoded: printable code point, or a named
key. Decoding lives in C++ because it needs a timeout: a lone `ESC` and the
first byte of `ESC [ A` are the same byte, distinguished by whether more
bytes arrive within ~50 ms (`poll` / `WaitForSingleObject`). Sequences
handled: CSI arrows/home/end/page/delete/insert, function keys (both
`ESC O P` and `ESC [ 11~` families), `Ctrl+letter` (bytes 1–26), `Alt+key`
(`ESC` prefix), backspace as both 8 and 127. Unknown sequences are consumed
and returned as `UNKNOWN`, never leaked as characters.

**When stdin is not a terminal** (`IsInputTty()` false) `ReadKey` reads the
same byte stream from the pipe and decodes it identically, without changing
any console mode. That is not a fallback, it is the test interface: the
regression harness feeds a scripted key sequence on stdin (as the debugger
harness already does) and every interactive component is exercised end to end
in CI on every platform, with no pseudo-terminal.

### 5.4 Ctrl-C and cancellation

Two behaviours, deliberately distinct: Ctrl-C **terminates** the process
(signal path, terminal restored) — the same as any other Objeck program; the
`Escape` key **cancels** the current form (`Run` returns false, terminal
restored, program continues). A component that wants to treat Ctrl-C as
cancel cannot, and that is intended: turning `ISIG` off is how tools leave
shells unusable.

## 6. Testing

- **Rendering is pure**: `programs/regression/term_render_test.obs` builds a
  `Capabilities` by hand (truecolor, 256, 16, none; unicode on/off; width 40)
  and compares `Render()` output byte for byte against expected sequences for
  every display component, including thresholds crossing, ETA `--:--`,
  truncation with balanced escapes, and the plain-text path.
- **Interactive components are scripted**: `term_form_test.obs` feeds each
  component a key sequence (through `Handle`, no terminal) and checks the
  frames and the final values; a second run drives the same components
  through `Form->Run` with the keys on **stdin** (`ReadKey` on a pipe, §5.3),
  which is the end-to-end path the runner can exercise.
- **Traps**: `IsTty()` is false and `Size()` is `0x0` under the harness (stdout
  is a pipe) — asserted, so the "not a terminal" path is the one CI proves
  every time; `EnableAnsi()` returns true on POSIX and is asserted not to
  throw on Windows. The equivalence probe runs them under `--jit=1`.
- **Restore**: a test that enters raw mode and calls `Runtime->Exit` inside a
  child `obr`, then asserts the parent's `tcgetattr` (POSIX) / console mode
  (Windows) is unchanged — the one property that matters most and is easiest
  to lose.
- **Pre-fix proof** applies as always: the render tests are written against
  the documented output before the components exist.

## 7. What adding a library touches (checklist)

`core/compiler/lib_src/term.obs`; `core/lib/term.obl` (tracked; rebuilt and
committed with every `.obs` change); one line in `core/compiler/build_libs.sh`
(`-lib gen_collect -tar lib -opt s3`); the five hand-kept documentation lists
guarded by `tools/cicd/check_doc_lists.py` (`ci-build.yml` code_doc line,
`code_doc64.in`, `code_doc64_debug.cmd`, `gen_json.cmd`, `gen_json.sh`);
regenerated `objk_apis.json` (both copies) so hover and completion know the
classes; doc comments on every public member (a ``` fence must be last);
`README.md` / `docs/FEATURES.md` / `docs/EXAMPLES.md`; a shipped example in
`programs/deploy` (not `programs/examples`, which the release does not ship):
`term_demo.obs` — a download-style multi-progress, a checklist that completes,
and a three-step form. Deploy scripts copy `*.obl` by glob and need no edit.

## 8. Phases

| Phase | Deliverable | Exit criterion |
|---|---|---|
| 1 | `Capabilities`, `Color`, `Style`, `Text`, `Ansi`; `ProgressBar`, `MultiProgress`, `Spinner`, `Checklist`, `Table`, `Panel`, `Rule`, `Live` — pure Objeck | `term_render_test` green on all legs under every capability level; `term_demo` runs in Windows Terminal, macOS Terminal, Linux xterm and piped to a file |
| 2 | `TERM_IS_TTY`, `TERM_SIZE`, `TERM_ENABLE_ANSI` in the VM; `Terminal->Capabilities()` detects for real | trap tests + JIT equivalence probe green on all five legs; `term_demo` correct in legacy conhost (plain) and Windows Terminal (colour) without a flag |
| 3 | `TERM_RAW_MODE`, `TERM_READ_KEY`, decoder; `Key`, `Component`, `Confirm`, `Select`, `Checkbox`, `Input`, `Password`, `Form` | `term_form_test` green (scripted and stdin-driven) on all legs; the restore test green on POSIX and Windows; `obd` runs a prompting program without breaking its own prompt |

Estimates: Phase 1 about two days including tests; Phase 2 one day across
platforms; Phase 3 three days, most of it the Windows console input path and
the restore guarantees. Phases land as three pull requests in order; the
release notes describe one feature.

## 9. Risks and open questions

- **Windows console input** is where the time goes: `ENABLE_VIRTUAL_TERMINAL_INPUT`
  behaviour differs between Windows Terminal and conhost for a few keys
  (`Home`/`End`, `F1`–`F4`); the decoder is written against both and the
  `term_form_test` fixture includes those keys.
- **Width**: code-point width is wrong for CJK and emoji. `Text->DisplayWidth`
  is the single place to fix it; no component computes width any other way.
- **Threads**: a `Live` region is not thread-safe by design; a program that
  updates bars from workers updates the model and lets one thread `Refresh`.
  Stated in the docs, not enforced.
- **`obi` (the REPL)** owns the terminal already; programs run inside it get
  `IsTty()` true and share the console. Raw mode inside `obi` is restored on
  return to the REPL prompt — to be verified in Phase 3.
- **Glyph policy** (open): default to Unicode when `Capabilities.unicode` is
  true, ASCII otherwise; `Glyphs()` overrides per component. Alternative: a
  process-wide `Terminal->AsciiOnly(true)` for users on fonts without block
  characters. Proposal: both — the per-component override wins.
- **Truecolor on macOS Terminal.app** (open): it advertises `xterm-256color`
  and quantizes 24-bit sequences; detection would pick `ANSI256`, which is
  correct behaviour, but `COLORTERM` is unset there so nothing to decide —
  noted so nobody "fixes" it.
