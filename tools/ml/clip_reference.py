#!/usr/bin/env python3
"""Reference CLIP image embeddings for System.ML.Clip, and the comparison against Objeck's.

Step 1, `make`: for a handful of images, run Hugging Face's CLIPImageProcessor (Pillow) and the
PyTorch CLIPModel, and the exported ONNX tower under onnxruntime, and save everything:

    python tools/ml/clip_reference.py make WORKDIR LFW_DIR CLIP_DIR

  WORKDIR/list.txt            the image paths (10 LFW images + 2 synthetic non-square images)
  WORKDIR/pix_<i>.f64         CLIPImageProcessor's pixel_values for image i, little-endian float64
  WORKDIR/ref_<model>_torch.txt, ref_<model>_ort.txt   embeddings, one row per image, 17 digits

  CLIP_DIR holds clip-vit-base-patch32 / -base-patch16 / -large-patch14 (the Hugging Face
  checkpoints) and onnx/<name>-image.onnx (the exported image towers).

Step 2: run programs/examples/ml/clip_embed_check.obs for each model (it writes
WORKDIR/obj_<model>_<mode>.txt and prints the pixel difference), then

    python tools/ml/clip_reference.py compare WORKDIR

prints the maximum absolute difference of the Objeck embeddings from the PyTorch and from the
onnxruntime ones, for each model and resize mode.

Needs torch, transformers, onnxruntime and numpy; and Pillow (CLIPImageProcessor requires it).
"""
import glob
import os
import sys

# EXTRA_SITE: a directory appended to sys.path, for an environment that has torch but not Pillow
# (point it at another interpreter's site-packages). Appended, so the first environment still wins.
if os.environ.get("EXTRA_SITE"):
    sys.path.append(os.environ["EXTRA_SITE"])

import numpy as np

MODELS = ["clip-vit-base-patch32", "clip-vit-base-patch16", "clip-vit-large-patch14"]


def pick_images(lfw, work):
    people = sorted(os.listdir(lfw))
    paths = []
    for p in people[:: max(1, len(people) // 10)]:
        files = sorted(glob.glob(os.path.join(lfw, p, "*.jpg")))
        if files:
            paths.append(files[0])
        if len(paths) == 10:
            break
    from PIL import Image

    # two non-square images, to exercise the aspect-ratio resize and the centre crop
    base = Image.open(paths[0]).convert("RGB")
    wide = base.resize((400, 250), Image.BILINEAR)
    wide_path = os.path.join(work, "synthetic_wide.png")
    wide.save(wide_path)
    tall = base.resize((180, 320), Image.BILINEAR)
    tall_path = os.path.join(work, "synthetic_tall.png")
    tall.save(tall_path)
    return paths + [wide_path, tall_path]


def make(work, lfw, clip):
    import torch
    import onnxruntime as ort
    from PIL import Image
    from transformers import CLIPImageProcessor, CLIPModel

    torch.set_num_threads(8)
    os.makedirs(work, exist_ok=True)
    paths = pick_images(lfw, work)
    with open(os.path.join(work, "list.txt"), "w", newline="\n") as f:
        f.write("\n".join(p.replace("\\", "/") for p in paths) + "\n")

    for name in MODELS:
        proc = CLIPImageProcessor.from_pretrained(os.path.join(clip, name))
        images = [Image.open(p).convert("RGB") for p in paths]
        pix = proc(images=images, return_tensors="np")["pixel_values"].astype(np.float32)
        if name == MODELS[0]:
            for i in range(len(paths)):
                pix[i].astype(np.float64).tofile(os.path.join(work, "pix_%d.f64" % i))
        model = CLIPModel.from_pretrained(os.path.join(clip, name)).eval()
        with torch.no_grad():
            t = model.get_image_features(pixel_values=torch.from_numpy(pix))
        t = getattr(t, "pooler_output", t) if not torch.is_tensor(t) else t
        t = t.numpy().astype(np.float64)
        so = ort.SessionOptions()
        so.intra_op_num_threads = 8
        sess = ort.InferenceSession(os.path.join(clip, "onnx", name + "-image.onnx"), so, providers=["CPUExecutionProvider"])
        o = sess.run(None, {"pixel_values": pix})[0].astype(np.float64)
        np.savetxt(os.path.join(work, "ref_%s_torch.txt" % name), t, fmt="%.17g")
        np.savetxt(os.path.join(work, "ref_%s_ort.txt" % name), o, fmt="%.17g")
        print("%s: torch vs onnxruntime on the same pixels: max abs diff %.3g" % (name, np.abs(t - o).max()))


def compare(work):
    for name in MODELS:
        t = np.loadtxt(os.path.join(work, "ref_%s_torch.txt" % name))
        o = np.loadtxt(os.path.join(work, "ref_%s_ort.txt" % name))
        for mode in ["pil", "opencv"]:
            path = os.path.join(work, "obj_%s_%s.txt" % (name, mode))
            if not os.path.exists(path):
                continue
            x = np.loadtxt(path)
            cos = (x * t).sum(1) / (np.linalg.norm(x, axis=1) * np.linalg.norm(t, axis=1))
            print(
                "%-24s %-7s vs torch: max abs %.3g  vs onnxruntime: max abs %.3g  min cosine to torch %.10f  (raw embeddings, %d images)"
                % (name, mode, np.abs(x - t).max(), np.abs(x - o).max(), cos.min(), len(x))
            )


if __name__ == "__main__":
    if sys.argv[1] == "make":
        make(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        compare(sys.argv[2])
