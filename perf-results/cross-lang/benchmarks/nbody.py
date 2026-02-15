import sys
from math import sqrt, pi

SOLAR_MASS = 4 * pi * pi
DAYS_PER_YEAR = 365.24

BODIES = {
    'sun': ([0.0, 0.0, 0.0], [0.0, 0.0, 0.0], SOLAR_MASS),
    'jupiter': (
        [4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01],
        [1.66007664274403694e-03 * DAYS_PER_YEAR, 7.69901118419740425e-03 * DAYS_PER_YEAR, -6.90460016972063023e-05 * DAYS_PER_YEAR],
        9.54791938424326609e-04 * SOLAR_MASS),
    'saturn': (
        [8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01],
        [-2.76742510726862411e-03 * DAYS_PER_YEAR, 4.99852801234917238e-03 * DAYS_PER_YEAR, 2.30417297573763929e-05 * DAYS_PER_YEAR],
        2.85885980666130812e-04 * SOLAR_MASS),
    'uranus': (
        [1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01],
        [2.96460137564761618e-03 * DAYS_PER_YEAR, 2.37847173959480950e-03 * DAYS_PER_YEAR, -2.96589568540237556e-05 * DAYS_PER_YEAR],
        4.36624404335156298e-05 * SOLAR_MASS),
    'neptune': (
        [1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01],
        [2.68067772490389322e-03 * DAYS_PER_YEAR, 1.62824170038242295e-03 * DAYS_PER_YEAR, -9.51592254519715870e-05 * DAYS_PER_YEAR],
        5.15138902046611451e-05 * SOLAR_MASS),
}

SYSTEM = list(BODIES.values())
PAIRS = [(SYSTEM[i], SYSTEM[j]) for i in range(len(SYSTEM)) for j in range(i+1, len(SYSTEM))]

def advance(dt, n):
    for _ in range(n):
        for (([x1, y1, z1], v1, m1), ([x2, y2, z2], v2, m2)) in PAIRS:
            dx = x1 - x2; dy = y1 - y2; dz = z1 - z2
            mag = dt * ((dx*dx + dy*dy + dz*dz) ** (-1.5))
            b1 = m2 * mag; b2 = m1 * mag
            v1[0] -= dx * b1; v1[1] -= dy * b1; v1[2] -= dz * b1
            v2[0] += dx * b2; v2[1] += dy * b2; v2[2] += dz * b2
        for (r, v, m) in SYSTEM:
            r[0] += dt * v[0]; r[1] += dt * v[1]; r[2] += dt * v[2]

def report_energy():
    e = 0.0
    for (([x1, y1, z1], v1, m1), ([x2, y2, z2], v2, m2)) in PAIRS:
        dx = x1 - x2; dy = y1 - y2; dz = z1 - z2
        e -= (m1 * m2) / sqrt(dx*dx + dy*dy + dz*dz)
    for (r, v, m) in SYSTEM:
        e += m * (v[0]*v[0] + v[1]*v[1] + v[2]*v[2]) / 2.
    print("%.9f" % e)

def offset_momentum(ref, px=0.0, py=0.0, pz=0.0):
    for (r, v, m) in SYSTEM:
        px -= v[0] * m; py -= v[1] * m; pz -= v[2] * m
    (r, v, m) = ref
    v[0] = px / m; v[1] = py / m; v[2] = pz / m

n = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
offset_momentum(SYSTEM[0])
report_energy()
advance(0.01, n)
report_energy()
