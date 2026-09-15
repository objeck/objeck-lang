"""The one schema for tools/fuzz/known.json, shared by the fuzzer
(tools/fuzz/run_fuzz.py) and the nightly triage (tools/cicd/nightly_triage.py).
Standard library only.

File
----
    {"schema": 1, "known": [ENTRY, ...]}

"schema" is optional and, when present, must be 1. Any other top-level key, a
bare list, or the old {"signatures": [...]} spelling is an error: two tools
that each accepted a different subset is how the fuzzer's entries came to be
skipped silently by the triage.

Entry
-----
Matchers (an entry needs at least one):

    signature  regex (re.search) over a fuzzer signature, as fuzzlib.signature
               prints it, e.g. "^diverge: s0/off,s3/default \\| s3/off ".
               Only a fuzzer finding has one.
    id         a nightly triage id (nightly_triage.signature), compared exactly.
               It alone is enough to match.
    leg        regex over the nightly leg ("linux-arm64"). A run with no leg (a
               local run_fuzz without --leg) never matches an entry that has one.
    step       regex over the nightly step ("fuzz", "stress", ...). run_fuzz is
               always step "fuzz".
    config     regex over the failure's configuration        (nightly only)
    test       regex over the failure's test name            (nightly only)
    message    regex over the failure's message plus detail  (nightly only)

Annotations: "note" (why it is expected; required with "signature"), "issue".

An entry with "signature" may narrow it only by "leg" and "step": the fuzzer
knows nothing else, so config/test/message beside a signature would mean one
thing to the fuzzer and another to the triage, and is rejected.

Every matcher present must match. Unknown keys are rejected (a misspelt
matcher would otherwise make an entry match everything).

The fuzzer's failure line
-------------------------
run_fuzz prints each new finding as FUZZ_FAILURE_FORMAT; the triage's generic
parser turns it into test "fuzz" and message "fuzz: <signature> (seed N)", and
fuzz_signature() recovers the signature from that, so one "signature" entry is
honoured identically by both tools.
"""

import json
import os
import re

SCHEMA_VERSION = 1
CONTEXT_FIELDS = ("leg", "step", "config", "test", "message")
MATCHERS = ("signature", "id") + CONTEXT_FIELDS
ANNOTATIONS = ("note", "issue")
SIGNATURE_CONTEXT = ("leg", "step")

FUZZ_STEP = "fuzz"
FUZZ_TEST = "fuzz"
FUZZ_FAILURE_FORMAT = "FAIL fuzz: %s (seed %s)"
_FUZZ_MESSAGE_RE = re.compile(r"^fuzz:\s*(.*?)\s*\(seed [^()]*\)\s*$", re.S)


class KnownError(ValueError):
    """known.json is missing, unreadable or does not follow the schema."""


def fuzz_failure_line(signature, seed):
    return FUZZ_FAILURE_FORMAT % (signature, seed)


def fuzz_signature(test, message):
    """The fuzzer signature inside a parsed fuzz failure, or None."""
    if test != FUZZ_TEST:
        return None
    m = _FUZZ_MESSAGE_RE.match(message or "")
    return m.group(1) if m else None


def parse(data, source="known.json"):
    """Validate decoded JSON; return compiled entries or raise KnownError."""
    if not isinstance(data, dict):
        raise KnownError("%s: top level must be an object {\"schema\": 1, \"known\": [...]}" % source)
    extra = sorted(set(data) - {"schema", "known"})
    if extra:
        raise KnownError("%s: unknown top-level key(s) %s (the list is \"known\")" % (source, ", ".join(extra)))
    if "schema" in data and data["schema"] != SCHEMA_VERSION:
        raise KnownError("%s: schema %r, this tool reads %d" % (source, data["schema"], SCHEMA_VERSION))
    items = data.get("known")
    if not isinstance(items, list):
        raise KnownError("%s: \"known\" must be a list" % source)
    entries = []
    for i, raw in enumerate(items):
        where = "%s: entry %d" % (source, i)
        if not isinstance(raw, dict):
            raise KnownError("%s is not an object" % where)
        unknown = sorted(set(raw) - set(MATCHERS) - set(ANNOTATIONS))
        if unknown:
            raise KnownError("%s: unknown key(s) %s" % (where, ", ".join(unknown)))
        present = [k for k in MATCHERS if raw.get(k)]
        if not present:
            raise KnownError("%s has no matcher (one of %s)" % (where, ", ".join(MATCHERS)))
        for k in list(MATCHERS) + list(ANNOTATIONS):
            if k in raw and not isinstance(raw[k], str):
                raise KnownError("%s: %s must be a string" % (where, k))
        if raw.get("signature"):
            mixed = [k for k in CONTEXT_FIELDS if raw.get(k) and k not in SIGNATURE_CONTEXT]
            if mixed:
                raise KnownError("%s: signature may be narrowed only by leg and step, not %s"
                                 % (where, ", ".join(mixed)))
            if not raw.get("note"):
                raise KnownError("%s: a signature entry needs a note" % where)
        compiled = {"raw": raw}
        for k in ("signature",) + CONTEXT_FIELDS:
            if raw.get(k):
                try:
                    compiled[k] = re.compile(raw[k])
                except re.error as err:
                    raise KnownError("%s: bad %s regex: %s" % (where, k, err))
        entries.append(compiled)
    return entries


def load(path):
    """Read and validate a known.json; raise KnownError on any problem."""
    if not path or not os.path.exists(path):
        raise KnownError("known-signature file missing: %s" % path)
    try:
        # utf-8-sig: a known.json saved by a Windows editor may carry a BOM
        with open(path, "r", encoding="utf-8-sig") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        raise KnownError("known-signature file unreadable: %s (%s)" % (path, e))
    return parse(data, path)


def match(entries, item):
    """The raw entry that `item` matches, or None.

    item keys: signature (a fuzzer signature or None), id, and the
    CONTEXT_FIELDS plus detail; absent keys read as ''."""
    for e in entries:
        raw = e["raw"]
        if raw.get("id"):
            if raw["id"] == item.get("id"):
                return raw
            if len(e) == 1:        # an id-only entry that did not match
                continue
        if "signature" in e:
            sig = item.get("signature")
            if sig is None or not e["signature"].search(sig):
                continue
        ok = True
        for k in CONTEXT_FIELDS:
            if k not in e:
                continue
            value = item.get(k) or ""
            if k == "message":
                value = value + "\n" + (item.get("detail") or "")
            if not e[k].search(value):
                ok = False
                break
        if ok and ("signature" in e or any(k in e for k in CONTEXT_FIELDS)):
            return raw
    return None
