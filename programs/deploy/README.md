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
| [`hello_0.obs`](hello_0.obs) | none | a class, Main, and one line of output |
| [`https_1.obs`](https_1.obs) | `-lib gen_collect,net,json,cipher` | fetch a page over HTTPS |
| [`xml_2.obs`](xml_2.obs) | `-lib xml,gen_collect` | parse an XML document and walk it |
| [`json_3.obs`](json_3.obs) | `-lib json,net,cipher` | fetch JSON over the network and parse it |
| [`regex_4.obs`](regex_4.obs) | `-lib regex` | match and extract with regular expressions |
| [`functions_5.obs`](functions_5.obs) | none | functions, parameters and return values |
| [`threads_6.obs`](threads_6.obs) | none | the Mandelbrot set, drawn by several threads |
| [`encrypt_7.obs`](encrypt_7.obs) | `-lib cipher` | hashing and symmetric encryption |
| [`http_xml_regex_8.obs`](http_xml_regex_8.obs) | `-lib net,regex,gen_collect,json,cipher` | fetch, then parse with XML and a regex together |
| [`http_regex_9.obs`](http_regex_9.obs) | `-lib net,json,regex,cipher` | pull fields out of a fetched page |
| [`calc_life_10.obs`](calc_life_10.obs) | none | a genetic algorithm evolving toward a target |
| [`odbc_select_11.obs`](odbc_select_11.obs) | `-lib odbc` | query a database through ODBC |
| [`fs_query_12.obs`](fs_query_12.obs) | `-lib query,regex,misc,net,json,csv,cipher` | structured queries over the filesystem |
| [`2d_game_13.obs`](2d_game_13.obs) | `-lib gen_collect,sdl2,sdl_game,json` | a side-scrolling platformer drawn with SDL2 |
| [`serial_14.obs`](serial_14.obs) | `-lib gen_collect` | write objects to a file and read them back |
| [`rss_https_xml_15.obs`](rss_https_xml_15.obs) | `-lib net,rss,gen_collect,xml,cipher` | fetch and parse an RSS feed |
| [`tic_tac_toe_16.obs`](tic_tac_toe_16.obs) | none | tic-tac-toe in the terminal |
| [`lambda_17.obs`](lambda_17.obs) | `-lib gen_collect` | lambdas |
| [`first_class_18.obs`](first_class_18.obs) | `-lib gen_collect` | functions passed around as values |
| [`closure_19.obs`](closure_19.obs) | `-lib gen_collect` | closures that capture their environment |
| [`loops_27.obs`](loops_27.obs) | none | every loop form the language has |
| [`visitor_20.obs`](visitor_20.obs) | none | the visitor pattern over an expression tree |
| [`neural_21.obs`](neural_21.obs) | `-lib gen_collect,ml,csv` | a neural network trained from a CSV file |
| [`gemini_22.obs`](gemini_22.obs) | `-lib gemini,net,json,misc,cipher,net_server` | calling the Gemini API |
| [`json_stream_23.obs`](json_stream_23.obs) | `-lib json_stream` | reading large JSON as a stream |
| [`3d_gl_24.obs`](3d_gl_24.obs) | `-lib sdl2,sdl_gl` | a lit, shadowed, spinning scene in OpenGL |
| [`ai_search_25.obs`](ai_search_25.obs) | `-lib ai` | shortest-path search across a small map |
| [`ml_regression_26.obs`](ml_regression_26.obs) | `-lib ml,csv` | linear regression over a CSV dataset |
| [`records_28.obs`](records_28.obs) | `-lib gen_collect` | records: generated constructors and accessors, and readonly |

## Building one

With no libraries:

```
obc -src hello_0.obs
obr hello_0.obe
```

With them, naming the list from the table above:

```
obc -src json_stream_23.obs -lib json_stream
obr json_stream_23.obe
```

## Notes

- `2d_game_13.obs` and `3d_gl_24.obs` open a window and need SDL2; the rest
  run in a terminal.
- `odbc_select_11.obs` expects an ODBC datasource named `foo`.
- `gemini_22.obs` needs an API key, and the network examples need a
  connection.
- `neural_21.obs` trains and stores a model on its first run, then tests it
  when run again with `brun`.

---

*Generated from `welcome.obs` by `tools/cicd/gen_examples_readme.py`.
Edit the arrays there, not this file, and regenerate.*
