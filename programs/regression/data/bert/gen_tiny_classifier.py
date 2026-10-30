#!/usr/bin/env python3
"""Generate the TextClassifier fixtures: tiny real classifiers and what Python makes of them.

Each model is a real transformers architecture -- DistilBertForSequenceClassification or
BertForSequenceClassification -- scaled down (hidden 16, 2 layers) with seeded random
weights, saved the way save_pretrained saves a fine-tuned model and exported to ONNX with
torch.onnx.export(dynamo=True), the torch.export-based exporter. The Objeck test opens
each folder as a user would open a downloaded model and must reproduce, for every text:

  - the token ids Python's tokenizer gives (exact);
  - the logits ONNX Runtime gives on the same file (to 1e-5) and PyTorch gives (to 1e-4),
    relative to max(1, |logit|);
  - the scores and top label transformers' pipeline("text-classification") gives (to 1e-4).

Three folders, the three shapes a downloaded classifier comes in:

  distilbert_cased/  DistilBERT, no token_type_ids, cased tokenizer.json, two named labels
                     (the shape of a DistilBERT malware classifier fine-tuned on Colab)
  bert_multilabel/   BERT with token_type_ids, three labels, problem_type
                     multi_label_classification (sigmoid), vocab.txt + tokenizer_config.json
                     and no tokenizer.json, so the vocab.txt path is exercised
  bert_one_label/    BERT, uncased tokenizer.json, one label (sigmoid, as a cross-encoder)

The vocabulary is parity_vocab.txt (gen_wordpiece_parity.py). tokenizer_config.json caps
model_max_length at 24 so the long texts truncate.

Run with torch, transformers, tokenizers, onnx, onnxscript and onnxruntime installed:
    python gen_tiny_classifier.py
It rewrites classifier/ and classifier_expected.txt beside this script and checks that
ONNX Runtime and PyTorch agree before writing anything.
"""

import json
import os
import shutil
import sys

import numpy as np
import onnx
import onnxruntime as ort
import torch
from tokenizers import BertWordPieceTokenizer
from transformers import (BertConfig, BertForSequenceClassification, BertTokenizer,
                          DistilBertConfig, DistilBertForSequenceClassification,
                          PreTrainedTokenizerFast, pipeline)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "classifier")
VOCAB = os.path.join(HERE, "parity_vocab.txt")
MAX_LEN = 24

TEXTS = [
    "the quick brown fox",
    "The Quick Brown Fox",
    "",
    "Configure the regression runner with -opt s3 and run it again.",
    "Café naïve résumé",
    "unknownwordzzz qqq",
    "a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f",
    "GetProcAddress LoadLibraryExW CreateFileW WriteFile CloseHandle RegSetValueExW",
    "x",
    "multi line\ttext with\ttabs",
]


class Logits(torch.nn.Module):
    """The model with only the inputs the export takes, returning the logits."""

    def __init__(self, model, with_types):
        super().__init__()
        self.model = model
        self.with_types = with_types

    def forward(self, input_ids, attention_mask, token_type_ids=None):
        if self.with_types:
            return self.model(input_ids=input_ids, attention_mask=attention_mask,
                              token_type_ids=token_type_ids).logits
        return self.model(input_ids=input_ids, attention_mask=attention_mask).logits


def strip(onnx_path):
    """Drops what the exporter records for debugging -- per-node stack traces and source
    locations, doc strings, intermediate shapes -- which is most of a tiny model's bytes.
    The graph and weights are untouched; ONNX Runtime then runs the stripped file."""
    model = onnx.load(onnx_path)
    for node in model.graph.node:
        del node.metadata_props[:]
        node.doc_string = ""
    del model.graph.value_info[:]
    del model.metadata_props[:]
    model.doc_string = ""
    onnx.checker.check_model(model)
    onnx.save(model, onnx_path)


def tokenizer_json(folder, lowercase, with_types):
    wordpiece = BertWordPieceTokenizer(VOCAB, lowercase=lowercase, strip_accents=None)
    wordpiece.save(os.path.join(folder, "tokenizer.json"))
    fast = PreTrainedTokenizerFast(tokenizer_file=os.path.join(folder, "tokenizer.json"),
                                   unk_token="[UNK]", sep_token="[SEP]", pad_token="[PAD]",
                                   cls_token="[CLS]", mask_token="[MASK]",
                                   model_max_length=MAX_LEN,
                                   model_input_names=(["input_ids", "token_type_ids", "attention_mask"]
                                                      if with_types else ["input_ids", "attention_mask"]))
    fast.save_pretrained(folder)
    return fast


def vocab_only(folder, lowercase):
    shutil.copy(VOCAB, os.path.join(folder, "vocab.txt"))
    slow = BertTokenizer(os.path.join(folder, "vocab.txt"), do_lower_case=lowercase,
                         model_max_length=MAX_LEN)
    slow.save_pretrained(folder)
    for extra in ("special_tokens_map.json", "tokenizer.json"):
        path = os.path.join(folder, extra)
        if os.path.exists(path):
            os.remove(path)
    return BertTokenizer(os.path.join(folder, "vocab.txt"), do_lower_case=lowercase,
                         model_max_length=MAX_LEN)


