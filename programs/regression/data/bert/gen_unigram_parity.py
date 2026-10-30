#!/usr/bin/env python3
"""Generate the Unigram (SentencePiece) parity fixture: a tiny tokenizer in the two shapes
DeBERTa-v3 and XLM-R ship, and what Hugging Face's tokenizers library makes of a corpus
with each.

The pieces are trained here with tokenizers' UnigramTrainer on the lines of the
repository's docs/*.md, to 2,000 pieces. The normalizer uses SentencePiece's own
precompiled_charsmap, taken from a real DeBERTa-v3 tokenizer.json (NFKC-style folding as
SentencePiece compiles it), so the fixture checks the real normalization table:

  unigram/pieces.txt     piece<TAB>score, one per line, in id order
  unigram/charsmap.bin   the precompiled_charsmap bytes (base64-decoded)

and two tokenizer shapes are built from them (not written: each would carry the 237 KB
charsmap again as base64):

  deberta   DeBERTa-v3's: Strip, Precompiled, Replace ' {2,}' -> ' '; Metaspace;
            [CLS] A [SEP] / [CLS] A [SEP] B [SEP]
  xlmr      XLM-R's: Precompiled; WhitespaceSplit, Metaspace; <s> A </s> /
            <s> A </s></s> B </s>; <mask> lstrip

unigram_parity.txt holds, for each corpus text and each shape, the ids and character
offsets Python's tokenizer gives for the text alone and paired with the next text.
unigram_sweep.txt holds what the normalizer alone (Precompiled) makes of every code point
it changes, one code point at a time, over all of Unicode.

Requires: tokenizers (0.23.3 when this was written), and the DeBERTa-v3 tokenizer.json
whose charsmap is used: pass its path. Rerun from this directory:
    python gen_unigram_parity.py <path to a DeBERTa-v3 tokenizer.json>
"""

import base64
import glob
import json
import os
import sys

from tokenizers import AddedToken, Regex, Tokenizer, decoders, models, normalizers, pre_tokenizers, processors, trainers

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
OUT = os.path.join(HERE, "unigram")
SPECIALS = ["[PAD]", "[CLS]", "[SEP]", "[UNK]", "[MASK]", "<s>", "<pad>", "</s>", "<unk>", "<mask>"]

