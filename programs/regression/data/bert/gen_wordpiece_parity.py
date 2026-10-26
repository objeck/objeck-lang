#!/usr/bin/env python3
"""Generate the WordPiece parity fixture: texts and the ids BERT's tokenizer gives them.

transformers is not installed where this was written (and is not to be installed
for it), so the reference is a transcription of Hugging Face's slow BertTokenizer
-- BasicTokenizer, WordpieceTokenizer and the special-token split in
PreTrainedTokenizer.tokenize -- written against Python's own unicodedata and
str.lower(), NOT against the Objeck code it checks. Where transformers IS
installed, --check-hf compares this transcription with the real BertTokenizer
on the same corpus first.

Corpus: the lines of the repository's docs/*.md (English with code, paths and
punctuation) plus a block of multilingual and edge-case lines below. Vocabulary
(parity_vocab.txt): the specials, the 1,500 most frequent words of the corpus
as the uncased tokenizer splits them, a set of '##' suffixes, and every
character seen, bare and as '##c' -- so most words split into several pieces
and some stay whole, and the greedy longest match is exercised rather than
bypassed.

Each fixture case is three modes of one text:
  U  lowercase + strip accents   (bert-base-uncased)
  C  neither                     (bert-base-cased)
  L  lowercase, keep accents
Hugging Face composes text to NFC before splitting it; the Objeck tokenizer
does not. For U and L that cannot matter (stripping decomposes anyway, and
lowercasing commutes with composition for these texts); for C it can, given
decomposed input, so a C case whose ids differ with and without NFC is left
out and counted. Every other case is kept.

  python gen_wordpiece_parity.py                       write the fixture (a sample)
  python gen_wordpiece_parity.py --all OUT.txt         every corpus line, for a full local run
"""
import argparse
import collections
import glob
import os
import random
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
SPECIALS = ["[PAD]", "[UNK]", "[CLS]", "[SEP]", "[MASK]"]

EXTRA = [
    "Héllo, Wörld! Ça va? Señor Müller's naïve café — déjà vu.",
    "ΟΔΟΣ και ΣΟΦΙΑ: Σ, ΑΣ. Ελληνικά άέήίόύώ ΐΰ",
    "Москва, Йошкар-Ола, ёлка! Привет мир.",
    "中文分词测试：北京欢迎你。日本語のテキスト、カタカナ。",
    "한국어 텍스트입니다. 안녕하세요!",
    "العربية: مرحبا بالعالم؟ עברית: שלום עולם",
    "Tiếng Việt có dấu: Hà Nội, Đà Nẵng.",
    "İstanbul ılık İĞNE ığdır",
    "emoji 😀 and flags 🇫🇷 and zwj 👩‍💻 done",
    "zero​width‍joiner﻿bom and soft­hyphen",
    "nbsp space em space ideographic　space line sep para sep",
    "tab\there\nnewline\r\nreturn \x0bvt \x0cff \x1cfs \x85nel",
    "control\x01\x02\x7fchars and NUL\x00here and �replacement",
    "decomposed é à ñ ö ų́",
    "full！width，punct？here（x）",
    "math ∑ ∫ √ ≤ ≥ ≠ ± × ÷ ∞ °C 5€ $10 £3 ¥4",
    "quotes «guillemets» „low“ ‘single’ “double” ‹s› 「corner」",
    "dashes - – — ‒ ― … ‥ · • ‰ ′ ″ ※",
    "[CLS] [SEP] [MASK] [PAD] [UNK] [cls] [Sep] x[SEP]y [SEP][SEP]",
    "CreateFileW,ReadFile;CloseHandle RegOpenKeyExA VirtualAlloc GetProcAddress",
    "a" * 99 + " " + "b" * 100 + " " + "c" * 101,
    "",
    "   ",
    "!!!???...,,,",
    "Ⅻ ⅻ ① ﬁ ﬂ Å K Ω µ ß ẞ ŉ ǅ ǈ",
    "ŁÓDŹ Ærøskøbing Œuvre ĲSSEL",
    "𝐁𝐨𝐥𝐝 𝔉𝔯𝔞𝔨 𐐀𐐨 𝒜",
    "Ꮳ ꭰ ꙁ Ⴀ ⴀ",
]


# --- the reference: Hugging Face's slow BertTokenizer, transcribed ---

def is_whitespace(ch):
    if ch in (" ", "\t", "\n", "\r"):
        return True
    return unicodedata.category(ch) == "Zs"


def is_control(ch):
    if ch in ("\t", "\n", "\r"):
        return False
    return unicodedata.category(ch).startswith("C")


def is_punctuation(ch):
    cp = ord(ch)
    if (33 <= cp <= 47) or (58 <= cp <= 64) or (91 <= cp <= 96) or (123 <= cp <= 126):
        return True
    return unicodedata.category(ch).startswith("P")


def is_chinese(cp):
    return ((0x4E00 <= cp <= 0x9FFF) or (0x3400 <= cp <= 0x4DBF) or (0x20000 <= cp <= 0x2A6DF)
            or (0x2A700 <= cp <= 0x2B73F) or (0x2B740 <= cp <= 0x2B81F) or (0x2B820 <= cp <= 0x2CEAF)
            or (0xF900 <= cp <= 0xFAFF) or (0x2F800 <= cp <= 0x2FA1F))


def strip_accents(text):
    return "".join(c for c in unicodedata.normalize("NFD", text) if unicodedata.category(c) != "Mn")


