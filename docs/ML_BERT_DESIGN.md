# BERT embeddings and a trainable head — Design

A frozen, pretrained BERT encoder run through `API.Onnx`, producing sentence embeddings, and a
classification head trained in Objeck on top of them. Built for a university
cybersecurity-ML lab ("ML Chapter 3 — Building a BERT-Based Malware Classifier"), and useful to
anyone who wants text embeddings from Objeck.

## Why, and what this is not

The lab fine-tunes BERT on API-call sequences and runs three experiments:

1. **Class imbalance.** Training sets with a positive ratio (POSRATIO) of 0.001, 0.01, 0.10
   and 0.50, comparing precision, recall, F1 and false negatives, with the **test** set's class
   distribution held fixed.
2. **Sequence length.** MAX_SEQ_LENGTH of 32, 64, 128, 256.
3. **Learning rate.** 1e-5, 2e-5, 5e-5, with a plot of the training loss.

Objeck has no transformer training: no autograd, no attention kernels, no optimizer over a
110M-parameter graph. Building that is a project of its own and is out of scope.

What is in scope is the honest, useful subset: **the encoder frozen, the head trained.**

```
 text ──WordPieceTokenizer──▶ ids, mask, types ──API.Onnx (frozen BERT)──▶ last_hidden_state
                                                                              │ pool: CLS or mean
                                                                              ▼
                                       Float[n, hidden] embeddings ──EmbeddingClassifier──▶ P(malware)
                                                                     (trained here: Adam/SGD, BCE)
```

All three experiments run on this variant. It is **not** the lab's fine-tuning, and every
document and the example say so:

| | Lab (fine-tuning) | This (frozen encoder) |
|---|---|---|
| Trained parameters | all ~110M (encoder + head) | the head only (hidden × 1 up to a few thousand) |
| Embedding of a sequence | changes as training proceeds | fixed; computed once per MAX_SEQ_LENGTH |
| Learning-rate experiment | the encoder's learning rate (1e-5 … 5e-5 suit a pretrained transformer) | the head's learning rate. The lab's values are far too small for a freshly initialised head with Adam over a few epochs; the example runs the lab's three values **and** a head-appropriate sweep (1e-3, 1e-2) so the loss curves show what the rate actually does |
| Imbalance and sequence-length experiments | as in the lab | as in the lab; the head is retrained per setting |
| Expected accuracy | higher: the encoder adapts to API-call text | lower or equal: BERT's pretraining saw English, not API traces |

What fine-tuning would add, for the record: back-propagation through 12 transformer layers
(attention, layer norm, GELU, the embedding tables), an optimizer state of 2–3× the model's
size, and mixed precision to make it affordable. ONNX Runtime has a training API
(`onnxruntime-training`) that could be bound later; nothing here precludes it.

## What an exported BERT looks like

A Hugging Face export (`optimum-cli export onnx --model bert-base-uncased`, or
`torch.onnx.export` with `dynamic_axes`) has:

| Tensor | Element type | Shape | Notes |
|---|---|---|---|
| `input_ids` | INT64 | [batch, sequence] | vocabulary ids |
| `attention_mask` | INT64 | [batch, sequence] | 1 for a real token, 0 for padding |
| `token_type_ids` | INT64 | [batch, sequence] | segment: 0 for the first text, 1 for the second. **Absent** from DistilBERT exports |
| `last_hidden_state` | FP32 | [batch, sequence, hidden] | hidden = 768 for base, 384 for MiniLM |
| `pooler_output` | FP32 | [batch, hidden] | tanh(dense(CLS)). **Optional**: absent from DistilBERT and from exports of `AutoModel` heads that drop it |

`Session->GetModelInfo()` reports each with its element type name (`"INT64"`, `"FP32"`) and its
shape, with **-1 for a dynamic axis** (`batch`, `sequence`). An export made without
`dynamic_axes` reports fixed numbers instead — `[1, 128]` is common.