EDGE = [
    "Hello world",
    "  two  spaces  and   three   ",
    "cafe\u0301 caf\u00e9 na\u00efve",
    "e\u0301\u0301 two marks",
    "\uff21\uff22\uff23 full width \uff11\uff12\uff13",
    "\ufb01ligature and \ufb02ow",
    "x\u00a0y non breaking",
    "\u2160 \u2161 roman \u00bd \u00b2",
    "tab\there\tand\tthere",
    "line\nbreak and\r\ncrlf",
    "Greek \u03b1\u03b2\u03b3 Cyrillic \u043f\u0440\u0438\u0432\u0435\u0442",
    "CJK \u6f22\u5b57\u306f\u96e3\u3057\u3044 Korean \ud55c\uad6d\uc5b4",
    "Hangul jamo \u1100\u1161\u11a8",
    "Devanagari \u0915\u094d\u0937 \u0928\u092e\u0938\u094d\u0924\u0947",
    "emoji \U0001F600 \U0001F44D\U0001F3FD family \U0001F468\u200d\U0001F469\u200d\U0001F467",
    "flags \U0001F1EF\U0001F1F5 keycap 1\ufe0f\u20e3",
    "[MASK] and <mask> and [CLS]",
    "the <mask> sat",
    "unknownzzz qqq \u2603\u2604",
    "",
    "   ",
    "x",
    "a" * 80,
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


def charsmap_from(path):
    config = json.load(open(path, encoding="utf-8"))
    normalizer = config["normalizer"]
    for n in normalizer.get("normalizers", [normalizer]):
        if n["type"] == "Precompiled":
            return base64.b64decode(n["precompiled_charsmap"])
    raise SystemExit("no Precompiled normalizer in " + path)


def build(pieces, unk, shape, charsmap):
    tokenizer = Tokenizer(models.Unigram(pieces, unk_id=unk))
    precompiled = normalizers.Precompiled(charsmap)
    if shape == "deberta":
        tokenizer.normalizer = normalizers.Sequence([
            normalizers.Strip(left=True, right=True), precompiled,
            normalizers.Replace(Regex(" {2,}"), " ")])
        tokenizer.pre_tokenizer = pre_tokenizers.Metaspace(replacement="\u2581", prepend_scheme="always")
        tokenizer.post_processor = processors.TemplateProcessing(
            single="[CLS] $A [SEP]", pair="[CLS] $A [SEP] $B:1 [SEP]:1",
            special_tokens=[("[CLS]", 1), ("[SEP]", 2)])
        tokenizer.add_special_tokens([AddedToken(t, normalized=False, special=True)
                                      for t in ("[PAD]", "[CLS]", "[SEP]", "[UNK]", "[MASK]")])
    else:
        tokenizer.normalizer = precompiled
        tokenizer.pre_tokenizer = pre_tokenizers.Sequence([
            pre_tokenizers.WhitespaceSplit(),
            pre_tokenizers.Metaspace(replacement="\u2581", prepend_scheme="always")])
        tokenizer.post_processor = processors.TemplateProcessing(
            single="<s> $A </s>", pair="<s> $A </s> </s> $B </s>",
            special_tokens=[("<s>", 5), ("</s>", 7)])
        tokenizer.add_special_tokens([AddedToken(t, normalized=False, special=True)
                                      for t in ("<s>", "<pad>", "</s>", "<unk>")])
        tokenizer.add_special_tokens([AddedToken("<mask>", lstrip=True, normalized=False, special=True)])
    tokenizer.decoder = decoders.Metaspace(replacement="\u2581", prepend_scheme="always")
    return tokenizer


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    charsmap = charsmap_from(sys.argv[1])
    lines = corpus_lines()

    trainer = trainers.UnigramTrainer(vocab_size=2000, special_tokens=SPECIALS, unk_token="[UNK]",
                                      shrinking_factor=0.75, max_piece_length=16)
    base = Tokenizer(models.Unigram())
    base.normalizer = normalizers.Precompiled(charsmap)
    base.pre_tokenizer = pre_tokenizers.Metaspace(replacement="\u2581", prepend_scheme="always")
    base.train_from_iterator(lines, trainer)
    pieces = [(p, s) for p, s in json.loads(base.to_str())["model"]["vocab"]]
    assert [p for p, _ in pieces[:len(SPECIALS)]] == SPECIALS, pieces[:12]
    assert all("\t" not in p and "\n" not in p for p, _ in pieces)

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "pieces.txt"), "w", encoding="utf-8", newline="\n") as fh:
        for piece, score in pieces:
            fh.write(f"{piece}\t{score!r}\n")
    with open(os.path.join(OUT, "charsmap.bin"), "wb") as fh:
        fh.write(charsmap)

    shapes = {"deberta": build(pieces, 3, "deberta", charsmap), "xlmr": build(pieces, 8, "xlmr", charsmap)}
    texts = lines[::max(1, len(lines) // 100)][:100] + EDGE
    out = []
    for name, tokenizer in shapes.items():
        for index, text in enumerate(texts):
            pair = texts[(index + 1) % len(texts)]
            for kind, encoding in (("single", tokenizer.encode(text)), ("pair", tokenizer.encode(text, pair))):
                out.append("\t".join([name, kind, escape(text), escape(pair) if kind == "pair" else "",
                                      " ".join(map(str, encoding.ids)),
                                      " ".join(f"{a},{b}" for a, b in encoding.offsets)]))
    with open(os.path.join(HERE, "unigram_parity.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# shape\tkind\ttext\tpair\tids\toffsets (start,end)\n")
        fh.write("\n".join(out) + "\n")

    precompiled = normalizers.Precompiled(charsmap)
    sweep = []
    for cp in range(0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        ch = chr(cp)
        got = precompiled.normalize_str(ch)
        if got != ch:
            sweep.append(f"{cp:X}\t{' '.join(f'{ord(c):X}' for c in got)}")
    with open(os.path.join(HERE, "unigram_sweep.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# code point\twhat Precompiled makes of it alone (empty: removed); every other code point is unchanged\n")
        fh.write("\n".join(sweep) + "\n")

    print(f"pieces {len(pieces)}, {len(texts)} texts, {len(out)} rows, sweep {len(sweep)} changed code points")


def escape(text):
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


if __name__ == "__main__":
    main()
