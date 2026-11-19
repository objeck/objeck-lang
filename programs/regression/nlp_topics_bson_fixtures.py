#!/usr/bin/env python3
"""
Hand-encodes the BSON fixtures of nlp_topics_bson.obs, straight from the specification at
bsonspec.org (struct.pack only; no bson or pymongo package, neither was installed), and prints
them as hex strings plus the values the test expects. The expected ISO dates come from Python's
datetime module, so they do not depend on the library's own calendar code.

Run: python nlp_topics_bson_fixtures.py
"""
import struct
import datetime

def cstr(s):
    return s.encode("utf-8") + b"\x00"

def elem(t, name, payload):
    return bytes([t]) + cstr(name) + payload

def doc(*elems, raw_len=None, term=b"\x00"):
    body = b"".join(elems) + term
    n = 4 + len(body) if raw_len is None else raw_len
    return struct.pack("<i", n) + body

def string(s):
    b = s.encode("utf-8")
    return struct.pack("<i", len(b) + 1) + b + b"\x00"

def array(*values):
    # values are (type, payload) pairs
    return doc(*[elem(t, str(i), p) for i, (t, p) in enumerate(values)])

def ms(dt):
    return int((dt - datetime.datetime(1970, 1, 1)) / datetime.timedelta(milliseconds=1))