def build(name, model, tokenizer, with_types):
    folder = os.path.join(OUT, name)
    model.eval()
    model.config.save_pretrained(folder)

    example = tokenizer(["a b c", "d"], padding=True, return_tensors="pt")
    args = (example["input_ids"], example["attention_mask"])
    if with_types:
        args += (example["token_type_ids"],)
    batch = torch.export.Dim("batch_size")
    sequence = torch.export.Dim("sequence_length", max=MAX_LEN)
    shapes = {"input_ids": {0: batch, 1: sequence}, "attention_mask": {0: batch, 1: sequence}}
    if with_types:
        shapes["token_type_ids"] = {0: batch, 1: sequence}
    names = ["input_ids", "attention_mask"] + (["token_type_ids"] if with_types else [])
    onnx_path = os.path.join(folder, "model.onnx")
    torch.onnx.export(Logits(model, with_types).eval(), args, onnx_path, dynamo=True,
                      input_names=names, output_names=["logits"], dynamic_shapes=shapes,
                      external_data=False)
    for leftover in os.listdir(folder):
        if leftover.endswith(".data"):
            raise SystemExit(f"{name}: the export wrote external data ({leftover})")
    strip(onnx_path)

    session = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    classify = pipeline("text-classification", model=model, tokenizer=tokenizer, top_k=None,
                        device="cpu")

    rows = []
    for index, text in enumerate(TEXTS):
        encoded = tokenizer(text, truncation=True, max_length=MAX_LEN, return_tensors="pt")
        feed = {"input_ids": encoded["input_ids"].numpy(),
                "attention_mask": encoded["attention_mask"].numpy()}
        if with_types:
            feed["token_type_ids"] = encoded["token_type_ids"].numpy()
        ort_logits = session.run(["logits"], feed)[0][0]
        with torch.no_grad():
            torch_logits = Logits(model, with_types).eval()(*[torch.from_numpy(feed[n]) for n in names])[0].numpy()
        # relative to the logit's size: float32 rounding grows with the magnitude
        gap = float(np.max(np.abs(ort_logits - torch_logits) / np.maximum(1.0, np.abs(torch_logits))))
        if gap > 1e-5:
            raise SystemExit(f"{name} text {index}: ONNX Runtime and PyTorch differ by {gap}")

        result = classify(text, truncation=True, max_length=MAX_LEN)
        if result and isinstance(result[0], list):
            result = result[0]
        by_label = {r["label"]: r["score"] for r in result}
        labels = [model.config.id2label[i] for i in range(model.config.num_labels)]
        scores = [by_label[label] for label in labels]
        top = max(result, key=lambda r: r["score"])["label"]
        ids = encoded["input_ids"][0].tolist()
        truncated = len(tokenizer(text)["input_ids"]) > MAX_LEN
        rows.append("\t".join([
            name, str(index), " ".join(map(str, ids)),
            " ".join(f"{v:.9g}" for v in ort_logits),
            " ".join(f"{v:.9g}" for v in torch_logits),
            " ".join(f"{v:.9g}" for v in scores),
            top, "1" if truncated else "0"]))
    return rows


def main():
    torch.manual_seed(1453)
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    vocab_size = sum(1 for _ in open(VOCAB, encoding="utf-8"))
    rows = []

    folder = os.path.join(OUT, "distilbert_cased")
    os.makedirs(folder)
    config = DistilBertConfig(vocab_size=vocab_size, dim=16, n_layers=2, n_heads=2, hidden_dim=32,
                              max_position_embeddings=64, initializer_range=0.2, num_labels=2,
                              id2label={0: "benign", 1: "malware"}, label2id={"benign": 0, "malware": 1})
    rows += build("distilbert_cased", DistilBertForSequenceClassification(config),
                  tokenizer_json(folder, lowercase=False, with_types=False), with_types=False)

    folder = os.path.join(OUT, "bert_multilabel")
    os.makedirs(folder)
    config = BertConfig(vocab_size=vocab_size, hidden_size=16, num_hidden_layers=2, num_attention_heads=2,
                        intermediate_size=32, max_position_embeddings=64, initializer_range=0.2, num_labels=3,
                        id2label={0: "toxic", 1: "obscene", 2: "insult"},
                        label2id={"toxic": 0, "obscene": 1, "insult": 2},
                        problem_type="multi_label_classification")
    rows += build("bert_multilabel", BertForSequenceClassification(config),
                  vocab_only(folder, lowercase=True), with_types=True)

    folder = os.path.join(OUT, "bert_one_label")
    os.makedirs(folder)
    config = BertConfig(vocab_size=vocab_size, hidden_size=16, num_hidden_layers=2, num_attention_heads=2,
                        intermediate_size=32, max_position_embeddings=64, initializer_range=0.2, num_labels=1)
    rows += build("bert_one_label", BertForSequenceClassification(config),
                  tokenizer_json(folder, lowercase=True, with_types=True), with_types=True)

    with open(os.path.join(HERE, "classifier_texts.txt"), "w", encoding="utf-8", newline="\n") as out:
        for text in TEXTS:
            out.write(text.replace("\t", "\\t") + "\n")
    with open(os.path.join(HERE, "classifier_expected.txt"), "w", encoding="utf-8", newline="\n") as out:
        out.write("# folder\ttext\tids\tlogits (onnxruntime)\tlogits (pytorch)\tscores (pipeline)\ttop label\ttruncated\n")
        for row in rows:
            out.write(row + "\n")
    print(f"wrote {len(rows)} rows for {len(TEXTS)} texts")


if __name__ == "__main__":
    sys.exit(main())
