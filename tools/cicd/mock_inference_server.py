#!/usr/bin/env python3
"""A STRICT mock of KServe v2 and TensorFlow Serving v1, for API.Inference.

WHY THIS EXISTS
---------------
`programs/regression/inference_client_test.obs` asserts the request body through
`Client->DescribeRequest`, which needs no server and so runs on every platform.
What it cannot establish is that a server ACCEPTS that body, or that a real
response is parsed back into the right tensors: the round trip was untested.

This is the other half. It speaks both protocols over localhost, validates every
incoming request against the published schema and REJECTS anything malformed
with a 400 naming the reason -- so a request this accepts is one a conforming
server would accept, and a test that passes here exercised a real HTTP round
trip through `Client`, not a formatted string.

WHAT IT DOES AND DOES NOT ESTABLISH
-----------------------------------
It establishes that the client's requests satisfy the protocols as documented,
and that responses are parsed into tensors with the right names, datatypes,
shapes and values.

It does NOT establish agreement with Google's binary. This is a reading of the
published specifications, carefully made strict, but a real server that rejects
something this accepts would not be caught here. That residue is "our reading of
the spec", and closing it needs the real thing:

    docker run -p 8501:8501       --mount type=bind,source=<saved_model_dir>,target=/models/test_model       -e MODEL_NAME=test_model tensorflow/serving

then point the driver at it directly, which needs no mock at all:

    obr programs/tests/inference_round_trip.obe http://localhost:8501 v1

The driver takes a base URL precisely so it can be aimed at a real server. That
step is network- and container-dependent, which is why it is not wired into CI:
the convention here is that such steps are non-gating, and this suite is gating
because it reaches nothing but localhost.

No GPU path is covered. That needs a GPU and a served model, which no CI runner
here has.

PROTOCOLS
---------
KServe v2 (also called Open Inference):
  POST /v2/models/<model>/infer   {"inputs":[{name,shape,datatype,data}, ...]}
  GET  /v2/models/<model>         metadata
  GET  /v2/models/<model>/ready   204/200 when ready

TF Serving v1:
  POST /v1/models/<model>:predict {"inputs":{<name>: <nested array>, ...}}
  GET  /v1/models/<model>         {"model_version_status":[{"state": ...}]}

Usage:
  mock_inference_server.py --port 0 --print-port     # serve until killed
  mock_inference_server.py --selftest                # validate the validator
"""

import argparse
import json
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# KServe v2, "Tensor Data Types". A datatype outside this set is a client bug,
# and a real server rejects it, so this does too.
V2_DATATYPES = {
    "BOOL", "UINT8", "UINT16", "UINT32", "UINT64",
    "INT8", "INT16", "INT32", "INT64", "FP16", "FP32", "FP64", "BYTES",
}

MODEL_NAME_RE = re.compile(r"^[A-Za-z0-9_.\-]+$")


class Rejected(Exception):
    """A request a conforming server would refuse, with the reason."""


def _flat_len(shape):
    total = 1
    for dim in shape:
        total *= dim
    return total


def _depth_and_count(value, depth=0):
    """(nesting depth, leaf count) of a nested JSON array."""
    if isinstance(value, list):
        if not value:
            return depth + 1, 0
        depths = set()
        count = 0
        for item in value:
            d, c = _depth_and_count(item, depth + 1)
            depths.add(d)
            count += c
        if len(depths) != 1:
            raise Rejected("ragged nested array: branches have depths %s" % sorted(depths))
        return depths.pop(), count
    return depth, 1


