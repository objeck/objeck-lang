#!/usr/bin/env python3
"""Generate the tiny BERT-shaped ONNX models the System.NLP.Onnx tests run.

A real BERT is 400+ MB and the regression tree ships no model. What the tests
need is not BERT's numbers but BERT's SIGNATURE -- the inputs a Hugging Face
export takes, the outputs it gives back, the dynamic axes -- with numbers simple
enough that the test can recompute every one of them itself, in Objeck, without
reading the model. So each model here is an embedding lookup plus a pooler:

  last_hidden_state[b,s,j] = word[id(b,s), j] + type[tt(b,s), j] + pos[s, j]
                             + 0.25 * attention_mask[b,s]
  pooler_output[b,j]       = tanh(sum_k last_hidden_state[b,0,k] * Wp[k,j] + bp[j])

with every table a closed-form function of its indices (see the *_value
functions below) whose values are multiples of 1/32, so each is exact in FP32
and so is every sum the graph forms. The 0.25 * mask term is there so the mask
is wired into the output: a caller that sends the wrong mask gets different
numbers, and padded positions are NOT zero, so a mean pool that forgets to
exclude them gets a different answer too.

Three models, the three shapes a real export comes in:

  tiny_bert.onnx         input_ids, attention_mask, token_type_ids  INT64[batch,sequence]
                         -> last_hidden_state FP32[batch,sequence,8], pooler_output FP32[batch,8]
                         (BERT as transformers / optimum export it, dynamic axes)
  tiny_bert_nopool.onnx  input_ids, attention_mask -> last_hidden_state only
                         (the DistilBERT shape: no token types, no pooler; the type term is absent)
  tiny_bert_fixed.onnx   as tiny_bert.onnx but every axis fixed at [1,16]
                         (an export made without dynamic_axes)

The vocabulary the tests use is vocab.txt beside this script; its 64 lines are
the 64 rows of the word table.

Run from anywhere:  python gen_tiny_bert.py   (needs the 'onnx' package only;
onnxruntime, if present, is used to check the files it wrote). The output is
deterministic: rerunning it rewrites the same bytes.
"""
import os
import sys

import onnx
from onnx import TensorProto, helper

HERE = os.path.dirname(os.path.abspath(__file__))

VOCAB = 64       # rows of the word table == lines in vocab.txt
HIDDEN = 8       # hidden size
TYPES = 2        # token type vocabulary
POSITIONS = 512  # BERT's max_position_embeddings
MASK_WEIGHT = 0.25

# Opset 13 and IR version 8: old enough for every ONNX Runtime the release ships
# (1.19 on Linux, 1.22 on Windows, 1.30 on macOS). Leaving the IR version at the
# onnx package's default produced a file ORT 1.19 refuses.
OPSET = 13
IR_VERSION = 8


# The closed forms. programs/regression/nlp_onnx_*.obs recomputes these; change
# one here and the tests must change with it.
def word_value(i, j):
    return ((i * 7 + j * 3) % 17 - 8) / 8.0


def type_value(t, j):
    return ((t * 5 + j) % 5 - 2) / 16.0


def pos_value(p, j):
    return ((p * 3 + j * 5) % 13 - 6) / 32.0


def pool_weight(k, j):
    return ((k * 3 + j * 7) % 9 - 4) / 16.0


def pool_bias(j):
    return (j % 3 - 1) / 8.0


def table(name, rows, cols, fn):
    values = [fn(r, c) for r in range(rows) for c in range(cols)]
    return helper.make_tensor(name, TensorProto.FLOAT, [rows, cols], values)


