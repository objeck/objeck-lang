"""gdb Python analysis of the #816 Linux arm64 core (probe-816-core run 34790817733).

Map:Find's frame (callee sp + 0x270) holds node = 1 and cmp = 1. Walk the Map's tree
from @root and report any node whose child fields are small integers, and whether
each node lies in the nursery. Run: gdb -batch -x core816.py obr core."""
import gdb

CALLEE_SP = 0xffffec980af0
CALLER_SP = CALLEE_SP + 0x270
LOCALS = CALLER_SP + 0x108
INSTANCE_MEM = CALLER_SP + 0x40

inf = gdb.selected_inferior()


def word(addr):
    return int.from_bytes(bytes(inf.read_memory(addr, 8)), "little")


def symbol_word(mangled):
    try:
        addr = int(gdb.parse_and_eval("(long)&'%s'" % mangled))
        return addr, word(addr)
    except Exception as e:
        return None, "unavailable (%s)" % e


yr_addr, young_region = symbol_word("_ZN13MemoryManager12young_regionE")
yo_addr, young_offset = symbol_word("_ZN13MemoryManager12young_offsetE")
print("young_region @%s = %s" % (hex(yr_addr) if yr_addr else yr_addr,
      hex(young_region) if isinstance(young_region, int) else young_region))
print("young_offset @%s = %s" % (hex(yo_addr) if yo_addr else yo_addr,
      hex(young_offset) if isinstance(young_offset, int) else young_offset))


def region(p):
    if isinstance(young_region, int) and isinstance(young_offset, int):
        if young_region <= p < young_region + young_offset:
            return "young"
        if young_region <= p < young_region + (128 << 20):
            return "young-range-past-offset"
    return "other"


def dump(label, p, n=6):
    try:
        hdr = [word(p - 8 * k) for k in (3, 2, 1)]
        fields = [word(p + 8 * k) for k in range(n)]
        print("%s 0x%x [%s] hdr[-3..-1]=%s fields=%s" % (
            label, p, region(p), " ".join(hex(h) for h in hdr), " ".join(hex(f) for f in fields)))
        return fields
    except Exception as e:
        print("%s 0x%x unreadable: %s" % (label, p, e))
        return None


print("Find locals @0x%x: key=0x%x node=0x%x cmp=0x%x" % (LOCALS, word(LOCALS), word(LOCALS + 8), word(LOCALS + 16)))
self_map = word(INSTANCE_MEM)
print("Find self (Map) = 0x%x" % self_map)
map_fields = dump("Map", self_map, 8)
key = word(LOCALS)
dump("key", key, 4)

if map_fields:
    root = map_fields[0]
    print("root = 0x%x" % root)
    # BFS over the tree: fields 0 key, 1 value, 2 left, 3 right (Find's bytecode), 4.. extra
    seen, queue, bad, count = set(), [root], [], 0
    while queue and count < 200000:
        p = queue.pop(0)
        if p in seen:
            continue
        seen.add(p)
        count += 1
        try:
            f = [word(p + 8 * k) for k in range(5)]
        except Exception as e:
            bad.append("unreadable node 0x%x (%s)" % (p, e))
            continue
        for idx in (2, 3):
            c = f[idx]
            if c == 0:
                continue
            if c < 0x10000:
                bad.append("node 0x%x [%s] field %d = 0x%x; fields=%s" % (p, region(p), idx, c, " ".join(hex(x) for x in f)))
            else:
                queue.append(c)
    print("tree nodes walked: %d" % count)
    young = sum(1 for p in seen if region(p) == "young")
    print("nodes in nursery: %d, elsewhere: %d" % (young, count - young))
    for b in bad[:20]:
        print("SUSPECT " + b)
    if not bad:
        print("no node has a small-integer left/right field")

print("\n=== caller frame words (sp+0x0 .. sp+0x1c0)")
for off in range(0, 0x1c8, 8):
    v = word(CALLER_SP + off)
    tag = ""
    if isinstance(young_region, int) and v and region(v) != "other":
        tag = "  <%s>" % region(v)
    print("  [sp+0x%03x] 0x%016x%s" % (off, v, tag))
