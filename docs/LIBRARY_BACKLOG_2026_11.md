# Library backlog, November 2026

Defects found while auditing the libraries for Nil dereferences (the sweep in PR #1527, rows 6.1 to 6.3 of `ARCHITECTURE_ASSESSMENT_2026_11.md`) that are not fixed there because the fix is bigger than that change, or belongs to another layer. Each entry says what, where, how to reproduce it, a suggested fix and a rough size. All of them were reproduced on master with the Linux build; none is a regression.

Sizes: S is under half a day, M is one to three days, L is more. They include the tests.

## 1. A corrupt model file ends the program

**What.** Every machine-learning `Load(filename)` handles a file that is missing (it returns Nil) but not one that exists and is not a model. The file's bytes go to `System.IO.Deserializer->New(data)`, whose first statement is `Byte->UncompressZlib(buffer)`. For data that is not a zlib stream the intrinsic raises a runtime error, `Invalid, truncated or oversized zlib data`, and the program ends. A truncated download, a file from another tool and a model written by an older format all land here.

**Where.**
- The raise: `TrapProcessor::UncompressZlibBytes`, `core/vm/common.cpp` (the `TrapError(RECOVERED_OTHER, ...)` after `OutputStream::UncompressZlib` returns null).
- The constructor: `Deserializer->New`, `core/compiler/lib_src/lang.obs`.
- The 15 loaders: `ml_linear.obs` (6: 506, 849, 1227, 1658, 1928, 2201), `ml_tree.obs` (5: 222, 518, 911, 2074, 2352), `ml_bayes.obs` 282, `ml_cluster.obs` 831, `ml_core.obs` 1605, `ml_data.obs` 359. The other callers of `Deserializer->New` found under `programs/` are tests and examples that deserialize bytes they just serialized.

**Reproduce.** Write six bytes to a file and load it, for any of the ten loaders tried:

```
w := FileWriter->New("bad.model"); w->WriteString("nope!!"); w->Close();
model := LinearRegression->Load("bad.model");   # ends: Invalid, truncated or oversized zlib data
```

**Suggested fix.** Make the failure a value rather than a trap, in this order:
1. `Byte->UncompressZlib` keeps raising for a program that gives it garbage on purpose, so add a non-raising form (`TryUncompressZlib`, returning Nil) and leave the old one alone, or change the old one to return Nil and document it. The first is compatible.
2. `Deserializer->New` uses it; on Nil it keeps an empty buffer and records the failure, and a new `IsOk()` reports it. The Read methods return their zero value on an exhausted buffer already (check that, and add a bounds check if not).
3. Each loader does `if(<>deserializer->IsOk()) { return Nil; }` as its second line, the same line the missing-file case already has.

**Size.** M. `lang.obl` is built by the bootstrap compiler and committed, so the change is a runtime change plus a `lang.obl` rebuild on every platform; the 15 loader edits are one line each. The test is the six-byte file above for each of the 15 loaders, which a generated loop over them can cover.

## 2. Machine-learning Fit and Predict do not validate their arguments

**What.** About two dozen `Fit`, `Predict`, `Transform` and `Score` methods take a matrix or array with no check that it is not Nil, not empty, and the right shape for the model. A caller who gets the shape wrong (a feature matrix with a different column count at predict time than at fit time is the usual mistake, and the one a changed CSV produces) ends the program with `Index over bounds` or `Divide by zero` rather than a false or Nil result. Their siblings do check: `LogisticRegression->Fit(Float[,], Float[,])` and `Predict` test `X = Nil | y = Nil` and the row counts, and return false or Nil.

**Where.** Methods with no Nil check in their first six lines (a scan of every public `Fit*`/`Predict*`/`Transform*`/`Score*` that takes a matrix or array, so a check further down is missed and a non-checking helper called first is not; confirm each when fixing):
- `ml_core.obs` 1904 `Fit(X, Y, epochs)`
- `ml_embed.obs` 698 `PredictScore`, 726 `PredictClass`
- `ml_linear.obs` 215 and 578 `Fit(X, y)` (the overloads that forward without checking)
- `ml_outlier.obs`: `Fit` and `Predict` of the outlier detectors, lines 641, 701, 1105, 1237, 1468, 1603, 1863, 2617, 2873, 2985, 3213, 3364, 3636
- `ml_svm.obs` 1658 `PredictScore`, 2257 `Fit(X, y)`
- `ml_tree.obs` 86 and 2239 `Fit(Bool[,])`, 2272 `PredictAll`

and, where the Nil check exists but the shape is not checked, `LinearRegression->Predict`, `PCA->Transform` and `LogisticRegression->Predict` (column count), `LogisticRegression->Fit` (zero rows).

**Reproduce.** Each ends the program on master:

```
m := Float->New[6, 2];                      # fit on 2 columns ...
lin := LinearRegression->New();  lin->Fit(m, Float->New[6, 1]);
lin->Predict(Float->New[3, 5]);             # ... predict on 5: Index over bounds in native JIT code
pca := PCA->New(2);  pca->Fit(m);  pca->Transform(Float->New[3, 5]);          # same
LogisticRegression->New(0.1, 50)->Fit(Float->New[0, 0], Bool->New[0]);          # Divide by zero
DecisionTree->New(3, 2)->Fit(Nil->As(Bool[,]));                                 # Nil dereference in Bool->Size
```

**Suggested fix.** One small private check per model family (non-Nil, at least one row, column count equal to what `Fit` stored, row count of X equal to that of y), called as the first statement of each public method and returning the method's existing failure value (false for Fit, Nil for Predict, 0.0 for Score). The sibling methods are the template. Keep the checks cheap: a dimension read and two compares, nothing per row.

**Size.** M to L. The edits are mechanical but touch about 15 classes, so the cost is the tests: one per class for each of Nil, empty and wrong width, which a table-driven test over the model list keeps to one file. `ml.obl` and `ml_lab.obl` are rebuilt and committed with it.

## 3. Documentation examples that chain on a call that can return Nil

**What.** Some doc-comment examples call a method that returns Nil by contract and then call a method on the result in the same expression. The library code is guarded; a reader who copies the example into a program that meets a missing element ends it with a Nil dereference. The sweep treated these as the caller's responsibility, but the examples are what callers copy.

**Where** (found by searching the doc-comment blocks for a chain on `Get`, `GetFirstChild`, `FindElements`, `TextToElement` and `Inference`):
- `xml.obs` 386: `elem->GetFirstChild("body")->GetContent()` (Nil when there is no `body`)
- `xml.obs` 1754: `parser->GetRoot()->GetFirstChild("item")->GetContent()`
- `json.obs` 458, 478, 522, 1141, 1333, 1350, 1373, 1435: `root->Get("name")->GetString()` and its relatives (`Get` is Nil for a missing member)
- `json.obs` 1141 also calls `JsonParser->Parse(...)` as a static function and `GetValue()` on an element; the static entry point in `json.obs` is `TextToElement` (`Parse()` is an instance method), so that example may not compile as written; check it while there
- `onnx.obs` 65, 176, 453, 561: `session->Inference(...)` is used without checking for Nil (a Nil image, or a session that failed to load)

**Reproduce.** Run the example from `xml.obs` 386 with `<msg></msg>` as the document: it ends with `Attempting to dereference a 'Nil' memory instance` in `XmlElement->GetContent()`.

**Suggested fix.** Rewrite each example the way `json.obs` 474 to 480 and the `if(parser->Parse()) { ... }` examples already do: assign the intermediate result, test it against Nil, then use it. Keep the examples short; one `if` each. The examples feed the API documentation (`api.zip`), which is rebuilt by the usual docs step, not by hand. A check that compiles every example would stop new ones with a wrong signature, as at `json.obs` 1141; whether the docs build already does that was not checked.

**Size.** S. Roughly 15 comment edits and no code change. Comments do not reach the compiled library, so the `.obl` files should rebuild byte-identical; confirm with `core/compiler/build_libs.sh` (or `local_linux.sh --strict-libs`) rather than assume.

## Also found in the same audit, not entered above

These are in the description of PR #1527 and have their own changes, so they are listed here only so the set is in one place.

- `WebDownloader->Download` skips URLs when given seven or more (`download_count += group_ending` in `net_common.obs`; the fix is `:=`). A fix and a failing test exist; it goes in after the connection-reuse change because it is part of `net.obl`.
- A regular expression quantifier of `a{99999999999}` did not return, and `a{2,1}` was accepted; and `Data.JSON.RPC.Client` sent the id 1 on every request. Both are fixed on a branch stacked on the Nil sweep and are waiting for the library rebuild that follows the connection-reuse change.
