# Objeck REPL (obi)

Interactive shell for rapid prototyping and experimentation with Objeck. `obi` lets you write and run code immediately without creating source files, and doubles as a small line-oriented editor for the buffer it builds up.

> **New to Objeck?** Start `obi` and type `/t` for a short, hands-on guided tutorial that walks you through expressions, variables, formatting, collections, and control flow one step at a time.

## How it works

`obi` keeps an in-memory program — a `class Repl { function : Main(args : String[]) ~ Nil { ... } }` scaffold — and inserts the statements you type into `Main`. Each time you enter something, the buffer is recompiled and re-run, so the program always reflects everything you've added.

Two input styles control whether a line is *kept* or *evaluated once*:

- **End a line with `;`** — it becomes part of the program. The buffer re-runs, so persisted statements (including their side effects and output) run again on each subsequent entry.
- **Omit the `;`** — the line is evaluated as an expression, its value is printed, and it is **not** kept. Use this to inspect values without cluttering (or re-running) the program.

```ruby
$ obi
Objeck REPL (2026.6.x)
['/h' for help, omit ';' to print an expression]
---
> 40 + 2            # expression — printed once, not kept
42
> x := 10           # declaration — kept (a trailing ';' is added for you)
> x * 2             # expression using the variable
20
> "hi {$x}"->PrintLine();   # statement — kept, re-runs with the buffer
hi 10
```

> **Tip:** prefer `:=` for state you want to keep and `;`-less expressions for inspection. Because the whole buffer re-runs on each entry, a *persisted* statement with an external side effect (an API call, a file write, a counter) will fire again every time — keep those as one-shot expressions or run the finished program with `obc`/`obr`.

### Multi-line blocks

When a line leaves braces open, `obi` keeps prompting with `...` until they balance, so blocks and control flow can be entered naturally:

```ruby
> if(x > 5) {
... "big"->PrintLine();
... }
big
```

### Errors roll back

If a line you add fails to compile, the errors are shown and the line is **discarded** so the buffer stays runnable — you won't get stuck re-hitting the same error.

## Usage

```bash
obi                                 # start the interactive shell
obi --file hello.obs                # load a source file, run it, then drop into the shell
obi -f hello.obs --library mylib    # load with extra libraries
obi --inline 'Int->New(42)->PrintLine();' --quit   # run one statement and exit
```

### Command-line options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Show help |
| `--file`, `-f <files>` | Source files (comma-separated) |
| `--inline`, `-i <code>` | Inline source statements |
| `--library`, `-l <libs>` | Linked libraries (comma-separated) |
| `--optimize`, `-o <level>` | Optimization level `s0`–`s3` |
| `--quit`, `-q` | Exit after executing the supplied code |

## Interactive commands

All commands start with `/`.

| Command | Description |
|---------|-------------|
| `/h` | Help |
| `/t` | Guided step-by-step tutorial (`/t <n>` to jump to a step) |
| `/q` | Quit |
| `/x` | Reset the buffer (and libraries/optimization) |
| `/c` | Clear the screen |
| `/l` | List the program |
| `/v` | List defined variables |
| `/g <n>` | Move the cursor to line *n* |
| `/i` | Insert a line below the cursor |
| `/m` | Insert multiple lines (end with `/m`) |
| `/r [n]` | Replace line *n* (or the current line) |
| `/d <n>` | Delete line *n* (or a range, e.g. `2-4`) |
| `/a` | Set command-line arguments |
| `/u` | Set library `use` statements |
| `/p` | Set the compiler optimization level |
| `/o <file>` | Open a source file |
| `/s <file>.obs` | Save the buffer to a `.obs` file |

## Output & color

`obi` colorizes the prompt, status lines, and errors when writing to a terminal. Color is automatically disabled when output is piped or redirected, and honors the [`NO_COLOR`](https://no-color.org) convention.

## Architecture

- **Document**: the in-memory buffer (read-only scaffold lines + your read/write lines)
- **Editor**: the command loop, input classification (expression vs. statement vs. block), and multi-line handling
- **ObjeckLang**: shared compiler front-end — compiles the buffer in memory and executes it on the VM

## Limitations

- Each entry recompiles and re-runs the whole buffer; there is no persistent live VM state between entries (a variable's value is recomputed from its declaration each run).
- Arrow-key history and tab-completion are not yet available (input is plain line entry).
- Debug symbols are not generated; for multi-file programs and debugging, use `obc`/`obr`/`obd`.

## See Also

- [Main README](../../README.md) — project overview
- [Compiler](../compiler/README.md) — full compilation options
- [Virtual Machine](../vm/README.md) — VM architecture
- [API Documentation](https://www.objeck.org) — library reference
