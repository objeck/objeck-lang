#!/usr/bin/env python3
"""API.Inference over a live HTTP round trip, both protocols.

WHAT THIS ADDS
--------------
`programs/regression/inference_client_test.obs` asserts the request body through
`Client->DescribeRequest`. That needs no server, so it runs in every regression
pass on every platform -- but it cannot establish that a server ACCEPTS the body,
or that a real response is parsed back into the right tensors. The round trip
was untested.

This starts `mock_inference_server.py` -- a strict mock of KServe v2 and
TensorFlow Serving v1 that rejects anything malformed with a 400 naming the
reason -- and runs `programs/tests/inference_round_trip.obs` against it. So each
assertion there went out over HTTP, was validated against the protocol, and came
back through the library's own parser.

It also asserts the server's own view: that the expected routes were hit, and
that the mock rejected NOTHING. A request the mock rejects is a request a
conforming server would reject, so a clean rejection log is part of the result
rather than a detail in a log file.

HONEST LIMITS
-------------
The mock is a careful reading of the published protocols, made strict. It does
not establish agreement with Google's binary: a real server that rejects
something this accepts would not be caught here. Closing that needs a container
(`tensorflow/serving`) and is a separate, network-dependent step -- the
convention in this repo is that such steps are non-gating, and this one is
gating precisely because it reaches nothing but localhost.

No GPU path is covered; that needs a GPU and a served model.

Usage: test_inference_round_trip.py <bin-dir> [--obe <path>]
"""

import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

import mock_inference_server as mock  # noqa: E402

PASS = 0
FAIL = 0


