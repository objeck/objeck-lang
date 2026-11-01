# Error reporting in the libraries: what exists, and one rule to adopt

**Status: a proposal for the maintainer.** The first half records, with file and line, the
ways the libraries report failure today. The second half proposes a single rule for new
code. Nothing here changes behaviour, and no library has been changed to follow it.

Lines refer to `core/compiler/lib_src/` as of this writing.

## What is in use

### 1. A process-wide "last error" string: OpenAI, Gemini, Ollama, inference

The failing call returns `Nil` or `false` and leaves the reason in a class variable that the
next call overwrites.

| Library | Where it is kept | Notes |
|---|---|---|
| OpenAI | `@last_message : static` (`openai.obs:6337`), `SetLastError` (`:6370`), `GetLastError` (`:6470`) | returns the text `"<none>"` when nothing failed. `ParseBody` (`:6429`) records the reason for every parse failure |
| Gemini | two copies: `OAuthEndPoint` (`gemini.obs:15`, `GetLastError` `:97`) and `EndPoint` (`:2400`, `GetLastError` `:2523`) | returns `Nil` when nothing failed. A call through the other class does not see the first one's error |
| Ollama | `@last_error : static` (`ollama.obs:17`), `GetLastError` (`:26`) | returns `Nil` when nothing failed; `Content` (`:101`) turns "no response" into a recorded reason |
| Inference | `EndPoint->SetLastError` calls in `inference.obs` (21 of them; the first is `:50`) | separate from `API.Models.EndPoint`, as its header says |
| Models, MCP server | `models.obs:351`, `net_server.obs:3121` (`McpServer->GetLastError`) | the same shape again; ONNX has its own |

Because the string is shared by every thread, two threads that fail at once can read each
other's message. The three AI libraries also differ on the "no error" value (`"<none>"` versus
`Nil`), and until recently several legacy methods printed `### Error: ... ###` with
`->ErrorLine()` and returned `Nil` without recording anything (`openai.obs:84`, `:172`;
`gemini.obs` has 26 such lines). A caller of those cannot ask why.

### 2. An instance error: ML, CSV, JSON, XML, NLP, Query columns

The object remembers why *its* last call failed; a failed call leaves the object usable.

- ML models: `GetError()` at `ml_core.obs:2360`, `ml_embed.obs:176`, `ml_outlier.obs` (several
  classes, `:889` and onwards). Returns `""` when the last call worked, per its comment.
- `json.obs:510`, `xml.obs:283`, `csv.obs:691`: `GetError()` after a parse.
- `ml_svm.obs:1813` and `:2472`, `odbc.obs:306`, `:1042`, `:1815`: `GetLastError()` per object.
- `IsOk()` after construction, with the reason held for the same object: `rss.obs:93`,
  `regex.obs:116`.

This is safe with one thread per object. The "no error" value is `""` in ML and `Nil` in most
others.

### 3. A per-thread error: Query tables, sockets

`GetError()` answers for the calling thread's last call on the object, so several threads can
share a `Table` or `Catalog`.

- `ThreadErrors` (`query.obs:1440`), a map keyed by `Thread->GetCurrentId()`, absent when no
  thread has failed so the success path costs one field read. `Table->GetError()` is at
  `query.obs:4176`, `Catalog->GetError()` at `:18556`; design in `docs/QUERY_DESIGN.md`, "Errors per thread".
- Sockets: `TCPSocket->GetLastError()` (`lang.obs:12572`) and its TLS, UDP, DTLS and server
  twins (`:12873`, `:13093`, `:13184`, `:13304`, `:13522`, `:13739`, `:13864`) keep the reason per
  thread and return `Nil` when the thread's latest socket call worked.

This is the convention that is correct under threads, and the one the runtime's own sockets use.

### 4. Raising: `Runtime->Raise`, typed `try`/`on`

- `System.Error` and its subclasses (`lang.obs:8150`, `NilError` `:8182`, `IndexError` `:8195`,
  `DivideByZeroError` `:8208`, `CastError` `:8221`, `RuntimeError` `:8235`),
  `Runtime->Raise(e)` (`:8318`) and `Runtime->GetLastError()` (`:8332`). A caller recovers with
  `try { } on(e : T) { } finally { }`. Design and status: `docs/ERROR_HANDLING_DESIGN.md`
  (phases 1 to 4 shipped).
- `ml_lab.obs` is the one library that uses it: `LabError from Error` (`:20`), raised from
  `Runtime->Raise(LabError->New(...))` for input it cannot act on (`:453`, `:784`, `:1033`,
  `:2012`, `:2259`, `:3021`). An uncaught one ends the program with its message.

### 5. Return value only

Many small methods (`Vector->Set`, `Hash->Remove`) say only whether they worked. That is fine when there is exactly one way to fail.

## What goes wrong with the mix

- A caller has to learn which of five mechanisms a library uses, and which of three "no error"
  values it returns (`"<none>"`, `Nil`, `""`).
- The process-wide string is racy, and was the source of the unrecorded failures: a library
  with no obligation to set it tends not to.
- Only conventions 3 and 4 are safe under threads, and only 4 can be ignored by mistake without
  anyone noticing: a `Nil` return that the caller then dereferences is the failure the library
  sweep for this document found in the OpenAI, Gemini, `misc` and `net` libraries.
- `ml_lab` raises where `ml`, which it builds on, returns a value and sets `GetError()`; a
  program using both handles two styles in one experiment.

## Proposed rule

For new code, and for existing code the next time it is touched:

1. **A failure the caller should expect (I/O, network, parsing untrusted input, a missing
   record): return the failure value and keep the reason per thread.** The failure value is
   `Nil` for an object or a string, `false` for an action, and `-1` for a count or index. The
   reason is read with `GetError()`; it returns `Nil` when the calling thread's last call
   worked. Use the `ThreadErrors` shape in `query.obs:1440`, or the sockets' runtime-kept error,
   rather than a class variable. Never print and return.
2. **A failure that means the program is wrong (a precondition the caller broke: an empty
   training set, mismatched lengths, a fraction outside 0 to 1): `Runtime->Raise` an error
   that subclasses `System.Error`.** Name it for the library (`LabError`), and document it on
   every function that can raise it. This is what `ml_lab` does today.
3. **New error classes subclass `System.Error`**, so a caller can name one `on(e : T)` clause
   for a whole library, and `finally` still runs.
4. **Do not mix the two for one condition.** A function that can fail for a reason in rule 1
   does not also raise for it; a function that raises (rule 2) never returns `Nil` to say the
   same thing.
5. **A doc comment names the failure value and where the reason is read**, the way Ollama's
   does (`@return ... or Nil with the last error set`).

### What adopting it would touch

Nothing in this change. When the maintainer decides:

- The OpenAI, Gemini and Ollama `GetLastError` become thin readers of a per-thread map (the
  `static` name stays, as a function, so callers do not change), and settle on `Nil`.
- ML's `GetError()` returns `Nil` instead of `""` (a compatibility decision for callers that compare against `""`).
- The `->ErrorLine()` lines that stand in for a recorded reason are replaced by `SetLastError`.
- New libraries follow rules 1 to 5 from the start.

Open questions for the maintainer: whether the `static` last-error functions are public API
that a deprecation period must cover, and whether `ml` should raise like `ml_lab` or
`ml_lab` should return like `ml`.
