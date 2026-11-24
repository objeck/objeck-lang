## ONNX on the GPU

`API.Onnx` runs a model on the GPU when the ONNX Runtime beside `libobjk_onnx` carries a GPU provider (see [Choosing the provider](../../core/lib/onnx/README.md#choosing-the-provider)). This section is about making that fast and about what the GPU path can do that the CPU path cannot: keep tensors on the device between runs (**IO binding**), run half-precision and quantized models, use the TensorRT provider and its engine cache, and share one session between threads.

Everything here is **measured, not estimated**. The numbers come from the programs in [`programs/tests/onnx_gpu/`](../../programs/tests/onnx_gpu/README.md), which you can run yourself:

| | |
|---|---|
| GPU | NVIDIA GeForce RTX 4090, 24 GB; Windows driver 621.24 (WSL2 kernel-mode 621.24, user-mode 620.73) |
| Host | Windows 11 with WSL2 Ubuntu 24.04, Ryzen 9 7950X3D; the numbers below are Linux (WSL2) |
| ONNX Runtime | `Microsoft.ML.OnnxRuntime.Gpu` 1.24.2 (NuGet, linux-x64) |
| CUDA / cuDNN / TensorRT | CUDA runtime 12.6.77, cuBLAS 12.6.4.1, cuDNN 9.25.1, TensorRT 10.14.1 |
| Method | 10 warm-up runs, then 40 or more timed runs; median and 95th percentile in milliseconds; the box was shared with other work, so treat differences under about 10% as noise |

### What you need

CUDA 12, cuDNN 9 and the NVIDIA driver are yours to supply; no Objeck release ships them. Build `libobjk_onnx` against a GPU ONNX Runtime and run with that runtime first on the library path (`ORT_ROOT=<dir with libonnxruntime.so> ./build.sh cpu`, then `LD_LIBRARY_PATH=<that dir>`, see the README). A session asks the runtime what it carries and **refuses** a provider that is absent; nothing is silently moved to the CPU.

```objeck
use API.Onnx, Collection;

config := Map->New()<String, String>;
config->Insert("ep", "cuda");
session := Session->New("resnet34.onnx", config);
if(<>session->IsOpen()) {
  "no session, the reason was printed above"->PrintLine();
  return;
};
```

### IO binding: tensors that stay on the device

A normal `Session->Run` converts and copies every input from host memory to the device, copies every output back, and allocates on both sides, on every call. For a model that is fed by the GPU (a video pipeline), consumed by the GPU (a decoder loop), or simply benchmarked on its own, that traffic is most of the time. IO binding removes it:

- a **`DeviceTensor`** is a tensor in the provider's memory: allocated once, filled with `Upload`, read with `Download` only when the program wants the numbers;
- a **`BoundSession`** points the model's inputs and outputs, by name, at device tensors; `Run()` then reads and writes them in place.

```objeck
use API.Onnx, Collection;

config := Map->New()<String, String>;
config->Insert("ep", "cuda");
session := Session->New("resnet34.onnx", config);

# the model's input is FP32[-1,3,224,224]: the batch is yours to choose
input := DeviceTensor->ForInput(session, "data", [8, 3, 224, 224]);
scores := DeviceTensor->ForOutput(session, "resnetv23_dense0_fwd", [8, 1000]);

bound := BoundSession->New(session);
bound->BindInput("data", input);
bound->BindOutput("resnetv23_dense0_fwd", scores);

host := Float->New[8 * 1000];           # one buffer, reused: no allocation per iteration
for(i := 0; i < frames->Size(); i += 1;) {
  input->Upload(frames[i]);             # skip this when the input is already resident
  bound->Run();
  scores->Download(host);               # skip this when the output stays on the device
};

bound->Close();
input->Close();
scores->Close();
session->Close();
```

`ForInput` and `ForOutput` take the element type from the model; `DeviceTensor->New(session, "FP16", shape)` names it. An output whose shape you do not know in advance is bound with `BindOutputToDevice(name)`: ONNX Runtime allocates it on the device each run and `GetOutput(name)` hands it back as a `DeviceTensor` (close it when done). Uploading new data into the same tensor and running again is the intended loop; the binding is reused.

**What it guarantees.** A bound `Run` on a provider gives *exactly* the answer `Session->Run` gives on the same provider and input: the regression test `onnx_iobinding` asserts bit-for-bit equality, on `ep=cpu` everywhere and on `ep=cuda` where a GPU opens (`onnx_iobinding_gpu`; it prints `SKIP:` and does not run on a machine without one). Against the CPU the GPU result is within float tolerance (the test checks 1e-3 on the tiny classifier, where the difference is 1.7e-4; ResNet-34 logits differ from the CPU's by up to 1e-2 with the top-1 class unchanged, because the CUDA provider convolves in TF32 on Ampere and newer cards; the provider option `use_tf32` turns that off).

**Where the memory is.** `ep=cuda` and `ep=tensorrt` allocate on the GPU; `ep=cpu` allocates ordinary host memory, so the same program runs without a GPU (it just has nothing to save). Other providers are refused by name. The GPU copies use the CUDA runtime library the CUDA provider already loaded; nothing about CUDA is needed to *build* the library.

**Lifetime.** Device memory is the provider's, not the garbage collector's: `Close()` every `DeviceTensor` and `BoundSession`. A session closed while tensors or a binding still use it is released when the last of them is closed, never before, so a tensor is never freed into a session that is gone. A `BoundSession` belongs to one thread at a time.

**Measured: ResNet-34, batch 1/8/32, `ep=cuda`.** `run` is `Session->Run(Vector<Tensor>)`; `run_raw` skips splitting the outputs into Tensors; `bound_copy` does the same host traffic as `run` (upload every input, download every output) through the binding; `bound_resident` keeps the data on the device and only runs; `bound_dynout` lets ONNX Runtime allocate the outputs.

| model | batch | provider | mode | median ms | p95 ms | images/s |
|---|---:|---|---|---:|---:|---:|
| resnet34 | 1 | cuda | run | 4.94 | 7.26 | 202 |
| resnet34 | 1 | cuda | run_raw | 4.89 | 6.25 | 205 |
| resnet34 | 1 | cuda | bound_copy | 3.15 | 5.21 | 317 |
| resnet34 | 1 | cuda | bound_resident | 2.78 | 4.66 | 359 |
| resnet34 | 1 | cuda | bound_dynout | 2.87 | 4.78 | 348 |
| resnet34 | 8 | cuda | run | 14.74 | 18.23 | 543 |
| resnet34 | 8 | cuda | run_raw | 14.13 | 20.22 | 566 |
| resnet34 | 8 | cuda | bound_copy | 4.22 | 6.47 | 1,894 |
| resnet34 | 8 | cuda | bound_resident | 3.03 | 6.43 | 2,637 |
| resnet34 | 8 | cuda | bound_dynout | 2.87 | 4.80 | 2,789 |
| resnet34 | 32 | cuda | run | 48.92 | 53.78 | 654 |
| resnet34 | 32 | cuda | run_raw | 46.92 | 50.37 | 682 |
| resnet34 | 32 | cuda | bound_copy | 10.14 | 10.96 | 3,155 |
| resnet34 | 32 | cuda | bound_resident | 6.15 | 6.99 | 5,205 |
| resnet34 | 32 | cuda | bound_dynout | 6.25 | 7.14 | 5,117 |

At batch 32 the normal path spends about 43 of its 49 ms outside the model: a 32 x 3 x 224 x 224 input is 4.8 million values that are widened to `Float`, narrowed to the model's type and copied, and 32,000 logits that come back the same way. Binding the same traffic (`bound_copy`) is already 4.8x faster; keeping the data resident is 8x. The run-to-run difference between `bound_resident` and `bound_dynout` is within noise. The `CHECK` line of the benchmark confirms `run` and `bound_copy` outputs differ by exactly 0.

**GPU memory does not grow.** 10,000 bound runs of each of `bound_resident`, `bound_dynout` and `bound_copy` (30,000 runs in one process, ResNet-34 batch 1) were watched with `nvidia-smi` every 2 seconds: the process settled at about 3.1 GiB after the first run and stayed within a 60 MiB band (3,077 to 3,136 MiB) to the end, and the card returned to its starting 2,245 MiB when the process exited.

**Rebuilding the numbers.** `programs/tests/onnx_gpu/run_bench.sh` runs the whole matrix and prints the table rows; see its README.