def validate_v2_infer(body):
    """KServe v2 inference request. Returns the input tensors."""
    if not isinstance(body, dict):
        raise Rejected("body is not a JSON object")
    if "inputs" not in body:
        raise Rejected("no 'inputs' member")
    inputs = body["inputs"]
    if not isinstance(inputs, list) or not inputs:
        raise Rejected("'inputs' must be a non-empty array")

    seen = set()
    tensors = []
    for i, entry in enumerate(inputs):
        where = "inputs[%d]" % i
        if not isinstance(entry, dict):
            raise Rejected("%s is not an object" % where)
        for field in ("name", "shape", "datatype", "data"):
            if field not in entry:
                raise Rejected("%s has no '%s'" % (where, field))

        name = entry["name"]
        if not isinstance(name, str) or not name:
            raise Rejected("%s name must be a non-empty string" % where)
        if name in seen:
            raise Rejected("two inputs are both named '%s'" % name)
        seen.add(name)

        datatype = entry["datatype"]
        if datatype not in V2_DATATYPES:
            raise Rejected("%s datatype '%s' is not a v2 datatype" % (where, datatype))

        shape = entry["shape"]
        if not isinstance(shape, list) or not shape:
            raise Rejected("%s shape must be a non-empty array" % where)
        for dim in shape:
            if not isinstance(dim, int) or isinstance(dim, bool) or dim < 0:
                raise Rejected("%s shape holds a non-negative-integer dim %r" % (where, dim))

        data = entry["data"]
        if not isinstance(data, list):
            raise Rejected("%s data must be an array" % where)
        # v2 sends data FLAT; the element count must equal the shape's product.
        want = _flat_len(shape)
        if len(data) != want:
            raise Rejected("%s has %d element(s) but shape %s wants %d"
                           % (where, len(data), shape, want))
        for value in data:
            if isinstance(value, bool):
                if datatype != "BOOL":
                    raise Rejected("%s datatype %s but data holds a boolean" % (where, datatype))
            elif not isinstance(value, (int, float)):
                raise Rejected("%s data holds a non-numeric %r" % (where, value))

        tensors.append({"name": name, "shape": shape, "datatype": datatype, "data": data})
    return tensors


def validate_v1_predict(body):
    """TF Serving v1 predict request. Returns the named inputs."""
    if not isinstance(body, dict):
        raise Rejected("body is not a JSON object")
    # The real API accepts exactly one of "instances" or "inputs", never both.
    has_inputs = "inputs" in body
    has_instances = "instances" in body
    if has_inputs and has_instances:
        raise Rejected("body has both 'inputs' and 'instances'; the API allows exactly one")
    if not has_inputs and not has_instances:
        raise Rejected("body has neither 'inputs' nor 'instances'")

    if has_instances:
        instances = body["instances"]
        if not isinstance(instances, list) or not instances:
            raise Rejected("'instances' must be a non-empty array")
        _depth_and_count(instances)
        return {"instances": instances}

    inputs = body["inputs"]
    if not isinstance(inputs, dict) or not inputs:
        raise Rejected("'inputs' must be a non-empty object of name -> nested array")
    named = {}
    for name, value in inputs.items():
        if not name:
            raise Rejected("an input has an empty name")
        if not isinstance(value, list):
            raise Rejected("input '%s' must be a nested array" % name)
        # Ragged nesting is what _depth_and_count refuses; TF Serving infers the
        # shape from the nesting, so a ragged body has no shape to infer.
        _depth_and_count(value)
        named[name] = value
    return named


