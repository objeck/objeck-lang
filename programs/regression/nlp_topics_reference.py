#!/usr/bin/env python3
"""
Independent reference values for the System.NLP.Topics regression tests.

Nothing here calls Objeck. It is a second implementation, in numpy and scipy, written from the
published algorithms and from Gensim's documented behaviour, whose output is pasted into the
expectations of nlp_topics_lda.obs and nlp_topics_metrics.obs:

  1. special functions (digamma, trigamma, log-gamma) from scipy.special
  2. online variational Bayes LDA (Hoffman, Blei and Bach 2010; the form Gensim's LdaModel uses):
     the same random generator, initialisation and update order as the Objeck library, so the
     two must agree to rounding error. This checks the Objeck code (indexing, update order,
     the bound), not the algorithm's choice of formulas.
  3. u_mass, c_npmi and c_v coherence, written out from Roeder et al. 2015 and the structure of
     Gensim's topic_coherence package (segmentation, probability estimation, confirmation measure,
     aggregation)

Gensim itself was NOT installed when these values were produced, so no number here was compared
with a Gensim run. Run: python nlp_topics_reference.py
"""
import math
import numpy as np
from scipy.special import digamma, polygamma, gammaln

# ---------------------------------------------------------------- 1. special functions
def special():
    print("# special functions")
    for x in (0.01, 0.5, 1.0, 2.5, 10.0, 123.456):
        print("digamma(%r) = %.15g  trigamma = %.15g  lgamma = %.15g" % (x, digamma(x), polygamma(1, x), gammaln(x)))

# ---------------------------------------------------------------- 2. LDA reference
class Minstd:
    def __init__(self, seed):
        r = seed % 2147483646
        self.state = r + 1
    def uniform(self):
        self.state = (self.state * 48271) % 2147483647
        return self.state / 2147483647.0
    def gamma(self, shape, scale):
        d = shape - 1.0 / 3.0
        c = 1.0 / math.sqrt(9.0 * d)
        while True:
            u1 = self.uniform()
            u2 = self.uniform()
            x = math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2)
            t = 1.0 + c * x
            if t > 0.0:
                v = t * t * t
                u = self.uniform()
                if math.log(u) < 0.5 * x * x + d - d * v + d * math.log(v):
                    return d * v * scale

def dirichlet_expectation(a):
    return digamma(a) - digamma(a.sum())

def fit_lda(docs, V, K, alpha_value, mode, eta_value, passes, iterations, seed, chunk, decay=0.5, offset=1.0, thr=0.001):
    """docs: list of (ids array, counts array). mode 0 symmetric, 1 asymmetric, 2 auto."""
    D = len(docs)
    eta = eta_value if eta_value > 0 else 1.0 / K
    sym = alpha_value if alpha_value > 0 else 1.0 / K
    if mode == 1:
        alpha = np.array([1.0 / (k + math.sqrt(K)) for k in range(K)])
        alpha /= alpha.sum()
    else:
        alpha = np.full(K, sym)
    rng = Minstd(seed)
    sstats = np.zeros((V, K))
    for w in range(V):
        for k in range(K):
            sstats[w, k] = rng.gamma(100.0, 0.01)

    def exp_elogbeta():
        lam = sstats + eta
        return np.exp(digamma(lam) - digamma(lam.sum(axis=0))[None, :])

    eb = exp_elogbeta()
    chunk = min(chunk, D)

    def infer(ids, cts, eb, alpha, acc=None):
        gamma = np.ones(K)
        if len(ids) == 0:
            return alpha.copy()
        etheta = np.exp(dirichlet_expectation(gamma))
        ebd = eb[ids]                       # n x K
        phinorm = ebd @ etheta + 1e-100
        for _ in range(iterations):
            tmp = (cts / phinorm) @ ebd
            fresh = alpha + etheta * tmp
            change = np.abs(fresh - gamma).sum()
            gamma = fresh
            etheta = np.exp(dirichlet_expectation(gamma))
            phinorm = ebd @ etheta + 1e-100
            if change / K < thr:
                break
        if acc is not None:
            np.add.at(acc, ids, np.outer(cts / phinorm, etheta))
        return gamma

    seen = 0
    for p in range(passes):
        cs = 0
        while cs < D:
            ce = min(cs + chunk, D)
            nb = ce - cs
            batch = np.zeros((V, K))
            gammas = np.zeros((nb, K))
            for d in range(cs, ce):
                gammas[d - cs] = infer(docs[d][0], docs[d][1], eb, alpha, batch)
            rho = (offset + p + seen / chunk) ** (-decay)
            if mode == 2:
                logphat = np.array([dirichlet_expectation(g) for g in gammas]).sum(axis=0) / nb
                prior = alpha
                gradf = nb * (digamma(prior.sum()) - digamma(prior) + logphat)
                c = nb * polygamma(1, prior.sum())
                q = -nb * polygamma(1, prior)
                b = np.sum(gradf / q) / (1.0 / c + np.sum(1.0 / q))
                dprior = -(gradf - b) / q
                upd = rho * dprior + prior
                if np.all(upd > 0):
                    alpha = upd
            scale = rho * D / nb
            sstats = (1.0 - rho) * sstats + scale * batch * eb
            eb = exp_elogbeta()
            seen += nb
            cs = ce
    return dict(sstats=sstats, eb=eb, alpha=alpha, eta=eta, infer=infer, V=V, K=K)

