// CLBG fasta, single-threaded, same algorithm as programs/tests/clbg/fasta.obs
import java.io.BufferedOutputStream;
import java.io.IOException;

public class fasta {
    static final int IM = 139968, IA = 3877, IC = 29573;
    static final String ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA";
    static int last = 42;
    static BufferedOutputStream out;

    static double[] cumulative(double[] p) {
        double cp = 0.0;
        double[] o = new double[p.length];
        for (int i = 0; i < p.length; i++) { cp += p[i]; o[i] = cp; }
        return o;
    }

    static void repeat(String id, String desc, int n) throws IOException {
        out.write((">" + id + " " + desc + "\n").getBytes());
        byte[] alu = ALU.getBytes();
        int k = 0, kn = alu.length;
        while (n > 0) {
            int m = n >= 60 ? 60 : n;
            for (int i = 0; i < m; i++) {
                if (k == kn) k = 0;
                out.write(alu[k++]);
            }
            out.write('\n');
            n -= 60;
        }
    }

    static void random(String id, String desc, char[] chars, double[] probs, int n) throws IOException {
        out.write((">" + id + " " + desc + "\n").getBytes());
        int size = chars.length;
        while (n > 0) {
            int m = n >= 60 ? 60 : n;
            for (int j = 0; j < m; j++) {
                last = (last * IA + IC) % IM;
                double r = 1.0 * last / IM;
                char pick = chars[size - 1];
                for (int i = 0; i < size; i++) {
                    if (r < probs[i]) { pick = chars[i]; break; }
                }
                out.write(pick);
            }
            out.write('\n');
            n -= 60;
        }
    }

    public static void main(String[] args) throws IOException {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 1000;
        out = new BufferedOutputStream(System.out, 65536);
        char[] iubc = "acgtBDHKMNRSVWY".toCharArray();
        double[] iubp = new double[15];
        iubp[0] = 0.27; iubp[1] = 0.12; iubp[2] = 0.12; iubp[3] = 0.27;
        for (int i = 4; i < 15; i++) iubp[i] = 0.02;
        char[] homc = "acgt".toCharArray();
        double[] homp = {0.3029549426680, 0.1979883004921, 0.1975473066391, 0.3015094502008};
        repeat("ONE", "Homo sapiens alu", n * 2);
        random("TWO", "IUB ambiguity codes", iubc, cumulative(iubp), n * 3);
        random("THREE", "Homo sapiens frequency", homc, cumulative(homp), n * 5);
        out.flush();
    }
}
