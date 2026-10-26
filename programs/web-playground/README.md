# Objeck Web Playground

The browser playground at [playground.objeck.org](https://playground.objeck.org): a FastAPI backend that compiles and runs a program in a locked-down container, a small static frontend, and a set of demo programs.

```
frontend/     index.html, css/, js/        no build step; Monaco from a pinned CDN URL, text-area fallback
backend/      FastAPI app + tests          /api/run, /api/share, /api/demos, /api/libs, /api/health
demos/        *.obs + index.json           every demo is compiled and run by the tests
docker/       sandbox image                Dockerfile.sandbox, build-sandbox.sh
deploy/       systemd, Caddy, update.sh    what runs on the VPS
tools/        gen_libmap.py                regenerates the library map
```

## Libraries are not the user's problem

A program names the libraries it needs with `use` statements, and the server turns those into the right `obc -lib` list. Nothing in the UI asks anyone to tick a box.

```
use Query.Structured, Collection;      ->   obc -lib cipher,net,regex,csv,xml,json,misc,query
```

How it works (`backend/app/services/libmap.py`):

1. `source_scan.py` blanks comments (string-aware, so a `#` inside a string is not a comment).
2. `use` statements and fully qualified names (`Cipher.Hash->SHA256(...)`) give the **bundles** the program names.
3. Each bundle maps to the library that declares it. Bundles that several libraries share (`System.Time` is in `lang` and `misc`; `System.Concurrency` in `lang` and `concurrent`) pull in the extra library only if the program names one of the classes it adds (`DateUtility`, `Semaphore`...).
4. The libraries' **dependencies are added**. `obc` loads exactly what it is given and does not chase a library's own dependencies (`Unable to resolve external library class: 'Data.CSV.CsvTable'. Add it with '-lib csv'`), so `Query.Structured` needs csv, xml, regex, json, misc, net and cipher as well. `lang` and `gen_collect` are always supplied by the compiler and are never passed.
5. Each library is checked against the policy (below). A refused one stops the run before a container starts: `Web.HTTP isn't available in the playground (line 1): the sandbox has no network access.`
6. If the compile still fails with `Undefined class: 'JsonParser'`, the response carries a hint (`'JsonParser' is in bundle Data.JSON. Add use Data.JSON; at the top of your program.`), and the compiler's `program.obs:(line,col): message` lines come back parsed as `diagnostics` so the UI can jump to the line.

### Source of truth

`backend/app/services/libmap_data.json` is **generated** by `tools/gen_libmap.py` from the compiler's own files: the `bundle X { ... }` blocks in `core/compiler/lib_src/*.obs` (which library declares which bundle and public class) and the `-lib` list each library is built with in `core/compiler/build_libs.sh` (its dependencies). Nothing in the backend hard-codes a bundle name. Run `python tools/gen_libmap.py` after adding a library, a bundle or a dependency; `tests/test_policy.py` fails (and the *Playground Tests* workflow goes red) until you do.

### Policy: what the sandbox permits

Libraries are classified in `backend/app/config.py`:

| set | meaning | now |
|---|---|---|
| `allowed_libs` | a program may use them | gen_collect (default), json, xml, regex, cipher, csv, query, query_ml, ml, ml_lab, nlp, ai, math, misc, diags, concurrent |
| `dependency_libs` | linked only because an allowed library needs them | net (query and misc are built against it) |
| `blocked_libs` | refused, with a reason | net_h2, net_quic, net_server, web_server, json_rpc, json_stream, rss, odbc, query_odbc, query_server, query_vision, sdl*, lame, openai, gemini, ollama, inference, models, onnx, opencv |

A library in none of the sets is blocked too, and `test_policy.py` fails until someone decides, so a new `.obl` is never silently exposed or silently dropped.

Bundles that live inside `lang.obl` cannot be withheld by leaving a library out, so `libmap.LANG_BLOCKED_BUNDLES` refuses them by name: `System.IO.Filesystem` (File, Directory, Logger...), `System.IO.Net` (sockets, HTTP/2 and HTTP/3 sessions), `System.API` (DllProxy).

`validator.py` refuses individual calls and classes in bundles that *are* allowed: `Runtime->Command`, `CommandOutput`, `Execute`, `IsProcessAlive`, `Exit`, environment access, `Pipe`, the Query.Structured sources and stores that open a path or a URL (`CsvSource`, `FilesystemSource`, `RemoteSource`, `ArchiveSource`, `XlsxSource`, `TableStore`, `OtabFile`...), and SQL that names a file or URL (`select * from 'x.csv'`).

**The container is the security boundary; the validator is the courtesy layer in front of it.** The sandbox has no network, a read-only root, a 10 MB `noexec` tmpfs, all capabilities dropped, a pid cap, and a memory and CPU limit. The validator gives a clear message instead of an obscure failure and closes the common routes even if the container configuration regresses. What it cannot see is not blocked: `Catalog->SetFileAccess(false)` is per catalog, so a program cannot be forced to call it, and `Table->Save(path)` takes a plain string. Those can only reach the container's read-only image and its 10 MB tmpfs. A process-wide switch in `query.obl` would be the real fix.

### Optional manual override

`POST /api/run` still accepts `libs` (and shares still store it): extra libraries added on top of the detected ones. The UI hides this behind a collapsed *Advanced* disclosure. It can never loosen the policy, and share links made before libraries were inferred still load.

## API

| endpoint | |
|---|---|
| `POST /api/run` `{code, libs?, timeout?}` | `{success, output, error, compile_error, execution_time_ms, truncated}` plus `libs` (libraries used), `blocked`, `hint`, `diagnostics[{line, column, severity, message}]` |
| `POST /api/share` `{code, libs?}` / `GET /api/share/{id}` | unchanged; refuses code the playground would refuse to run |
| `GET /api/demos`, `GET /api/demos/{id}` | `libs` is computed from the demo's code |
| `GET /api/libs` | the libraries a program may use, with names and bundles (the Advanced list) |
| `GET /api/health` | `{status, version}` |

## Run it locally

No Docker needed: `local_dev` runs `obc` and `obr` directly.

```bash
cd programs/web-playground/backend
pip install -r requirements-dev.txt
export PLAYGROUND_LOCAL_DEV=true
export PLAYGROUND_OBC_PATH=/path/to/deploy/bin/obc
export PLAYGROUND_OBR_PATH=/path/to/deploy/bin/obr
export PLAYGROUND_OBJECK_LIB_PATH=/path/to/deploy/lib
uvicorn app.main:app --port 8000        # serves the frontend too: http://localhost:8000
```

On Windows use `set` and the `.exe` paths. `?editor=plain` forces the text-area editor; `?demo=<id>` opens an example.

The deploy tree is a built Objeck (`core/release/deploy-x64` on Windows, `core/release/deploy` elsewhere). Its `lib/` must hold the `.obl` files the policy allows and, for `cipher`, `ml` and `diags`, `lib/native/libobjk_*`.

## Tests

```bash
cd programs/web-playground/backend
python -m pytest                              # everything; toolchain tests skip without a deploy tree
OBJECK_DEPLOY=/path/to/deploy python -m pytest
OBJECK_DEPLOY=/path/to/deploy python tests/test_demos.py      # a table of every demo
```

| file | covers |
|---|---|
| `test_libmap.py` | inference from `use`: dotted bundles, lists, qualified names, comments, shared bundles, closures, refusals, hints |
| `test_validator.py` | the manual override, blocked calls and SQL, line numbers |
| `test_policy.py` | the library sets against the compiler's libraries, the sandbox image script, natives, and that `libmap_data.json` is current |
| `test_diagnostics.py` | obc output parsing, the container command and its exit codes |
| `test_api.py` | the HTTP contract with a stand-in sandbox |
| `test_toolchain.py` | against a real `obc`/`obr`: every allowed bundle compiles, compile errors, timeouts, natives |
| `test_demos.py` | every demo compiles and runs with libraries picked by the server |

## Deploying

The playground runs the **released** toolchain, not this checkout's build: `deploy/update.sh` downloads the Linux tarball for the version in `core/shared/version.h`. So:

1. Release first. The demos (Query indexes, `BitSet`, `query_ml`, typed errors) need the engine that is on `master`; `v2026.10.1` does not have `CreateIndex`, `BitSet` or `query_ml.obl`, and `build-sandbox.sh` refuses to build an image missing a library it ships.
2. `bash /opt/playground/repo/programs/web-playground/deploy/update.sh <version>` (see the `deploy-playground` skill). It pulls, installs the toolchain, updates the venv, **rebuilds the sandbox image**, restarts, and now also runs a Query + Cipher program through `/api/run`.
3. The image must be rebuilt whenever `allowed_libs` or `dependency_libs` change: `docker/build-sandbox.sh` ships `net`, `query_ml`, `ai`, `math`, `concurrent`, and the native modules `libobjk_crypto`, `libobjk_ml` and `libobjk_diags` (earlier images shipped no `lib/native`, so `cipher`, `ml` and `diags` compiled and then failed at run time). `test_policy.py` keeps the script and `config.py` in step.
4. `container_memory` is now `128m` (was `64m`): linking the Query.Structured closure peaks near 60 MB. Four runs at once is 512 MB.