def bound(m, docs):
    """Gensim LdaModel.bound over the whole corpus, divided by the number of words (log_perplexity)."""
    V, K, alpha, eta = m["V"], m["K"], m["alpha"], m["eta"]
    lam = m["sstats"] + eta                         # V x K
    elogbeta = digamma(lam) - digamma(lam.sum(axis=0))[None, :]
    score = 0.0
    words = 0.0
    for ids, cts in docs:
        gammad = m["infer"](ids, cts, m["eb"], alpha)
        elt = dirichlet_expectation(gammad)
        for w, c in zip(ids, cts):
            x = elt + elogbeta[w]
            mx = x.max()
            score += c * (mx + math.log(np.exp(x - mx).sum()))
        score += np.sum((alpha - gammad) * elt)
        score += np.sum(gammaln(gammad) - gammaln(alpha))
        score += gammaln(alpha.sum()) - gammaln(gammad.sum())
        words += cts.sum()
    score += np.sum((eta - lam) * elogbeta)
    score += np.sum(gammaln(lam) - gammaln(eta))
    score += np.sum(gammaln(V * eta) - gammaln(lam.sum(axis=0)))
    return score / words

# a small fixed corpus: 3 planted topics over 12 words, 30 documents; the generator is the same
# Park-Miller stream as the library's, so the corpus text can be regenerated exactly
def make_corpus():
    r = Minstd(2024)
    def below(n):
        r.state = (r.state * 48271) % 2147483647
        return r.state % n
    topics = [[0, 1, 2, 3], [4, 5, 6, 7], [8, 9, 10, 11]]
    docs = []
    for d in range(30):
        t = d % 3
        toks = []
        for i in range(10):
            if below(10) < 8:
                toks.append(topics[t][below(4)])
            else:
                toks.append(below(12))
        docs.append(toks)
    return docs

def bow(docs):
    """ids by first appearance in the whole corpus, counts by first appearance in the document: the library's ToCorpus order."""
    idmap = {}
    out = []
    for toks in docs:
        for w in toks:
            if w not in idmap:
                idmap[w] = len(idmap)
        seen = {}
        for w in toks:
            seen[idmap[w]] = seen.get(idmap[w], 0) + 1
        out.append((np.array(list(seen.keys()), dtype=int), np.array(list(seen.values()), dtype=float)))
    return idmap, out

def lda_reference():
    docs_tokens = make_corpus()
    print("# corpus (one document per line, words w0..w11)")
    for toks in docs_tokens:
        print("CORPUS " + " ".join("w%d" % t for t in toks))
    idmap, docs = bow(docs_tokens)
    V = len(idmap)
    print("# V = %d" % V)
    for name, mode, alpha, eta in (("sym", 0, 0.1, 0.01), ("asym", 1, 0.0, 0.0), ("auto", 2, 0.1, 0.01)):
        m = fit_lda(docs, V, 3, alpha, mode, eta, 4, 50, 7, 10)
        b = bound(m, docs)
        lam = (m["sstats"] + m["eta"])
        print("# %s: alpha=%s eta=%r" % (name, np.array2string(m["alpha"], precision=12), m["eta"]))
        print("%s bound = %.12f perplexity = %.9f" % (name, b, math.exp(-b)))
        print("%s lambda[topic 0 word id 0] = %.12f  lambda sum = %.9f" % (name, lam[0, 0], lam.sum()))
        for k in range(3):
            row = lam[:, k] / lam[:, k].sum()
            order = np.argsort(-row, kind="stable")[:3]
            inv = {v: k2 for k2, v in idmap.items()}
            print("%s topic %d top3: %s" % (name, k, ", ".join("w%d:%.9f" % (inv[i], row[i]) for i in order)))
        theta = m["infer"](docs[0][0], docs[0][1], m["eb"], m["alpha"])
        theta = theta / theta.sum()
        print("%s theta[doc 0] = %s" % (name, " ".join("%.12f" % x for x in theta)))