def build(name, with_types, with_pooler, fixed):
    batch, seq = (1, 16) if fixed else ("batch", "sequence")
    positions = 16 if fixed else POSITIONS

    inputs = [helper.make_tensor_value_info("input_ids", TensorProto.INT64, [batch, seq]),
              helper.make_tensor_value_info("attention_mask", TensorProto.INT64, [batch, seq])]
    if with_types:
        inputs.append(helper.make_tensor_value_info("token_type_ids", TensorProto.INT64, [batch, seq]))

    outputs = [helper.make_tensor_value_info("last_hidden_state", TensorProto.FLOAT, [batch, seq, HIDDEN])]
    if with_pooler:
        outputs.append(helper.make_tensor_value_info("pooler_output", TensorProto.FLOAT, [batch, HIDDEN]))

    inits = [table("word_embeddings", VOCAB, HIDDEN, word_value),
             table("position_embeddings", positions, HIDDEN, pos_value),
             helper.make_tensor("one_index", TensorProto.INT64, [], [1]),
             helper.make_tensor("range_start", TensorProto.INT64, [], [0]),
             helper.make_tensor("range_step", TensorProto.INT64, [], [1]),
             helper.make_tensor("unsqueeze_axes", TensorProto.INT64, [1], [2]),
             helper.make_tensor("mask_weight", TensorProto.FLOAT, [], [MASK_WEIGHT])]

    nodes = [helper.make_node("Gather", ["word_embeddings", "input_ids"], ["word"], axis=0),
             # positions = range(0, sequence length): the position embedding
             # rows for however long this batch is, as an export computes them
             helper.make_node("Shape", ["input_ids"], ["ids_shape"]),
             helper.make_node("Gather", ["ids_shape", "one_index"], ["seq_len"], axis=0),
             helper.make_node("Range", ["range_start", "seq_len", "range_step"], ["positions"]),
             helper.make_node("Gather", ["position_embeddings", "positions"], ["pos"], axis=0),
             helper.make_node("Cast", ["attention_mask"], ["mask_f"], to=TensorProto.FLOAT),
             helper.make_node("Unsqueeze", ["mask_f", "unsqueeze_axes"], ["mask_3d"]),
             helper.make_node("Mul", ["mask_3d", "mask_weight"], ["mask_term"]),
             helper.make_node("Add", ["word", "pos"], ["word_pos"])]

    if with_types:
        inits.append(table("token_type_embeddings", TYPES, HIDDEN, type_value))
        nodes.append(helper.make_node("Gather", ["token_type_embeddings", "token_type_ids"], ["typ"], axis=0))
        nodes.append(helper.make_node("Add", ["word_pos", "typ"], ["embedded"]))
    else:
        nodes.append(helper.make_node("Identity", ["word_pos"], ["embedded"]))
    nodes.append(helper.make_node("Add", ["embedded", "mask_term"], ["last_hidden_state"]))

    if with_pooler:
        inits.append(table("pooler_weight", HIDDEN, HIDDEN, pool_weight))
        inits.append(helper.make_tensor("pooler_bias", TensorProto.FLOAT, [HIDDEN],
                                        [pool_bias(j) for j in range(HIDDEN)]))
        inits.append(helper.make_tensor("zero_index", TensorProto.INT64, [], [0]))
        nodes += [helper.make_node("Gather", ["last_hidden_state", "zero_index"], ["cls"], axis=1),
                  helper.make_node("MatMul", ["cls", "pooler_weight"], ["pooled_linear"]),
                  helper.make_node("Add", ["pooled_linear", "pooler_bias"], ["pooled_biased"]),
                  helper.make_node("Tanh", ["pooled_biased"], ["pooler_output"])]

    graph = helper.make_graph(nodes, name, inputs, outputs, inits)
    model = helper.make_model(graph, producer_name="objeck gen_tiny_bert.py",
                              opset_imports=[helper.make_opsetid("", OPSET)])
    model.ir_version = IR_VERSION
    onnx.checker.check_model(model, full_check=True)
    return model


def reference(ids, mask, types, with_types):
    """Pure-Python forward pass, for checking ONNX Runtime against the formula."""
    hidden = []
    for s, token in enumerate(ids):
        row = []
        for j in range(HIDDEN):
            v = word_value(token, j) + pos_value(s, j) + MASK_WEIGHT * mask[s]
            if with_types:
                v += type_value(types[s], j)
            row.append(v)
        hidden.append(row)
    return hidden


def check_with_runtime(path, with_types, with_pooler, fixed):
    try:
        import numpy as np
        import onnxruntime as ort
    except ImportError:
        print("  (onnxruntime not installed: written, not run)")
        return
    seq = 16 if fixed else 5
    ids = [2, 12, 13, 14, 3] + [0] * (seq - 5)
    mask = [1] * 5 + [0] * (seq - 5)
    types = [0, 0, 0, 1, 1] + [0] * (seq - 5)
    feed = {"input_ids": np.array([ids], dtype=np.int64),
            "attention_mask": np.array([mask], dtype=np.int64)}
    if with_types:
        feed["token_type_ids"] = np.array([types], dtype=np.int64)
    session = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    outputs = session.run(None, feed)
    want = reference(ids, mask, types, with_types)
    worst = max(abs(float(outputs[0][0][s][j]) - want[s][j]) for s in range(seq) for j in range(HIDDEN))
    if worst != 0.0:
        sys.exit("%s: last_hidden_state differs from the formula by %g" % (path, worst))
    if with_pooler:
        import math
        for j in range(HIDDEN):
            z = sum(want[0][k] * pool_weight(k, j) for k in range(HIDDEN)) + pool_bias(j)
            if abs(math.tanh(z) - float(outputs[1][0][j])) > 1e-6:
                sys.exit("%s: pooler_output[%d] differs from the formula" % (path, j))
    print("  checked with onnxruntime %s: matches the formula" % ort.__version__)


def main():
    variants = [("tiny_bert.onnx", True, True, False),
                ("tiny_bert_nopool.onnx", False, False, False),
                ("tiny_bert_fixed.onnx", True, True, True)]
    for file_name, with_types, with_pooler, fixed in variants:
        path = os.path.join(HERE, file_name)
        model = build(file_name[:-5], with_types, with_pooler, fixed)
        with open(path, "wb") as out:
            out.write(model.SerializeToString())
        print("%s: %d bytes" % (file_name, os.path.getsize(path)))
        check_with_runtime(path, with_types, with_pooler, fixed)

    with open(os.path.join(HERE, "vocab.txt"), encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if len(lines) != VOCAB:
        sys.exit("vocab.txt has %d lines; the word table has %d rows" % (len(lines), VOCAB))


if __name__ == "__main__":
    main()
