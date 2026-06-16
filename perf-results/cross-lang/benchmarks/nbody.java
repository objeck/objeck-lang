// nbody — idiomatic single-threaded Java, matched to the Python/Ruby reference
// Usage: java nbody <steps>

public class nbody {
    static final double PI = 3.141592653589793;
    static final double SOLAR_MASS = 4 * PI * PI;
    static final double DAYS_PER_YEAR = 365.24;

    static final class Body {
        double x, y, z, vx, vy, vz, mass;
        Body(double x, double y, double z, double vx, double vy, double vz, double mass) {
            this.x = x; this.y = y; this.z = z;
            this.vx = vx; this.vy = vy; this.vz = vz;
            this.mass = mass;
        }
    }

    static Body[] system() {
        Body sun = new Body(0, 0, 0, 0, 0, 0, SOLAR_MASS);
        Body jupiter = new Body(
            4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
            1.66007664274403694e-03 * DAYS_PER_YEAR, 7.69901118419740425e-03 * DAYS_PER_YEAR,
            -6.90460016972063023e-05 * DAYS_PER_YEAR, 9.54791938424326609e-04 * SOLAR_MASS);
        Body saturn = new Body(
            8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
            -2.76742510726862411e-03 * DAYS_PER_YEAR, 4.99852801234917238e-03 * DAYS_PER_YEAR,
            2.30417297573763929e-05 * DAYS_PER_YEAR, 2.85885980666130812e-04 * SOLAR_MASS);
        Body uranus = new Body(
            1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
            2.96460137564761618e-03 * DAYS_PER_YEAR, 2.37847173959480950e-03 * DAYS_PER_YEAR,
            -2.96589568540237556e-05 * DAYS_PER_YEAR, 4.36624404335156298e-05 * SOLAR_MASS);
        Body neptune = new Body(
            1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
            2.68067772490389322e-03 * DAYS_PER_YEAR, 1.62824170038242295e-03 * DAYS_PER_YEAR,
            -9.51592254519715870e-05 * DAYS_PER_YEAR, 5.15138902046611451e-05 * SOLAR_MASS);
        return new Body[] { sun, jupiter, saturn, uranus, neptune };
    }

    static void offsetMomentum(Body[] bodies) {
        double px = 0, py = 0, pz = 0;
        for (Body b : bodies) {
            px += b.vx * b.mass; py += b.vy * b.mass; pz += b.vz * b.mass;
        }
        bodies[0].vx = -px / SOLAR_MASS;
        bodies[0].vy = -py / SOLAR_MASS;
        bodies[0].vz = -pz / SOLAR_MASS;
    }

    static void advance(Body[] bodies, double dt, int n) {
        int len = bodies.length;
        for (int step = 0; step < n; step++) {
            for (int i = 0; i < len; i++) {
                Body bi = bodies[i];
                for (int j = i + 1; j < len; j++) {
                    Body bj = bodies[j];
                    double dx = bi.x - bj.x, dy = bi.y - bj.y, dz = bi.z - bj.z;
                    double d2 = dx * dx + dy * dy + dz * dz;
                    double mag = dt / (d2 * Math.sqrt(d2));
                    double b1 = bj.mass * mag, b2 = bi.mass * mag;
                    bi.vx -= dx * b1; bi.vy -= dy * b1; bi.vz -= dz * b1;
                    bj.vx += dx * b2; bj.vy += dy * b2; bj.vz += dz * b2;
                }
            }
            for (Body b : bodies) {
                b.x += dt * b.vx; b.y += dt * b.vy; b.z += dt * b.vz;
            }
        }
    }

    static double energy(Body[] bodies) {
        double e = 0;
        int len = bodies.length;
        for (int i = 0; i < len; i++) {
            Body bi = bodies[i];
            e += 0.5 * bi.mass * (bi.vx * bi.vx + bi.vy * bi.vy + bi.vz * bi.vz);
            for (int j = i + 1; j < len; j++) {
                Body bj = bodies[j];
                double dx = bi.x - bj.x, dy = bi.y - bj.y, dz = bi.z - bj.z;
                e -= (bi.mass * bj.mass) / Math.sqrt(dx * dx + dy * dy + dz * dz);
            }
        }
        return e;
    }

    public static void main(String[] args) {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 1000;
        Body[] bodies = system();
        offsetMomentum(bodies);
        System.out.printf("%.9f%n", energy(bodies));
        advance(bodies, 0.01, n);
        System.out.printf("%.9f%n", energy(bodies));
    }
}
