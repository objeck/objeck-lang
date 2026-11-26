// Cross-runtime baseline (Java host). Workload definition: tools/research/baselines/README.md
//   java [gc flags] -cp onnxruntime_gpu-1.24.2.jar:out Baseline <model.onnx> <ep> <batch> <warmup_s> <measure_s>
import ai.onnxruntime.*;
import java.lang.management.*;
import java.nio.*;
import java.util.*;
import java.util.concurrent.atomic.*;
import javax.management.*;
import javax.management.openmbean.CompositeData;
import com.sun.management.GarbageCollectionNotificationInfo;

public class Baseline {
    static volatile int phase = 0; // 0 warm-up, 1 measuring, 2 stop
    static final long PERIOD_NS = 100_000;
    static long seed = 12345;

    static int next() { seed = (seed * 1103515245L + 12345L) % 2147483648L; return (int) seed; }
    static int size() { return 1024 + (next() / 256) % 64513; }

    static double pct(double[] sorted, int n, double p) { return sorted[(int) ((n - 1) * p)]; }

    public static void main(String[] a) throws Exception {
        String model = a[0], ep = a[1];
        int batch = Integer.parseInt(a[2]), warmup = Integer.parseInt(a[3]), measure = Integer.parseInt(a[4]);

        Map<String, long[]> gcMax = new java.util.concurrent.ConcurrentHashMap<>();
        for (GarbageCollectorMXBean b : ManagementFactory.getGarbageCollectorMXBeans()) {
            ((NotificationEmitter) b).addNotificationListener((n, hb) -> {
                if (!n.getType().equals(GarbageCollectionNotificationInfo.GARBAGE_COLLECTION_NOTIFICATION)) return;
                GarbageCollectionNotificationInfo info = GarbageCollectionNotificationInfo.from((CompositeData) n.getUserData());
                long ms = info.getGcInfo().getDuration();
                if (phase != 1) return;
                gcMax.compute(info.getGcName(), (k, v) -> v == null ? new long[]{1, ms, ms} : new long[]{v[0] + 1, v[1] + ms, Math.max(v[2], ms)});
            }, null, null);
        }

        OrtEnvironment env = OrtEnvironment.getEnvironment();
        OrtSession.SessionOptions opts = new OrtSession.SessionOptions();
        if (ep.equals("cuda")) opts.addCUDA(0);
        OrtSession session = env.createSession(model, opts);
        String inName = session.getInputNames().iterator().next();

        int count = batch * 3 * 224 * 224;
        FloatBuffer fb = ByteBuffer.allocateDirect(count * 4).order(ByteOrder.nativeOrder()).asFloatBuffer();
        for (int i = 0; i < count; i++) fb.put(i, (float) ((((long) i * 7919L) % 1000L) / 1000.0));
        OnnxTensor input = OnnxTensor.createTensor(env, fb, new long[]{batch, 3, 224, 224});

        double[] stalls = new double[1_200_000];
        AtomicInteger stallN = new AtomicInteger();
        double[] lat = new double[400_000];
        int[] latN = new int[1];
        float[][] first = new float[1][];

        Thread alloc = new Thread(() -> {
            byte[][] live = new byte[8192][];
            for (int i = 0; i < 8192; i++) live[i] = new byte[size()];
            long prevEnd = System.nanoTime();
            long k = 0;
            int ph = 0;
            while (ph < 2) {
                long target = prevEnd + PERIOD_NS;
                while (System.nanoTime() < target) { /* spin */ }
                int sz = size();
                byte[] data = new byte[sz];
                data[0] = (byte) (k % 251);
                data[sz - 1] = (byte) (k % 251);
                if (k % 16 == 0) live[next() % 8192] = data;
                long end = System.nanoTime();
                if (ph == 1) {
                    int i = stallN.get();
                    if (i < stalls.length) { stalls[i] = (end - target) / 1000.0; stallN.set(i + 1); }
                }
                prevEnd = end;
                k++;
                if ((k & 63) == 0) {
                    int p = phase;
                    if (p != ph) { ph = p; if (p == 1) stallN.set(0); }
                }
            }
        }, "allocator");

        Thread infer = new Thread(() -> {
            try {
                Map<String, OnnxTensor> in = Map.of(inName, input);
                int ph = 0;
                while (ph < 2) {
                    long t0 = System.nanoTime();
                    try (OrtSession.Result r = session.run(in)) {
                        float[][] out = (float[][]) r.get(0).getValue();
                        long t1 = System.nanoTime();
                        if (first[0] == null) {
                            float[] flat = new float[out.length * out[0].length];
                            for (int i = 0; i < out.length; i++) System.arraycopy(out[i], 0, flat, i * out[0].length, out[0].length);
                            first[0] = flat;
                        }
                        ph = phase;
                        if (ph == 1 && latN[0] < lat.length) lat[latN[0]++] = (t1 - t0) / 1e6;
                        if (ph == 0) latN[0] = 0;
                    }
                }
            } catch (OrtException e) { throw new RuntimeException(e); }
        }, "inference");

        long[] gc0 = new long[2];
        alloc.start();
        infer.start();
        Thread.sleep(warmup * 1000L);
        long c0 = 0, ms0 = 0;
        for (GarbageCollectorMXBean b : ManagementFactory.getGarbageCollectorMXBeans()) { c0 += Math.max(b.getCollectionCount(), 0); ms0 += Math.max(b.getCollectionTime(), 0); }
        long w0 = System.nanoTime();
        phase = 1;
        Thread.sleep(measure * 1000L);
        double elapsed = (System.nanoTime() - w0) / 1e9;
        long c1 = 0, ms1 = 0;
        for (GarbageCollectorMXBean b : ManagementFactory.getGarbageCollectorMXBeans()) { c1 += Math.max(b.getCollectionCount(), 0); ms1 += Math.max(b.getCollectionTime(), 0); }
        phase = 2;
        infer.join();
        alloc.join();

        int n = stallN.get(), runs = latN[0];
        double[] st = Arrays.copyOf(stalls, n), it = Arrays.copyOf(lat, runs);
        Arrays.sort(st);
        Arrays.sort(it);
        double sum = 0;
        float[] f = first[0];
        for (float v : f) sum += v;
        int best = 0;
        for (int i = 0; i < 1000; i++) if (f[i] > f[best]) best = i;

        StringBuilder gcj = new StringBuilder();
        gcj.append("{\"events\":").append(c1 - c0).append(",\"mxbean_ms\":").append(ms1 - ms0).append(",\"notif\":{");
        boolean firstE = true;
        for (Map.Entry<String, long[]> e : gcMax.entrySet()) {
            if (!firstE) gcj.append(',');
            firstE = false;
            gcj.append('"').append(e.getKey()).append("\":{\"n\":").append(e.getValue()[0]).append(",\"total_ms\":").append(e.getValue()[1]).append(",\"max_ms\":").append(e.getValue()[2]).append('}');
        }
        gcj.append("}}");
        System.out.printf(Locale.ROOT,
            "RESULT {\"stall_us\":{\"n\":%d,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"p999\":%.3f,\"max\":%.3f},"
            + "\"infer_ms\":{\"n\":%d,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},\"img_s\":%.3f,\"gc\":%s,"
            + "\"checksum\":{\"sum\":%.6f,\"argmax0\":%d,\"v0\":%.6f,\"v1\":%.6f,\"v2\":%.6f}}%n",
            n, st[n / 2], pct(st, n, .95), pct(st, n, .99), pct(st, n, .999), st[n - 1],
            runs, it[runs / 2], pct(it, runs, .95), pct(it, runs, .99), it[runs - 1], runs * (double) batch / elapsed, gcj,
            sum, best, f[0], f[1], f[2]);
        session.close();
        env.close();
        System.exit(0);
    }
}
