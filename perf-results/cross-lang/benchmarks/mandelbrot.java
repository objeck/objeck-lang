// CLBG mandelbrot, single-threaded, same algorithm as programs/tests/clbg/mandelbrot.obs
import java.io.BufferedOutputStream;
import java.io.IOException;

public class mandelbrot {
    public static void main(String[] args) throws IOException {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 200;
        BufferedOutputStream out = new BufferedOutputStream(System.out, 65536);
        out.write(("P4\n" + n + " " + n + "\n").getBytes());
        double inv = 2.0 / n;
        int tail = 8 - (n & 7);
        for (int y = 0; y < n; y++) {
            double civ = y * inv - 1.0;
            int acc = 0, bit = 0;
            for (int x = 0; x < n; x++) {
                double crv = x * inv - 1.5;
                double zr = crv, zi = civ, tr = crv * crv, ti = civ * civ, t;
                int i = 49;
                do {
                    zi = zr * zi + zr * zi + civ;
                    zr = tr - ti + crv;
                    tr = zr * zr;
                    ti = zi * zi;
                    t = tr + ti;
                    if (t <= 4.0) i--;
                } while (t <= 4.0 && i > 0);
                acc <<= 1;
                if (i == 0) acc++;
                if (++bit == 8) { out.write(acc); acc = 0; bit = 0; }
            }
            if (bit != 0) { acc <<= tail; out.write(acc); }
        }
        out.flush();
    }
}
