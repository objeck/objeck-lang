#!/usr/bin/env python3
"""
gen_json.py <out_dir>: the JSON documents json_bench reads. Deterministic (fixed seed), ASCII output with
escapes where the text has any, so every parser reads the same bytes.

  small.json    one MCP tools/call request (about 250 bytes)
  medium.json   a tools/list answer with 120 tools and their schemas (about 60 KB)
  large.json    20 000 records of mixed fields (about 4 MB)
  deep.json     arrays and objects nested 60 deep (the parser's limit is 64), 2 000 times in an array
  strings.json  5 000 long strings, half of them with escapes (quotes, newlines, \\u escapes)
  numbers.json  200 000 numbers: integers, fractions, exponents
"""
import json, os, random, sys


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(20261104)
    words = ("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau "
             "upsilon phi chi psi omega").split()

    def sentence(n):
        return " ".join(rnd.choice(words) for _ in range(n))

    def write(name, doc, **kw):
        with open(os.path.join(out, name), "w", encoding="ascii", newline="\n") as f:
            f.write(json.dumps(doc, ensure_ascii=True, separators=(",", ":"), **kw))

    write("small.json", {"jsonrpc": "2.0", "id": 17, "method": "tools/call",
                         "params": {"name": "search_orders", "arguments": {"customer": "ACME-0042", "limit": 25,
                                                                           "open_only": True, "since": "2026-11-01"},
                                    "_meta": {"progressToken": "p-17"}}})

    tools = []
    for i in range(120):
        props = {}
        for j in range(rnd.randint(2, 7)):
            props["%s_%d" % (rnd.choice(words), j)] = {"type": rnd.choice(["string", "integer", "number", "boolean"]),
                                                       "description": sentence(rnd.randint(4, 12))}
        tools.append({"name": "tool_%03d" % i, "title": sentence(3), "description": sentence(rnd.randint(8, 30)),
                      "inputSchema": {"type": "object", "properties": props, "required": list(props)[:2]},
                      "annotations": {"readOnlyHint": bool(i % 2), "destructiveHint": False}})
    write("medium.json", {"jsonrpc": "2.0", "id": 2, "result": {"tools": tools}})

    records = []
    for i in range(20000):
        records.append({"id": i, "guid": "%032x" % rnd.getrandbits(128), "active": bool(rnd.getrandbits(1)),
                        "balance": round(rnd.uniform(-5000, 5000), 2), "age": rnd.randint(18, 90),
                        "name": sentence(2).title(), "tags": [rnd.choice(words) for _ in range(rnd.randint(0, 6))],
                        "address": {"street": "%d %s St" % (rnd.randint(1, 9999), rnd.choice(words).title()),
                                    "zip": "%05d" % rnd.randint(0, 99999), "geo": [round(rnd.uniform(-90, 90), 5),
                                                                                  round(rnd.uniform(-180, 180), 5)]},
                        "note": None if i % 7 == 0 else sentence(rnd.randint(3, 15))})
    write("large.json", records)

    def nest(depth):
        doc = {"leaf": depth, "v": [1, 2, 3]}
        for d in range(depth):
            doc = {"k": doc} if d % 2 else [doc]
        return doc
    write("deep.json", [nest(58) for _ in range(2000)])

    strings = []
    for i in range(5000):
        s = sentence(rnd.randint(20, 80))
        if i % 2:
            s = s.replace(" a", ' "a\\').replace(" t", "\n\tt").replace(" p", " ép").replace(" o", " \U0001F600o")
        strings.append(s)
    write("strings.json", strings)

    numbers = []
    for i in range(200000):
        k = i % 4
        if k == 0:
            numbers.append(rnd.randint(-10**9, 10**9))
        elif k == 1:
            numbers.append(round(rnd.uniform(-1e6, 1e6), 4))
        elif k == 2:
            numbers.append(rnd.randint(0, 100))
        else:
            numbers.append(rnd.uniform(-1, 1) * 10 ** rnd.randint(-20, 20))
    write("numbers.json", numbers)

    edge_cases(os.path.join(out, "edge"))

    for name in sorted(os.listdir(out)):
        path = os.path.join(out, name)
        if os.path.isfile(path):
            print("%-14s %9d bytes" % (name, os.path.getsize(path)))


