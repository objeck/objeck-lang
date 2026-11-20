#!/usr/bin/env python3
"""Generates the fixtures for ml_clip_preprocess.obs and ml_clip_encoder.obs.

    python programs/regression/data/clip/gen_tiny_clip.py

Needs numpy, Pillow, onnx, onnxruntime and transformers (CLIPImageProcessor). Run with EXTRA_SITE=<a
site-packages with Pillow> when the interpreter has the others but not Pillow.

Writes, next to this file:
  img_*.png                   seven small synthetic images (landscape, tall, exact size, a downscale, an
                              upscale, greyscale, RGBA), from an integer generator, so no photo is needed
  expected_pixels.txt         "name|v0,v1,..." the pixel_values Hugging Face's CLIPImageProcessor makes for each
                              image at 32 x 32 (shortest_edge 32, crop 32), the reference for ClipPreprocessor
  tiny_clip_image.onnx        a 32 x 32 "image tower": AveragePool 8 x 8, Flatten, MatMul with a 48 x 8 matrix
                              of closed-form values, so its output depends on where the pixels are
  expected_embeddings.txt     "name|e0,...,e7": that model, under onnxruntime, on the processor's pixel_values
  pairs_tiny.csv              image pairs with labels, and expected_scores.txt the cosine of each pair's
                              L2-normalised embeddings, computed in numpy
"""
import os
import sys

if os.environ.get("EXTRA_SITE"):
    sys.path.append(os.environ["EXTRA_SITE"])

import numpy as np
import onnx
import onnxruntime as ort
from onnx import TensorProto, helper, numpy_helper
from PIL import Image
from transformers import CLIPImageProcessor

HERE = os.path.dirname(os.path.abspath(__file__))
SIDE = 32

state = 20261120


def draw():
    global state
    state = (state * 1103515245 + 12345) % 2147483648
    return state / 2147483648.0


def make_image(w, h, mode):
    """A smooth gradient plus noise, different in each channel, drawn from the integer generator."""
    arr = np.zeros((h, w, 3), np.uint8)
    for y in range(h):
        for x in range(w):
            for c in range(3):
                base = (x * (c + 1) * 7 + y * (3 - c) * 5) % 256
                arr[y, x, c] = (base + int(draw() * 60)) % 256
    if mode == "L":
        return Image.fromarray(arr).convert("L")
    if mode == "RGBA":
        a = np.full((h, w, 1), 255, np.uint8)
        a[: h // 2, : w // 2] = 90
        return Image.fromarray(np.concatenate([arr, a], axis=2), "RGBA")
    return Image.fromarray(arr)


def fmt(a):
    return ",".join("%.9g" % v for v in np.asarray(a, np.float64).ravel())


def main():
    specs = [
        ("wide", 50, 40, "RGB"),
        ("tall", 33, 71, "RGB"),
        ("exact", 32, 32, "RGB"),
        ("down", 100, 100, "RGB"),
        ("up", 20, 30, "RGB"),
        ("grey", 40, 40, "L"),
        ("rgba", 36, 28, "RGBA"),
    ]
    names, images = [], []
    for name, w, h, mode in specs:
        im = make_image(w, h, mode)
        im.save(os.path.join(HERE, "img_%s.png" % name))
        names.append(name)
        images.append(im.convert("RGB"))

    proc = CLIPImageProcessor(size={"shortest_edge": SIDE}, crop_size={"height": SIDE, "width": SIDE})
    pix = proc(images=images, return_tensors="np")["pixel_values"].astype(np.float32)
    with open(os.path.join(HERE, "expected_pixels.txt"), "w", newline="\n") as f:
        for n, p in zip(names, pix):
            f.write("%s|%s\n" % (n, fmt(p)))

    # the tiny image tower
    w = np.array([[np.sin(0.37 * (i + 1) * (j + 1)) + 0.1 * ((i * 8 + j) % 5) for j in range(8)] for i in range(48)], np.float32)
    x = helper.make_tensor_value_info("pixel_values", TensorProto.FLOAT, ["N", 3, SIDE, SIDE])
    y = helper.make_tensor_value_info("image_embeds", TensorProto.FLOAT, ["N", 8])
    pool = helper.make_node("AveragePool", ["pixel_values"], ["pooled"], kernel_shape=[8, 8], strides=[8, 8])
    flat = helper.make_node("Flatten", ["pooled"], ["flat"], axis=1)
    mm = helper.make_node("MatMul", ["flat", "W"], ["image_embeds"])
    graph = helper.make_graph([pool, flat, mm], "tiny_clip_image", [x], [y], [numpy_helper.from_array(w, "W")])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 13)])
    model.ir_version = 8
    onnx.checker.check_model(model)
    path = os.path.join(HERE, "tiny_clip_image.onnx")
    onnx.save(model, path)

    sess = ort.InferenceSession(path, providers=["CPUExecutionProvider"])
    emb = sess.run(None, {"pixel_values": pix})[0].astype(np.float64)
    with open(os.path.join(HERE, "expected_embeddings.txt"), "w", newline="\n") as f:
        for n, e in zip(names, emb):
            f.write("%s|%s\n" % (n, fmt(e)))

    # pairs over the images and their cosine scores
    unit = emb / np.linalg.norm(emb, axis=1, keepdims=True)
    pairs = [("wide", "tall", 0), ("wide", "exact", 0), ("exact", "down", 1), ("down", "up", 0), ("up", "grey", 0),
             ("grey", "rgba", 1), ("rgba", "wide", 0), ("tall", "down", 1), ("exact", "exact", 1), ("up", "tall", 0)]
    idx = {n: i for i, n in enumerate(names)}
    with open(os.path.join(HERE, "pairs_tiny.csv"), "w", newline="\n") as f:
        f.write("# image1,image2,label (1 = same person)\n")
        for a, b, l in pairs:
            f.write("img_%s.png,img_%s.png,%d\n" % (a, b, l))
    with open(os.path.join(HERE, "expected_scores.txt"), "w", newline="\n") as f:
        for a, b, l in pairs:
            f.write("%.17g\n" % float(unit[idx[a]] @ unit[idx[b]]))
    print("wrote", len(names), "images,", len(pairs), "pairs")


if __name__ == "__main__":
    main()
