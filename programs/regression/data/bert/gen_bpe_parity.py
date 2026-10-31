#!/usr/bin/env python3
"""Generate the byte-level BPE parity fixture: a tiny RoBERTa-style tokenizer and what
Hugging Face's tokenizers library makes of a corpus with it.

The tokenizer is trained here with tokenizers' own ByteLevelBPETokenizer on the lines of
the repository's docs/*.md, to 2,000 tokens, and configured as RoBERTa's tokenizer.json
is: ByteLevel pre-tokenizer (add_prefix_space false), RobertaProcessing with trim_offsets,
and the five specials, <mask> taking the whitespace before it (lstrip). It is written as:

  bpe/tokenizer.json   the tokenizer itself, for the loaders
  bpe/vocab.txt        its tokens in id order, one per line (a byte-level token never holds
                       a raw newline: byte 0x0A is the character U+010A)
  bpe/merges.txt       its merges in rank order, "left right"

and bpe_parity.txt holds, for each line of the corpus -- docs lines, then the edge cases
below -- the ids, tokens and character offsets Python's tokenizer gives for the text
alone and for the text paired with the next line, both untruncated. The Objeck test
(nlp_bpe_tokenizer.obs) must reproduce every one exactly.

Requires: tokenizers (0.23.3 when this was written). Rerun from this directory:
    python gen_bpe_parity.py
"""

import glob
import json
import os

from tokenizers import AddedToken, Tokenizer
from tokenizers.implementations import ByteLevelBPETokenizer
from tokenizers.processors import RobertaProcessing

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
OUT = os.path.join(HERE, "bpe")
SPECIALS = ["<s>", "<pad>", "</s>", "<unk>", "<mask>"]

EDGE = [
    "Hello world",
    " leading space and trailing space ",
    "two  spaces,   three,    four",
    "I'm sure it's fine; they're here, we've won, I'll go, he'd say.",
    "DON'T SHOUT'S CAPITALS",
    "'quoted' \"double\" `back`",
    "numbers 12345 and 3.14159 and 1e10 and -42",
    "unicode: café naïve résumé über",
    "Greek αβγ ΔΕ and Cyrillic привет",
    "CJK 漢字は難しい and 한국어",
    "emoji \U0001F600\U0001F680 mixed ❤️!",
    "roman Ⅷ and superscript ² and fraction ½",
    "tabs\there\tand\tthere",
    "the <mask> sat on the mat",
    "no space<mask>here",
    "two masks <mask> <mask> end",
    "<s> literal specials </s> in text <pad>",
    "symbols !@#$%^&*()_+-=[]{}|;:,./<>?~",
    "x",
    "",
    "   ",
    "a" * 120,
    "supercalifragilisticexpialidocious antidisestablishmentarianism",
    "non breaking spaces and　ideographic space",
]


def corpus_lines():
    lines = []
    for path in sorted(glob.glob(os.path.join(ROOT, "docs", "*.md"))):
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                line = line.rstrip("\n").rstrip("\r")
                if line.strip():
                    lines.append(line)
    return lines


def main():
    lines = corpus_lines()
    trainer = ByteLevelBPETokenizer(add_prefix_space=False, trim_offsets=True)
    trainer.train_from_iterator(lines, vocab_size=2000, min_frequency=2, special_tokens=SPECIALS)

    os.makedirs(OUT, exist_ok=True)
    raw = os.path.join(OUT, "tokenizer.json")
    trainer.save(raw)

    tokenizer = Tokenizer.from_file(raw)
    tokenizer.post_processor = RobertaProcessing(("</s>", tokenizer.token_to_id("</s>")),
                                                 ("<s>", tokenizer.token_to_id("<s>")),
                                                 trim_offsets=True, add_prefix_space=False)
    tokenizer.add_special_tokens([AddedToken("<mask>", lstrip=True, special=True, normalized=False)])
    tokenizer.save(raw)

    vocab = tokenizer.get_vocab()
    tokens = [None] * (max(vocab.values()) + 1)
    for token, index in vocab.items():
        tokens[index] = token
    assert all(t is not None and "\n" not in t for t in tokens)
    with open(os.path.join(OUT, "vocab.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(tokens) + "\n")

    model = json.load(open(raw, encoding="utf-8"))["model"]
    merges = [m if isinstance(m, str) else " ".join(m) for m in model["merges"]]
    with open(os.path.join(OUT, "merges.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(merges) + "\n")

    texts = lines[::max(1, len(lines) // 150)][:150] + EDGE
    out = []
    for index, text in enumerate(texts):
        pair = texts[(index + 1) % len(texts)]
        single = tokenizer.encode(text)
        both = tokenizer.encode(text, pair)
        for kind, encoding in (("single", single), ("pair", both)):
            out.append("\t".join([
                kind,
                text.replace("\\", "\\\\").replace("\t", "\\t"),
                pair.replace("\\", "\\\\").replace("\t", "\\t") if kind == "pair" else "",
                " ".join(map(str, encoding.ids)),
                " ".join(f"{a},{b}" for a, b in encoding.offsets),
            ]))

    with open(os.path.join(HERE, "bpe_parity.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# kind\ttext\tpair\tids\toffsets (start,end)\n")
        fh.write("\n".join(out) + "\n")
    print(f"vocabulary {len(tokens)}, merges {len(merges)}, {len(texts)} texts, {len(out)} rows")


if __name__ == "__main__":
    main()