def edge_cases(out):
    """Small documents for the equivalence check (json_dump before and after a change): every escape, empty
    containers and strings, keys that need escaping, number forms, the scanner's extensions (comments, bare
    words), and text the parser refuses."""
    os.makedirs(out, exist_ok=True)
    bs = chr(92)
    docs = [
        '{}', '[]', '""', '0', 'true', 'false', 'null', '[[]]', '[{}]', '{"a":{}}', '{"a":[]}', '[[],[],{}]',
        '{"":""}', '{"":{"":[""]}}', '[""]', '["a"]', '["' + "x" * 31 + '"]', '["' + "x" * 32 + '"]', '["' + "x" * 33 + '"]',
        '["' + "y" * 5000 + '"]',
        '["a' + bs + 'nb"]', '["a' + bs + '"b"]', '["a' + bs + bs + 'b"]', '["a' + bs + '/b"]', '["' + bs + 'b' + bs + 'f' + bs + 'r' + bs + 't"]',
        '["' + bs + 'u0041' + bs + 'u00e9' + bs + 'u20ac"]', '["' + bs + 'ud83d' + bs + 'ude00"]', '["' + bs + 'ud83d"]', '["' + bs + 'ude00x"]',
        '["' + bs + 'u0000"]', '["' + bs + 'u0001' + bs + 'u001f' + bs + 'u007f' + bs + 'u0080"]', '["a/b"]', '["tab' + bs + 'there"]',
        '{"k' + bs + '"q":1,"k' + bs + 'n":2,"k' + bs + bs + '":3,"' + bs + 'u00e9":4,"plain":5}',
        '{"b":1,"a":2,"c":3,"B":4,"aa":5,"":6,"a b":7,"10":8,"9":9}',
        '{"dup":1,"dup":2}', '{"a":1,"b":{"a":1,"b":{"a":[1,2,{"z":null}]}}}',
        '[0,-0,1,-1,10,1.5,-1.5,1e3,1E3,1e+3,1e-3,1.25e-10,123456789012345678,0.000001,1.0,100]',
        '[true,false,null,"true","null"]', ' [ 1 , 2 ,' + chr(10) + chr(9) + '3 ] ', '[1,2,3,]', '{"a":1,}', '[1 2 3]', '{"a" 1}',
        '// a comment' + chr(10) + '[1,2]', '[1, // one' + chr(10) + ' 2]', '[abc]', '{abc:1}', '[tru]', '[nul]', '[True]', '[a_b]',
        '[', ']', '{', '}', '[1', '{"a"', '{"a":', '{"a":1', '["a', '["a' + bs + '"]', '["a' + bs + 'x"]', '["' + bs + 'u12"]', '["' + bs + 'uzzzz"]',
        '[01]', '[1.]', '[.5]', '[-]', '[1e]', '[1e+]', '[1-2]', '[1.2.3]', '[+1]', '[0x10]', '', ' ', 'nope', '[1]]', '{}{}', '[1],[2]',
        '"top level string"', '"esc' + bs + 'n"', '-12.5e2', '[' * 64 + ']' * 64, '[' * 65 + ']' * 65, '[' * 70 + ']' * 70,
        '{"a":' * 40 + '1' + '}' * 40, '["a",{"b":["c",{"d":["e"]}]}]', '[1,"two",3.0,true,null,{"six":6},[7]]',
        '["' + chr(9) + '"]', '["line' + chr(10) + 'break"]', '[' + chr(12) + '1' + chr(11) + ']', '{"a":"x","b":"' + "z" * 100 + '","c":"' + bs + 't"}',
    ]
    for i, doc in enumerate(docs):
        with open(os.path.join(out, "edge_%03d.json" % i), "w", encoding="ascii", newline="") as f:
            f.write(doc)
    # text that is not ASCII in the file itself: raw UTF-8, a raw control character
    with open(os.path.join(out, "edge_utf8.json"), "w", encoding="utf-8", newline="") as f:
        f.write('{"caf' + chr(0xe9) + ' r' + chr(0xe9) + 'sum' + chr(0xe9) + '":"' + chr(0xfc) + 'ber ' + chr(0x20ac) + ' ' + chr(0x1F600) + '","k":"' + chr(0x4e2d) + chr(0x6587) + '"}')


if __name__ == "__main__":
    sys.exit(main())
