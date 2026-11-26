# Objeck example programs

The programs in this folder ship with every Objeck release, in the
`examples/` directory of the distribution. `welcome.obs` prints this same
index at the terminal:

```
obc -src welcome.obs -lib json,net,cipher
obr welcome.obe
```

Each program is self-contained and opens with a comment giving its exact
compile and run lines. Run them from this folder (or from `examples/` in an
install): two of them read files from `data/`, which ships alongside.

| Example | Libraries | What it shows |
|---|---|---|
| [`hello_0.obs`](hello_0.obs) | none | a class, a function, Main, and one line of output |
| [`values_1.obs`](values_1.obs) | none | types, literals, variables, interpolation and conversions |
| [`control_flow_2.obs`](control_flow_2.obs) | none | if, select, while, do/while, for, each, break and continue |
| [`functions_3.obs`](functions_3.obs) | none | functions, default and named arguments, ref parameters, recursion |
| [`classes_4.obs`](classes_4.obs) | none | classes, inheritance, virtual methods, interfaces and Stringify |
| [`records_enums_5.obs`](records_enums_5.obs) | none | records, readonly records, enums and consts |
| [`errors_6.obs`](errors_6.obs) | none | custom errors, try/on/finally, built-in errors and ?-> recovery |
| [`generics_7.obs`](generics_7.obs) | none | generic classes, bounded generic functions, Vector and Map |
| [`closures_8.obs`](closures_8.obs) | none | functions as values, lambdas and closures |
| [`arrays_9.obs`](arrays_9.obs) | none | arrays: literals, 2-D, jagged, sorting, Fill and Copy |
| [`strings_10.obs`](strings_10.obs) | `-lib cipher` | string methods, comparison, formatting, SHA-256 and Base64 |
| [`collections_11.obs`](collections_11.obs) | none | Vector, List, Map, Hash, Set, Stack, Queue, unboxed collections |
| [`threads_12.obs`](threads_12.obs) | `-lib concurrent` | threads, critical sections and a CountDownLatch |
| [`files_13.obs`](files_13.obs) | none | write, read, append and list files and folders |
| [`json_14.obs`](json_14.obs) | `-lib json,json_stream` | parse, walk, query and build JSON; stream a file |
| [`xml_15.obs`](xml_15.obs) | `-lib xml` | parse XML, walk elements and attributes, build XML |
| [`csv_16.obs`](csv_16.obs) | `-lib csv` | read CSV by row and column, quoted fields, write CSV |
| [`regex_17.obs`](regex_17.obs) | `-lib regex` | match, find all, capture groups, replace and split |
| [`sql_tables_18.obs`](sql_tables_18.obs) | `-lib query,misc,net,json,regex,xml,csv,cipher` | SQL over in-memory tables: group by, indexes, Explain |
| [`sql_files_19.obs`](sql_files_19.obs) | `-lib query,misc,net,json,regex,xml,csv,cipher` | SQL over CSV and JSON files and a folder, with a join |
| [`serialize_20.obs`](serialize_20.obs) | none | save objects to a file and load them back |
| [`odbc_21.obs`](odbc_21.obs) | `-lib odbc` | a database through ODBC: prepared inserts and queries |
| [`http_client_22.obs`](http_client_22.obs) | `-lib net,json,cipher` | HTTPS GET: status, headers, JSON and failure handling |
| [`web_server_23.obs`](web_server_23.obs) | `-lib net,net_server,json,cipher` | a two-route web server that tests itself and stops |
| [`rss_reader_24.obs`](rss_reader_24.obs) | `-lib rss,xml,net,cipher` | fetch an RSS feed and list the latest headlines |
| [`ml_regression_25.obs`](ml_regression_25.obs) | `-lib csv,ml` | linear and logistic regression with a test split |
| [`ml_classify_26.obs`](ml_classify_26.obs) | `-lib csv,ml` | random forest, cross-validation and a confusion matrix |
| [`ml_outliers_27.obs`](ml_outliers_27.obs) | `-lib csv,ml` | anomaly detection with isolation forest and LOF |
| [`nlp_28.obs`](nlp_28.obs) | `-lib nlp` | tokenizing, stemming, sentiment and TF-IDF search |
| [`llm_29.obs`](llm_29.obs) | `-lib gemini,net,net_server,json,misc,cipher` | ask a large language model (Gemini) a question |
| [`ai_search_30.obs`](ai_search_30.obs) | `-lib ai` | A* pathfinding and minimax |
| [`game_31.obs`](game_31.obs) | `-lib sdl2,sdl_game,json` | an SDL2 game: collect gems, dodge spiders, keep a best score |
| [`mcp_32.obs`](mcp_32.obs) | `-lib mcp,json,net,net_server,cipher,misc,gen_collect` | an MCP server and a client calling its tools, in one program |
| [`agent_33.obs`](agent_33.obs) | `-lib agent,json,net,cipher,misc` | a tool-using agent loop: model asks, tools answer, with a trace |
| [`agent_control_34.obs`](agent_control_34.obs) | `-lib agent,json,net,cipher,misc` | stopping an agent: budgets, cancellation and a hung tool |
| [`actors_35.obs`](actors_35.obs) | `-lib gen_collect,concurrent,actor` | actors and a supervisor that restarts a failing worker |
| [`rag_36.obs`](rag_36.obs) | `-lib rag,ml_vector,gen_collect` | retrieval-augmented answers from documents, with citations |
| [`structured_37.obs`](structured_37.obs) | `-lib llm_util,json,regex,gen_collect` | JSON out of a model: templates, schema checks, retry and repair |
| [`telemetry_38.obs`](telemetry_38.obs) | `-lib telemetry,gen_collect,json` | timing a computation: p50, p95, p99 and saved profiles |
| [`forecast_39.obs`](forecast_39.obs) | `-lib ml_timeseries,gen_collect` | forecasting a seasonal series: Holt-Winters against a benchmark |
| [`mcp_declare_40.obs`](mcp_declare_40.obs) | `-lib gen_collect,json,net,net_server,cipher,misc,mcp` | MCP: a function marked @tool is a tool; schema, hints and arguments are written for you |
| [`mcp_stub_41.obs`](mcp_stub_41.obs) | `-lib gen_collect,json,net,net_server,cipher,misc,mcp` | MCP: call a server through a generated typed client |
| [`mcp_agent_42.obs`](mcp_agent_42.obs) | `-lib gen_collect,json,net,net_server,cipher,misc,mcp,agent,mcp_agent` | MCP: tools as agent tools, with approval by effect |
| [`mcp_resources_43.obs`](mcp_resources_43.obs) | `-lib gen_collect,json,net,net_server,cipher,misc,mcp` | MCP: @resource and @prompt functions next to a @tool, read through a client |
| [`mcp_serve_44.obs`](mcp_serve_44.obs) | `-lib gen_collect,json,net,net_server,cipher,misc,mcp` | MCP: obr --mcp and --mcp-http serve the tagged functions with no server code |
| [`json_class_46.obs`](json_class_46.obs) | `-lib gen_collect,json,regex,llm_util,net,cipher,misc,agent,agent_json` | class Weather : json: a model's answer as a typed object, with the schema and the reader written for you |

