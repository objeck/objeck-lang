# `System.ML.Lab`: experiment support for a cybersecurity-ML lab

**Status:** design, approved direction 2026-10-26 for v2026.10.2. Delivered in
four stages after this document, each its own PR against `master`.

## Why

A university cybersecurity-ML lab hands in a *table, screenshot or graph of
results* for parameter sweeps: random-forest `n_estimators`, `max_depth` and
`min_samples_leaf`; SVM kernel, `C` and `gamma`; anomaly-detector sensitivities;
class-imbalance ratios; sequence lengths; learning rates. Each sweep compares
accuracy, precision, recall, F1, false-positive rate, false negatives and
**training time**, sometimes training against testing accuracy (an overfitting
curve), and sometimes a training-loss plot.

`System.ML` has the models, `TableEncoder`, `FeatureScaler`, `StratifiedKFold`,
`CrossValidation->Evaluate` and both label- and score-based `Metrics`. What it
does not have is the plumbing *around* a sweep, so today a student hand-writes
all of it: a deterministic stratified hold-out split, a resampler that changes
the training class ratio while leaving the test set alone, a wall-clock
stopwatch, a loop that trains once per parameter value and gathers eight numbers
per value, a table renderer, and a chart. That code is the same in every lab
and is where the mistakes live (scaling fitted on the test split, a resampled
test set, timing that includes prediction, a chart whose axis hides the result).

