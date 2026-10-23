# System.NLP: from a demo to a usable toolkit — Design

`core/compiler/lib_src/nlp.obs`, built into `core/lib/nlp.obl` against `gen_collect` alone.
Pure Objeck; no model, no native module, and this design keeps it that way.

## Why

A language aimed at AI research will have people reach for its NLP library early, and on
2026-10-22 a probe of the shipped library (v2026.10.2) found it was not fit for real text:

| Part | Probe result |
|---|---|
| Sentiment | 4 of 10 ordinary sentences right. "not good" scored positive, "Not bad at all" negative; "loved", "hated", "boring" were unknown. |
| Stemmer | Documented as Porter; agreed with Porter on 11 of 19 words (`running`→`runn`, `speed`→`spe`, `makes`→`mak`, `connection`→`connec`). |
| Word tokenizer | `don't`→`don`,`t`; `3.14`→`3`,`14`; split on the space character only, so `first\nsecond\tthird` was one token. |
| Sentence splitter | `Dr. Smith paid $3.50 in the U.S. today.` became 5 pieces; `Wait...` became 3. |
| TF-IDF | A second `Fit` kept the first corpus's vocabulary. Unsmoothed IDF: a one-document corpus gave all-zero vectors. |
| Similarity | `NormalizedEditDistance(Nil, "abc")` aborted the program. Jaccard of two empty sets was 0. |

Nothing caught any of it: the only test, `programs/tests/nlp_test.obs`, prints and never
fails, and it is not in the regression suite.

## Decisions

### 1. The public API stays; behaviour changes where it was wrong

Every existing class and method keeps its name and signature. Additions are new methods.
Where the old output was simply wrong (the stemmer, tokenization, an all-zero TF-IDF vector,
"not good" as positive) the new output replaces it without a flag: keeping a wrong answer
available is not a feature anyone would choose. The changes are listed in the changelog.

### 2. Word tokenizer: a character scanner

A token is a maximal run of word characters, where:

- a word character is a letter (any case), a digit, or any character above U+007F that is not
  in a known punctuation block (General Punctuation U+2000–U+206F, Latin-1 punctuation
  U+00A1–U+00BF, CJK punctuation U+3000–U+303F, the full-width ASCII punctuation), so
  accented and non-Latin words survive;
- an apostrophe (`'` or U+2019) between two letters stays inside the word: `don't`, `it's`,
  `O'Brien`. Contractions are kept whole rather than split Penn-style, because the sentiment
  analyzer treats a word ending in `n't` as a negator and the stopword list is word-based;
- a hyphen between two letters or digits stays inside: `state-of-the-art`, `e-mail`, `COVID-19`;
- `.` or `,` between two digits stays inside: `3.14`, `5,000`.

Everything else (all whitespace, ASCII and Unicode punctuation, `$`, `/`) separates tokens and
is dropped, as before. Case is preserved, as before.

### 3. Sentence splitter

A sentence ends at a run of `.`, `!` or `?` (so `...` and `?!` are one boundary), optionally
followed by closing quotes or brackets, then whitespace or the end of the text. A `.` does
not end a sentence when the word before it is a known abbreviation (`Mr`, `Mrs`, `Ms`, `Dr`,
`Prof`, `Sr`, `Jr`, `St`, `vs`, `etc`, `e.g`, `i.e`, `U.S`, `U.K`, `Inc`, `Ltd`, `Co`, `Corp`,
`Fig`, `approx`, `Dept`, `est`, `Jan` … `Dec` without `May`) or a single capital letter other
than `I` (an initial, `J. R. R. Tolkien`). `No` is left out because "the answer is no." ends a
sentence far more often than "No. 5" continues one. A `.` with no whitespace after it
(`3.50`, `U.S.A`) never ends one, and `…` (U+2026) counts as a terminator.

This is a rule-based splitter, the same family as the Punkt defaults with a fixed list. It
does not split after an abbreviation that really does end a sentence (`… in the U.S. Then …`
stays one sentence); that is the accepted trade-off of every list-based splitter.

### 4. Stemmer: the Porter algorithm, as the reference implementation

`TextPreprocessor->Stem` becomes the Porter (1980) algorithm, steps 1a through 5b, with the
two departures Martin Porter's own reference implementation makes (`bli`→`ble` and
`logi`→`log` in step 2), so its output matches the published vocabulary/output pairs. Stems
are not words (`happi`, `relat`); that is how Porter works and what every Porter-based tool
returns. Input is lowercased; words of one or two letters, and words holding anything but
`a`–`z`, are returned lowercased and otherwise unchanged.

