// Cross-runtime baseline (C++ control, no garbage collector). Workload: tools/research/baselines/README.md
//   g++ -O2 -std=c++17 -I<ort>/include baseline.cpp -L<ort>/lib -lonnxruntime -lpthread -o baseline
//   ./baseline <model.onnx> <ep> <batch> <warmup_s> <measure_s>
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using clk = std::chrono::steady_clock;
static std::atomic<int> phase{0};  // 0 warm-up, 1 measuring, 2 stop
static int64_t seed = 12345;
static int64_t next_rand() { seed = (seed * 1103515245LL + 12345LL) % 2147483648LL; return seed; }
static int size_rand() { return 1024 + (int)((next_rand() / 256) % 64513); }
static int64_t nanos() { return std::chrono::duration_cast<std::chrono::nanoseconds>(clk::now().time_since_epoch()).count(); }
static double pct(const std::vector<double>& s, double p) { return s[(size_t)((s.size() - 1) * p)]; }

int main(int argc, char** argv) {
  std::string model = argv[1], ep = argv[2];
  int batch = atoi(argv[3]), warmup = atoi(argv[4]), measure = atoi(argv[5]);

  Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "baseline");
  Ort::SessionOptions so;
  if (ep == "cuda") {
    Ort::CUDAProviderOptions co;
    so.AppendExecutionProvider_CUDA_V2(*co);
  }
  Ort::Session session(env, model.c_str(), so);
  Ort::AllocatorWithDefaultOptions alloc_default;
  auto in_name = session.GetInputNameAllocated(0, alloc_default);
  auto out_name = session.GetOutputNameAllocated(0, alloc_default);

  size_t count = (size_t)batch * 3 * 224 * 224;
  std::vector<float> in(count);
  for (size_t i = 0; i < count; i++) in[i] = (float)((double)((int64_t)i * 7919 % 1000) / 1000.0);
  std::vector<int64_t> shape{batch, 3, 224, 224};
  auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::Value input = Ort::Value::CreateTensor<float>(mem, in.data(), count, shape.data(), shape.size());
  const char* in_names[] = {in_name.get()};
  const char* out_names[] = {out_name.get()};

  std::vector<double> stalls, lat;
  stalls.reserve(1200000);
  lat.reserve(400000);
  std::vector<float> first;

  std::thread allocator([&] {
    std::vector<std::unique_ptr<unsigned char[]>> live(8192);
    for (auto& b : live) b.reset(new unsigned char[size_rand()]());
    int64_t prev_end = nanos();
    int64_t k = 0;
    int ph = 0;
    while (ph < 2) {
      int64_t target = prev_end + 100000;
      while (nanos() < target) {}
      int sz = size_rand();
      std::unique_ptr<unsigned char[]> data(new unsigned char[sz]());
      data[0] = (unsigned char)(k % 251);
      data[sz - 1] = (unsigned char)(k % 251);
      if (k % 16 == 0) live[next_rand() % 8192] = std::move(data);
      int64_t end = nanos();  // 'data' (if not stored) is freed after this point, so free() is outside the sample
      if (ph == 1 && stalls.size() < stalls.capacity()) stalls.push_back((end - target) / 1000.0);
      prev_end = end;
      k++;
      if ((k & 63) == 0) {
        int p = phase.load();
        if (p != ph) { ph = p; if (p == 1) stalls.clear(); }
      }
    }
  });

  std::thread infer([&] {
    int ph = 0;
    while (ph < 2) {
      int64_t t0 = nanos();
      auto out = session.Run(Ort::RunOptions{nullptr}, in_names, &input, 1, out_names, 1);
      int64_t t1 = nanos();
      if (first.empty()) {
        float* p = out[0].GetTensorMutableData<float>();
        size_t n = out[0].GetTensorTypeAndShapeInfo().GetElementCount();
        first.assign(p, p + n);
      }
      ph = phase.load();
      if (ph == 1 && lat.size() < lat.capacity()) lat.push_back((t1 - t0) / 1e6);
      if (ph == 0) lat.clear();
    }
  });

  std::this_thread::sleep_for(std::chrono::seconds(warmup));
  int64_t w0 = nanos();
  phase = 1;
  std::this_thread::sleep_for(std::chrono::seconds(measure));
  double elapsed = (nanos() - w0) / 1e9;
  phase = 2;
  infer.join();
  allocator.join();

  std::sort(stalls.begin(), stalls.end());
  std::sort(lat.begin(), lat.end());
  double sum = 0;
  for (float v : first) sum += v;
  int best = 0;
  for (int i = 0; i < 1000; i++) if (first[i] > first[best]) best = i;
  printf("RESULT {\"stall_us\":{\"n\":%zu,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"p999\":%.3f,\"max\":%.3f},"
         "\"infer_ms\":{\"n\":%zu,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f,\"max\":%.3f},\"img_s\":%.3f,"
         "\"gc\":{\"note\":\"no collector\"},"
         "\"checksum\":{\"sum\":%.6f,\"argmax0\":%d,\"v0\":%.6f,\"v1\":%.6f,\"v2\":%.6f}}\n",
         stalls.size(), stalls[stalls.size() / 2], pct(stalls, .95), pct(stalls, .99), pct(stalls, .999), stalls.back(),
         lat.size(), lat[lat.size() / 2], pct(lat, .95), pct(lat, .99), lat.back(), lat.size() * (double)batch / elapsed,
         sum, best, first[0], first[1], first[2]);
  fflush(stdout);
  std::_Exit(0);
}
