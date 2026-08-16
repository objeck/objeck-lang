# F5 freezes obi — design for a responsive run

> **IMPLEMENTED — shipped in v2026.8.2.** Option B
> (subprocess) below is the design that shipped. The worker-thread attempt that
> the two banners under this one debate was reverted (it deadlocked even a
> trivial program: the in-process VM has main-thread affinity). F5 now compiles
> the buffer with `obc` and runs the `.obe` with `obr` as child processes,
> draining their merged output into the pane while `ReadKey`'s ~100 ms tick
> keeps Esc live; Esc kills the child (`core/repl/child_run.h`,
> `core/repl/tui_editor.h::DoRun`, plan built in `core/repl/editor.cpp`).
> `IoCapture` is dropped from the F5 path, and `core/repl/io_capture.h` has since
> been deleted — handing the child its own pipes achieves the same isolation
> structurally, so references to that file below are historical.
> Built clean via `core/repl/repl.sln`;
> **NOT interactively verified** — `/e` needs a real TTY, so the manual matrix at
> the end of this note is still the gate before this is trusted.

# F5 freezes obi — design for a responsive run

> **UPDATE — branch `fix/obi-f5-blocks-ui-thread` now addresses ALL THREE**
> **defects on the in-process path; the earlier "do not merge" is resolved.**
>
> 1. freeze -> run on a worker thread, UI polls Esc (ReadKey already ticks);
> 2. "frozen" appearance -> `Screen::Invalidate()` + `term.Clear()` after a run;
> 3. "no output" on Windows -> `IoCapture` now also `SetStdHandle`s the std
>    handles to the sink, so the VM's `WinWriteWide`/`GetStdHandle` path fails
>    `GetConsoleMode` and falls back to `wcout`, which the fd redirect
>    captures. Same mechanism the subprocess would use, done in-process.
>
> Built clean; NOT interactively tested (`/e` needs a TTY). The subprocess
> design below remains the stronger target -- true kill on cancel, live pane
> streaming, and the JIT that obi's `_NO_JIT` VM lacks -- but is no longer
> required to fix the reported bug.

# F5 freezes obi — design for a responsive run

> **STOP — ground truth invalidated the threading fix. Do NOT merge branch**
> **`fix/obi-f5-blocks-ui-thread`.** Verified against the tree:
>
> 1. **On Windows the program output never reaches the capture at all.** The VM
>    writes via `WinWriteWide(GetStdHandle(STD_OUTPUT_HANDLE), ...)`
>    (`core/vm/common.cpp:3381`), and `_dup2` on CRT fd 1 does not change
>    `GetStdHandle`. Output goes straight to the console, paints over the
>    raw-mode editor, and the pane receives nothing. **That is the "no output"
>    half of the bug, and threading does not touch it.**
> 2. **The editor also paints via the console handle on Windows**
>    (`core/repl/term.h:223` `WriteConsoleW(out_handle, ...)`), so repaints are
>    NOT captured there — my "do not paint during capture" reasoning was right
>    for POSIX (`write(STDOUT_FILENO)`, `term.h:251`) and wrong for Windows.
> 3. **The damage-diff repaint cannot recover.** After the child scribbles on
>    the console, `front` still believes the editor is on screen, so the next
>    `Screen::Flush` emits almost nothing (`core/repl/screen.h:179-215`). The
>    editor IS still reading keys -- it simply cannot redraw. There is no
>    `ForceRepaint`; only `Screen::Resize` invalidates.
> 4. **Threading the in-process VM races process-global state** --
>    `Loader::GetProgram()`, `MemoryManager::Clear()`,
>    `StackInterpreter::Clear()` (`core/module/lang.cpp:129-159`).
>
> So the threading change fixes only the freeze, leaves "no output" untouched,
> and adds a data race. The subprocess design fixes all three at once -- and
> for a reason I had not seen: handing the child PIPE handles makes
> `GetConsoleMode` fail in it, so `WinWriteWide` falls back to `wcout`
> (`common.cpp:141-147`) and the output becomes capturable. **The bypass is
> fixed for free by the subprocess, and only by it.**
>
> Two further findings worth keeping: obi's in-process VM is built `_NO_JIT`
> and without the HTTP/2-3 macros, so F5-via-`obr` would also be faster and
> more capable; and `Screen` needs an invalidation hook regardless of design,
> or the UI cannot recover from a scribbled console.


> **Status: PLAN, pending ground-truth verification.** Written before code, on
> the pattern that has caught several expensive mistakes in this repo. A
> verification agent is checking the claims below against the tree; anything it
> refutes must be corrected here before implementation.

## The bug

Pressing **F5** in `/e` edit mode freezes obi. The terminal becomes
unresponsive and no output appears.

`DoRun()` in `core/repl/tui_editor.h` calls the `runner` callback
**synchronously on the UI thread**. Nothing polls for input until the user's
program finishes, so obi cannot process keys — there is no way to interrupt,
and a program that loops takes the editor with it permanently.

## Why the obvious fix is wrong

The tempting change is "run it on a `std::thread`". That trades one bug for two.

`core/repl/io_capture.h` redirects **fd 1 and fd 2 process-wide** (`dup2` on
POSIX, `_dup2` on Windows). Those descriptors are a process-level resource, not
a per-thread one. So with the run on a background thread and the UI still
painting:

- the UI's own repaints go **into the capture file** instead of the screen; and
- the captured "program output" is polluted with escape sequences and screen
  redraws.