**As built:** checked against NLTK 3.9.1's `PorterStemmer(mode=MARTIN_EXTENSIONS)`, which
is Porter's reference implementation, on the 8,559 distinct words in the repository's docs
and Rosetta programs: all 8,559 agree. Against `ORIGINAL_ALGORITHM` the 21 differences are
the `bli`/`logi` departures and two-letter words, which the paper stems (`as` → `a`) and the
reference does not.

### 5. Sentiment: a valence lexicon with VADER's rules

The analyzer becomes rule-based in the manner of VADER (Hutto & Gilbert, 2014), with its own
lexicon:

- **Lexicon.** A few hundred English words with valences on VADER's −4…+4 scale, written for
  this library (not copied from VADER's). Lookups try the lowercased token, then its Porter
  stem against the stems of the lexicon entries, so `loved`, `loves` and `loving` find `love`.
  `SentimentAnalyzer->AddWord(word, valence)` adds or overrides an entry, for domain
  vocabularies.
- **Negation.** A negator (`not`, `no`, `never`, `none`, `nobody`, `nothing`, `neither`, `nor`,
  `without`, `hardly`, `barely`, `cannot`, or any word ending in `n't`) within the three tokens
  before a sentiment word multiplies its valence by −0.74.
- **Boosters and dampeners.** `very`, `really`, `extremely`, `absolutely`, `so`, `totally` …
  add 0.293 in the direction of the word they precede; `slightly`, `somewhat`, `kind of` …
  subtract it. The effect decays by 5% and 10% at two and three tokens away.
- **Emphasis.** A sentiment word in capitals, in text that is not all capitals, gains 0.733.
  Each `!` (up to four) adds 0.292 in the direction of the total.
- **Contrast.** Valences before `but` are halved and those after it are multiplied by 1.5.
- **Normalization.** `compound = sum / sqrt(sum² + 15)`, in −1…1. `Classify` returns
  `positive` at compound ≥ 0.05, `negative` at ≤ −0.05, else `neutral` (VADER's thresholds).

`Analyze` keeps returning an `Int`: the summed valence rounded to the nearest integer, so its
sign is still the label. New: `AnalyzeCompound(text) ~ Float`, and `SentimentResult` gains
`GetCompound()`.

### 6. TF-IDF

- `Fit` starts from an empty vocabulary every time.
- IDF is smoothed by default, as scikit-learn's: `ln((1 + N) / (1 + df)) + 1`. A term in every
  document now weighs 1 instead of 0; a one-document corpus gives a useful vector.
- Vectors are L2-normalized by default, so `CosineSimilarity` between two of them is their dot
  product and long documents do not dominate.
- Both are constructor options, with defaults: `New(smooth_idf : Bool := true,
  l2_normalize : Bool := true, remove_stopwords : Bool := false, stem : Bool := false)`.
  With `stem`, `cat` and `cats` share a column.
- `GetIdf()` returns the IDF vector, indexed like the vocabulary.

### 7. Smaller fixes

- `NormalizedEditDistance` treats `Nil` as the empty string, as `LevenshteinDistance` does.
- `JaccardSimilarity` of two empty sets is 1.0 (identical sets), the usual convention.
- `CosineSimilarity` is documented as −1…1; it was documented as 0…1.
- The sentiment lexicon's duplicate entries (`outstanding` in two lists, `disappointing` twice)
  go away with the new lexicon.

## Tests

`programs/regression/nlp_*.obs`, `# EXTRA_LIBS: nlp`, each exiting non-zero on failure, and a
`nlp` category in `gen_manifest.py`:

- `nlp_tokenize.obs`: every case in the table above, plus Unicode, newlines and empty input.
- `nlp_porter.obs`: the worked examples from Porter's paper, every step, about 80 pairs.
- `nlp_sentiment.obs`: negation, boosters, `but`, capitals, `!`, stemmed lookup, `AddWord`,
  and a fixed set of 30 labelled sentences that must score at least 27/30.
- `nlp_tfidf.obs`: refit, smoothing, normalization, stemming, one-document corpus, and the
  similarity fixes.

`programs/tests/nlp_test.obs` stays as a demo.

## Phases

1. Bug fixes (section 7, TF-IDF refit, whitespace) and the test scaffolding.
2. Tokenizer and sentence splitter.
3. Porter stemmer.
4. Sentiment.
5. TF-IDF options; rebuild `core/lib/nlp.obl`; regenerate the editor API index.

## Not in scope

Part-of-speech tagging, named entities, lemmatization, embeddings and transformer models. The
last two belong to `API.Onnx` and `API.Inference`, which already run any ONNX model; a
sentence-embedding example there is the better route to modern NLP than growing this library
into one.
