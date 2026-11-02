# Documentation coverage of the standard libraries

Measured by `tools/cicd/doc_coverage.py` against `core/compiler/lib_src/` (last changed in `b5de3d6736`).
This is a measurement only: no library source was edited. Writing the missing comments is a
change to `core/compiler/lib_src/*.obs`, which is reviewed apart from this documentation work.
(A comment-only edit does not change the compiled library: `obc -tar lib -opt s3` produced a
byte-identical `.obl` for a sample class with and without doc comments, because line numbers
are written to an `.obl` only with `--debug`, see `IntermediateInstruction::Write` in
`core/compiler/emit.cpp`. Confirm it again with a rebuild when the comments are written.)

## How it was measured

A declaration counts as documented when only whitespace separates it from the end of a
`#~ ... ~#` comment, which is how `code_doc` (`core/lib/code_doc/doc_parser.obs`) attaches a
comment to the declaration after it. Comments, string literals and character literals are
blanked first, so a brace or keyword inside one is not seen. The file header `#~~ ... ~~#` is
not a doc comment.

What is public follows the compiler (`ParseClass` and `ParseMethod` in
`core/compiler/parser.cpp`):

- a class or interface is public unless declared `class : private : Name`;
- `function` is public unless declared `function : private :`;
- `method` is private unless declared `method : public :`;
- `New` is public unless declared `New : private (...)`;
- enums and consts count, except inside a private class;
- nothing inside a private class counts, as `code_doc` lists no private class.

A library is one `.obl`: the files `core/compiler/build_libs.sh` hands to one `obc` call
(`lang` is `lang.obs`, built by the bootstrap compiler). Fields, enum items and parameters are
not counted. The script does not judge whether a comment is accurate or complete.

## By library

`doc/total` per kind; "all" is every public declaration in the library. Sorted by the number
of undocumented declarations, most first.

