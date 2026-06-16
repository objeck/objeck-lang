// spectralnorm — idiomatic single-threaded Java, matched to the Python/Ruby reference
// Usage: java spectralnorm <n>

public class spectralnorm {
    static double a(int i, int j) {
        return 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1);
    }

    static void av(double[] x, double[] y, int n) {
        for (int i = 0; i < n; i++) {
            double val = 0.0;
            for (int j = 0; j < n; j++) val += a(i, j) * x[j];
            y[i] = val;
        }
    }

    static void atv(double[] x, double[] y, int n) {
        for (int i = 0; i < n; i++) {
            double val = 0.0;
            for (int j = 0; j < n; j++) val += a(j, i) * x[j];
            y[i] = val;
        }
    }

    static void atav(double[] x, double[] y, int n) {
        double[] u = new double[n];
        av(x, u, n);
        atv(u, y, n);
    }

    public static void main(String[] args) {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 100;
        double[] u = new double[n];
        double[] v = new double[n];
        for (int i = 0; i < n; i++) u[i] = 1.0;

        for (int i = 0; i < 10; i++) {
            atav(u, v, n);
            atav(v, u, n);
        }

        double vBv = 0.0, vv = 0.0;
        for (int i = 0; i < n; i++) {
            vBv += u[i] * v[i];
            vv += v[i] * v[i];
        }

        System.out.printf("%.9f%n", Math.sqrt(vBv / vv));
    }
}