def result(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s" % name)
    else:
        FAIL += 1
        print("  [FAIL] %s%s" % (name, ("  " + detail) if detail else ""))
    return ok


def run_driver(obe, bin_dir, base_url, protocol):
    exe = ".exe" if os.name == "nt" else ""
    obr = os.path.join(bin_dir, "obr" + exe)
    env = dict(os.environ)
    lib = os.path.abspath(os.path.join(bin_dir, "..", "lib"))
    env["OBJECK_LIB_PATH"] = lib
    # A client must not sit in a JIT-compiled spin; the LSP harness sets the
    # same thing for the same reason.
    env.setdefault("OBJECK_JIT_THRESHOLD", "999999999")
    proc = subprocess.run([obr, obe, base_url, protocol],
                          capture_output=True, text=True, env=env, timeout=180)
    return proc


def check_protocol(obe, bin_dir, protocol, label):
    httpd, _ = mock.serve(0)
    port = httpd.server_address[1]
    base = "http://127.0.0.1:%d" % port
    print("\n%s (%s)" % (label, base))
    try:
        # the mock must actually be up before the driver starts; a connection
        # refused here would read as a client bug
        ready = False
        deadline = time.time() + 20
        while time.time() < deadline:
            try:
                urllib.request.urlopen(base + "/v2/models/test_model/ready", timeout=2).read()
                ready = True
                break
            except urllib.error.HTTPError:
                ready = True            # answered, which is all this checks
                break
            except OSError:
                time.sleep(0.1)
        if not result("the mock server accepts connections", ready):
            return

        proc = run_driver(obe, bin_dir, base, protocol)
        for line in (proc.stdout or "").splitlines():
            line = line.strip()
            if line.startswith("PASS: "):
                result(line[6:], True)
            elif line.startswith("FAIL: "):
                result(line[6:], False)

        result("the driver exited 0", proc.returncode == 0,
               "exit=%d stderr=%r" % (proc.returncode, (proc.stderr or "")[:400]))

        # The server's own view. A request the mock REJECTED is one a conforming
        # server would have rejected, so this is a result, not a log line.
        result("the server rejected nothing", not httpd.rejections,
               "rejections: %s" % httpd.rejections[:4])

        kinds = {kind for kind, _ in httpd.hits}
        wanted = {"v2_infer", "v2_metadata", "v2_ready"} if protocol == "v2" \
            else {"v1_predict", "v1_status"}
        result("every expected route was exercised: %s" % ", ".join(sorted(wanted)),
               wanted <= kinds, "hit: %s" % sorted(kinds))
    finally:
        httpd.shutdown()
        httpd.server_close()


def check_response_shapes(obe, bin_dir):
    """The three TF Serving response shapes, which differ per model.

    A v1 server answers {"outputs": <nested>} for one output, {"outputs":
    {name: ...}} for several, and {"predictions": <nested>} in the row format.
    The library reads all three; a model that answers in the shape the library
    does not expect yields a reported error rather than a wrong tensor.
    """
    print("\nTF Serving response shapes")
    for mode, expect_ok, label in (
        ("v1_single", True, "a single unnamed output is read"),
        ("v1_predictions", True, "the row format's 'predictions' is read"),
        ("v1_neither", False, "a response with neither key is REPORTED, not guessed"),
    ):
        httpd, _ = mock.serve(0, mode=mode)
        base = "http://127.0.0.1:%d" % httpd.server_address[1]
        try:
            proc = run_driver(obe, bin_dir, base, "v1")
            out = proc.stdout or ""
            got_tensors = "PASS: a v1 inference returns tensors" in out
            result(label, got_tensors == expect_ok,
                   "exit=%d out=%r" % (proc.returncode, out[-220:]))
            result("  and the server rejected nothing (%s)" % mode, not httpd.rejections,
                   "rejections: %s" % httpd.rejections[:3])
        finally:
            httpd.shutdown()
            httpd.server_close()


def check_error_paths(obe, bin_dir):
    """The failure modes Wire->ParseResponse documents but nothing exercised.

    A caller gets Nil and a message; the message is the only thing it can act
    on, so the message is what is asserted. Each mode names a real situation: a
    gateway answering with HTML before the model server does, a truncated or
    empty body, and the server's own JSON error envelope.
    """
    print("\nerror paths")
    cases = (
        ("json_error_500", "model server is loading",
         "the server's own JSON error text is reported verbatim"),
        ("html_error", "502 Bad Gateway",
         "a proxy's HTML page is reported as the body, not as a parse failure"),
        ("empty_body", "empty",
         "an empty body is reported as empty"),
    )
    for mode, needle, label in cases:
        httpd, _ = mock.serve(0, mode=mode)
        base = "http://127.0.0.1:%d" % httpd.server_address[1]
        try:
            proc = run_driver(obe, bin_dir, base, "errors")
            out = proc.stdout or ""
            result("[%s] yields Nil rather than a tensor" % mode,
                   "PASS: a failing server yields Nil rather than a tensor" in out,
                   out[-200:])
            text = ""
            for line in out.splitlines():
                if line.startswith("ERROR-TEXT: "):
                    text = line[len("ERROR-TEXT: "):]
            result("[%s] %s" % (mode, label), needle.lower() in text.lower(),
                   "reported: %r" % text)
        finally:
            httpd.shutdown()
            httpd.server_close()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])

    obe = None
    if "--obe" in sys.argv:
        obe = os.path.abspath(sys.argv[sys.argv.index("--obe") + 1])
    else:
        obe = os.path.join(REPO, "programs", "tests", "inference_round_trip.obe")

    exe = ".exe" if os.name == "nt" else ""
    for needed in (os.path.join(bin_dir, "obr" + exe), obe):
        if not os.path.exists(needed):
            print("Error: missing %s" % needed)
            print("Build the driver first:")
            print("  obc -src programs/tests/inference_round_trip.obs "
                  "-lib models,inference,net,json,cipher,gen_collect "
                  "-dest programs/tests/inference_round_trip.obe")
            return 2

    print("API.Inference round trip")
    print("  driver: %s" % obe)
    print("  bin:    %s" % bin_dir)

    # The mock's own validator first: a mock that accepts everything would make
    # every assertion below pass while proving nothing.
    print("\nthe mock rejects malformed requests")
    rc = subprocess.run([sys.executable, "-I",
                         os.path.join(HERE, "mock_inference_server.py"), "--selftest"],
                        capture_output=True, text=True)
    result("the strict validator's self-test passes", rc.returncode == 0,
           (rc.stdout or "")[-300:])

    check_protocol(obe, bin_dir, "v2", "KServe v2")
    check_protocol(obe, bin_dir, "v1", "TensorFlow Serving v1")
    check_response_shapes(obe, bin_dir)
    check_error_paths(obe, bin_dir)

    print("\n  Results: %d passed, %d failed" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    raise SystemExit(main())