This document specifies that plumbing. Everything is library-level Objeck with
one VM fix (the timer's clock); nothing needs a new native dependency.

## Where it lives

A **new library, `ml_lab.obl`**, from `core/compiler/lib_src/ml_lab.obs`,
declaring `bundle System.ML.Lab` and built with `-lib gen_collect,csv,json,ml`.

Why not inside `ml.obl`: the result table writes JSON through `Data.JSON`, and a
library's link list is inherited by every program that links it -- the compiler
refuses to load a `.obl` whose signatures mention a class from a library the
program did not name (`Unable to resolve external library class:
'Data.JSON.JsonElement'. Add it with '-lib json'`), even when the program never
touches that class (verified against the current compiler). Linking `json` into
`ml.obl` would therefore break every existing `System.ML` program that compiles
with `-lib ml,gen_collect,csv`. A separate library keeps that cost on the
programs that want the lab support, exactly as `query_ml.obl` already layers on
`ml.obl`. It also keeps `ml.obl`'s class table untouched while three other
branches are adding classes to it.

Compile a lab program with `-lib ml,ml_lab,csv,json` (`gen_collect` is always
supplied). The `[@ai]` alias group in `core/lib/configobjk.ini` gains
`ml_lab.obl` and `json.obl`, so `-lib @ai` also works.

Dependents: nothing links `ml_lab.obl`. It links `ml.obl`, so like `query_ml.obl`
it must be rebuilt whenever `ml.obl`'s class ids shift; `build_libs.sh` orders it
after `ml`.

### Lists to update

Adding a source file to `lib_src` touches the five hard-coded doc lists that
`tools/cicd/check_doc_lists.py` keeps in agreement (`release-build.yml`,
`ci-build.yml`, `core/release/code_doc64.in`, `gen_json.cmd`, `gen_json.sh`),
plus `core/compiler/build_libs.sh` (a new build line, after `ml`) and the LSP
index `tools/lsp/server/objk_apis.json` with its VS Code copy. The deploy
scripts copy `core/lib/*.obl` wholesale, so the tracked `core/lib/ml_lab.obl` is
the only deployment change. Other branches are adding `ml_svm.obs`,
`ml_outlier.obs` and an embedding pipeline to the same lists; conflicts are
resolved by keeping every entry.

## Conventions shared by every class

- **Labels are `Bool[]`**, `true` for the positive class (the attack), matching
  every classifier `Fit`, `ScoreModel`, and `Metrics`. `CrossValidation->ToBool`
  and `ToFloat` convert to and from the `Float[]` that `TableEncoder` and
  `StratifiedKFold` use. Strata for splitting are `Float[]` codes so a split can
  be stratified by attack family rather than by the binary label.
- **Determinism from a seed.** Every function that shuffles or samples takes a
  seed and uses a local generator (`System.ML.Random`, MINSTD), never
  `Float->Random()`. The same inputs and seed give the same rows on every
  platform, which is what makes a hand-in reproducible and what the tests assert.
- **Refusals raise.** Bad input raises `System.ML.Lab.LabError` (from
  `System.Error`) with a message naming the problem, e.g. *stratum 2.0 has 1
  row; at least 2 are needed to put one in each split*. Uncaught, the VM ends
  the program printing exactly that; a program can `try { } on(e : LabError) { }`.
  The rest of `System.ML` returns `Nil` on bad input, which in a lab is the
  silent failure that costs an afternoon; verified that a raise from a library
  function is caught in all four execution modes.
- **Nothing here fits a scaler or encoder on anything but the training split.**
  The split happens first; the resampler changes only the training half;
  `TableEncoder` and `FeatureScaler` are fitted on `GetTrainX()`.

## Stage 1: data utilities

### `Partition`

```objeck
split := Partition->TrainTest(X, y, 0.3, 42, true);          # stratified by y
split := Partition->TrainTest(X, y, families, 0.3, 42);      # stratified by Float[] codes
idx   := Partition->Indices(strata, 0.3, 42);                # the row indexes alone
```

Named `Partition` rather than `Split`: `System.ML` already has a `Split` class
(a tree's split point) and the compiler reports an ambiguous name as an error
when both bundles are in use.

- `function : TrainTest(X : Float[,], y : Bool[], test_fraction : Float, seed : Int, stratify : Bool) ~ DataSplit`
- `function : TrainTest(X : Float[,], y : Bool[], strata : Float[], test_fraction : Float, seed : Int) ~ DataSplit`
- `function : Indices(strata : Float[], test_fraction : Float, seed : Int) ~ IndexSplit` -- `strata` of one distinct value is an unstratified split
- `function : Take(X : Float[,], rows : Int[]) ~ Float[,]`, `Take(y : Bool[], rows : Int[]) ~ Bool[]`, `Take(y : Float[], rows : Int[]) ~ Float[]` -- gather rows, in order
- `DataSplit`: `GetTrainX()`, `GetTrainY()`, `GetTestX()`, `GetTestY()`, `GetTrainRows()`, `GetTestRows()`, `GetTrainSize()`, `GetTestSize()`
- `IndexSplit`: `GetTrain()`, `GetTest()`

These result types are record-shaped (immutable, constructor plus getters) but
written as classes rather than with the `record` keyword: the API-doc and LSP
index generators index only doc-commented members, and a record's generated
accessors have none, so a `record` would be invisible to hover, completion and
the published reference.

Allocation is **per stratum and exact**: a stratum of `n_c` rows contributes
`Float->Round(n_c * test_fraction)` rows to the test split, clamped to
`[1, n_c - 1]` so every stratum appears in both halves. With 10 positives in
100 rows and `test_fraction` 0.2 the test split holds exactly 2 positives and 18
negatives, and the tests assert those counts rather than a tolerance. Within a
stratum the rows are Fisher-Yates shuffled from the seed; the first `k` become
test rows. Row indexes come back **ascending** in each half, so a row's position
is independent of the shuffle and `GetTestRows()` can be used to join
predictions back to the source table.

Refused (raises `LabError`): `test_fraction` outside `(0, 1)`; `X`, `y` or
`strata` `Nil` or of different lengths; fewer than two rows; **a stratum with
fewer than 2 rows** (it cannot be in both halves) -- the message names the
stratum value and its count.

### `Resample`

The lab's imbalance experiment trains at a positive ratio `POSRATIO` from 0.001
to 0.5 and must measure every run on the *same* test set. The API makes that
the only possible order: split first, then resample the training half.

```objeck
split := Partition->TrainTest(X, y, 0.3, 42, true);
each(r : ratios) {
  train := Resample->ToPositiveRatio(split->GetTrainX(), split->GetTrainY(), ratios[r], 42);
  # train->GetX(), train->GetY(); split->GetTestX() is untouched
};
```

- `function : ToPositiveRatio(X : Float[,], y : Bool[], ratio : Float, seed : Int) ~ Sample` -- **down-samples** whichever class is over-represented: if the current positive ratio is above `ratio`, positives are dropped to `Round(ratio * negatives / (1 - ratio))`; otherwise negatives are dropped to `Round(positives * (1 - ratio) / ratio)`. The class that is kept is kept whole. The rows kept are chosen by a seeded shuffle.
- `function : ToPositiveRatio(X : Float[,], y : Bool[], ratio : Float, seed : Int, oversample : Bool) ~ Sample` -- with `oversample` true the *under*-represented class is duplicated by seeded sampling with replacement until the ratio holds, and nothing is dropped. Duplicates are visible in `GetRows()`.
- `function : PositiveRatio(y : Bool[]) ~ Float`, `function : CountPositives(y : Bool[]) ~ Int`
- `Sample`: `GetX()`, `GetY()`, `GetRows()` (the source row of each sample row), `Size()`, `GetPositiveRatio()`

The achieved ratio is exact whenever the arithmetic allows (10 positives and 90
negatives at 0.5 keeps 10 of each) and otherwise within one row; `PositiveRatio`
on the result reports what was achieved and the table carries that number, not
the request. Refused: `ratio` outside `(0, 1)`; a class with no rows; a
down-sample that would leave a class with zero rows (asking for 0.001 from 10
positives and 90 negatives would keep 0 positives; the message says so and
suggests `oversample`).

### `Stopwatch`

```objeck
sw := Stopwatch->New();
sw->Start();
model->Fit(train_x, train_y);
ms := sw->Stop();            # also returned by GetElapsedMs()
```

`Start()`, `Stop() ~ Float` (milliseconds), `Reset()`, `GetElapsedMs() ~ Float`,
`GetElapsedSeconds() ~ Float`, `IsRunning() ~ Bool`. Accumulates across
Start/Stop pairs until `Reset()`.

It wraps `System.Time.Timer`, **whose clock is fixed in this stage**: the VM
implements `TIMER_START`/`TIMER_END`/`TIMER_ELAPSED` with C `clock()`, which
on Windows is wall time at 1 ms granularity but on Linux and macOS is *process
CPU time* -- a multi-threaded fit is over-counted and a sleep is not counted at
all, so "training time" differs by platform. The fix (core/vm/common.cpp) stores
`std::chrono::steady_clock` nanoseconds in the same instance slot; `Timer`'s
API and its seconds-as-`Float` result are unchanged, resolution becomes
sub-millisecond everywhere. A regression test times `Thread->Sleep(50)` and
requires at least 0.04 s, which the CPU clock fails. Nothing in the tree relied
on CPU time.

### Labels and encoding

`TableEncoder` already handles the NSL-KDD shape: `protocol_type`, `service` and
`flag` are detected as categorical (one non-numeric value makes a column
categorical), one-hot encoded with a vocabulary fixed at `Fit`, and
`TransformLabelsBinary(table, "normal")` maps the attack label to 0.0 for
`normal` and 1.0 for everything else. `FeatureScaler->Fit` learns from whatever
it is given; `CrossValidation->Evaluate` fits it on the training split only. A
new regression test exercises exactly that path on a miniature NSL-KDD table
(header, the three string columns among numeric ones, a test row with a service
unseen in training) and asserts widths, column names, the binary mapping and
that the test transform uses the training statistics.

What is missing is the label side when labels do not come through
`TableEncoder` -- a `String[]` read some other way, or multi-class strata:

- `Labels->ToBinary(values : String[], positive : String) ~ Bool[]`
- `Labels->ToBinaryExcept(values : String[], negative : String) ~ Bool[]` -- `normal` against every attack name
- `Labels->Classes(values : String[]) ~ String[]` -- distinct values in first-seen order
- `Labels->ToIndices(values : String[], classes : String[]) ~ Float[]` -- class index per value, -1.0 for an unknown, ready for `Partition->Indices` strata or `StratifiedKFold`
- `Labels->Column(table : CsvTable, name : String) ~ String[]` -- a data column (header skipped) as strings

## Stage 2: `Experiment` and `ResultTable`

### The callback

An interface rather than a function reference, as `ScoreModel` and
`ScoreMetric` are: it crosses the library boundary reliably and reads clearly in
a lab write-up.

```objeck
interface Trial {
  method : virtual : public : Run(value : Float, name : String, seed : Int) ~ TrialResult;
}
```

`value` is the swept parameter's numeric value, `name` its display name (for a
categorical sweep such as `kernel = linear | rbf | poly` the values are
0, 1, 2 and the names carry the meaning), `seed` the repetition's seed. The trial
builds the model, fits it under the stopwatch, predicts on train and test and
returns:

```objeck
class TrialResult {
  New(train_pred : Bool[], train_y : Bool[], test_pred : Bool[], test_y : Bool[], fit_ms : Float)
  New(..., loss : Float[])          # optional per-epoch training loss
}
```

Both label arrays travel with the predictions because a trial may have resampled
its own training set (the imbalance sweep), so the runner cannot assume one `y`.

### `Experiment`

```objeck
exp := Experiment->New("Random forest: trees", "n_estimators");
exp->SetValues([10.0, 50.0, 100.0, 200.0, 500.0]);
exp->SetSeeds([1, 2, 3, 4, 5]);                 # optional: mean and std over seeds
exp->AddSetting("max_depth", "8");               # footnote of fixed settings
exp->AddSetting("dataset", "synthetic-2d v1, 70/30 stratified, seed 42");
table := exp->Run(trial);
```

- `New(title : String, parameter : String)`
- `SetValues(values : Float[])`, `SetValues(values : Float[], names : String[])`
- `SetSeeds(seeds : Int[])` -- default one run with seed 0
- `AddSetting(name : String, value : String)` -- becomes the table's footnote
- `Run(trial : Trial) ~ ResultTable`

Per value the runner computes, from the trial's predictions: `train_accuracy`,
`test_accuracy`, `precision`, `recall`, `f1`, `fpr`, `false_negatives` (a count),
`fit_ms`. Test-set metrics use the test predictions; `train_accuracy` is the only
training-set number, there for the overfitting curve. With several seeds each
column holds the mean and the table carries a sample standard deviation per
cell, which the renderers show as `mean ± std` and the chart draws as error bars.
A single seed stores no deviations and the renderers omit them.

Counting is the four confusion cells from two `Bool[]`; the rates come from the
existing `ThresholdResult` (`GetAccuracy`, `GetPrecision`, `GetRecall`,
`GetFpr`), F1 from precision and recall, every ratio guarded (Objeck raises on a
float division by zero, and a value that predicts nothing positive has no
precision). The outlier-detection branch is adding a `ConfusionMatrix` class and
`Metrics->FalsePositiveRate` to `ml_data.obs`; the runner's counting is one
private helper that takes the four counts, so once that lands the helper
delegates to it and nothing public changes. Neither `ConfusionMatrix` nor a
sensitivity sweep is defined here.

As built (integration of the lab and outlier branches): the helper now returns
`System.ML.ConfusionMatrix`, which counts the cells and supplies every rate,
F1 included (`2TP / (2TP + FP + FN)`, equal to the harmonic mean of precision
and recall); the lab library no longer counts or divides on its own.

Refused: no values; `names` of a different length; a trial that returns `Nil`
or arrays of unequal length (the message names the value and seed).

### `ResultTable`

A small, self-describing table: a title, one row per parameter value (display
name plus numeric value), named numeric columns, optional per-cell standard
deviations, a footnote.

- `New(title : String, parameter : String)`
- `AddColumn(name : String)`, `AddColumn(name : String, precision : Int)` -- decimals when rendered (4 by default; `Experiment` uses 0 for `false_negatives`, 1 for `fit_ms`)
- `AddRow(name : String, value : Float, cells : Float[])`, `AddRow(name : String, value : Float, cells : Float[], stds : Float[])`
- `SetFootnote(text : String)`; accessors `GetTitle`, `GetParameter`, `GetFootnote`, `GetColumnNames`, `Size`, `GetRowName(r)`, `GetRowValue(r)`, `Get(r, c)`, `GetStd(r, c)`, `HasStds()`, `ColumnIndex(name)`, `GetColumn(name) ~ Float[]`, `GetColumnStds(name) ~ Float[]`
- `ToText() ~ String` -- aligned columns, title above, footnote below
- `ToMarkdown() ~ String` -- a GitHub table under a `###` title, footnote in italics; pipes in names escaped
- `ToCsv() ~ String` -- RFC 4180 through `Data.CSV`'s `CsvTable->FormatRow` (a field holding a comma, quote or line break is quoted, quotes doubled); the header is `<parameter>,<parameter>_value,<columns...>` and, when the table holds deviations, `<column>_std` for each; the title and footnote are *not* in the CSV (a CSV is data; the Markdown and JSON carry the prose)
- `ToJson() ~ String` -- built with `Data.JSON.JsonElement`, so string escaping is the library's: `{"title","parameter","footnote","columns":[...],"precision":[...],"rows":[{"name","value","cells":{col: v},"stds":{col: s}}]}`. `FromJson` reads strings with `GetString()`, which returns decoded text once PR #1403 (*Data.JSON: decode strings on parse, escape once on output*) is in; on the current `master` the parser hands back the escaped text, so a title with a non-ASCII letter or a `/` does not round-trip until that lands. Stage 2 therefore merges after #1403, and its JSON golden is compared by structure (parsed back), not by bytes, because the serializer's escape choices belong to `Data.JSON`.
- `function : FromCsv(text : String) ~ ResultTable`, `function : FromJson(text : String) ~ ResultTable` -- round trip; numbers written with up to 10 decimals and trailing zeros trimmed, so a value survives the trip to within 1e-10
- `Save(path : String) ~ Bool` -- by extension: `.md`, `.csv`, `.json`, otherwise text
- `ToChart(metrics : String[]) ~ Chart` (Stage 3) -- one series per named column, x = the parameter values, error bars from the stds

Rendering is deterministic: fixed decimals through `Float->ToString(v, p)`,
which is `printf("%.*f")` and correctly rounded on every platform the tests run
on, so the golden files are byte-comparable.

## Stage 3: `Chart`

Standalone **SVG 1.1** text with no script, no external font and no dependency,
written by `Save(path)` or returned by `ToSvg()`. It opens in a browser,
pastes into Word, and converts with any SVG tool.

```objeck
c := Chart->New("Recall vs C", "C", "recall");
c->SetLogX(true);
c->AddLine("rbf", cs, recall_rbf, recall_rbf_std);
c->AddLine("linear", cs, recall_lin);
c->Save("recall_vs_c.svg");
```

- `New(title : String, x_label : String, y_label : String)`; `SetSize(width : Int, height : Int)` (default 800 x 480); `SetLogX(on : Bool)`; `SetYRange(min : Float, max : Float)` (default: nice bounds from the data, and `[0, 1]` when every value is within it)
- `SetCategories(names : String[])` -- categorical x: ticks are the names at 0..n-1
- `AddLine(name : String, xs : Float[], ys : Float[])`, `AddLine(name, xs, ys, errs : Float[])` -- a polyline with markers; `errs` draws symmetric error bars
- `AddBars(name : String, ys : Float[])`, `AddBars(name, ys, errs)` -- grouped bars over the categories (one group per category, one bar per series)
- `function : LossCurve(title : String, names : String[], losses : Float[,]) ~ Chart` -- rows are series (one per learning rate), columns are epochs, x = epoch
- `ToSvg() ~ String`, `Save(path : String) ~ Bool`, `Size()` (series added)
- `ResultTable->ToChart(metrics : String[])` and `ToChart(metrics, log_x : Bool)`: x is the parameter value, or the row names as categories when a row's name is not its number; one line per named column (an empty array means every column); error bars from the deviations when the table holds them; y labelled with the metric names

Layout: a title, axis titles, a legend in the top-right of the plot area on a
translucent box (so it reads where it sits over a line), a light grid, tick
labels from a 1-2-5 "nice step" rule (log x: powers of ten in range, labelled
`0.001`, `0.01`, ...), `font-family="sans-serif"`, a fixed eight-colour
palette, coordinates emitted with one decimal so the text is stable. A y range
inside `[0, 1]` is drawn as the whole unit interval, so rate charts compare; any
other range starts at 0 (bars, counts, milliseconds) and ends on a nice bound.
Every text node is XML-escaped. An error bar is one `line` plus one `path` for
its two caps. A series is held in a `ChartSeries` (name, xs, ys, errors, bars or
line). Log x with a non-positive value is refused with a message.

The renderer is split into small methods (`Ranges`, `Grid`, `XTicks`, `Axes`,
`LineSeries`, `BarSeries`, `Legend`): written as one method it exceeded the
VM's local-variable frame ("Local space has been exceeded"), which is a limit
worth knowing when writing a long formatter in Objeck.

Verification: the tests parse the SVG with `Data.XML` (well-formed, the root is
`svg` with `xmlns`, `version` and `viewBox`; attribute values are read with
`GetDecodedValue`, since the parser keeps a space as `&#x20;`), count the
elements a chart must have (one `polyline` per line series, one `circle` per
point, one `rect` per bar, one error-bar `line` per point with a non-zero
error, plus the grid and axis lines), check that every emitted coordinate lies
inside the viewport, check that log-axis decades are equally spaced, and
compare to golden files under `programs/regression/ml_lab_golden/`. **There is no SVG
rasterizer in the tree** -- OpenCV's `Image->Load` does not read SVG and SDL
has no SVG path -- so no PNG is produced here and none is added: a student opens
the SVG in a browser or pastes it into Word. Where a rasterizer exists on the
developer's machine (a browser), the goldens were eyeballed once; the tests
guard structure and determinism, not pixels.

## Stage 4: the end-to-end example

`programs/examples/ml/lab_sweeps.obs` builds a documented **synthetic**
two-feature dataset from a fixed seed (two overlapping classes, 500 rows, 20 %
positive -- the generator and its parameters are in the file header so the
hand-in can cite them), splits it 70/30 stratified, fits a `FeatureScaler` on
the training half, and runs two sweeps:

1. `RandomForestClassifier` over `n_estimators` 10, 50, 100, 200, 500 with the
   existing constructor (`max_depth`, `min_samples`, `seed` fixed and footnoted),
   two seeds, so the table shows `mean ± std` and the chart error bars. The
   500-tree fits dominate: about 16 s each on a laptop, 50 s for the whole
   program (1,000 rows and three seeds took five and a half minutes, which is
   too long for an example; the `fit_ms` column is the point of the sweep);
2. the linear `SVM` over `lambda` (L2 strength, the inverse role of `C`) on a
   log axis, 1e-3 .. 10.

It writes `rf_trees.md`, `rf_trees.csv`, `rf_trees.svg`, `rf_train_vs_test.svg`,
`svm_lambda.md`, `svm_lambda.csv`, `svm_lambda.svg` to the directory named on
the command line (default `lab_output`), and prints each path. It uses only
classes on `master` today; the kernel-SVM and outlier-detector classes plug in
through the same `Trial` interface when they land, and the docs show that.

## Testing

Every function gets a known-answer regression test in `programs/regression/`
(`PASS:`/`FAIL:` lines, exit 1 on failure), each run compiled at `-opt s3` and
`-opt s0`, under `obr`, `obr --jit=off`, and with `OBJECK_JIT_THRESHOLD=1`, on
Windows and on a Linux native clone before cloud CI:

- `time_timer_wall_clock` -- `Timer` measures a sleep (fails on the CPU clock)
- `ml_lab_split` -- exact per-stratum counts; every row in exactly one half; **no leakage** (labels stay a function of their own features through the split, the same construction `ml_experiment_runner` uses); same seed = same rows, different seed = different rows; unstratified path; refusals: fraction 0 and 1, a 1-row stratum, mismatched lengths
- `ml_lab_resample` -- 10/90 at 0.5 keeps 10+10; 0.1 keeps 10+90; oversample doubles the minority with duplicates visible in `GetRows()`; the test split object is untouched (same reference, same contents); seed determinism; refusals outside (0, 1) and the all-dropped case
- `ml_lab_labels_encoding` -- `Labels` functions; the miniature NSL-KDD table through `TableEncoder` and `FeatureScaler` fitted on train only
- `ml_lab_stopwatch` -- accumulates, resets, measures a sleep
- `ml_lab_result_table` -- text/Markdown/CSV/JSON against goldens; CSV and JSON round trips with a name holding a comma, a quote and non-ASCII (`naïve "rbf", γ=0.5`); `GetColumn`
- `ml_lab_experiment` -- a trial whose predictions are chosen so every metric has a known value (tp=3, fp=1, fn=2, tn=4 gives accuracy 0.7, precision 0.75, recall 0.6, F1 2/3, fpr 0.2, fn 2); repeated seeds give the right mean and sample std; fit time is non-negative; a categorical sweep keeps its names; refusals
- `ml_lab_chart_svg` -- structural checks via `Data.XML`, coordinate bounds, goldens for a line chart with error bars, a log-x chart, a grouped bar chart and a loss curve; `ResultTable->ToChart`
- the example is not compiled by CI (`ci-build.yml` compiles the shipped `programs/deploy` set, not `programs/examples`), so it is run by hand on Windows and Linux before its stage merges and its outputs are opened in a browser; every library call it makes is covered by the tests above

## Out of scope

Multi-class metrics (the lab is binary: benign vs attack); a PNG rasterizer; a
grid search over two parameters at once (two sweeps or a nested trial cover the
hand-ins listed); per-epoch loss from models that do not expose it
(`NeuralNetwork` is unchanged; `LossCurve` charts whatever the student records).

## Release note

> **`System.ML.Lab` (`ml_lab.obl`)**: the plumbing a parameter sweep needs --
> `Partition->TrainTest` (stratified, seeded, exact), `Resample->ToPositiveRatio`
> for class-imbalance runs against a fixed test set, `Stopwatch`, `Labels`,
> `Experiment` over any model through a `Trial`, a `ResultTable` with text,
> Markdown, CSV and JSON output, and `Chart` writing standalone SVG line, bar
> and loss-curve charts. `System.Time.Timer` now measures wall-clock time on
> every platform (it was process CPU time on Linux and macOS). The compiler no
> longer crashes on a subscript applied to a later link of a call chain
> (`v->Get(r)->Get()[c]`).

## What landed where

| Stage | PR | Contents |
|---|---|---|
| 0 | #1402 | this document |
| 1 | #1405 | `ml_lab.obl`: `Partition`, `Resample`, `Stopwatch`, `Labels`, `LabError`; the `Timer` clock fix; lists, alias group, LSP index, playground policy |
| 2 | stage-2 PR | `Experiment`, `Trial`, `TrialResult`, `ResultTable`; the chained-subscript compiler fix; merges after #1403 |
| 3 | stage-3 PR | `Chart`, `ChartSeries`, `ResultTable->ToChart` |
| 4 | stage-4 PR | `programs/examples/ml/lab_sweeps.obs`; `docs/AI.md` and `docs/cli_options.md` |