Tensors cross the native boundary as `Float[]`, and the native side converts each input to the
element type the **model** declares (#1047): the caller builds `Tensor->New("input_ids", shape,
ids)` with `Int[]` ids and never names INT64. `Tensor->AsFloats` refuses a value past 2^53,
far above any vocabulary id.

## API

Three libraries, each where its dependencies already are.

### `System.NLP` (`nlp.obl`): tokenizers — no model, no native code

```objeck
tokenizer := WordPieceTokenizer->New("vocab.txt");            # lowercase + strip accents (uncased BERT)
cased := WordPieceTokenizer->New("vocab.txt", false, false);  # cased BERT

pieces := tokenizer->Tokenize("unaffable");                   # ["un", "##aff", "##able"]
enc := tokenizer->Encode("He's unaffable.", 16);              # TokenEncoding
enc->GetIds();             # [CLS] he ' s un ##aff ##able . [SEP] [PAD]...  (16 ids)
enc->GetAttentionMask();   # 1 x 9, then 0 x 7
enc->GetTokenTypeIds();    # all 0
pair := tokenizer->EncodePair("question", "context", 32);   # types 0 ... 0 1 ... 1
text := tokenizer->Decode(enc->GetIds());                     # "he ' s unaffable ."
```

- **BasicTokenizer**, as BERT's: drop control characters and U+FFFD, map whitespace to a space,
  put spaces around CJK ideographs, split on whitespace, then per word lowercase (option),
  strip accents (option: NFD, then drop combining marks), and split off every punctuation
  character (ASCII 33–47, 58–64, 91–96, 123–126, and Unicode punctuation). The special tokens
  (`[CLS]`, `[SEP]`, `[PAD]`, `[MASK]`, `[UNK]`) are never split when they appear in the text.
- **WordPiece**: greedy longest-match-first over each word, continuation pieces prefixed `##`;
  a word with any unmatched remainder, or longer than 100 characters, becomes one `[UNK]`.