**Process-global fd redirection and a live UI cannot coexist.** That is the
real reason the in-process design was doomed, and any fix has to address it
rather than work around it.

## Verified facts (checked against the tree)

- **The timed key read already exists.** `core/repl/term.h:287` uses
  `WaitForSingleObject(in_handle, 100)`, and `KEY_NONE` at `term.h:38` is
  documented as *"timeout tick; poll again"*. The open dependency below is
  therefore **already satisfied** — a responsive `running… Esc to cancel` loop
  needs no change to the terminal layer.
- **No process spawning exists anywhere in the REPL.** `CreateProcess`,
  `popen`/`_popen`, `execvp` and `system` are all absent from `core/repl/`.
  So option B has to introduce process handling from scratch on **both**
  Windows and POSIX — this is the bulk of the work, not the UI loop.

That pairing is the whole cost picture: the responsive loop is cheap because
`ReadKey` already polls; the subprocess plumbing is not, because none exists.

## Options

### A — background thread, painting suspended during the run

Paint `running… Esc to abandon` *before* starting the capture, run on a
`std::thread`, then poll for keys **without repainting** until it completes.

*For:* small; keeps the existing `DoExecute()` path; obi survives.
*Against:* the screen is frozen for the duration, so it still *looks* hung. And
a runaway program cannot truly be cancelled — a thread mid-VM cannot be safely
killed, only detached, so it keeps burning CPU (and holding the redirected fds)
after the user "abandons" it. That last point is disqualifying: abandoning
leaves the process in a worse state than before.

### B — run the program in a subprocess (recommended)

Compile the buffer to a temp `.obe`, then spawn `obr` on it and read its
stdout/stderr through pipes.

*For:*
- fd redirection becomes the **child's** problem, so the UI paints freely
- output can stream into the pane live instead of appearing all at once
- cancellation is a real **kill**, not a detach
- a crashing or looping user program can no longer take obi with it

*Against:* needs process spawning plus non-blocking pipe reads on both Windows
(`CreateProcess` + `CreatePipe`, or `_popen`) and POSIX (`fork`/`execvp` +
`pipe`). More code, and platform-specific.

### C — keep in-process, add a watchdog

Rejected. There is no safe way to interrupt the VM mid-execution from another
thread, so the watchdog could only kill the whole process — losing the user's
unsaved buffer.

**Recommendation: B.** It is the design F5 should have had. A is a patch that
leaves obi hostage to user code, which is the actual complaint.

## Implementation shape

1. `core/repl/editor.cpp` — replace the `run_program` lambda's in-process
   `DoExecute()` with: compile to a temp `.obe`, spawn `obr`, return a handle.
   **If the REPL already shells out to `obc`/`obr` anywhere, reuse that path**
   rather than writing a second one.
2. `core/repl/tui_editor.h` — `DoRun()` becomes non-blocking: start the child,
   then loop `{ timed key read; drain available pipe output into the pane;
   repaint }` until exit or **Esc**, which kills the child.
3. Drop `IoCapture` from the F5 path entirely. It stays valid for any
   genuinely in-process capture, but the whole point of B is that it is no
   longer needed here.

**Open dependency:** a responsive loop requires a **timed** key read. If
`core/repl/term.h` only offers a blocking read, that must be added first —
otherwise the loop blocks on input and we are back to a frozen UI. This is the
one thing that could change the shape of the fix, which is why it is being
verified before implementation.

## Cross-platform status

**The responsive loop is free on both platforms.** Windows polls with
`WaitForSingleObject(in_handle, 100)` (`term.h:287`); POSIX gets the same
100 ms tick from `raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 1` (`term.h:168-169`),
so `read()` returns empty instead of blocking. **No terminal-layer change is
needed on either platform.**

**The subprocess half must be written twice**, behind one `#ifdef _WIN32` —
the same shape `io_capture.h` already uses for redirection:

| | Windows | POSIX |
| --- | --- | --- |
| spawn | `CreateProcess` | `fork` + `execvp` |
| pipes | `CreatePipe` | `pipe` |
| non-blocking drain | `PeekNamedPipe` | `O_NONBLOCK` + `read` |
| cancel | `TerminateProcess` | `kill` |

**Reuse the existing POSIX precedent:** `core/utils/updater/obu.cpp` already
runs argv vectors via `fork`/`execvp` with no shell (a deliberate injection
defence). Follow that shape rather than inventing a second pattern — and keep
the no-shell property here too, since the path being executed comes from a
user buffer.

Only the Windows half can be tested on the current box; the POSIX half needs a
Linux or macOS terminal.

## What must not happen

- The user's buffer being lost because a runaway program forced a process kill.
- Captured output containing screen escape sequences (the symptom of pointing
  a global redirect at a live UI).
- A "cancel" that only detaches, leaving the program running and the fds held.
- Shipping this without interactive testing. F5 cannot be exercised from a
  non-interactive shell, so it needs a human at a terminal — an automated green
  build proves nothing here.

## Testing

Automated coverage is not really available: `/e` is an interactive full-screen
mode. The honest plan is a manual matrix, run by a person:

| case | expectation |
| --- | --- |
| `"hi"->PrintLine();` | output in the pane, editor still responsive |
| a compile error | diagnostics in the pane, F8 jumps to the line |
| `while(true) {};` | **Esc cancels**, obi survives, buffer intact |
| a program printing a lot | pane scrolls, no truncation, no escape-sequence garbage |
| run twice in a row | second run works; no leaked handles or stale output |

The third row is the actual bug being fixed and must be tested explicitly.
