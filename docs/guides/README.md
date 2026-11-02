# Getting-started guides

Short, task-oriented guides, each built around a complete program you can compile and run. The
programs are in [`programs/examples/guides/`](../../programs/examples/guides/); every one was
compiled and run, and the output printed in a guide is what it printed. If you have not yet written a
first program, start with the [Quick Start](https://www.objeck.org/getting_started.html), then
the 32 numbered examples in [`programs/deploy`](../../programs/deploy/README.md) that ship with
every release.

| Guide | Libraries | What you learn |
|---|---|---|
| [What is new in v2026.10.3](whats_new_10_3.md) | | what to do when you upgrade, and what changed for users |
| [Query.Structured](query.md) | `query` and friends | tables, SQL, joins, prepared statements, saving to disk |
| [HTTP client and web server](http.md) | `net`, `net_server` | a server, a client, connection reuse |
| [JSON, XML and regular expressions](data_and_text.md) | `json`, `xml`, `regex` | parsing, and what happens on bad input |
| [Collections](collections.md) | none | the five collections you reach for most |
| [Game AI with System.AI](ai.md) | `ai` | minimax and Monte Carlo tree search |
| [Editor setup and the language server](language_server.md) | | VS Code and the other editors, debugging |

## The programs

| Program | Compile with `-lib` |
|---|---|
| [`query_start.obs`](../../programs/examples/guides/query_start.obs) | `query,misc,net,json,regex,xml,csv,cipher` |
| [`query_join.obs`](../../programs/examples/guides/query_join.obs) | `query,misc,net,json,regex,xml,csv,cipher` |
| [`query_otab.obs`](../../programs/examples/guides/query_otab.obs) | `query,misc,net,json,regex,xml,csv,cipher` |
| [`http_keepalive.obs`](../../programs/examples/guides/http_keepalive.obs) | `net,net_server,json,cipher` |
| [`data_basics.obs`](../../programs/examples/guides/data_basics.obs) | `json,xml,regex` |
| [`data_bad_input.obs`](../../programs/examples/guides/data_bad_input.obs) | `json,xml,regex` |
| [`collections_tour.obs`](../../programs/examples/guides/collections_tour.obs) | none |
| [`ai_tictactoe.obs`](../../programs/examples/guides/ai_tictactoe.obs) | `ai` |

Each file opens with a comment giving its exact compile and run lines. To build one:

```text
cd programs/examples/guides
obc -src collections_tour.obs
obr collections_tour.obe
```

`obc` does not pull in a library's own dependencies, which is why a `-lib` list can name libraries
the program never uses directly.

## More

- [`docs/EXAMPLES.md`](../EXAMPLES.md): networking and AI snippets.
- [`docs/FEATURES.md`](../FEATURES.md): language features with examples.
- [AI & ML developer guide](https://www.objeck.org/ai_guide.html) and [`docs/AI.md`](../AI.md).
- [`docs/COLLECTIONS_GUIDE.md`](../COLLECTIONS_GUIDE.md) and
  [`docs/ERROR_CONVENTIONS.md`](../ERROR_CONVENTIONS.md).
