import sys
from math import sqrt

def A(i, j):
    return 1.0 / ((i + j) * (i + j + 1) // 2 + i + 1)

def Av(x, y, n):
    for i in range(n):
        y[i] = sum(A(i, j) * x[j] for j in range(n))

def Atv(x, y, n):
    for i in range(n):
        y[i] = sum(A(j, i) * x[j] for j in range(n))

def AtAv(x, y, n):
    u = [0.0] * n
    Av(x, u, n)
    Atv(u, y, n)

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    u = [1.0] * n
    v = [0.0] * n
    for _ in range(10):
        AtAv(u, v, n)
        AtAv(v, u, n)
    vBv = vv = 0.0
    for ue, ve in zip(u, v):
        vBv += ue * ve
        vv += ve * ve
    print("%.9f" % sqrt(vBv / vv))

main()
