// Cross-runtime baseline (.NET host). Workload definition: tools/research/baselines/README.md
//   DOTNET_gcServer=0|1 DOTNET_gcConcurrent=0|1 dotnet run -c Release -- <model.onnx> <ep> <batch> <warmup_s> <measure_s>
using System.Diagnostics;
using System.Globalization;
using System.Runtime;
using Microsoft.ML.OnnxRuntime;
using Microsoft.ML.OnnxRuntime.Tensors;

class Baseline
{
    static volatile int phase; // 0 warm-up, 1 measuring, 2 stop
    static long seed = 12345;
    static int Next() { seed = (seed * 1103515245L + 12345L) % 2147483648L; return (int)seed; }
    static int Size() => 1024 + (Next() / 256) % 64513;
    static double Pct(double[] s, int n, double p) => s[(int)((n - 1) * p)];

    static void Main(string[] a)
    {
        string model = a[0], ep = a[1];
        int batch = int.Parse(a[2]), warmup = int.Parse(a[3]), measure = int.Parse(a[4]);
        double tick = 1e6 / Stopwatch.Frequency;  // microseconds per tick
        long periodTicks = (long)(100.0 / tick);

        var so = new SessionOptions();
        if (ep == "cuda") so.AppendExecutionProvider_CUDA(0);
        var session = new InferenceSession(model, so);
        string inName = session.InputMetadata.Keys.First();

        int count = batch * 3 * 224 * 224;
        var values = new float[count];
        for (long i = 0; i < count; i++) values[i] = (float)((i * 7919L % 1000L) / 1000.0);
        var tensor = new DenseTensor<float>(values, new[] { batch, 3, 224, 224 });

        var stalls = new double[1_200_000];
        int stallN = 0;
        var lat = new double[400_000];
        int latN = 0;
        float[]? first = null;

        var alloc = new Thread(() =>
        {
            var live = new byte[8192][];
            for (int i = 0; i < 8192; i++) live[i] = new byte[Size()];
            long prevEnd = Stopwatch.GetTimestamp();
            long k = 0;
            int ph = 0;
            while (ph < 2)
            {
                long target = prevEnd + periodTicks;
                while (Stopwatch.GetTimestamp() < target) { }
                int sz = Size();
                var data = new byte[sz];
                data[0] = (byte)(k % 251);
                data[sz - 1] = (byte)(k % 251);
                if (k % 16 == 0) live[Next() % 8192] = data;
                long end = Stopwatch.GetTimestamp();
                if (ph == 1 && stallN < stalls.Length) stalls[stallN++] = (end - target) * tick;
                prevEnd = end;
                k++;
                if ((k & 63) == 0)
                {
                    int p = phase;
                    if (p != ph) { ph = p; if (p == 1) stallN = 0; }
                }
            }
        }) { Name = "allocator" };

        var infer = new Thread(() =>
        {
            int ph = 0;
            while (ph < 2)
            {
                long t0 = Stopwatch.GetTimestamp();
                var inputs = new List<NamedOnnxValue> { NamedOnnxValue.CreateFromTensor(inName, tensor) };
                using var r = session.Run(inputs);
                var o = r.First().AsEnumerable<float>().ToArray();
                long t1 = Stopwatch.GetTimestamp();
                first ??= o;
                ph = phase;
                if (ph == 1 && latN < lat.Length) lat[latN++] = (t1 - t0) * tick / 1000.0;
                if (ph == 0) latN = 0;
            }
        }) { Name = "inference" };

        alloc.Start();
        infer.Start();
        Thread.Sleep(warmup * 1000);
        int g0 = GC.CollectionCount(0), g1 = GC.CollectionCount(1), g2 = GC.CollectionCount(2);
        var p0 = GC.GetTotalPauseDuration();
        long w0 = Stopwatch.GetTimestamp();
        phase = 1;
        Thread.Sleep(measure * 1000);
        double elapsed = (Stopwatch.GetTimestamp() - w0) / (double)Stopwatch.Frequency;
        int h0 = GC.CollectionCount(0) - g0, h1 = GC.CollectionCount(1) - g1, h2 = GC.CollectionCount(2) - g2;
        var pause = GC.GetTotalPauseDuration() - p0;
        phase = 2;
        infer.Join();
        alloc.Join();

        int n = stallN, runs = latN;
        var st = stalls.Take(n).OrderBy(x => x).ToArray();
        var it = lat.Take(runs).OrderBy(x => x).ToArray();
        double sum = 0;
        foreach (var v in first!) sum += v;
        int best = 0;
        for (int i = 0; i < 1000; i++) if (first![i] > first[best]) best = i;

        var c = CultureInfo.InvariantCulture;
        Console.WriteLine(string.Format(c,
            "RESULT {{\"stall_us\":{{\"n\":{0},\"p50\":{1:F3},\"p95\":{2:F3},\"p99\":{3:F3},\"p999\":{4:F3},\"max\":{5:F3}}},"
            + "\"infer_ms\":{{\"n\":{6},\"p50\":{7:F3},\"p95\":{8:F3},\"p99\":{9:F3},\"max\":{10:F3}}},\"img_s\":{11:F3},"
            + "\"gc\":{{\"gen0\":{12},\"gen1\":{13},\"gen2\":{14},\"pause_total_ms\":{15:F3},\"server\":{16},\"concurrent\":\"{17}\"}},"
            + "\"checksum\":{{\"sum\":{18:F6},\"argmax0\":{19},\"v0\":{20:F6},\"v1\":{21:F6},\"v2\":{22:F6}}}}}",
            n, st[n / 2], Pct(st, n, .95), Pct(st, n, .99), Pct(st, n, .999), st[n - 1],
            runs, it[runs / 2], Pct(it, runs, .95), Pct(it, runs, .99), it[runs - 1], runs * (double)batch / elapsed,
            h0, h1, h2, pause.TotalMilliseconds, GCSettings.IsServerGC.ToString().ToLower(),
            AppContext.GetData("System.GC.Concurrent")?.ToString()?.ToLower() ?? "default",
            sum, best, first![0], first[1], first[2]));
        Environment.Exit(0);
    }
}
