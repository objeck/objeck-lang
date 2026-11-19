#!/usr/bin/env python3
"""
Compares System.NLP.Topics.Lda with gensim.models.LdaModel on two synthetic corpora with planted topics.
Manual tool, not run by CI. Needs gensim (tested with 4.4.0), numpy, scipy and a built Objeck toolchain.

  python compare.py --obc PATH/obc --obr PATH/obr --lib PATH/lib [--work DIR]

Corpus A: 600 documents, 5 planted topics of 20 words, 30 background words, 20 tokens per document.
Corpus B: 3000 tweet-like documents, 6 planted topics of 12 words, words shared by every topic, English function
words and a pool of rare noise tokens (function words and shared words stay in: no stopword removal).
Both systems use the same settings (num_topics, alpha and eta symmetric floats, passes, iterations 50, chunk size,
decay 0.5, offset 1, gamma threshold 0.001); the random generators differ, so seeds are not shared and nothing is
expected to match digit for digit. Printed: topic recovery (document purity against the planted truth), the
per-word bound (gensim LdaModel.log_perplexity against Lda.LogPerplexityBound), topic-word agreement (mean best-match
cosine of the two models' topic-word vectors), document-topic agreement (mean cosine of the two models' mixtures
after matching topics one-to-one by the Hungarian method on topic-word cosine), and training time.
"""
import argparse
import os
import random
import subprocess
import sys
import time
import warnings

import numpy as np
from scipy.optimize import linear_sum_assignment

warnings.filterwarnings("ignore")
from gensim.corpora import Dictionary
from gensim.models import LdaModel

HERE = os.path.dirname(os.path.abspath(__file__))


def corpus_a():
    r = random.Random(11)
    docs, truth = [], []
    for _ in range(600):
        t = r.randrange(5)
        truth.append(t)
        docs.append(["t%dw%d" % (t, r.randrange(20)) if r.random() < 0.8 else "b%d" % r.randrange(30) for _ in range(20)])
    return docs, truth, 5, 600


def corpus_b():
    r = random.Random(7)
    shared = ["security", "attack", "new", "report", "users", "company", "hackers", "data", "researchers", "warns"]
    func = ["the", "of", "in", "to", "is", "and", "for", "on", "this", "that", "with", "was", "are", "by"]
    docs, truth = [], []
    for _ in range(3000):
        t = r.randrange(6)
        truth.append(t)
        d = []
        for _ in range(r.randint(8, 14)):
            x = r.random()
            if x < 0.6:
                d.append("p%dw%d" % (t, r.randrange(12)))
            elif x < 0.75:
                d.append(r.choice(shared))
            elif x < 0.9:
                d.append(r.choice(func))
            else:
                d.append("h%d" % r.randrange(700))
        docs.append(d)
    return docs, truth, 6, 500


def purity(theta, truth, k, nt):
    counts = np.zeros((k, nt), dtype=int)
    for d, row in enumerate(theta):
        counts[int(np.argmax(row)), truth[d]] += 1
    return counts.max(axis=1).sum() / len(truth)


def cos_rows(a, b):
    a = a / np.linalg.norm(a, axis=1, keepdims=True)
    b = b / np.linalg.norm(b, axis=1, keepdims=True)
    return a @ b.T


def run_objeck(args, path, k, alpha, passes, chunk, seed, out):
    env = dict(os.environ, OBJECK_LIB_PATH=args.lib)
    t = time.time()
    subprocess.run([args.obr, os.path.join(args.work, "lda_dump.obe"), path, str(k), str(alpha), str(passes), "50",
                    str(chunk), str(seed), out], check=True, env=env, capture_output=True)
    res = {"wall": time.time() - t}
    phi, theta = [], []
    for line in open(out, encoding="utf-8"):
        tag, _, rest = line.rstrip("\n").partition(" ")
        if tag == "BOUND":
            res["bound"] = float(rest)
        elif tag == "SECONDS":
            res["secs"] = float(rest)
        elif tag == "VOCAB":
            res["vocab"] = rest.split(" ")
        elif tag == "PHI":
            phi.append([float(x) for x in rest.split(" ")])
        elif tag == "THETA":
            theta.append([float(x) for x in rest.split(" ")])
    res["phi"], res["theta"] = np.array(phi), np.array(theta)
    return res