def nest(flat, shape):
    """Rebuild flat values into shape's nesting, as TF Serving answers."""
    if len(shape) <= 1:
        return list(flat)
    stride = _flat_len(shape[1:])
    return [nest(flat[i * stride:(i + 1) * stride], shape[1:])
            for i in range(shape[0])]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    # The model this mock serves, and what it answers with.
    model = "test_model"
    # Set by the suite to make the server answer a specific way.
    mode = "echo"

    def log_message(self, fmt, *args):      # quiet; the suite owns the output
        pass

    # ---------------------------------------------------------------- helpers
    def _send(self, code, payload):
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _raw(self, code, body, ctype):
        raw = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _reject(self, reason):
        self.server.rejections.append(reason)
        self._send(400, {"error": reason})

    def _body(self):
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        self.server.last_raw_body = raw
        try:
            return json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as err:
            raise Rejected("body is not valid JSON: %s" % err)

    # ------------------------------------------------------------------- GET
    def do_GET(self):
        path = self.path
        m = re.match(r"^/v2/models/([^/]+)/ready$", path)
        if m:
            if not MODEL_NAME_RE.match(m.group(1)):
                return self._reject("model name %r is not a valid path segment" % m.group(1))
            self.server.hits.append(("v2_ready", m.group(1)))
            if m.group(1) != self.model:
                return self._send(404, {"error": "model not found"})
            return self._send(200, {"name": m.group(1), "ready": True})

        m = re.match(r"^/v2/models/([^/]+)$", path)
        if m:
            self.server.hits.append(("v2_metadata", m.group(1)))
            if m.group(1) != self.model:
                return self._send(404, {"error": "model not found"})
            return self._send(200, {
                "name": self.model,
                "platform": "onnxruntime_onnx",
                "inputs": [{"name": "x", "datatype": "FP32", "shape": [1, 3]}],
                "outputs": [{"name": "y", "datatype": "FP32", "shape": [1, 2]}],
            })

        m = re.match(r"^/v1/models/([^/]+)$", path)
        if m:
            self.server.hits.append(("v1_status", m.group(1)))
            if m.group(1) != self.model:
                return self._send(404, {"error": {"code": 5, "message": "model not found"}})
            return self._send(200, {"model_version_status": [
                {"version": "1", "state": "AVAILABLE",
                 "status": {"error_code": "OK", "error_message": ""}}]})

        self._send(404, {"error": "no such route: %s" % path})

    # ------------------------------------------------------------------ POST
    def do_POST(self):
        # The failure modes ParseResponse documents but nothing exercised. A
        # gateway answers before the model server does, so these come first and
        # do not validate the body.
        if self.mode == "html_error":
            return self._raw(502, "<html><head><title>502 Bad Gateway</title></head>"
                                  "<body><h1>502 Bad Gateway</h1></body></html>",
                             "text/html")
        if self.mode == "empty_body":
            return self._raw(200, "", "application/json")
        if self.mode == "json_error_500":
            return self._send(500, {"error": "model server is loading"})

        try:
            m = re.match(r"^/v2/models/([^/]+)/infer$", self.path)
            if m:
                body = self._body()
                tensors = validate_v2_infer(body)
                self.server.hits.append(("v2_infer", m.group(1)))
                self.server.last_request = body
                # A real server 404s an unknown model on the INFER route too,
                # not only on metadata/ready. Serving any name here would have
                # made the suite's "unknown model" assertions vacuous.
                if m.group(1) != self.model:
                    return self._send(404, {"error": "Request for unknown model: '%s'"
                                                     % m.group(1)})
                if self.mode == "no_outputs":
                    return self._send(200, {"model_name": m.group(1)})
                # Echo each input back, doubled, so the suite can tell a parsed
                # value from a coincidence.
                return self._send(200, {"model_name": m.group(1), "outputs": [
                    {"name": t["name"] + "_out", "datatype": t["datatype"],
                     "shape": t["shape"],
                     "data": [v * 2 for v in t["data"]] if t["datatype"] != "BOOL"
                             else [not v for v in t["data"]]}
                    for t in tensors]})

            m = re.match(r"^/v1/models/([^/]+):predict$", self.path)
            if m:
                body = self._body()
                named = validate_v1_predict(body)
                self.server.hits.append(("v1_predict", m.group(1)))
                self.server.last_request = body
                # As above; TF Serving answers 404 with its own error envelope.
                if m.group(1) != self.model:
                    return self._send(404, {"error": "Servable not found for request: "
                                                     "Latest(%s)" % m.group(1)})
                if self.mode == "v1_predictions":
                    # the row format: {"predictions": <nested>}
                    first = next(iter(named.values()))
                    return self._send(200, {"predictions": first})
                if self.mode == "v1_single":
                    first = next(iter(named.values()))
                    return self._send(200, {"outputs": first})
                if self.mode == "v1_neither":
                    return self._send(200, {"model_version": "1"})
                # named outputs, each echoed
                return self._send(200, {"outputs": {
                    name + "_out": value for name, value in named.items()}})

            self._send(404, {"error": "no such route: %s" % self.path})
        except Rejected as err:
            self._reject(str(err))


