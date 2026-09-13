# `System.ML`: what a tabular classification study needs, and what is missing

**Status:** analysis, not a commitment. Written 2026-09-12 against `master`
(`7638f738da`, v2026.9.1). Every claim below was checked against
`core/compiler/lib_src/ml_*.obs` rather than inferred from the API index.

The occasion is an intrusion-detection study (NSL-KDD, logistic regression vs
random forest, compared at fixed false-positive rates) that intends to run
end to end in Objeck. That workload is a good forcing function because it is
the *ordinary* shape of a tabular ML experiment: load a CSV with categorical
columns, encode it, split it, train two models, and compare them on a metric
that needs decision scores rather than labels. Where it snags is where any
such study snags.

## What the library has today

| Area | Present |
|---|---|
| Linear | `LogisticRegression`, `SVM`, `LinearRegression`, `Ridge`, `Lasso`, `ElasticNet`, `Perceptron` |
| Bayes | `GaussianNaiveBayes`, `NaiveBayes` |
| Neighbours | `KNearestNeighbors`, `KDTree` |
| Trees | `DecisionTree`, `RandomForest`, `RegressionTree`, `GradientBoostedTrees`, `AdaBoost` |
| Unsupervised | `KMeans`, `DBSCAN`, `GaussianMixture`, `PCA` |
| Data | `MatrixReader`, `FeatureScaler`, `CrossValidation`, `Metrics`, `Matrix` |

## The four gaps that block the study

These are ordered by how early they stop you, not by size.

### 1. `RandomForest` cannot emit a decision score

```
LogisticRegression:  Predict(X : Float[,]) ~ Float[]    -> "probability array (values between 0 and 1)"
RandomForest:        Predict(input : Bool[,]) ~ Bool[]  -> hard labels
```

Logistic regression returns a real sigmoid (`result[i] := LogSigmoid(z)`), so a
threshold sweep works on it today. The forest returns booleans, and **you cannot
sweep a threshold over booleans** -- so "recall at 0.1% false-positive rate", the
comparison the IDS literature is built on, is not computable for the forest at all.

The fix is small and the information is already there. `Predict` computes
`votes_true` per row and then discards it:

```objeck
result[i] := votes_true * 2 >= ntrees;
```

A sibling returning `votes_true->As(Float) / ntrees` gives a usable score with no
change to training. This is the single highest-value item in this document and
the cheapest.

### 2. Every metric takes hard labels

`Metrics` exposes `Accuracy`, `Precision`, `Recall`, `F1Score`, `MCC`,
`ConfusionMatrix` and `PrintConfusionMatrix` -- all with the signature
`(predictions : Bool[], actuals : Bool[])`. There is no curve, no AUC, and no
"recall at a given FPR". Searching the library for `RocCurve`, `PrCurve`,
`AucRoc` or `RecallAtFpr` returns nothing.

So even with scores from (1), nothing consumes them. A score-based metrics
surface is the other half of the same result.

### 3. `RandomForest` accepts only boolean features

`DecisionTree`'s own doc comment: *"Fits a real recursive binary tree to boolean
training data."* `Fit` and `Predict` both take `Bool[,]`. NSL-KDD's 38 numeric
columns must therefore be quantile-binned before the forest sees them, which
throws away ordering information and makes the forest's numbers a function of the
binning strategy rather than of the data.

Continuous threshold splits are the standard fix and the largest item here.
Binning is a legitimate interim step, but it should be a *recorded* choice in any
write-up, not an invisible one.

### 4. CSV ingestion materialises the entire file

`CsvTable->New(FileReader->ReadFile(path))` reads the file into one `String`,
`Split`s it into row strings, then builds a `Vector<CsvRow>` where each row holds
a `CompareVector<String>` -- **one String object per cell**.

For NSL-KDD that is 125,973 x 42 = **~5.3 million live String objects**, plus the
whole-file string and the split array, all resident simultaneously. The encoded
matrix is comparatively cheap:

```
125,973 rows x 122 one-hot columns = 15.4M cells = 123 MB dense
```

123 MB of `Float[,]` is unremarkable. 5.3M transient objects is not: a `String`
is an object *plus* a char array, and arrays go straight to the old generation,
so `old_allocation_size` climbs past the collection trigger repeatedly during
parsing alone. At 300k x 80 (a CIC-IDS2017 subsample) the same path implies
~24M String objects.

**Partial mitigation exists today and needs no code.** `--gc-threshold` sets that
trigger, and it defaults to `MEM_START_MAX` = 8 MB:

```
obr --gc-threshold=1g ids_bench.obe
```

The adaptive heap does grow on its own (16x after seven zero-dead cycles), but
starting at 8 MB against millions of allocations means many cycles before it
catches up. *This has been reasoned from the allocator, not measured* -- it wants
a benchmark before anyone quotes a number for it.

The structural fix is an encoder that **streams**: `ReadLine` -> split one row ->
encode -> discard, never holding more than a row of strings. `FileReader->ReadLine()`
already exists, so this needs no VM work. Note the difference from a
`CsvTable -> Float[,]` encoder, which would inherit the materialisation above and
leave the problem in place.

## A plan, in dependency order

Each step is independently useful and independently shippable, with a
known-answer test in `programs/regression/ml_*_test.obs` as the existing ML work
does.

| # | Item | Unblocks | Rough size |
|---|---|---|---|
| 1 | Forest vote-proportion score | the primary comparison; nothing else works without it | tens of lines |
| 2 | Score-based metrics: ROC/PR points, AUC, `RecallAtFpr`, `ThresholdAt` | consuming (1) | ~200 lines |
| 3 | Streaming CSV -> `Float[,]` encoder with one-hot and train/test vocabulary consistency | loading any real tabular dataset | ~250 lines |
| 4 | `StratifiedKFold` with a seed | at 0.1% FPR on an imbalanced set, unstratified folds make the number noise | ~80 lines |
| 5 | Continuous-feature tree splits | removes the binning caveat from (3) | largest |

1 and 2 together are the smallest change that makes a fixed-FPR comparison
possible at all. 3 and 4 are what make the resulting number trustworthy. 5 is
the one that makes it competitive with what the literature reports.

## What is explicitly *not* a gap

The linear models, scalers, k-fold mechanics and point metrics are sound, and the
maths is not the problem anywhere in the list above. Nor is any of this a VM or
language change: every item is library-level, and the JIT calling-convention work
in v2026.9.1 (a bound call 26.5 ns -> 5.5 ns) directly benefits the call-heavy
`Fit` loops these models run.

## A documentation gap worth closing first

Nothing in the API index tells a reader that `Metrics` operates on hard labels
while only `LogisticRegression` yields scores. That asymmetry is discovered by
reading source. A doc comment on `Metrics` and on each `Predict` naming what it
returns would cost minutes and save the next person the afternoon it cost here.