def run_gensim(docs, k, alpha, passes, chunk, seed, vocab):
    d = Dictionary()                             # same id order as Objeck's dictionary (doc2bow would sort new tokens)
    d.token2id = {t: i for i, t in enumerate(vocab)}
    corpus = [d.doc2bow(t) for t in docs]
    a = alpha if alpha > 0 else 1.0 / k
    t = time.time()
    m = LdaModel(corpus=corpus, id2word=d, num_topics=k, alpha=a, eta=1.0 / k, passes=passes, iterations=50,
                 chunksize=chunk, decay=0.5, offset=1.0, gamma_threshold=0.001, update_every=1, eval_every=None,
                 random_state=seed, minimum_probability=0.0)
    secs = time.time() - t
    theta = np.array([[p for _, p in m.get_document_topics(c, minimum_probability=0.0)] for c in corpus])
    phi = m.get_topics()
    return {"bound": float(m.log_perplexity(corpus)), "secs": secs, "phi": phi, "theta": theta}


def agree(o, g):
    c = cos_rows(o["phi"], g["phi"])
    best = 0.5 * (c.max(axis=1).mean() + c.max(axis=0).mean())
    r, col = linear_sum_assignment(-c)           # topic r of Objeck <-> topic col of Gensim
    ot, gt = o["theta"][:, r], g["theta"][:, col]
    dc = np.sum(ot * gt, axis=1) / (np.linalg.norm(ot, axis=1) * np.linalg.norm(gt, axis=1))
    return best, float(dc.mean())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--obc", required=True)
    ap.add_argument("--obr", required=True)
    ap.add_argument("--lib", required=True)
    ap.add_argument("--work", default=os.path.join(HERE, "work"))
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)
    env = dict(os.environ, OBJECK_LIB_PATH=args.lib)
    subprocess.run([args.obc, "-src", os.path.join(HERE, "lda_dump.obs"), "-lib", "nlp_topics,json,plot,gen_collect",
                    "-dest", os.path.join(args.work, "lda_dump.obe")], check=True, env=env, capture_output=True)

    for name, make in (("A planted", corpus_a), ("B tweet-like", corpus_b)):
        docs, truth, nt, chunk = make()
        path = os.path.join(args.work, "corpus_%s.txt" % name[0])
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("".join(" ".join(d) + "\n" for d in docs))
        k, base_passes = nt, 20
        print("\n=== corpus %s: %d documents, %d planted topics, K = %d ===" % (name, len(docs), nt, k))

        def both(alpha, passes, seed, tag):
            o = run_objeck(args, path, k, alpha, passes, chunk, seed, os.path.join(args.work, "o_%s.txt" % tag))
            g = run_gensim(docs, k, alpha, passes, chunk, seed, o["vocab"])
            ba, da = agree(o, g)
            return o, g, ba, da

        print("seeds (alpha 1/K, 20 passes): merged-topic fits score lower in both systems?")
        print("%-5s | %-26s | %-26s | topic-word | mixture" % ("seed", "Objeck bound / purity", "Gensim bound / purity"))
        rows = []
        for seed in range(1, 9):
            o, g, ba, da = both(0, base_passes, seed, "seed%d" % seed)
            po, pg = purity(o["theta"], truth, k, nt), purity(g["theta"], truth, k, nt)
            rows.append((o["bound"], po, g["bound"], pg))
            print("%-5d | %10.4f / %6.3f       | %10.4f / %6.3f       | %9.3f  | %7.3f" % (seed, o["bound"], po, g["bound"], pg, ba, da))
        ob, op, gb, gp = (np.array(x) for x in zip(*rows))
        print("correlation of bound and purity across seeds: Objeck %.3f, Gensim %.3f; best-bound run: Objeck purity %.3f, Gensim purity %.3f"
              % (np.corrcoef(ob, op)[0, 1] if op.std() > 0 else float("nan"), np.corrcoef(gb, gp)[0, 1] if gp.std() > 0 else float("nan"),
                 op[int(np.argmax(ob))], gp[int(np.argmax(gb))]))

        print("passes (alpha 1/K, seed 1):")
        print("%-7s | Objeck bound  purity  secs | Gensim bound  purity  secs" % "passes")
        for p in (1, 5, 10, 20):
            o, g, _, _ = both(0, p, 1, "p%d" % p)
            print("%-7d | %12.4f  %6.3f  %5.2f | %12.4f  %6.3f  %5.2f" % (p, o["bound"], purity(o["theta"], truth, k, nt), o["secs"], g["bound"], purity(g["theta"], truth, k, nt), g["secs"]))

        print("alpha (20 passes, seed 1): mean top-topic probability per document")
        print("%-7s | Objeck bound  mean max p | Gensim bound  mean max p | mixture agreement" % "alpha")
        for a in (0.01, 0.1, 1.0):
            o, g, _, da = both(a, 20, 1, "a%s" % a)
            print("%-7s | %12.4f  %9.3f | %12.4f  %9.3f | %7.3f" % (a, o["bound"], o["theta"].max(axis=1).mean(), g["bound"], g["theta"].max(axis=1).mean(), da))


if __name__ == "__main__":
    sys.exit(main())