## Building all of them

`build_examples.py` beside this file reads the **Libraries** column above and
compiles each example with exactly what it names:

```
python build_examples.py              compile every example, report
python build_examples.py --only json  just the ones whose name matches
python build_examples.py --keep       leave the .obe files behind
```

It finds `obc` in `../bin` (an install) or the repo's deploy tree, and exits
non-zero if any example fails. CI runs it on every change, so the table above is
verified rather than merely written down: a wrong entry in the Libraries column
fails the build.

## Building one

With no libraries:

```
obc -src hello_0.obs
obr hello_0.obe
```

With them, naming the list from the table above:

```
obc -src json_14.obs -lib json,json_stream
obr json_14.obe
```

## Notes

- Read them in order: 0-13 teach the language, 14-21 work with data, 22-24
  are networking, 25-30 are AI and machine learning, 31 is a game, and 32-39 are
  AI engineering (MCP tools, agents, actors, retrieval, structured output,
  telemetry, forecasting).
- 32-39 run offline and print the same thing every time: a scripted model stands
  in for a real one, so they need no key. Swap in a real backend to use one.
- Each program's header gives its compile and run lines and what it prints.
- `obc` does not pull in a library's own dependencies, so some `-lib` lists name
  libraries the program never uses directly: `ml` needs `csv`, and `query` needs
  the rest of its list.
- `game_31.obs` opens a window and needs a display; the rest run in a terminal.
- `odbc_21.obs` needs an ODBC data source (it explains how to set one up when
  it cannot connect), and `llm_29.obs` needs a Gemini API key.
- `http_client_22.obs` and `rss_reader_24.obs` need an internet connection.

---

*Generated from `welcome.obs` by `tools/cicd/gen_examples_readme.py`.
Edit the arrays there, not this file, and regenerate.*
