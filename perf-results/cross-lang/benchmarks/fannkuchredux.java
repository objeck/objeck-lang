// fannkuchredux — idiomatic single-threaded Java, matched to the Python/Ruby reference
// Usage: java fannkuchredux <n>

public class fannkuchredux {
    static long[] fannkuch(int n) {
        int maxFlips = 0;
        long checksum = 0;
        int[] perm1 = new int[n];
        int[] count = new int[n];
        for (int i = 0; i < n; i++) { perm1[i] = i; count[i] = i + 1; }
        int[] perm = new int[n];

        while (true) {
            if (perm1[0] != 0) {
                System.arraycopy(perm1, 0, perm, 0, n);
                int flips = 0;
                int k = perm[0];
                while (k != 0) {
                    // reverse perm[0..k]
                    for (int lo = 0, hi = k; lo < hi; lo++, hi--) {
                        int t = perm[lo]; perm[lo] = perm[hi]; perm[hi] = t;
                    }
                    flips++;
                    k = perm[0];
                }
                if (flips > maxFlips) maxFlips = flips;
                checksum += ((checksum & 1) == 0) ? flips : -flips;
            }

            // generate next permutation: rotate prefix
            int i = 1;
            for (; i < n; i++) {
                int first = perm1[i];
                for (int k = i; k > 0; k--) perm1[k] = perm1[k - 1];
                perm1[0] = first;
                count[i] -= 1;
                if (count[i] > 0) break;
                count[i] = i + 1;
            }
            if (i == n) break;
        }
        return new long[] { maxFlips, checksum };
    }

    public static void main(String[] args) {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 7;
        long[] r = fannkuch(n);
        System.out.println(r[1] + "\nPfannkuchen(" + n + ") = " + r[0]);
    }
}
