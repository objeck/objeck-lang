## Gradient boosting (System.ML.Boost)

`System.ML.Boost` adds gradient boosted trees to the machine learning suite: `GradientBoostingRegressor` (squared, absolute and Huber loss) and `GradientBoostingClassifier` (binary logistic and multiclass softmax). It is pure Objeck, in its own library `ml_boost.obl`, which links `ml` (the classifier implements `ScoreModel`, so a binary model drops into `CrossValidation` and the lab's experiments).

Compile with `-lib ml_boost,ml,gen_collect,csv`. The `@ai` alias includes `ml_boost`.

The trees are XGBoost's: each stage fits a regression tree to the first and second derivative of the loss, grown level by level, and a leaf holds `-G / (H + lambda)` times the learning rate (`G` and `H` are the sums of the derivatives over the rows in the leaf). A split is kept only when its gain, `0.5 * (GL^2/(HL+lambda) + GR^2/(HR+lambda) - G^2/(H+lambda))`, is above `SetMinSplitGain` (0.0 by default). With squared loss and `lambda` 0 a leaf is the mean residual and the model is scikit-learn's `GradientBoostingRegressor`; with `lambda` 1 it is XGBoost's default.

### Regression

```objeck
use System.ML;
use System.ML.Boost;

model := GradientBoostingRegressor->New(100, 0.1, 3, 42);   # stages, learning rate, max depth, seed
model->SetSubsample(0.8);                                    # stochastic gradient boosting
if(<>model->Fit(train_x, train_y)) {
  "fit refused: {$model->GetError()}"->PrintLine();
  return;
};

predictions := model->Predict(test_x);
"test mean squared error {$model->Score(test_x, test_y)}"->PrintLine();
```

The loss is the first argument of the longer constructor: `GradientBoostingRegressor->New(BoostLoss->HUBER, 100, 0.1, 3, 42)`. `SQUARED` is the default. `ABSOLUTE` and `HUBER` start from the median of the targets and set each leaf the way scikit-learn does (the median of the leaf's residuals, plus for Huber the mean of the residuals' deviation from it clipped at delta), so they ignore `SetLambda`. The Huber delta is re-estimated at every stage as the 0.9 quantile of the absolute residuals unless `SetHuber(delta, alpha)` fixes it.

Rows can carry weights: `Fit(X, y, w)`.

### Classification

```objeck
classifier := GradientBoostingClassifier->New(60, 0.1, 3, 42);
classifier->SetLambda(1.0);
classifier->Fit(train_x, train_labels);          # Int labels; two classes use the logistic loss, more use softmax
proba := classifier->PredictProba(test_x);       # rows x classes; each row sums to 1.0
labels := classifier->Predict(test_x);
"accuracy {$classifier->Score(test_x, test_labels)}, log loss {$classifier->LogLoss(test_x, test_labels)}"->PrintLine();
```

Labels are any `Int` values; they are sorted into `GetClasses()`, which is also the column order of `PredictProba`. Multiclass boosting fits one tree per class per stage with XGBoost's Hessian `2 p (1 - p)`. The starting scores are the log odds (binary) or the log class priors (multiclass). `Fit(X, Bool[])` and `PredictScore` (the probability of the larger label) make a binary model a `ScoreModel`.

### Staged predictions

`Predict(X, stages)` uses the first `stages` stages only (0 gives the starting score), and `StagedPredict(X)` returns the prediction after every stage as a stages x rows matrix whose last row equals `Predict(X)`. For the classifier it is a matrix of predicted labels. `PredictProba(X, stages)` does the same for probabilities.

```objeck
staged := model->StagedPredict(test_x);
each(s : [0, 9, 49, 99]) { ... staged[stage, row] ... }
```

On the data of `programs/examples/ml/gradient_boosting.obs` the test mean squared error is 0.594 after 1 stage, 0.204 after 10, 0.048 after 50 and 0.033 after 100.

### Early stopping

```objeck
stopper := GradientBoostingRegressor->New(1000, 0.1, 3, 42);
stopper->SetEarlyStopping(10, 0.1);    # patience, share of the training rows held out
stopper->Fit(train_x, train_y);
"kept {$stopper->GetNumStages()} stages, stopped {$stopper->GetStopped()}"->PrintLine();
```

The held-out rows are drawn with the model's seed. A validation set of your own is `Fit(X, y, Xv, yv)`. Training stops when the validation loss has not improved for `patience` stages, and the model is cut back to its best stage: `GetNumStages()` is `GetBestIteration() + 1`. `GetTrainLoss()` and `GetValidationLoss()` return the mean loss after every stage that ran (squared error, absolute error, Huber loss or log loss). With a patience of 0, the default, every stage runs and every stage is kept. In the example above 1000 stages were asked for and 121 kept.

### Feature importance

`GetFeatureImportances()` is each feature's share of the total loss reduction of all splits (gain), so the values sum to 1.0; `GetGainTotals()` has the unnormalized sums and `GetSplitCounts()` counts the splits that use each feature. On the example data (`y = sin(3 a) + b^2 + 0.5 c d + 0.3 e + noise`, plus a feature `f` of pure noise) the importances are 0.79, 0.14, 0.014, 0.010, 0.046 and 0.002 for `a` to `f`.

### Speed: the histogram option

`SetHistogram(255)` finds splits on a histogram of at most 255 bins per feature instead of on the sorted values (`SetHistogram(0)`, the default, is the exact search). A feature with no more distinct values than bins loses nothing: the test suite checks that the two searches build the same model on features with 11 distinct values. Otherwise the bin edges are quantiles of the distinct values. On 8,000 rows x 20 features, 50 stages of depth 4, the histogram search took 0.6 s and the exact search (15 stages) 1.0 s with the JIT, and 12 s and 11 s with `--jit=off`; per stage the histogram search is 3 to 5 times faster here, and its test error was within 1% of XGBoost's `hist` method.

### Other settings

| Method | Meaning (default) |
|---|---|
| `SetMinSamplesLeaf(n)` | fewest training rows in a leaf (1) |
| `SetSubsample(f)` | fraction of the rows each stage uses, drawn without replacement with the seed (1.0) |
| `SetColumnSample(f)` | fraction of the features each tree may split on (1.0) |
| `SetLambda(x)` | L2 penalty on the leaf values (0.0) |
| `SetMinSplitGain(x)` | smallest gain a split must exceed, XGBoost's gamma (0.0) |
| `SetMinChildWeight(x)` | smallest sum of Hessians in a child (0.0) |
| `SetHistogram(bins)` | 0 for the exact search, or 2 to 255 bins (0) |
| `SetEarlyStopping(patience, fraction)` | see above (off) |
| `SetHuber(delta, alpha)` | fixed Huber delta, or 0.0 for the adaptive rule at quantile alpha |

Every setter returns `false` and sets `GetError()` for a value it refuses. The learning rate, stage count and depth are checked by `Fit`, which returns `false` (and keeps an earlier fitted model) rather than ending the program.

Missing values: a `NaN` in a feature is a missing value, in training and at predict time. Every split learns which side the missing rows go to (both are tried, as in XGBoost), and the histogram search has a bin for them. A `NaN` target is refused.

### Model files

```objeck
classifier->Store("model.bmod");
loaded := GradientBoostingClassifier->Load("model.bmod");      # Nil when missing, corrupt or the other kind
text := classifier->Export();                                   # the same model as a string of hex digits
again := GradientBoostingClassifier->Import(text);
```

The file holds the starting scores, the class labels, the loss history and every tree; a loaded model predicts bit for bit what the original did. A regressor file does not load as a classifier. A truncated or damaged file is `Nil`, and a model whose tree indices point backwards is refused, so a bad file can neither crash nor loop.

### Accuracy against scikit-learn and XGBoost

The regression tests generate their data with the seeded `System.ML.Random` (500 rows to train, 200 to test, 100 stages, learning rate 0.1, depth 3) and compare with numbers from scikit-learn 1.9.1 and XGBoost 3.4.1 (`tree_method=exact`, the same starting scores) produced by `programs/regression/ml_boost_ref/boost_ref.py` in a scratch virtual environment; its output is `reference_output.txt` beside it.

| Task | ml_boost | XGBoost | scikit-learn |
|---|---|---|---|
| regression, lambda 0: test MSE | 0.03762 | 0.03750 | 0.03763 |
| regression, lambda 1: test MSE | 0.03818 | 0.03807 | |
| absolute loss: test MAE | 0.1710 | | 0.1704 |
| Huber loss: test MAE | 0.1599 | | 0.1589 |
| binary, lambda 1: accuracy / log loss | 0.790 / 0.42677 | 0.790 / 0.42676 | |
| binary, lambda 0: accuracy / log loss | 0.810 / 0.4122 | 0.810 / 0.4100 | 0.780 / 0.4610 |
| 3 classes, lambda 0: accuracy / log loss | 0.855 / 0.33406 | 0.855 / 0.33406 | 0.835 / 0.3562 |
| 3 classes, lambda 1: accuracy / log loss | 0.830 / 0.33146 | 0.830 / 0.33146 | |
| 8,000 x 20 data, 15 stages: test MSE | 0.19376 | 0.19376 | 0.19376 |

The first five test predictions of the lambda 0 and lambda 1 regressions match XGBoost's to 1e-4, and the lambda 1 binary probabilities to 1e-3. The classifier at lambda 0 is the one place that agrees only loosely (a probability off by 0.02 on one row): a logistic leaf divides by a Hessian that is near zero for confident rows, so tiny differences in arithmetic (XGBoost keeps float32) move a tree choice; use `SetLambda(1.0)` for classifiers, as XGBoost does by default. scikit-learn grows each tree on the first derivative alone and fits leaves afterwards by line search, so its classifiers differ more (accuracy within 0.04, log loss within 15% is what the tests assert).

### Limits and caveats

- The trees are grown level by level to `max_depth` (1 to 30); there is no best-first or leaf-count growth, no categorical features and no monotone constraints.
- The exact search costs `O(rows x features)` per tree level, plus one sort per feature at the start; the memory is a few arrays of `rows x features` integers. The histogram search keeps `rows x features` bin codes.
- Training is single threaded.
- A loaded model predicts but cannot be refitted: call `Fit` on a new object.
- With a `min_samples_leaf` of 1 the absolute loss can isolate a single outlier row in a leaf and move it by the whole outlier; use `SetMinSamplesLeaf(3)` or more on contaminated targets.
- A split's threshold is the midpoint between two training values, so a test value above every training value follows the right side, as in XGBoost. A `NaN` split of "present against missing" uses a very large threshold, so every present value goes left.
- The loss histories (`GetTrainLoss`, `GetValidationLoss`) are plain means over the rows; row weights do not enter them.

### API summary

| Class | Methods |
|---|---|
| `GradientBoostingRegressor` | `New()`, `New(stages, rate, depth, seed)`, `New(loss, stages, rate, depth, seed)`, `Fit(X, y)`, `Fit(X, y, w)`, `Fit(X, y, Xv, yv)`, `Predict(X)`, `Predict(X, stages)`, `StagedPredict(X)`, `Score(X, y)`, `SetHuber`, `GetLoss`, `Store`, `Load`, `Export`, `Import` |
| `GradientBoostingClassifier` | `New()`, `New(stages, rate, depth, seed)`, `Fit(X, Int[])`, `Fit(X, Int[], w)`, `Fit(X, Int[], Xv, yv)`, `Fit(X, Bool[])` (`ScoreModel`), `Predict(X)`, `Predict(X, stages)`, `PredictProba(X)`, `PredictProba(X, stages)`, `PredictScore(X)`, `StagedPredict(X)`, `GetClasses`, `Score(X, y)` (accuracy), `LogLoss(X, y)`, `Store`, `Load`, `Export`, `Import` |
| both (from `BoostModel`) | `SetMinSamplesLeaf`, `SetSubsample`, `SetColumnSample`, `SetLambda`, `SetMinSplitGain`, `SetMinChildWeight`, `SetHistogram`, `SetEarlyStopping`, `GetEstimators`, `GetLearningRate`, `IsFitted`, `GetError`, `GetNumStages`, `GetBestIteration`, `GetStopped`, `GetTrainLoss`, `GetValidationLoss`, `GetFitSeconds`, `GetFeatureImportances`, `GetGainTotals`, `GetSplitCounts`, `GetLeafCount` |

The `BoostModel` methods `FitCore`, `Grow`, `RawScores`, `StagedRaw`, `ToBytes`, `FromBytes`, `Refuse`, `Probabilities`, `HuberDelta` and `MeanLoss` are the engine's: they are public only because the two model classes are separate, and are not a supported interface.

A complete program is `programs/examples/ml/gradient_boosting.obs`. The tests are `ml_boost_accuracy` (the table above), `ml_boost_hand` (a two-stage stump on six points worked out by hand, a one-stage logistic stump, missing values), `ml_boost_properties` (monotone training loss, early stopping, seeds, staged predictions, probabilities, importances, histogram against exact, weights, robust losses, refusals), `ml_boost_persist` and `ml_boost_perf` (10,000 x 20).