# ---------------------------------------------------------------- 3. coherence
EPS = 1e-12

def windows(text, size):
    if len(text) <= size:
        yield text
    else:
        for s in range(len(text) - size + 1):
            yield text[s:s + size]

def count_windows(texts, size, targets):
    occ = {w: 0 for w in targets}
    co = {}
    n = 0
    for text in texts:
        for win in windows(text, size):
            present = sorted(set(win) & set(targets))
            for a in present:
                occ[a] += 1
                for b in present:
                    if a < b:
                        co[(a, b)] = co.get((a, b), 0) + 1
            n += 1
    return occ, co, n

def pair_count(co, occ, a, b):
    if a == b:
        return occ[a]
    return co.get((min(a, b), max(a, b)), 0)

def npmi(occ, co, n, a, b):
    c = pair_count(co, occ, a, b) / n
    p1, p2 = occ[a] / n, occ[b] / n
    if p1 == 0 or p2 == 0:
        return 0.0
    num = math.log((c + EPS) / (p1 * p2))
    return num / (-math.log(c + EPS))

def c_npmi(topics, texts, size):
    targets = sorted({w for t in topics for w in t})
    occ, co, n = count_windows(texts, size, targets)
    res = []
    for t in topics:
        vals = [npmi(occ, co, n, a, b) for a in t for b in t if a != b]
        res.append(sum(vals) / len(vals))
    return res

def c_v(topics, texts, size):
    targets = sorted({w for t in topics for w in t})
    occ, co, n = count_windows(texts, size, targets)
    res = []
    for t in topics:
        vecs = np.array([[npmi(occ, co, n, a, b) for b in t] for a in t])
        total = vecs.sum(axis=0)
        sims = [float(v @ total / (np.linalg.norm(v) * np.linalg.norm(total))) for v in vecs]
        res.append(sum(sims) / len(sims))
    return res

def u_mass(topics, texts):
    """documents are the virtual documents: boolean document co-occurrence."""
    sets = [set(t) for t in texts]
    n = len(sets)
    res = []
    for t in topics:
        vals = []
        for i in range(1, len(t)):
            for j in range(i):
                dj = sum(1 for s in sets if t[j] in s)
                dij = sum(1 for s in sets if t[j] in s and t[i] in s)
                vals.append(math.log((dij / n + EPS) / (dj / n)) if dj else 0.0)
        res.append(sum(vals) / len(vals))
    return res

COHERENCE_TEXTS = [
    "malware ransomware encrypt files payload".split(),
    "ransomware payment bitcoin encrypt".split(),
    "phishing email link credentials malware".split(),
    "phishing credentials login email".split(),
    "patch vulnerability exploit cve".split(),
    "exploit cve patch update vulnerability malware".split(),
    "bitcoin payment ransomware".split(),
    "email phishing link".split(),
    [],
    "update patch".split(),
]
COHERENCE_TOPICS = [
    "ransomware malware encrypt payment".split(),
    "phishing email credentials link".split(),
    "patch vulnerability exploit cve".split(),
]

def coherence_reference():
    print("# coherence on the fixed 10-text corpus; topics are the fixed word lists")
    for w in (3, 10):
        print("c_npmi window=%d: %s" % (w, " ".join("%.12f" % x for x in c_npmi(COHERENCE_TOPICS, COHERENCE_TEXTS, w))))
    for w in (3, 110):
        print("c_v window=%d: %s" % (w, " ".join("%.12f" % x for x in c_v(COHERENCE_TOPICS, COHERENCE_TEXTS, w))))
    print("u_mass: %s" % " ".join("%.12f" % x for x in u_mass(COHERENCE_TOPICS, COHERENCE_TEXTS)))

if __name__ == "__main__":
    special()
    lda_reference()
    coherence_reference()