| Library | Classes | Enums | Methods | Functions | Constructors | All | Missing |
|---|---|---|---|---|---|---|---|
| `lang` | 78/80 | 5/5 | 494/557 | 384/444 | 91/93 | 1052/1179 (89%) | 127 |
| `ml` | 65/74 | 4/4 | 494/540 | 118/151 | 72/86 | 753/855 (88%) | 102 |
| `query` | 25/29 | 6/6 | 344/399 | 29/55 | 38/47 | 442/536 (82%) | 94 |
| `sdl_gl` | 29/29 | 12/12 | 345/385 | 131/132 | 34/38 | 551/596 (92%) | 45 |
| `openai` | 31/31 | 4/4 | 138/146 | 118/131 | 9/24 | 300/336 (89%) | 36 |
| `sdl2` | 50/50 | 35/35 | 416/416 | 138/140 | 22/50 | 661/691 (96%) | 30 |
| `gemini` | 25/25 | 1/1 | 110/114 | 61/74 | 8/20 | 205/234 (88%) | 29 |
| `ml_lab` | 14/20 | - | 77/77 | 21/44 | 11/11 | 123/152 (81%) | 29 |
| `concurrent` | 6/6 | - | 22/23 | 0/20 | 3/5 | 31/54 (57%) | 23 |
| `net` | 12/13 | 2/2 | 125/128 | 52/63 | 13/16 | 204/222 (92%) | 18 |
| `onnx` | 25/32 | 1/1 | 102/112 | 2/3 | 20/20 | 150/168 (89%) | 18 |
| `query_vision` | 5/5 | - | 24/24 | 3/20 | 5/5 | 37/54 (69%) | 17 |
| `sdl_game` | 10/10 | - | 181/184 | 12/12 | 7/17 | 210/223 (94%) | 13 |
| `ai_hub` | 1/1 | - | 13/13 | 0/12 | 2/2 | 16/28 (57%) | 12 |
| `nlp` | 11/11 | - | 63/64 | 24/33 | 11/11 | 109/119 (92%) | 10 |
| `ai` | 19/19 | - | 40/47 | 4/4 | 9/11 | 72/81 (89%) | 9 |
| `opencv` | 7/7 | 12/12 | 53/58 | 6/6 | 5/9 | 83/92 (90%) | 9 |
| `inference` | 4/4 | 1/1 | 12/16 | 3/6 | 4/5 | 24/32 (75%) | 8 |
| `ollama` | 11/11 | - | 85/85 | 35/36 | 4/11 | 135/143 (94%) | 8 |
| `gen_collect` | 29/29 | 3/3 | 393/396 | 5/5 | 40/44 | 470/477 (99%) | 7 |
| `json` | 5/6 | 3/3 | 66/69 | 7/7 | 25/27 | 106/112 (95%) | 6 |
| `query_server` | 1/2 | - | 8/9 | 0/3 | 1/2 | 10/16 (62%) | 6 |
| `odbc` | 6/6 | 1/1 | 92/93 | - | 6/10 | 105/110 (95%) | 5 |
| `web_server` | 4/4 | 1/1 | 16/16 | 4/6 | 2/4 | 27/31 (87%) | 4 |
| `csv` | 3/3 | - | 50/51 | 2/3 | 6/7 | 61/64 (95%) | 3 |
| `models` | 4/4 | - | 17/19 | 2/2 | 3/4 | 26/29 (90%) | 3 |
| `query_ml` | 2/2 | - | 23/23 | 0/3 | 3/3 | 28/31 (90%) | 3 |
| `rss` | 3/3 | - | 10/10 | - | 1/4 | 14/17 (82%) | 3 |
| `diags` | 3/3 | 1/1 | 35/35 | 2/2 | 0/2 | 41/43 (95%) | 2 |
| `net_h2` | 1/1 | - | 9/9 | 13/15 | 1/1 | 24/26 (92%) | 2 |
| `net_quic` | 1/1 | - | 9/9 | 13/15 | 1/1 | 24/26 (92%) | 2 |
| `net_server` | 22/23 | 3/3 | 135/135 | 25/26 | 37/37 | 222/224 (99%) | 2 |
| `json_stream` | 1/1 | 1/1 | 11/11 | 0/1 | 1/1 | 14/15 (93%) | 1 |
| `misc` | 4/4 | - | 1/1 | 7/8 | 1/1 | 13/14 (93%) | 1 |
| `nlp_onnx` | 3/3 | 2/2 | 41/41 | 3/4 | 5/5 | 54/55 (98%) | 1 |
| `query_odbc` | 1/1 | 1/1 | 12/12 | 3/4 | 2/2 | 19/20 (95%) | 1 |
| `cipher` | 3/3 | - | - | 10/10 | - | 13/13 (100%) | 0 |
| `json_rpc` | 2/2 | - | 5/5 | - | 2/2 | 9/9 (100%) | 0 |
| `lame` | 1/1 | - | - | 1/1 | - | 2/2 (100%) | 0 |
| `math` | 3/3 | - | 65/65 | 14/14 | 3/3 | 85/85 (100%) | 0 |
| `regex` | 2/2 | - | 15/15 | - | 3/3 | 20/20 (100%) | 0 |
| `xml` | 4/4 | 1/1 | 47/47 | 4/4 | 9/9 | 65/65 (100%) | 0 |
| **all** | 536/568 | 100/100 | 4198/4459 | 1256/1519 | 520/653 | **6610/7299 (91%)** | **689** |

## The ten worst gaps

The ten classes (or bundle-level declarations) with the most undocumented public members,
which is where documentation work pays off first.

| # | Library | Class | File | Missing | Of which |
|---|---|---|---|---|---|
| 1 | `lang` | `ConsoleIO` | `lang.obs` | 40 | 39 method, 1 function |
| 2 | `concurrent` | `Monitor` | `concurrent.obs` | 20 | 20 function |
| 3 | `query` | `Term` | `query.obs` | 15 | 14 method, 1 constructor |
| 4 | `ai_hub` | `HubClient` | `ai_hub.obs` | 12 | 12 function |
| 5 | `lang` | `Char` | `lang.obs` | 12 | 12 function |
| 6 | `query` | `OtabFile` | `query.obs` | 12 | 11 method, 1 function |
| 7 | `gemini` | `Model` | `gemini.obs` | 11 | 11 function |
| 8 | `net` | `WebCommon` | `net_common.obs` | 11 | 11 function |
| 9 | `openai` | `Models` | `openai.obs` | 10 | 10 function |
| 10 | `query_vision` | `ImageHeader` | `query_vision.obs` | 10 | 10 function |

## Reproduce

```
python tools/cicd/doc_coverage.py                       # print this report
python tools/cicd/doc_coverage.py --write docs/DOC_COVERAGE.md
python tools/cicd/doc_coverage.py --json                # raw numbers, per library and per class
```
