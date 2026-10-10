#!/usr/bin/env python3
"""A KServe v2 server that really runs the model, using Python onnxruntime.

WHY A SERVER THAT ACTUALLY INFERS
---------------------------------
`mock_inference_server.py` validates the wire format and echoes. That proves the
protocol is spoken correctly, and nothing about whether a served model gives the
same answer as the same model loaded in process.

This one loads the `.onnx` file and runs it, so the two routes in
`docs/INFERENCE_ENGINES_PLAN.md` can be compared against each other:

    API.Onnx   -> OnnxEngine  -> native ONNX Runtime, in process
    API.Inference -> HttpEngine -> KServe v2 over HTTP, to this server

Both `API.Models.Engine`. Same model file, same input, so the in-process result
is a GROUND TRUTH for the served one -- which is the thing P2 promised ("one
interface over both backends") and the thing nothing checked.

WHY THIS IS THE TENSORFLOW TEST TOO
-----------------------------------
KServe v2 is the protocol Triton and KServe use to serve TensorFlow, PyTorch,
ONNX and TensorRT alike; `API.Inference`'s own `Client` speaks it without
knowing what is behind it. So carrying a real model over the v2 wire exercises
exactly the path a served TensorFlow model takes. What it does not exercise is
TF Serving's *own* v1 REST API, which needs a TensorFlow SavedModel and a
TensorFlow install -- neither of which is here. That gap stays named in the plan
rather than papered over with a model that is not a TensorFlow one.

It needs no new model: `resnet34.onnx` is already in the tree, and it is the
same file the in-process side loads, which is the point.

Requires `onnxruntime` and `numpy`. Says SKIP and exits 0 without them -- a
machine lacking them cannot establish this, and should say so rather than fail.

Usage: serve_onnx_model.py --model <path.onnx> [--name test_model] [--port 0]
"""

import argparse
import json
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import numpy as np
    import onnxruntime as ort
except ImportError as err:                                  # pragma: no cover
    print("SKIP: %s -- install onnxruntime and numpy to run a real model" % err)
    raise SystemExit(0)

# The v2 datatype names, mapped to what numpy calls them.
NUMPY_OF = {
    "FP32": np.float32, "FP64": np.float64, "FP16": np.float16,
    "INT8": np.int8, "INT16": np.int16, "INT32": np.int32, "INT64": np.int64,
    "UINT8": np.uint8, "UINT16": np.uint16, "UINT32": np.uint32,
    "UINT64": np.uint64, "BOOL": np.bool_,
}
V2_OF = {v: k for k, v in NUMPY_OF.items()}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    session = None
    model_name = "test_model"

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, payload):
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if re.match(r"^/v2/models/%s/ready$" % re.escape(self.model_name), self.path):
            return self._send(200, {"name": self.model_name, "ready": True})
        if re.match(r"^/v2/models/%s$" % re.escape(self.model_name), self.path):
            return self._send(200, {
                "name": self.model_name,
                "platform": "onnxruntime_onnx",
                "inputs": [{"name": i.name, "datatype": "FP32",
                            "shape": [d if isinstance(d, int) else -1 for d in i.shape]}
                           for i in self.session.get_inputs()],
                "outputs": [{"name": o.name, "datatype": "FP32",
                             "shape": [d if isinstance(d, int) else -1 for d in o.shape]}
                            for o in self.session.get_outputs()],
            })
        self._send(404, {"error": "no such route: %s" % self.path})

    def do_POST(self):
        m = re.match(r"^/v2/models/([^/]+)/infer$", self.path)
        if not m:
            return self._send(404, {"error": "no such route: %s" % self.path})
        if m.group(1) != self.model_name:
            return self._send(404, {"error": "Request for unknown model: '%s'" % m.group(1)})

        length = int(self.headers.get("Content-Length") or 0)
        try:
            body = json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as err:
            return self._send(400, {"error": "body is not valid JSON: %s" % err})

        if not isinstance(body, dict) or not isinstance(body.get("inputs"), list):
            return self._send(400, {"error": "no 'inputs' array"})

        feed = {}
        for entry in body["inputs"]:
            try:
                name = entry["name"]
                shape = entry["shape"]
                datatype = entry["datatype"]
                data = entry["data"]
            except (KeyError, TypeError) as err:
                return self._send(400, {"error": "malformed input: %s" % err})
            if datatype not in NUMPY_OF:
                return self._send(400, {"error": "unknown datatype '%s'" % datatype})
            want = 1
            for dim in shape:
                want *= dim
            if len(data) != want:
                return self._send(400, {"error": "'%s' has %d element(s), shape %s wants %d"
                                                 % (name, len(data), shape, want)})
            feed[name] = np.asarray(data, dtype=NUMPY_OF[datatype]).reshape(shape)

        try:
            results = self.session.run(None, feed)
        except Exception as err:                            # the server's own failure
            return self._send(500, {"error": "inference failed: %s" % err})

        outputs = []
        for spec, value in zip(self.session.get_outputs(), results):
            arr = np.asarray(value)
            outputs.append({
                "name": spec.name,
                "datatype": V2_OF.get(arr.dtype.type, "FP32"),
                "shape": list(arr.shape),
                "data": arr.reshape(-1).tolist(),
            })
        self._send(200, {"model_name": self.model_name, "outputs": outputs})


def serve(model_path, name="test_model", port=0):
    Handler.session = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
    Handler.model_name = name
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd, thread


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--name", default="test_model")
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--print-port", action="store_true")
    args = ap.parse_args()

    httpd, _ = serve(args.model, args.name, args.port)
    if args.print_port:
        print(httpd.server_address[1], flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
