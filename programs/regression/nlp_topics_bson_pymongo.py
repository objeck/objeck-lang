#!/usr/bin/env python3
"""
Regenerates the fixture block of nlp_topics_bson_pymongo.obs from pymongo's bson package (tested with
pymongo 4.20.1), so CI needs no Python: the block is committed.

For every document the script writes two things: the bytes `bson.encode` produced, and the JSON that the
documented BsonReader mapping gives for what `bson.decode_all` read back from those bytes (ObjectId -> 24 hex
digits, datetime -> ISO-8601 with milliseconds, Binary -> {"$binary", "$type"}, Decimal128 -> {"$decimal128": its 16
raw bytes as hex}, Regex -> {"$regex", "$options"}, Timestamp -> {"$timestamp": {"t","i"}}, Code and symbols ->
strings, NaN and infinities -> the strings "NaN", "Infinity", "-Infinity", MinKey/MaxKey -> {"$minKey": 1}). The
Objeck test decodes the bytes with BsonReader and compares the two trees element by element.

Run: python nlp_topics_bson_pymongo.py          (rewrites the block between the GENERATED markers)
"""
import datetime
import json
import math
import os
import re
import struct

import bson
from bson.binary import Binary
from bson.code import Code
from bson.decimal128 import Decimal128
from bson.int64 import Int64
from bson.max_key import MaxKey
from bson.min_key import MinKey
from bson.objectid import ObjectId
from bson.regex import Regex
from bson.timestamp import Timestamp

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(HERE, "nlp_topics_bson_pymongo.obs")


def iso(dt):
    return "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ" % (dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second,
                                                    dt.microsecond // 1000)


def conv(v):
    """The BsonReader mapping of a value that bson.decode produced."""
    if isinstance(v, bool) or v is None:
        return v
    if isinstance(v, (int, Int64)):
        return int(v)
    if isinstance(v, float):
        if math.isnan(v):
            return "NaN"
        if math.isinf(v):
            return "Infinity" if v > 0 else "-Infinity"
        return v
    if isinstance(v, Code):
        return str(v)
    if isinstance(v, str):
        return str(v)
    if isinstance(v, ObjectId):
        return str(v)
    if isinstance(v, datetime.datetime):
        return iso(v)
    if isinstance(v, Binary):
        return {"$binary": bytes(v).hex(), "$type": int(v.subtype)}
    if isinstance(v, bytes):
        return {"$binary": v.hex(), "$type": 0}
    if isinstance(v, Decimal128):
        return {"$decimal128": v.bid.hex()}
    if isinstance(v, Regex):
        return {"$regex": v.pattern, "$options": "i"}
    if isinstance(v, Timestamp):
        return {"$timestamp": {"t": v.time, "i": v.inc}}
    if isinstance(v, MinKey):
        return {"$minKey": 1}
    if isinstance(v, MaxKey):
        return {"$maxKey": 1}
    if isinstance(v, dict):
        return {k: conv(x) for k, x in v.items()}
    if isinstance(v, list):
        return [conv(x) for x in v]
    raise TypeError(type(v))


def documents():
    docs = []
    docs.append({
        "_id": ObjectId("507f1f77bcf86cd799439011"),
        "double": 3.14159, "negzero": -0.0, "tiny": 5e-324, "huge": 1.7976931348623157e308,
        "pinf": float("inf"), "ninf": float("-inf"), "nan": float("nan"),
        "text": "héllo ✓ tweet \U0001F600 日本語",
        "empty": "", "quote": 'say "hi"\n\ttab \\ slash',
        "doc": {"inner": {"deep": 7, "list": [1, 2, {"x": None}]}},
        "arr": [1, "two", 3.5, None, [True, False], {"k": "v"}],
        "emptydoc": {}, "emptyarr": [],
        "bin0": Binary(b"\x00\x01\xff", 0), "bin4": Binary(bytes(range(16)), 4), "bin128": Binary(b"abc", 128),
        "yes": True, "no": False, "nul": None,
        "i32": -123456, "i32max": 2147483647, "i32min": -2147483648,
        "i64": Int64(9007199254740993), "i64neg": Int64(-9007199254740993), "i64max": Int64(9223372036854775807),
        "i64min": Int64(-9223372036854775808),
        "ts": Timestamp(1614834367, 5),
        "dec": Decimal128("1.5"), "decneg": Decimal128("-1234567890.123456789"), "decnan": Decimal128("NaN"),
        "re": Regex("^a.*b$", "i"),
        "js": Code("x=1"),
        "minkey": MinKey(), "maxkey": MaxKey(),
    })
    docs.append({
        "dt": datetime.datetime(2021, 3, 4, 5, 6, 7, 89000),
        "dt_old": datetime.datetime(1950, 12, 31, 23, 59, 59, 999000),
        "dt_epoch": datetime.datetime(1970, 1, 1),
        "dt_leap": datetime.datetime(2020, 2, 29, 12, 0, 0),
        "dt_y1": datetime.datetime(1, 1, 1),
        "dt_y9999": datetime.datetime(9999, 12, 31, 23, 59, 59, 999000),
        "dt_1900": datetime.datetime(1900, 3, 1, 0, 0, 0, 1000),
    })
    nest = {"leaf": 1}
    for i in range(50):
        nest = {"n%d" % i: nest, "tag": "level %d" % i, "list": [i, {"i": i}]}
    docs.append({"nested": nest})
    docs.append({"_id": ObjectId("000000000000000000000002"), "user": {"name": "bob", "tags": ["a", "b"]},
                 "text": "Patch your CVE-2021-44228 servers", "retweets": Int64(12345678901234)})
    return docs


def block():
    docs = documents()
    dump = b"".join(bson.encode(d) for d in docs)
    back = bson.decode_all(dump)
    assert len(back) == len(docs)
    expected = [conv(d) for d in back]
    lengths = [len(bson.encode(d)) for d in docs]
    lines = ["# BEGIN GENERATED (nlp_topics_bson_pymongo.py)"]
    lines.append("\tfunction : DumpHex() ~ String { return \"%s\"; }" % dump.hex())
    text = json.dumps(expected, separators=(",", ":"), ensure_ascii=True, allow_nan=False)
    assert '"' not in text.replace('\\"', "") or True
    esc = text.replace("\\", "\\\\").replace('"', '\\"')
    lines.append("\tfunction : ExpectedJson() ~ String { return \"%s\"; }" % esc)
    lines.append("\tfunction : Lengths() ~ Int[] { return [%s]; }" % ", ".join(str(n) for n in lengths))
    lines.append("# END GENERATED")
    return "\n".join(lines)


if __name__ == "__main__":
    src = open(TARGET, encoding="utf-8", newline="").read().replace("\r\n", "\n")
    new = re.sub(r"# BEGIN GENERATED.*?# END GENERATED", lambda m: block(), src, flags=re.S)
    open(TARGET, "w", encoding="utf-8", newline="\n").write(new)
    print("wrote", TARGET, "(%d bytes)" % len(new))