def iso(dt_ms):
    d = datetime.datetime(1970, 1, 1) + datetime.timedelta(milliseconds=dt_ms)
    return d.strftime("%Y-%m-%dT%H:%M:%S.") + "%03dZ" % (d.microsecond // 1000)

ts_new = datetime.datetime(2021, 3, 4, 5, 6, 7, 89000)
ts_old = datetime.datetime(1950, 12, 31, 23, 59, 59, 999000)
ts_epoch = datetime.datetime(1970, 1, 1)
oid = bytes.fromhex("507f1f77bcf86cd799439011")
dec = bytes(range(0x10, 0x20))
unicode_text = "héllo ✓ tweet \U0001F600"

all_types = doc(
    elem(0x01, "double", struct.pack("<d", 3.14159)),
    elem(0x01, "pinf", struct.pack("<d", float("inf"))),
    elem(0x01, "ninf", struct.pack("<d", float("-inf"))),
    elem(0x01, "nan", struct.pack("<d", float("nan"))),
    elem(0x02, "text", string(unicode_text)),
    elem(0x02, "empty", string("")),
    elem(0x03, "doc", doc(elem(0x03, "inner", doc(elem(0x10, "deep", struct.pack("<i", 7)))))),
    elem(0x04, "arr", array((0x10, struct.pack("<i", 1)), (0x02, string("two")), (0x01, struct.pack("<d", 3.5)), (0x0A, b""))),
    elem(0x05, "bin", struct.pack("<i", 3) + b"\x00" + b"\x00\x01\xff"),
    elem(0x06, "undef", b""),
    elem(0x07, "oid", oid),
    elem(0x08, "yes", b"\x01"),
    elem(0x08, "no", b"\x00"),
    elem(0x09, "dt", struct.pack("<q", ms(ts_new))),
    elem(0x09, "dt_old", struct.pack("<q", ms(ts_old))),
    elem(0x09, "dt_epoch", struct.pack("<q", 0)),
    elem(0x09, "dt_huge", struct.pack("<q", 253402300800000)),
    elem(0x0A, "nul", b""),
    elem(0x0B, "re", cstr("^a.*b$") + cstr("i")),
    elem(0x0D, "js", string("x=1")),
    elem(0x0E, "sym", string("s")),
    elem(0x10, "i32", struct.pack("<i", -123456)),
    elem(0x10, "i32max", struct.pack("<i", 2147483647)),
    elem(0x10, "i32min", struct.pack("<i", -2147483648)),
    elem(0x11, "ts", struct.pack("<II", 5, 1614834367)),
    elem(0x12, "i64", struct.pack("<q", 9007199254740993)),
    elem(0x12, "i64neg", struct.pack("<q", -9007199254740993)),
    elem(0x13, "dec", dec),
    elem(0xFF, "minkey", b""),
    elem(0x7F, "maxkey", b""),
)

tweets = [
    doc(elem(0x07, "_id", bytes.fromhex("000000000000000000000001")), elem(0x02, "text", string("First #malware sample, see http://t.co/x")), elem(0x03, "user", doc(elem(0x02, "name", string("alice"))))),
    doc(elem(0x07, "_id", bytes.fromhex("000000000000000000000002")), elem(0x02, "body", string("no text field here"))),
    doc(elem(0x07, "_id", bytes.fromhex("000000000000000000000003")), elem(0x10, "text", struct.pack("<i", 42))),
    doc(elem(0x07, "_id", bytes.fromhex("000000000000000000000004")), elem(0x02, "text", string("Patch your CVE-2021-44228 servers")), elem(0x03, "user", doc(elem(0x02, "name", string("bob"))))),
]
dump = b"".join(tweets)

def nested(levels):
    d = doc(elem(0x10, "leaf", struct.pack("<i", 1)))
    for _ in range(levels):
        d = doc(elem(0x03, "n", d))
    return d

def h(b):
    return b.hex()

print("ALL_TYPES   = " + h(all_types))
print("DUMP        = " + h(dump))
print("DUMP_LENGTHS = " + " ".join(str(len(t)) for t in tweets))
print("NESTED_100  = " + h(nested(100)))
print("NESTED_101  = " + h(nested(101)))
print("LEN_TOO_BIG = " + h(doc(elem(0x10, "a", struct.pack("<i", 1)), raw_len=1000)))
print("LEN_FOUR    = " + h(struct.pack("<i", 4) + b"\x00" * 8))
print("LEN_NEG     = " + h(struct.pack("<i", -1) + b"\x00" * 8))
print("NO_TERM     = " + h(doc(elem(0x10, "a", struct.pack("<i", 1)), term=b"\x01")))
print("BAD_STRLEN  = " + h(doc(elem(0x02, "s", struct.pack("<i", 100) + b"ab\x00"))))
print("BAD_STRZERO = " + h(doc(elem(0x02, "s", struct.pack("<i", 0)))))
print("STR_NO_NUL  = " + h(doc(elem(0x02, "s", struct.pack("<i", 3) + b"abc"))))
print("NO_KEY_END  = " + h(struct.pack("<i", 10) + b"\x10abcd" + b"\x00"))
print("BAD_BOOL    = " + h(doc(elem(0x08, "b", b"\x02"))))
print("BAD_TYPE    = " + h(doc(elem(0x99, "x", b"\x00\x00"))))
print("DBPOINTER   = " + h(doc(elem(0x0C, "p", string("ns") + oid))))
print("CODE_SCOPE  = " + h(doc(elem(0x0F, "c", struct.pack("<i", 12) + string("x") + doc()))))
print("NEG_BINARY  = " + h(doc(elem(0x05, "b", struct.pack("<i", -5) + b"\x00"))))
print("INNER_TOO_BIG = " + h(doc(elem(0x03, "d", doc(elem(0x10, "a", struct.pack("<i", 1)), raw_len=500)))))
print("ZERO_TYPE   = " + h(doc(b"\x00" + cstr("a") + struct.pack("<i", 1))))
print("TRUNC_VALUE = " + h(doc(elem(0x12, "q", b"\x01\x02\x03"))))
print()
print("expected ISO: dt=%s dt_old=%s dt_epoch=%s" % (iso(ms(ts_new)), iso(ms(ts_old)), iso(0)))
print("expected hex: oid=%s bin=0001ff dec=%s" % (oid.hex(), dec.hex()))
print("unicode text bytes: " + unicode_text.encode("utf-8").hex())
print("all_types length %d, dump length %d" % (len(all_types), len(dump)))