- **Encode(text, max_len)**: `[CLS] pieces [SEP]`, truncated to `max_len` (keeping the
  beginning), padded with `[PAD]` to exactly `max_len`. **EncodePair** truncates the longer
  of the two first, a token at a time (Hugging Face's `longest_first`), then lays out
  `[CLS] a [SEP] b [SEP]` with types 0 then 1.
- Special-token ids are read from the vocabulary by name, never assumed: `[PAD]` is 0 and
  `[CLS]` 101 in Google's vocabularies, but not in every vocabulary.
- **Unicode without a Unicode database.** Objeck's `ToLower` is ASCII-only and there is no NFD
  table in the runtime, so the tokenizer carries its own tables, generated from Python's
  `unicodedata` (see "As built" below); Hangul syllables decompose algorithmically into jamo,
  as NFD does.

```objeck
apis := ApiSequenceTokenizer->New();
apis->Fit(training_sequences);                  # vocabulary from training data
enc := apis->Encode("CreateFileW,ReadFile,CloseHandle", 64);
words := apis->Split("CreateFileW ReadFile");   # ["CreateFileW", "ReadFile"]
text := apis->ToText("CreateFileW,ReadFile", 62); # "CreateFileW ReadFile" -- for WordPiece
```

- For the malware use case: a sequence is API names separated by commas, whitespace or
  semicolons. `Fit` builds a vocabulary `[PAD]`=0, `[UNK]`=1, `[CLS]`=2, `[SEP]`=3, then names
  by descending training frequency (ties by name, so it is deterministic), optionally with a
  minimum count. Names are case-sensitive unless asked otherwise (Windows export names are
  case-sensitive to `GetProcAddress`).
- `Encode` returns the same `TokenEncoding` as WordPiece: `[CLS] names [SEP]`, the first
  `max_len - 2` calls kept, padded.
- **Which tokenizer feeds BERT.** A pretrained BERT knows only its own WordPiece vocabulary;
  ids from a vocabulary built over API names mean nothing to it. So for the frozen encoder,
  the API sequence goes in as text — `ToText` normalises separators and truncates by call
  count — and WordPiece splits `CreateFileW` into `create ##file ##w`. `ApiSequenceTokenizer`'s
  own ids serve a model trained from scratch on API ids, and its vocabulary gives the example's
  bag-of-API baseline.
- `Save(path)` / `Load(path)` write and read the vocabulary one token per line, the
  `vocab.txt` layout.

**As built (stage 1).**

- The Unicode tables (punctuation, control, nonspacing-mark ranges, lowercase runs, and the
  NFD-minus-marks decomposition of 2,061 characters) are generated from Python's
  `unicodedata` by `tools/nlp/gen_wordpiece_tables.py` into a marked block of `nlp.obs`, so
  coverage is every plane, not the Latin/Greek/Cyrillic subset planned above. Text is decoded
  to code points first (a surrogate pair is one code point where `Char` is UTF-16), so the
  same text tokenizes the same on Windows and elsewhere.
- **Parity.** `gen_wordpiece_parity.py` transcribes Hugging Face's slow `BertTokenizer`
  (BasicTokenizer, WordpieceTokenizer, and the special-token split `PreTrainedTokenizer`
  does first) against `unicodedata` and `str.lower()`, builds a 2,163-token vocabulary from
  the corpus, and records ids for three modes (uncased; cased; lowercase keeping accents).
  On every line of `docs/*.md` plus 28 multilingual and edge-case lines — 13,052 texts,
  39,154 cases — the Objeck tokenizer gives identical ids. The committed fixture is a
  234-text sample of the same (`nlp_wordpiece_parity.obs`); `--all` writes the full corpus
  for a local run. Two cases are left out of the fixture: no-strip modes on decomposed text,
  where Hugging Face's NFC step changes the answer and this tokenizer has none.
- **Found on the way:** `String->SubString` and `String->New(Char[])` end a string at U+0000
  (both go through `New(array, false)`, which scans for a NUL), while `Append(Char)` keeps it.
  BERT drops NUL as text, not as an end, so the tokenizer works on code points throughout
  and never cuts the input with `SubString`; the inconsistency in `lang.obs` is reported,
  not changed here.
- **Strings past U+FFFF.** A `Char` value holds any code point on every platform; only
  `String` storage is UTF-16 on Windows, and `String->Append(Char)` writes a surrogate pair
  there (#1185). The tokenizer asks `String->IsUtf16Storage()` rather than probing a `Char`,
  which answers the wrong question.
- `Encode` stops splitting once it has more pieces than `max_len` can hold: truncation keeps
  the beginning, and an API trace of hundreds of calls was otherwise split in full to keep its
  first 126 pieces. The ids are the same; the time halves for long traces.
- Throughput (Windows build box, `obr`, JIT on): the full parity corpus — 39,154
  tokenizations of lines averaging ~60 characters, plus reading and comparing — in 5–7 s;
  `Encode` of a 200-call API trace in 1.4 ms at `max_len` 128 and 2.2 ms at 256.

### `System.NLP.Onnx` (new `nlp_onnx.obl`): the embedder

```objeck
embedder := BertEmbedder->New("model.onnx", "vocab.txt", 128);
if(<>embedder->IsOpen()) {
  EndPoint->GetLastError()->PrintLine();
  return;
};
embedder->SetPooling(BertEmbedder->Pooling->MEAN);   # or CLS, or POOLER when the model has one
embedder->SetBatchSize(16);
X := embedder->Embed(texts);               # Float[texts->Size(), hidden]
embedder->Close();
```

- `New(model, vocab, max_len)` and `New(model, tokenizer, max_len, config)`; the second takes
  a `WordPieceTokenizer` built with other options and the session configuration (`ep`).
- **Pooling.** `CLS` is the hidden state at position 0. `MEAN` is the mean over the positions
  whose attention mask is 1 — padding excluded, as sentence-transformers does it. `POOLER` is
  the model's `pooler_output`, available only when the model has it. The pooling functions are
  public functions over a flat `Float[]` (`MeanPool`, `ClsPool`) so they can be tested and
  reused without a model.
- **Inputs from the signature.** At open, `GetModelInfo` decides what to send: `input_ids`
  always (the open fails without it), `attention_mask` and `token_type_ids` only when the model
  declares them. It asks the run for the one output the pooling needs, by name
  (`last_hidden_state`, `pooler_output`), falling back to the first FP32 output of rank 3 or
  rank 2 respectively for an export that named them otherwise.
- **Fixed axes.** If the model fixes the sequence axis and it is not `max_len`, the open fails
  and says so — silently running a different length would change an experiment. A fixed batch
  axis caps the batch size at that number.
- **Batching.** Texts are encoded and run `batch_size` at a time (default 8), every row padded
  to `max_len`. Padding to `max_len` rather than to the longest text in the batch keeps a
  text's embedding independent of the other texts in its batch, so results do not depend on
  batch size or order. For long API traces, which reach `max_len` anyway, it costs nothing.
- **Memory.** A batch's `last_hidden_state` is batch × max_len × hidden numbers, held as
  64-bit `Float`s once by the run and once more by the tensor it is split into: batch 8 ×
  256 × 768 is 1.6 M values, about 25 MB at peak. The embeddings returned are n × hidden.
- `EmbedEncoded(encodings : TokenEncoding[])` embeds pre-encoded rows — for a caller with its
  own tokenizer or a cached encoding.

**Why a library of its own.** The compiler resolves every external class a linked library
references, eagerly — linking `onnx` without `json` fails with "Unable to resolve external
library class: 'Data.JSON.JsonElement'" even for a program that never touches JSON. So a
`BertEmbedder` in `onnx.obl` that names `WordPieceTokenizer` would make **every** ONNX program
add `-lib nlp`, and one in `nlp.obl` would make every NLP program add ONNX Runtime. A bridge
library is the precedent `query_vision.obl` set for the same reason. A program using it links:

```
obc -src lab.obs -lib models,opencv,cipher,json,onnx,nlp,nlp_onnx
```

or `-lib @vision`, whose group now includes it.

**As built (stage 2).**

- `Pooling` is an enum nested in the class, as `Client->Protocol` is in `API.Inference`:
  `BertEmbedder->Pooling->MEAN`. `Load(model, vocab, max_len)` is the function form of the
  three-argument constructor. An input the model declares other than the three is refused at
  open with its name, rather than left unfilled.
- With no session options the build's own provider runs the model (DirectML on Windows,
  CoreML on macOS, CPU on Linux); `"ep" -> "cpu"` in the options map forces the CPU. The test
  checks both give identical rows. When that default provider cannot open the model — the
  Windows CI runner has no display adapter, so DirectML refuses every session there, as it
  would on a laptop without a DirectX 12 GPU — and no options were given, the embedder opens
  it on the CPU instead and `IsCpuFallback()` says so. Options that name a provider are
  honoured exactly.
- **Found on the way: `-lib @vision` had been broken since `onnx.obl` began linking
  `models.obl`.** The group did not list `models.obl`, and the linker resolves every class a
  linked library names, so every compile using the alias failed ("Unable to resolve external
  library class: 'API.Models.TensorSpec'"), whatever the program used. The group now lists
  `models`, and `nlp_onnx` with `nlp`; `lib_alias_vision.obs` compiles against the alias on
  every platform so a group that is not closed fails the suite.
- **Measured** (Windows build box, `obr`, the build's default provider), embedding 512 API
  traces of 200 calls with MEAN pooling. A real BERT's encoder compute is not in these
  numbers: the model is the tiny one's graph widened to hidden 768 (an embedding lookup), so
  they are the cost of everything around the encoder — encoding, building the tensors, the
  native crossing both ways, splitting the result and pooling:

  | max_len | batch | per text | peak working set |
  |---|---|---|---|
  | 128 | 8 | 1.6 ms | 182 MB |
  | 256 | 1 | 4.9 ms | 120 MB |
  | 256 | 8 | 5.5 ms | 235 MB |
  | 256 | 32 | 2.1 ms | 458 MB |

  Encoding the 512 traces with `EncodeBatch` took a further 0.7 s at 128 and 1.1 s at 256.
  A real BERT's encoder adds its own compute on top, which no model here measures. Peak memory
  grows with batch × max_len × hidden, as the table shows; the batch-8 row at 256 was slower
  than both batch 1 and batch 32 in repeated runs, so 32 is the better setting at that length
  when memory allows.

### `System.ML` (`ml.obl`): `EmbeddingClassifier`

```objeck
head := EmbeddingClassifier->New([64], 0.001, 20, 32, 42);   # hidden sizes, rate, epochs, batch, seed
head->SetOptimizer(EmbeddingClassifier->Optimizer->ADAM);     # or SGD
head->SetClassWeights(1.0, 99.0);                             # negative, positive
head->Fit(X_train, y_train);                                  # Float[,], Bool[]
loss := head->GetLossHistory();                               # Float[epochs]: mean training loss per epoch
p := head->PredictProba(X_test);                              # Float[]
yhat := head->PredictClass(X_test, 0.5);                      # Bool[]
```

- A dense network: zero or more ReLU hidden layers (`[]` is logistic regression), one sigmoid
  output, **binary cross-entropy** loss, optionally class-weighted for imbalance (or
  `SetBalancedWeights()`: n / (2 n_class) per class).
- Minibatch training, shuffled each epoch by the seeded `System.ML.Random`; He-initialised
  weights from the same generator. Adam (β₁ 0.9, β₂ 0.999, ε 1e-8) or plain SGD at the
  configured learning rate. Same seed, same data → the same weights, bit for bit, in every VM
  mode.
- Inputs are standardised with the training set's mean and deviation, stored with the model.
  BERT's dimensions have very different scales, and an unscaled head trains far slower.
- **The loss history** is the mean weighted BCE over the training set after each epoch — one
  number per epoch, for the lab's training-loss plot.
- Implements `ScoreModel`, so `CrossValidation` and the threshold tools in `Metrics` accept it.
- **Reuse.** The System.ML outlier work is adding a multi-layer dense network for its
  autoencoder. If it has landed on master when this stage starts, the head is built on it; if
  not, the head carries its own forward/backward pass and the duplication is noted for a later
  merge.

**As built (stage 3).**

- **No reuse yet.** The outlier work's `DenseNetwork` (docs/ML_OUTLIERS_DESIGN.md §2.5) is its
  stage 2 and had not reached master, so `EmbeddingClassifier` (`ml_embed.obs`) has its own
  dense forward and backward pass. The two overlap in the layer arithmetic and Adam, and
  differ where their jobs do: that network is trained on mean squared error with Glorot
  initialisation throughout; this head on class-weighted binary cross-entropy from the logit
  (a numerically stable `log(1 + e^-|z|)` form), He-uniform before each ReLU and Glorot before
  the sigmoid. Once `DenseNetwork` lands, the head's layers should become one with a sigmoid
  output and a BCE loss option; the public API here does not change.
- Matrix products go through `Matrix2D->DotProduct` (Eigen, in `libobjk_ml`); the elementwise
  work and the optimizer are Objeck. `GetWeights(layer)` / `GetBias(layer)` return copies, for
  inspection and for the gradient test.
- **The loss history** is measured after each epoch on the whole training set with that
  epoch's final weights, class-weighted and without L2 — so it is a curve of the objective,
  not an average of minibatch losses taken while the weights moved. `Loss(X, y)` measures the
  same on any rows, for a validation curve.
- **Found on the way: the AMD64 JIT refused the update method.** With the SGD loops, the Adam
  loops and the bias loops in one method, `OBJECK_JIT_REPORT=1` showed "compile failed at
  instruction 231", and every update ran in the interpreter: 30 s to train a 768→64→1 head
  for 20 epochs on 2,000 rows. With one loop per method the same training takes 1.8 s, with
  identical results. The refusal is a JIT defect (a legal method should compile or spill, not
  fall back silently); a standalone reproduction is reported with this stage rather than
  fixed in it.
- **Measured** (Windows build box, `obr`, JIT on), 20 epochs, batch 32, Adam, balanced
  weights: 2,000 × 768 logistic 0.36 s; 2,000 × 768 → 64 → 1 1.8 s; 500 × 768 → 64 → 1 0.47 s
  (0.31 s with SGD).

### The lab example: `programs/examples/ml/bert_malware_lab.obs`

```
obr bert_malware_lab.obe --model model.onnx --vocab vocab.txt --data traces.csv
obr bert_malware_lab.obe --synthetic            # no external data
```

- **CSV format**: a header row, then `api_sequence,label`; `api_sequence` is the API calls of
  one sample separated by spaces or semicolons (quoted if it contains commas), and `label` is
  `1`/`malware`/`true` or `0`/`benign`/`false`.
- **Synthetic mode** generates a labelled corpus whose malicious traces favour a few API
  families (process injection, registry persistence), and uses the tiny test model and
  vocabulary from `programs/regression/data/bert` when no model is given, so the program runs
  anywhere the ONNX library is deployed.
- Embeddings are computed **once per MAX_SEQ_LENGTH** and reused across every head trained
  at that length.
- **Experiment 1.** The test set is split off first, stratified, and never changes. Each
  training set is drawn from the remaining data at the requested POSRATIO (all positives kept
  up to what the ratio allows, negatives sampled; at 0.001 that can mean one positive). A
  table of precision, recall, F1 and false negatives per ratio, unweighted and class-weighted.
- **Experiment 2.** The same split at each MAX_SEQ_LENGTH; the same table per length.
- **Experiment 3.** One length, the head retrained per learning rate; the per-epoch loss
  histories printed as columns (CSV-ready for plotting), with the final table.

**As built (stage 4).**

- The CSV `api_sequence` field may separate calls with commas as well as spaces and
  semicolons (quoted), and CRLF files read as LF ones. A 300-row file mixing all three, with
  `1`/`malware`/`true` labels, loads with the right counts.
- POSRATIO keeps every training negative and adds the positives the ratio calls for; when the
  pool has too few positives (0.50 usually), it keeps them all and drops negatives instead, and
  the table prints the training size and its positives so the reader sees which happened. At
  0.001 the training set holds a single positive.
- Experiments 2 and 3 train with balanced class weights; experiment 1 shows both.
- **Measured** with `--synthetic` and the tiny test model, 2,000 traces of 40–300 calls
  (Windows, `obr`): 22 s for all three experiments, most of it encoding (2,000 traces at four
  lengths). The tables show the effects in kind — recall collapsing at POSRATIO 0.001 and
  0.01 without class weights; F1 rising with MAX_SEQ_LENGTH (0.27 at 32 to 0.77 at 256), since
  the synthetic malicious calls sometimes start late; the lab's three rates barely moving the
  loss in 20 epochs (0.85 → 0.79 at 1e-5) where 1e-3 and 1e-2 converge — but the tiny model is
  an embedding lookup, so the numbers say nothing about BERT.
- The six VM modes (`-opt s3`/`s0` × JIT, `--jit=off`, `OBJECK_JIT_THRESHOLD=1`) print
  identical tables (`--samples 400 --epochs 6`).

## Test plan

Correctness is the point. Every test is in `programs/regression`, exits non-zero on failure,
and runs at `-opt s3` and `-opt s0`, under `obr`, `obr --jit=off` and with
`OBJECK_JIT_THRESHOLD=1`, with identical output.

**The test model.** A real BERT is hundreds of megabytes and the regression tree ships no
model. What the tests need is BERT's *signature*, with numbers simple enough to recompute.
`programs/regression/data/bert/gen_tiny_bert.py` (needs the `onnx` package; checks its output
with `onnxruntime` when present) writes three models with the export signature above and
hidden size 8:

```
last_hidden_state[b,s,j] = word[id,j] + type[tt,j] + pos[s,j] + 0.25 * attention_mask[b,s]
pooler_output[b,j]       = tanh(sum_k last_hidden_state[b,0,k] * Wp[k,j] + bp[j])
```

Every table is a closed form of its indices with values that are multiples of 1/32, exact in
FP32, so a test recomputes every output itself, without numpy and without reading the model.
The mask term wires `attention_mask` into the output and keeps padded positions non-zero, so
a wrong mask, or a mean pool that forgets to exclude padding, gives a different answer.

| File | Inputs | Outputs | Stands in for |
|---|---|---|---|
| `tiny_bert.onnx` | ids, mask, types `[-1,-1]` | `last_hidden_state [-1,-1,8]`, `pooler_output [-1,8]` | BERT, dynamic axes |
| `tiny_bert_nopool.onnx` | ids, mask | `last_hidden_state` | DistilBERT |
| `tiny_bert_fixed.onnx` | ids, mask, types `[1,16]` | both, fixed | an export without `dynamic_axes` |

`vocab.txt` beside them (64 tokens: the specials, the BERT paper's `un ##aff ##able`, and
pieces that split Windows API names) is the tests' vocabulary. `onnx_tiny_bert_model.obs`
checks the assets alone, through the generic `Session`, on every platform's ONNX Runtime
(1.19 Linux, 1.22 Windows, 1.30 macOS) before anything is built on them.

**Stage tests.**

- *Tokenizer parity.* `transformers` is not installed here (and is not to be installed). The
  reference is therefore twofold: hand-computed ids for the BERT paper's examples and edge
  cases against the test vocabulary; and a transcription of Hugging Face's `BasicTokenizer` /
  `WordpieceTokenizer` in Python using `unicodedata` (true NFD and categories) run over a
  corpus — the repository's docs plus Unicode samples — whose (text, ids) pairs are committed
  as a fixture the Objeck test replays. Edge cases: empty text, whitespace only, punctuation
  only, a word over 100 characters, a text far past `max_len`, control characters, CJK,
  accents, Hangul, emoji, `max_len` of 2 and 3, pairs where either side is empty.
- *Pooling.* `ClsPool` and `MeanPool` against a reference loop in the test, padding included
  and excluded; the embedder end to end against the closed form, for all three models and all
  three poolings; `POOLER` refused on the model without one; a fixed-axis mismatch refused.
- *Batching and determinism.* The same texts embedded with batch sizes 1, 3 and 8 and in a
  shuffled order give identical rows; the same in all six VM modes.
- *Head.* Converges to 100% training accuracy on linearly separable embeddings; on a convex
  problem (no hidden layer, small rate) the per-epoch loss is non-increasing to within 1e-9
  after the first epoch; class weights move the decision toward the rare class; Adam and SGD
  both reduce the loss; same seed → identical weights and probabilities; `PredictClass`
  thresholds; refusals for mismatched shapes and empty data.
- *Example.* Runs in synthetic mode in under a minute and prints the three tables.

## Stages

Each is its own pull request against master, merged before the next.

0. This document; the tiny-model generator, the three models and the vocabulary;
   `onnx_tiny_bert_model.obs`.
1. `WordPieceTokenizer`, `TokenEncoding`, `ApiSequenceTokenizer` in `nlp.obs`; the parity
   fixture and tests; `nlp.obl` rebuilt; the editor index.
2. `nlp_onnx.obs` with `BertEmbedder`; every library list; tests; memory and throughput.
3. `EmbeddingClassifier` in System.ML; tests.
4. The lab example and its documentation.
