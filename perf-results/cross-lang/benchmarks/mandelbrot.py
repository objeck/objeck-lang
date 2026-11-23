# CLBG mandelbrot, single-threaded, same algorithm as programs/tests/clbg/mandelbrot.obs
# (49 iterations, bit-packed PBM on stdout)
import sys


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    out = sys.stdout.buffer
    out.write(b"P4\n%d %d\n" % (n, n))
    inv = 2.0 / n
    tail = 8 - (n & 7)
    for y in range(n):
        civ = y * inv - 1.0
        row = bytearray()
        acc = 0
        bit = 0
        for x in range(n):
            crv = x * inv - 1.5
            zr = crv
            zi = civ
            tr = crv * crv
            ti = civ * civ
            i = 49
            while True:
                zi = zr * zi + zr * zi + civ
                zr = tr - ti + crv
                tr = zr * zr
                ti = zi * zi
                t = tr + ti
                if t <= 4.0:
                    i -= 1
                if not (t <= 4.0 and i > 0):
                    break
            acc <<= 1
            if i == 0:
                acc += 1
            bit += 1
            if bit == 8:
                row.append(acc)
                acc = 0
                bit = 0
        if bit:
            acc <<= tail
            row.append(acc)
        out.write(row)
    out.flush()


main()
