#!/usr/bin/env python3
"""
Manual cross-check of System.NLP.Topics against Gensim (not run by CI; CI has no Python deps).

Needs gensim (tested with 4.4.0), numpy and scipy. Run: python nlp_topics_gensim_check.py [coherence|corpus]

  coherence  compares gensim.models.CoherenceModel (u_mass, c_npmi window 3/10, c_v window 3/110, explicit
             topics, processes=1) with the plain-Python reference in nlp_topics_reference.py, whose values
             nlp_topics_metrics.obs asserts for the Objeck code, on the fixed 10-text corpus, and prints a
             second, longer random corpus (the values pasted into nlp_topics_metrics.obs as the "sliding"
             case, where texts are longer than the window)
"""
import importlib.util
import os
import random
import sys
import warnings

warnings.filterwarnings("ignore")
HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ref", os.path.join(HERE, "nlp_topics_reference.py"))
ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref)


def gensim_values(texts, topics, topn):
    from gensim.corpora import Dictionary
    from gensim.models import CoherenceModel
    d = Dictionary(texts)
    corpus = [d.doc2bow(t) for t in texts]

    def g(kind, **kw):
        cm = CoherenceModel(topics=topics, texts=texts, corpus=corpus, dictionary=d, coherence=kind,
                            topn=topn, processes=1, **kw)
        return [float(x) for x in cm.get_coherence_per_topic()]
    out = {"u_mass": g("u_mass")}
    for w in (3, 10):
        out["c_npmi%d" % w] = g("c_npmi", window_size=w)
    for w in (3, 110):
        out["c_v%d" % w] = g("c_v", window_size=w)
    return out


def reference_values(texts, topics):
    out = {"u_mass": ref.u_mass(topics, texts)}
    for w in (3, 10):
        out["c_npmi%d" % w] = ref.c_npmi(topics, texts, w)
    for w in (3, 110):
        out["c_v%d" % w] = ref.c_v(topics, texts, w)
    return out


def random_corpus():
    rnd = random.Random(5)
    words = ["w%d" % i for i in range(15)]
    texts = []
    for d in range(40):
        base = (d % 3) * 5
        n = rnd.randint(5, 30)
        texts.append([words[base + rnd.randrange(5)] if rnd.random() < 0.7 else rnd.choice(words) for _ in range(n)])
    topics = [words[0:5], words[5:10], words[10:15]]
    return texts, topics


def compare(name, texts, topics, topn):
    g = gensim_values(texts, topics, topn)
    r = reference_values(texts, topics)
    worst = 0.0
    for k in g:
        diff = max(abs(a - b) for a, b in zip(g[k], r[k]))
        worst = max(worst, diff)
        print("%s %-9s gensim %s  max|gensim - reference| = %.2e" % (name, k, " ".join("%.12f" % x for x in g[k]), diff))
    return worst


if __name__ == "__main__":
    worst = compare("fixed ", ref.COHERENCE_TEXTS, ref.COHERENCE_TOPICS, 4)
    texts, topics = random_corpus()
    worst = max(worst, compare("random", texts, topics, 5))
    print("TEXTS " + " | ".join(" ".join(t) for t in texts))
    print("worst difference %.2e" % worst)
    sys.exit(0 if worst < 1e-9 else 1)
