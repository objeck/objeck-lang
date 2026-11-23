# CLBG fasta, single-threaded, same algorithm as programs/tests/clbg/fasta.obs
# (linear cumulative selection, LCG random, 60-column lines)
import sys

IM = 139968
IA = 3877
IC = 29573
ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"
IUB = [("a", 0.27), ("c", 0.12), ("g", 0.12), ("t", 0.27)] + [(c, 0.02) for c in "BDHKMNRSVWY"]
HOMO = [("a", 0.3029549426680), ("c", 0.1979883004921), ("g", 0.1975473066391), ("t", 0.3015094502008)]
last = 42


def cumulative(table):
    cp = 0.0
    out = []
    for c, p in table:
        cp += p
        out.append((c, cp))
    return out


def repeat_fasta(out, ident, desc, n):
    out.write(">%s %s\n" % (ident, desc))
    k = 0
    kn = len(ALU)
    buf = ALU * (60 // kn + 3)
    while n > 0:
        m = 60 if n >= 60 else n
        out.write(buf[k:k + m] + "\n")
        k = (k + m) % kn
        n -= 60


def random_fasta(out, ident, desc, table, n):
    global last
    out.write(">%s %s\n" % (ident, desc))
    chars = [c for c, _ in table]
    probs = [p for _, p in table]
    size = len(table)
    lines = []
    while n > 0:
        m = 60 if n >= 60 else n
        line = []
        for _ in range(m):
            last = (last * IA + IC) % IM
            r = 1.0 * last / IM
            for i in range(size):
                if r < probs[i]:
                    line.append(chars[i])
                    break
            else:
                line.append(chars[size - 1])
        lines.append("".join(line))
        if len(lines) == 512:
            out.write("\n".join(lines) + "\n")
            lines = []
        n -= 60
    if lines:
        out.write("\n".join(lines) + "\n")


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
    out = sys.stdout
    repeat_fasta(out, "ONE", "Homo sapiens alu", n * 2)
    random_fasta(out, "TWO", "IUB ambiguity codes", cumulative(IUB), n * 3)
    random_fasta(out, "THREE", "Homo sapiens frequency", cumulative(HOMO), n * 5)
    out.flush()


main()
