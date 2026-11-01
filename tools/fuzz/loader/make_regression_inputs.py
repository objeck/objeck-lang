#!/usr/bin/env python3
"""Writes the loader's regression inputs (run_vm_flag_tests.py, section 29).

    python3 tools/fuzz/loader/make_regression_inputs.py <program.obe> <out dir>

Each input is the smallest form of something the loader fuzz target found or
that was fixed with it: a slice of a compiled program (obe_image.py) -- two
classes, three methods each, string tables emptied -- with one field changed.
The images keep the version they were cut from; the test stamps its own.
Run it on any small program compiled by this toolchain, for example

    class LoaderControl { function : Main(args : String[]) ~ Nil { "loaded"->PrintLine(); } }
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_corpus  # noqa: E402
import obe_image  # noqa: E402

MAX_INT = 0x7FFFFFFF


def put_int(data, at, value):
    return data[:at] + struct.pack("<i", value) + data[at + 4:]


def offsets(image):
    """Where the fields the cases change sit in a sliced image."""
    classes_at, classes, _ = obe_image.parse(image)
    first = classes[0]
    r = obe_image.Reader(image, first["start"] + 4)
    r.string()  # name
    r.int()  # parent id
    r.string()  # parent name
    interfaces_at = r.pos
    method = next(m for m in first["methods"])
    name_at = method[1] + 4 + 3
    r = obe_image.Reader(image, name_at)
    r.string()
    r.string()
    r.int()
    r.int()
    obe_image.read_dclrs(r, False)
    count_at = r.pos
    return {
        "class_count": classes_at,
        "second_class": classes[1]["start"],
        "interfaces": interfaces_at,
        "method_count": first["methods_at"],
        "second_method": first["methods"][1][1],
        "method_name": name_at,
        "instr_count": count_at,
        "first_opcode": count_at + 4,
    }


def cases(image):
    at = offsets(image)
    name_len = struct.unpack_from("<i", image, at["method_name"])[0]
    bad_name = b"X:y:Q"
    bad_signature = (image[:at["method_name"]] + struct.pack("<i", len(bad_name)) + bad_name +
                     image[at["method_name"] + 4 + name_len:])
    return {
        # every count that sizes an allocation was read with ReadInt: 16 GB new[]
        "class_count_huge.img": put_int(image, at["class_count"], MAX_INT),
        "method_count_huge.img": put_int(image, at["method_count"], MAX_INT),
        "interface_count_huge.img": put_int(image, at["interfaces"], MAX_INT),
        # a repeated id left another slot uninitialized; SetClasses read it
        "class_id_repeated.img": put_int(image, at["second_class"], 0),
        "method_id_repeated.img": put_int(image, at["second_method"], 0),
        # StackMethod's constructor throws; ~StackProgram then freed unset pointers
        "method_signature_bad.img": bad_signature,
        # said nothing in a release build
        "instruction_unknown.img": image[:at["first_opcode"]] + bytes([250]) + image[at["first_opcode"] + 1:],
    }


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    with open(sys.argv[1], "rb") as f:
        image = make_corpus.decode(f.read())
    _, classes, _ = obe_image.parse(image)
    # two classes with at least two methods, the second carrying code
    pick = [i for i, c in enumerate(classes)
            if len(c["methods"]) >= 2 and c["methods"][0][2] - c["methods"][0][1] > 40][:2]
    sliced = obe_image.slice_image(image, pick, 3)
    os.makedirs(sys.argv[2], exist_ok=True)
    for name, data in cases(sliced).items():
        with open(os.path.join(sys.argv[2], name), "wb") as f:
            f.write(data)
        print(f"{name}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