def serve(port, model="test_model", mode="echo"):
    Handler.model = model
    Handler.mode = mode
    httpd = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    httpd.hits = []
    httpd.rejections = []
    httpd.last_request = None
    httpd.last_raw_body = b""
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd, thread


# ------------------------------------------------------------------ self-test
def _selftest():
    """The validator must reject what a real server rejects.

    A mock that accepts everything would make the suite pass without proving
    anything, which is the failure mode this whole file exists to avoid.
    """
    good_v2 = {"inputs": [{"name": "x", "shape": [1, 3], "datatype": "FP32",
                           "data": [1.0, 2.0, 3.0]}]}
    bad_v2 = [
        ("no inputs", {}),
        ("empty inputs", {"inputs": []}),
        ("missing datatype", {"inputs": [{"name": "x", "shape": [1], "data": [1]}]}),
        ("unknown datatype", {"inputs": [{"name": "x", "shape": [1], "datatype": "FLOAT", "data": [1]}]}),
        ("count disagrees with shape", {"inputs": [{"name": "x", "shape": [2, 2], "datatype": "FP32", "data": [1, 2, 3]}]}),
        ("nested data in v2", {"inputs": [{"name": "x", "shape": [1, 2], "datatype": "FP32", "data": [[1, 2]]}]}),
        ("duplicate names", {"inputs": [
            {"name": "x", "shape": [1], "datatype": "FP32", "data": [1]},
            {"name": "x", "shape": [1], "datatype": "FP32", "data": [2]}]}),
        ("negative dim", {"inputs": [{"name": "x", "shape": [-1], "datatype": "FP32", "data": [1]}]}),
        ("empty name", {"inputs": [{"name": "", "shape": [1], "datatype": "FP32", "data": [1]}]}),
        ("boolean in FP32", {"inputs": [{"name": "x", "shape": [1], "datatype": "FP32", "data": [True]}]}),
    ]
    good_v1 = {"inputs": {"x": [[1.0, 2.0]]}}
    bad_v1 = [
        ("neither key", {}),
        ("both keys", {"inputs": {"x": [[1]]}, "instances": [[1]]}),
        ("inputs not an object", {"inputs": [[1, 2]]}),
        ("empty inputs", {"inputs": {}}),
        ("ragged nesting", {"inputs": {"x": [[1, 2], 3]}}),
        ("scalar input", {"inputs": {"x": 5}}),
    ]

    failures = 0
    try:
        validate_v2_infer(good_v2)
        print("  ok   a conforming v2 request is accepted")
    except Rejected as err:
        print("  FAIL a conforming v2 request was rejected: %s" % err)
        failures += 1
    for label, body in bad_v2:
        try:
            validate_v2_infer(body)
            print("  FAIL v2 accepted: %s" % label)
            failures += 1
        except Rejected:
            print("  ok   v2 rejects: %s" % label)

    try:
        validate_v1_predict(good_v1)
        print("  ok   a conforming v1 request is accepted")
    except Rejected as err:
        print("  FAIL a conforming v1 request was rejected: %s" % err)
        failures += 1
    for label, body in bad_v1:
        try:
            validate_v1_predict(body)
            print("  FAIL v1 accepted: %s" % label)
            failures += 1
        except Rejected:
            print("  ok   v1 rejects: %s" % label)

    # nest() is what the v1 responses are built from
    if nest([1, 2, 3, 4], [2, 2]) != [[1, 2], [3, 4]]:
        print("  FAIL nest([1,2,3,4],[2,2]) = %r" % (nest([1, 2, 3, 4], [2, 2]),))
        failures += 1
    else:
        print("  ok   nest rebuilds a flat list into its shape")

    print("")
    print("PASS: the validator accepts conforming requests and rejects %d malformed ones"
          % (len(bad_v2) + len(bad_v1)) if not failures else "FAIL: %d" % failures)
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=0)
    ap.add_argument("--model", default="test_model")
    ap.add_argument("--mode", default="echo")
    ap.add_argument("--print-port", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()

    httpd, _ = serve(args.port, args.model, args.mode)
    if args.print_port:
        print(httpd.server_address[1], flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
