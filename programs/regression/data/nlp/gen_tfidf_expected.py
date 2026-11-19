#!/usr/bin/env python3
r"""Expected values for programs/regression/nlp_tfidf_ngrams.obs, from scikit-learn.

Prints Objeck array literals for each configuration: the vocabulary terms, the smoothed
IDF of each and the L2-normalised TF-IDF vector of two query documents, all from
sklearn.feature_extraction.text.TfidfVectorizer (default smooth_idf=True, norm='l2').
token_pattern r"(?u)\b\w+\b" so one-letter words count as System.NLP.Tokenizer's do. The
test pastes this output; rerun to regenerate:  python gen_tfidf_expected.py
(needs scikit-learn; the values were produced with scikit-learn 1.9.1).
"""
from sklearn.feature_extraction.text import TfidfVectorizer

DOCS = ["the cat sat on the mat", "the dog sat on the log", "cats and dogs are pets", "the cat and the dog"]
QUERIES = ["The cat sat, the dog sat!", "a cat and a dog on the mat today"]
CONFIGS = [
    ("unigrams", dict(ngram_range=(1, 1))),
    ("bigrams", dict(ngram_range=(1, 2))),
    ("trigrams", dict(ngram_range=(1, 3))),
    ("only_bigrams", dict(ngram_range=(2, 2))),
    ("min_df_2", dict(ngram_range=(1, 2), min_df=2)),
    ("max_features_2", dict(ngram_range=(1, 1), max_features=2)),
]
# the max_features configuration runs on this corpus instead: a:5, b:3, then three words once each
DOCS_MAX = ["a a a b b c", "a b d", "a e"]


def lit(values, fmt):
    return "[" + ", ".join(fmt(v) for v in values) + "]"


def main():
    for name, kw in CONFIGS:
        v = TfidfVectorizer(token_pattern=r"(?u)\b\w+\b", **kw)
        docs = DOCS_MAX if "max_features" in kw else DOCS
        v.fit(docs)
        terms = list(v.get_feature_names_out())
        if "max_features" in kw:
            # a tie at the cutoff would make the kept set depend on tie-breaking
            raw = TfidfVectorizer(token_pattern=v.token_pattern, ngram_range=kw["ngram_range"], use_idf=False, norm=None).fit(docs)
            ranked = sorted(raw.transform(docs).sum(axis=0).A1, reverse=True)
            assert ranked[kw["max_features"] - 1] > ranked[kw["max_features"]], "tie at the max_features cutoff"
        print("# --- %s %s: %d terms, corpus %r" % (name, kw, len(terms), docs))
        print("terms_%s := %s;" % (name, lit(terms, lambda t: '"%s"' % t)))
        print("idf_%s := %s;" % (name, lit(v.idf_, lambda x: repr(float(x)))))
        for qi, q in enumerate(QUERIES):
            row = v.transform([q]).toarray()[0]
            print("vec%d_%s := %s;" % (qi, name, lit(row, lambda x: repr(float(x)))))


if __name__ == "__main__":
    main()