def split_on_punc(text):
    out, cur = [], []
    for ch in text:
        if is_punctuation(ch):
            if cur:
                out.append("".join(cur))
                cur = []
            out.append(ch)
        else:
            cur.append(ch)
    if cur:
        out.append("".join(cur))
    return out


def basic_tokenize(text, lower, strip, nfc=True):
    cleaned = []
    for ch in text:
        cp = ord(ch)
        if cp == 0 or cp == 0xFFFD or is_control(ch):
            continue
        cleaned.append(" " if is_whitespace(ch) else ch)
    text = "".join(cleaned)
    text = "".join(" %s " % ch if is_chinese(ord(ch)) else ch for ch in text)
    if nfc:
        text = unicodedata.normalize("NFC", text)
    out = []
    for token in text.strip().split():
        if lower:
            token = token.lower()
            if strip:
                token = strip_accents(token)
        elif strip:
            token = strip_accents(token)
        out.extend(split_on_punc(token))
    return " ".join(out).strip().split()


def wordpiece(token, vocab):
    if len(token) > 100:
        return ["[UNK]"]
    pieces, start = [], 0
    while start < len(token):
        end, cur = len(token), None
        while start < end:
            sub = token[start:end]
            if start > 0:
                sub = "##" + sub
            if sub in vocab:
                cur = sub
                break
            end -= 1
        if cur is None:
            return ["[UNK]"]
        pieces.append(cur)
        start = end
    return pieces


def split_specials(text, vocab):
    """PreTrainedTokenizer.tokenize: exact occurrences of the special tokens are cut out first."""
    specials = [s for s in SPECIALS if s in vocab]
    out, start = [], 0
    while start < len(text):
        best, best_tok = -1, None
        for s in specials:
            at = text.find(s, start)
            if at >= 0 and (best < 0 or at < best):
                best, best_tok = at, s
        if best < 0:
            out.append((text[start:], False))
            break
        if best > start:
            out.append((text[start:best], False))
        out.append((best_tok, True))
        start = best + len(best_tok)
    return out


def tokenize(text, vocab, lower, strip, nfc=True):
    ids = []
    for part, special in split_specials(text, vocab):
        if special:
            ids.append(vocab[part])
            continue
        for word in basic_tokenize(part, lower, strip, nfc):
            ids.extend(vocab[p] for p in wordpiece(word, vocab))
    return ids


MODES = [("U", True, True), ("C", False, False), ("L", True, False)]


def corpus_lines():
    lines = []
    for path in sorted(glob.glob(os.path.join(ROOT, "docs", "*.md"))):
        with open(path, encoding="utf-8") as fh:
            for line in fh.read().split("\n"):
                line = line.strip()
                if line:
                    lines.append(line)
    return lines


def build_vocab(lines):
    counts = collections.Counter()
    chars = set()
    for line in lines:
        for word in basic_tokenize(line, True, True):
            counts[word] += 1
        for word in basic_tokenize(line, False, False):
            chars.update(word)
        for word in basic_tokenize(line, True, False):
            chars.update(word)
    vocab = list(SPECIALS)
    words = [w for w, _ in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))[:1500]]
    suffixes = ["##s", "##es", "##ed", "##ing", "##ly", "##er", "##ers", "##tion", "##ions", "##al",
                "##able", "##ment", "##ness", "##ity", "##ize", "##ise", "##ic", "##ous", "##ful", "##less"]
    for tok in words + suffixes + sorted(chars) + ["##" + c for c in sorted(chars)]:
        if tok not in vocab:
            vocab.append(tok)
    return vocab


def hexs(text):
    return " ".join("%X" % ord(c) for c in text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--all", metavar="OUT", help="write every corpus line to OUT instead of the sample")
    parser.add_argument("--check-hf", action="store_true", help="compare the transcription with transformers first")
    args = parser.parse_args()

    docs = corpus_lines()
    vocab_list = build_vocab(docs + EXTRA)
    vocab = {tok: i for i, tok in enumerate(vocab_list)}
    vocab_path = os.path.join(HERE, "parity_vocab.txt")
    with open(vocab_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(vocab_list) + "\n")

    if args.check_hf:
        from transformers import BertTokenizer
        for _, lower, strip in MODES:
            hf = BertTokenizer(vocab_path, do_lower_case=lower, strip_accents=strip)
            bad = sum(1 for t in docs + EXTRA if hf.convert_tokens_to_ids(hf.tokenize(t)) != tokenize(t, vocab, lower, strip))
            print("transformers vs transcription, lower=%s strip=%s: %d differ" % (lower, strip, bad))

    if args.all:
        texts, out_path = docs + EXTRA, args.all
    else:
        rng = random.Random(1286)
        texts = sorted(set(rng.sample(docs, 220))) + EXTRA
        out_path = os.path.join(HERE, "wordpiece_parity.txt")

    dropped = 0
    with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# generated by gen_wordpiece_parity.py; vocabulary parity_vocab.txt\n")
        fh.write("# T <code points of the text, hex>, then one line per mode: <mode> <ids>\n")
        for text in texts:
            rows = []
            for name, lower, strip in MODES:
                ids = tokenize(text, vocab, lower, strip)
                if not strip and ids != tokenize(text, vocab, lower, strip, nfc=False):
                    dropped += 1
                    continue
                rows.append("%s %s" % (name, " ".join(str(i) for i in ids)))
            fh.write("T %s\n" % hexs(text))
            fh.write("\n".join(rows) + "\n")
    print("%s: %d texts, vocabulary %d tokens; %d no-strip case(s) left out because NFC changes them"
          % (os.path.basename(out_path), len(texts), len(vocab_list), dropped))


if __name__ == "__main__":
    main()
