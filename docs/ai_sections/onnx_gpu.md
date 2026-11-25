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

### Half precision and 8-bit models

**You never have to say what type a model wants.** `Session->Run` reads the model's own declaration and converts: an FP16 input is rounded from the `Float` you gave, an FP16 output comes back as `Float`, and the same for BF16, INT8, UINT8, INT32 and the rest. The conversions are exact where the type allows and *correctly rounded* where it does not: to nearest, ties to even, taken straight from the `Float` (rounding through FP32 first would round twice), with overflow to the infinity of the right sign, subnormals, signed zeros and NaNs preserved. Integer inputs truncate toward zero and **saturate** at the type's range (a 300 sent to an INT8 input is 127, not an undefined cast).

For a program that wants the bit patterns themselves there are three small classes:

```objeck
use API.Onnx, Collection;

Half->ToBits(1.0)->PrintLine();                # 15360  (0x3C00)
Half->Round(0.1)->PrintLine();                 # 0.0999755859375, the nearest binary16
Half->FromBits(31743)->PrintLine();            # 65504, the largest finite half
BFloat16->ToBits(3.0)->PrintLine();            # 16448  (0x4040)

packed := Half->Pack(pixels);                  # Float[] -> Int[] of 16-bit patterns, once
input->UploadBits(packed);                     # raw patterns to a device tensor: no per-element conversion
output->DownloadBits(patterns);                # and back
values := Half->Unpack(patterns);              # Int[] -> Float[]

# a model with an INT8 input: scale and zero point are the ones it was quantized with
quantized := Quantization->ToInt8(features, 0.0078125, 0);      # Float[] of whole numbers, saturated
```

`Half` and `BFloat16` have `ToBits`, `FromBits`, `Round`, `Pack` and `Unpack`; `Quantization` has `ToInt8`, `ToUInt8` and `Dequantize`. `DeviceTensor->UploadBits(Int[])` and `DownloadBits(Int[])` move raw element patterns (the low 8, 16, 32 or 64 bits of each `Int`, by the tensor's element size) for any element type.

**What is asserted.** `programs/regression/onnx_float16.obs` needs no GPU and no model file: it checks a table of known patterns, the round trip of **all 65,536** half and bfloat16 patterns (every non-NaN pattern must come back unchanged, every NaN must stay a NaN), and **every interval between neighbouring finite values** (the midpoint must go to the neighbour with the even pattern, a hair above to the upper one, a hair below to the lower one: that pins the rounding in the normal range, the subnormal range and at the overflow edge, for 31,744 half and 32,640 bfloat16 intervals). It then writes a 407-byte model (Cast, Mul, Cast; FP16, BF16, INT8 and UINT8 in and out, listed as hex in the test so no model file is tracked) and checks what the conversion did to real numbers, through `Session->Run` and through a bound run with raw patterns, on `ep=cpu` and, where a GPU session opens, `ep=cuda`: `0.1` fed to an FP16 input is `0.0999755859375` before the model sees it, so the answer is exactly `0.199951171875`; 65520 overflows to infinity; 1e38 stays finite in BF16; 300 and -300 saturate to 127 and -128. (Before this change the FP32-to-FP16 helper rounded ties up instead of to even and went through FP32 for a `Float` input; the integer stores were an unchecked C++ cast.)

#### Making an FP16 or INT8 model

Both are produced offline, from the FP32 model, with Python; `programs/tests/onnx_gpu/make_models.py` does it for an image model:

```sh
pip install onnx onnxruntime onnxconverter-common numpy sympy
python programs/tests/onnx_gpu/make_models.py resnet34.onnx out_dir
```

- **FP16** is `onnxconverter_common.float16.convert_float_to_float16(model, keep_io_types=False)`: weights and activations in half precision, FP16 inputs and outputs. The script also writes `_fp16_io32` (a `Cast` at each edge, so the model takes and returns FP32 and replaces the original without changing a caller). Half the weight memory, and on a GPU with tensor cores most of the speed-up. A model that overflows half range (activations beyond 65504) needs `op_block_list` for the offending operators or BF16 instead.
- **INT8** is `onnxruntime.quantization.quantize_static` in **QDQ** format (`QuantizeLinear`/`DequantizeLinear` pairs around `Conv` and `MatMul`) with per-channel weights; a model older than opset 13 is upgraded first (the script does this: the ONNX model zoo's ResNet is opset 7). Calibrate on a few hundred *representative* inputs. `make_models.py` calibrates on synthetic data so it runs anywhere: **that is enough for speed numbers and not for accuracy**, and `compare_models.obs` shows what it costs. QDQ is the format TensorRT turns into int8 kernels; a dynamically quantized model (`QLinearConv`, `MatMulInteger`) runs only on the CPU provider.

#### Measured: ResNet-34, FP32 against FP16 against INT8, `ep=cuda`

Same card and runtime as above, 10 warm-up and 40 timed runs, `ep=cuda`, the CUDA provider with its default options (TF32 convolutions on). `run` is `Session->Run`; `bound_copy` the binding with the same host traffic; `bound_resident` the data left on the device. FP16 is `resnet34_fp16` (FP16 in and out); the host traffic for it is the cost of rounding every element to half, which `bound_bits` (pre-packed patterns, `UploadBits`) cuts.

| model | batch | provider | mode | median ms | p95 ms | images/s |
|---|---:|---|---|---:|---:|---:|
| resnet34 FP32 | 1 | cuda | run | 3.75 | 5.39 | 267 |
| resnet34 FP32 | 1 | cuda | bound_resident | 2.16 | 3.79 | 464 |
| resnet34 FP16 | 1 | cuda | run | 4.50 | 5.61 | 222 |
| resnet34 FP16 | 1 | cuda | bound_resident | 2.90 | 5.23 | 345 |
| resnet34 FP16 (FP32 in/out) | 1 | cuda | bound_resident | 3.54 | 5.08 | 282 |
| resnet34 INT8 QDQ | 1 | cuda | bound_resident | 9.13 | 16.95 | 110 |
| resnet34 FP32 | 8 | cuda | run | 13.56 | 15.67 | 590 |
| resnet34 FP32 | 8 | cuda | bound_resident | 2.60 | 3.92 | 3,075 |
| resnet34 FP16 | 8 | cuda | run | 13.29 | 14.77 | 602 |
| resnet34 FP16 | 8 | cuda | bound_resident | 2.74 | 4.65 | 2,916 |
| resnet34 FP16 (FP32 in/out) | 8 | cuda | bound_resident | 3.42 | 6.36 | 2,341 |
| resnet34 INT8 QDQ | 8 | cuda | bound_resident | 8.65 | 15.45 | 925 |
| resnet34 FP32 | 32 | cuda | run | 44.64 | 45.50 | 717 |
| resnet34 FP32 | 32 | cuda | bound_copy | 9.15 | 9.73 | 3,496 |
| resnet34 FP32 | 32 | cuda | bound_resident | 5.81 | 6.36 | 5,505 |
| resnet34 FP16 | 32 | cuda | run | 45.19 | 48.27 | 708 |
| resnet34 FP16 | 32 | cuda | bound_copy | 10.95 | 11.37 | 2,923 |
| resnet34 FP16 | 32 | cuda | bound_bits | 10.37 | 10.88 | 3,085 |
| resnet34 FP16 | 32 | cuda | bound_resident | 4.05 | 4.50 | 7,895 |
| resnet34 FP16 (FP32 in/out) | 32 | cuda | run | 42.56 | 45.42 | 752 |
| resnet34 FP16 (FP32 in/out) | 32 | cuda | bound_copy | 7.27 | 7.80 | 4,402 |
| resnet34 FP16 (FP32 in/out) | 32 | cuda | bound_resident | 4.17 | 4.81 | 7,682 |
| resnet34 INT8 QDQ | 32 | cuda | bound_resident | 10.07 | 15.84 | 3,176 |

What this says, honestly:

- **FP16 helps where the GPU is the bottleneck and not before.** Batch 32 with the data resident: 5.81 ms (FP32) to 4.05 ms (FP16), a 1.4x speed-up and 7,895 images/s. At batch 1 and 8 the model is too small for half precision to show (the kernels are launch-bound) and FP16 is no faster, within noise. Through plain `Session->Run` it is not faster at any batch, because the run is dominated by converting millions of `Float`s on the host, which FP16 makes slightly *more* expensive (rounding each one to half). The place to use an FP16 model is a binding, with the data resident or pre-packed (`bound_bits`), or the FP32-in/out wrapper (`_fp16_io32`), whose host traffic is plain FP32: 7.27 ms with copies at batch 32 against 10.95 ms for the FP16-IO model.
- **INT8 QDQ is slower than FP32 on the CUDA provider** (9 to 10 ms against 2 to 6 ms). The CUDA provider executes each `QuantizeLinear` and `DequantizeLinear` as its own kernel around FP32 convolutions; it does not fuse them into int8 convolutions. That is not a defect in the quantization, it is the wrong provider for it: quantized models are for the CPU provider (see the next row) and for TensorRT, which is the next section. (These INT8 rows were taken with an earlier `make_models.py` that quantized every Conv, MatMul and Gemm and the biases; the script now quantizes only `Conv`, symmetric, with unquantized biases, the form TensorRT's explicit-quantization path accepts. The conclusion about the CUDA provider does not change; the TensorRT rows below use the current script.)
- On the CPU provider, the same INT8 QDQ model runs 30 ms (batch 1) and 181 ms (batch 8) against 26 ms and 111 ms for FP32 here, on a 16-core CPU shared with other work, so the CPU numbers are the least stable in this document; do not read a ranking into them.

#### How far a variant drifts

`programs/tests/onnx_gpu/compare_models.obs` runs two models on the same inputs and compares the first output. ResNet-34, 16 deterministic synthetic inputs, FP32 on the CPU provider against:

| variant | max abs diff | mean abs diff | max diff / max logit | top-1 agrees |
|---|---:|---:|---:|---:|
| FP32 on `cuda` (TF32 convolutions) | 0.0063 | 0.00079 | 0.0006 | 16 / 16 |
| FP16 on `cuda` | 0.0336 | 0.0038 | 0.0031 | 16 / 16 |
| FP16 (FP32 in/out) on `cuda` | 0.0336 | 0.0038 | 0.0031 | 16 / 16 |
| INT8 QDQ on `cuda`, **calibrated on random data** | 4.78 | 0.19 | 0.44 | 5 / 16 |

The INT8 row is what synthetic calibration costs: a model calibrated on a handful of random images and fed a synthetic ramp is a speed fixture, not a classifier. Calibrate on real data before you read an accuracy off it.

### Session and provider options

The config map handed to a `Session` is one flat `String`-to-`String` map. The keys:

| Key | Meaning |
|---|---|
| `ep` | the provider: `cpu`, `cuda`, `tensorrt` (`trt`), `dml`, `qnn`, `vitisai`, `coreml` |
| `intra_op_threads`, `inter_op_threads` | threads ONNX Runtime may use inside one operator / across operators (0 = its own choice) |
| `graph_optimization` | `disable`, `basic`, `extended`, `all` |
| `execution_mode` | `sequential` or `parallel` |
| `mem_pattern`, `cpu_mem_arena` | `0` or `1` |
| `session_config.<key>` | a raw ONNX Runtime session config entry, `<key>` verbatim, e.g. `session_config.session.disable_prepacking=1` |
| anything else | an option of the provider chosen by `ep` |

A session option with a value it cannot read is **refused** (the session does not open and says why), exactly as a provider option ONNX Runtime does not know is (`Unknown provider option`, an invalid `device_id`): nothing is dropped silently. Key names are not case sensitive. What you ask for wins over this library's per-provider defaults (the CUDA provider defaults to the parallel executor with the memory pattern off).

```objeck
config := Map->New()<String, String>;
config->Insert("ep", "cuda");
config->Insert("device_id", "0");
config->Insert("cudnn_conv_algo_search", "HEURISTIC");   # CUDA provider options go straight through
config->Insert("gpu_mem_limit", "2147483648");           # bytes the provider's arena may take
config->Insert("intra_op_threads", "4");
session := Session->New("model.onnx", config);
```

**CUDA provider options** worth knowing (the full list is ONNX Runtime's): `device_id`; `gpu_mem_limit` (bytes) and `arena_extend_strategy` (`kNextPowerOfTwo`, `kSameAsRequested`) for the memory arena; `cudnn_conv_algo_search` (`EXHAUSTIVE`, the default; `HEURISTIC`; `DEFAULT`); `cudnn_conv_use_max_workspace`; `use_tf32` (on by default on Ampere and newer, `0` for strict FP32); `enable_cuda_graph`; `do_copy_in_default_stream`; `tunable_op_enable`.

**Measured on ResNet-34, `ep=cuda`, data resident, median ms (batch 1 / 8 / 32)**, 60 runs, the box shared with other work for this part of the run (load average 11 to 14), so read differences under 20% as noise:

| options | batch 1 | batch 8 | batch 32 |
|---|---:|---:|---:|
| defaults (`EXHAUSTIVE`, TF32 on, parallel executor) | 2.98 | 3.26 | 6.39 |
| `execution_mode=sequential` | 2.84 | 2.93 | 6.25 |
| `cudnn_conv_algo_search=HEURISTIC` | 3.43 | 3.56 | 6.73 |
| `cudnn_conv_algo_search=DEFAULT` | **8.34** | **9.32** | **15.55** |
| `use_tf32=0` | 2.14 | 2.54 | 6.42 |

`DEFAULT` skips cuDNN's algorithm search and is 2.5 to 3x slower than the search; `HEURISTIC` is within noise of `EXHAUSTIVE` and starts faster (no search at the first run of each shape). TF32 off costs nothing measurable on this model and brings the GPU within 1e-5 of the CPU instead of 1e-2. The executor mode makes no measurable difference for a single-chain network.

**Arena and memory.** `gpu_mem_limit` caps the provider's arena, not the process: ResNet-34 at batch 32 held 1.0 to 1.3 GiB of GPU memory over the card's idle level with or without `gpu_mem_limit=536870912` and `arena_extend_strategy=kSameAsRequested` (the CUDA context, cuDNN and cuBLAS workspaces live outside the arena), and ran at the same speed (6.49 against 6.39 ms). Use it to keep several sessions from each growing to the whole card, not to shrink one.

### CUDA graphs

A CUDA graph records the kernels of a run once and replays them with a single launch: it removes the per-kernel launch cost, which is most of a small model's time. It needs fixed shapes, every node on the GPU, and the input and output tensors to stay at the same addresses, which is exactly what a bound `DeviceTensor` is:

```objeck
config->Insert("ep", "cuda");
config->Insert("enable_cuda_graph", "1");          # ep=tensorrt: cuda_graph=1
session := Session->New("model.onnx", config);

input := DeviceTensor->ForInput(session, "data", [1, 3, 224, 224]);
output := DeviceTensor->ForOutput(session, "output", [1, 1000]);
bound := BoundSession->New(session);
bound->BindInput("data", input);
bound->BindOutput("output", output);
for(i := 0; i < frames; i += 1;) {
  input->Upload(frame[i]);        # refill IN PLACE: the replayed graph reads this address
  bound->Run();                   # the first runs record the graph, the rest replay it
};
```

A session that asks for a graph runs the CUDA provider **sequentially** unless it says otherwise: the parallel executor this library defaults the CUDA provider to launches on several streams and the recording fails (measured: the first run errors with a CUDA call failure). `onnx_gpu_options` checks that six runs, a refill in place and a return to the first image all answer the CPU's, for both providers.

| ResNet-34, data resident, median ms | batch 1 | batch 8 | batch 32 |
|---|---:|---:|---:|
| `ep=cuda` | 2.98 | 3.26 | 6.39 |
| `ep=cuda`, CUDA graph | **0.89** (1,122/s) | 2.34 | 5.87 |
| `ep=tensorrt` FP16 | 0.99 | 2.34 | 1.88 |
| `ep=tensorrt` FP16, CUDA graph | **0.86** (1,157/s) | 0.99 | 1.40 |

A graph is a 3x win at batch 1 on the CUDA provider (launch-bound), and nothing at batch 32 (kernel-bound).

### TensorRT

`ep=tensorrt` compiles the model, or the parts of it TensorRT supports, into an engine for this GPU. It needs the NVIDIA driver, CUDA 12, cuDNN 9 and **TensorRT 10** libraries beside a GPU ONNX Runtime that carries the TensorRT provider (`OnnxRuntime->HasProvider("tensorrt")`). A runtime without it **refuses** the session with the usual message and never runs the model on another provider; one that has the provider but cannot load TensorRT says so from ONNX Runtime's own error. `ep=trt` is accepted as a short spelling.

```objeck
config := Map->New()<String, String>;
config->Insert("ep", "tensorrt");
config->Insert("fp16", "1");                  # half precision kernels where TensorRT can
config->Insert("cuda_graph", "1");            # replay the captured kernels (see above)
session := Session->New("resnet34.onnx", config);
```

| Key | ONNX Runtime option | Default |
|---|---|---|
| `fp16=1` | `trt_fp16_enable` | off |
| `int8=1` | `trt_int8_enable`; the model must be QDQ-quantized (or give `trt_int8_calibration_table_name`) | off |
| `engine_cache=0\|1` | `trt_engine_cache_enable` | **on** |
| `engine_cache_path=` | `trt_engine_cache_path` | a per-user directory, one subdirectory per precision setting |
| `timing_cache=0\|1` | `trt_timing_cache_enable` | on with the engine cache |
| `workspace_mb=n` | `trt_max_workspace_size` | ONNX Runtime's |
| `opt_level=0..5` | `trt_builder_optimization_level` | ONNX Runtime's |
| `cuda_graph=1` | `trt_cuda_graph_enable` | off |
| `cuda_fallback=0\|1` | append the CUDA provider after TensorRT for what TensorRT cannot take | **on** |
| `cuda.<option>` | an option of that fallback CUDA provider, e.g. `cuda.gpu_mem_limit` | |
| `trt_*` | any other TensorRT provider option, unchanged | |

TensorRT builds an engine the first time it sees a model, a precision and an input shape, and an engine is tied to the model, the GPU, the TensorRT version and the settings above. **Engines are cached by default** (`~/.cache/objeck-onnx/tensorrt_cache/fp16-0_int8-0` on Linux, `%LOCALAPPDATA%\objeck-onnx\tensorrt_cache\...` on Windows, `~/Library/Caches/...` on macOS), in a subdirectory per precision setting so that an FP32 engine is never reused for `fp16=1`. Measured, ResNet-34 FP16, batch 1, from `Session->New` to the first finished run: **27.6 s cold, 2.3 s with the cache** (the engine file is 44 MB). Pass `engine_cache=0` to turn it off, or `engine_cache_path` to choose the directory (the test checks the engine lands there). Delete the directory after upgrading TensorRT or ONNX Runtime.

TensorRT takes the parts of the model it supports; the rest would fall to the CPU. So the CUDA provider is appended after it, and only a node neither can run executes on the CPU (`cuda_fallback=0` leaves it out). Unknown `trt_` options are refused by ONNX Runtime, as is `device_id=99`.

**Measured, ResNet-34, data resident (`bound_resident`), median ms and images/s**, 60 runs, same card. FP32 is the original model, `INT8 QDQ` the Conv-only QDQ model from `make_models.py` (symmetric, calibrated on synthetic data: a speed fixture):

| model / settings | provider | batch 1 | batch 8 | batch 32 |
|---|---|---:|---:|---:|
| FP32 | cuda | 2.98 (336/s) | 3.26 (2,452/s) | 6.39 (5,010/s) |
| FP32 | tensorrt | 1.14 (877/s) | 4.75 (1,684/s) | 5.17 (6,187/s) |
| FP32, `fp16=1` | tensorrt | 0.99 (1,012/s) | 2.34 (3,414/s) | 1.88 (16,990/s) |
| FP32, `fp16=1`, `cuda_graph=1` | tensorrt | 0.86 (1,157/s) | 0.99 (8,091/s) | 1.40 (22,878/s) |
| INT8 QDQ, `int8=1` | tensorrt | 1.46 (684/s) | 3.16 (2,534/s) | 2.19 (14,619/s) |
| INT8 QDQ, `int8=1`, `fp16=1` | tensorrt | 1.36 (736/s) | 1.66 (4,828/s) | 1.90 (16,804/s) |
| INT8 QDQ, `int8=1`, `fp16=1`, `cuda_graph=1` | tensorrt | **0.57 (1,751/s)** | **0.71 (11,315/s)** | **1.28 (24,943/s)** |

The single-image medians at batch 8 are the least stable here (a p95 of 8 ms for one of them), see `bench_run` output for each row's p95; the trend is not in doubt: **TensorRT FP16 with a CUDA graph is 3.3x to 4.5x the plain CUDA provider** (22,878 against 5,010 images/s at batch 32), and an INT8 QDQ model on TensorRT with FP16 and a graph reaches 24,943 images/s, 5x. On plain FP32 TensorRT is not clearly better than the CUDA provider's cuDNN kernels at batch 8 and 32. Through `Session->Run`, with host copies, none of this shows (every `run` row is 12 to 66 ms at batch 8 to 32), which is the argument for binding.

A YOLO11 segmentation model (`yolov11.onnx`, 640x640, batch 1, data resident): `ep=cuda` 6.24 ms, `ep=tensorrt` 2.85 ms (350 images/s), `ep=tensorrt` with `fp16=1` 3.52 ms with a p95 of 5.8 ms (within noise of the FP32 engine at this size).

**What was not checked.** `int8=1` with a calibration table (the implicit-quantization route) rather than a QDQ model; DLA (`trt_dla_enable`); dynamic-shape profiles (`trt_profile_*_shapes`), so each new batch size builds a new engine the first time; the Windows TensorRT DLLs (this host has no CUDA 12, cuDNN 9 or TensorRT 10 on Windows, and the build of `libobjk_onnx` for Windows was checked to compile only).

### Threads: one session, many callers

ONNX Runtime lets many threads call `Run` on one session at once, and so does `API.Onnx`: `Session->Run` from several Objeck threads on the same `Session` is supported on every provider. Three things make it safe from Objeck threads in particular.

**The collector does not wait for an inference.** Objeck's collector is stop-the-world: a collection starts only when every other thread is at a safe point, and a thread inside a native call is not at one unless the library says it is blocking. A 50 ms inference would hold up every collection, and so every allocating thread, for up to 50 ms. `Session->Run`, `BoundSession->Run` and the device copies of `DeviceTensor` therefore **park** the calling thread (`APITools_Parked`) for the length of the native work, and so does the conversion of a large input tensor, which at batch 32 is 4.8 million elements. Inside a parked scope the library touches nothing the VM manages: every input is converted into native memory first (Objeck's `Float[]` arguments are old-generation arrays, which do not move, and the caller's argument keeps them alive), and every result is built after the thread is unparked. `tools/cicd/check_native_write_barrier.py` still passes.

Measured with `programs/tests/onnx_gpu/bench_gc_stall.obs`: one thread runs ResNet-34 back to back while another allocates small objects and times every round of that work; the longest round is how long a collection kept the allocating thread waiting. The "before" is the library of the previous PR (its `BoundSession->Run` already parked, so only `Session->Run` is compared):

| model, provider, mode | longest wait of the allocating thread, before | after | p99 round, before | after |
|---|---:|---:|---:|---:|
| ResNet-34, batch 32, `ep=cuda`, `Session->Run` | 176.7 ms | **19.9 ms** | 4.5 ms | 4.6 ms |
| ResNet-34, batch 8, `ep=cpu`, `Session->Run` | 137.9 ms | **53.9 ms** | 88.8 ms | **10.9 ms** |

The remaining 20 ms is the collection itself (a major collection of the large arrays this loop makes), not waiting for the inference; the CPU case has the model's own work on the same cores as the allocator. The inference thread also got faster for not being stalled: median `Session->Run` on the CPU 104 ms before and 135 ms after, while the allocator got 3x the rounds done; on the GPU it is unchanged within noise.

**One `BoundSession` per thread.** A `BoundSession` and the `DeviceTensor`s bound to it belong to one thread at a time, while the `Session` underneath is shared. Each thread's copies run on its own non-blocking CUDA stream, taken from a pool (a stream is handed back when the thread ends, so a program that starts and ends many threads does not accumulate streams), so one thread's transfers do not queue behind another's, and ONNX Runtime gives every `Run` its own execution stream. A session closed while threads still use its tensors is released after the last of them (the registry from the IO binding PR is thread safe).

```objeck
class Worker from Thread {
  @session : Session;                       # shared by every worker
  New(session : Session) { Parent("worker"); @session := session; }

  method : public : Run(param : System.Base) ~ Nil {
    input := DeviceTensor->ForInput(@session, "data", [1, 3, 224, 224]);   # this thread's own
    output := DeviceTensor->ForOutput(@session, "output", [1, 1000]);
    bound := BoundSession->New(@session);
    bound->BindInput("data", input);
    bound->BindOutput("output", output);
    each(i : 1000) {
      input->Upload(next_frame());
      bound->Run();
    };
    bound->Close();
    input->Close();
    output->Close();
  }
}
```

**Throughput, ResNet-34 `ep=cuda`, one shared session, 6 s per row, images per second:**

| batch | mode | 1 thread | 2 | 4 | 8 |
|---:|---|---:|---:|---:|---:|
| 1 | `Session->Run` | 255 | 388 | 452 | 422 |
| 1 | bound, own tensors per thread | 391 | 461 | 483 | 447 |
| 8 | `Session->Run` | 612 | 1,108 | 1,867 | 2,222 |
| 8 | bound, own tensors per thread | 2,750 | 2,958 | 3,147 | 2,881 |

`Session->Run` scales with threads (3.6x from 1 to 8 at batch 8) because most of its time is conversion on the host, which runs in parallel when parked. A bound run is already limited by the GPU and ONNX Runtime's host-side launch work after one thread: more threads add 23% at batch 1 and 14% at batch 8 and then fall back as they contend. For small models, a CUDA graph (above) is the better lever than threads.

**What is asserted.** `programs/regression/onnx_concurrency.obs` (every platform, `ep=cpu`) and `onnx_concurrency_gpu.obs` (`SKIP:` unless a GPU provider opens a session; `ep=cuda` and `ep=tensorrt`) run four threads for 60 rounds each on one shared session, half through `Session->Run` and half through their own `BoundSession`, with a fifth thread allocating and dropping garbage without pause so collections keep happening mid-run. Every answer must equal the single-threaded answer for that input (the inputs differ per worker and round, so a crossed result would show), the program must finish, and the garbage thread must have made progress. They passed 30 of 30 runs, then 30 of 30 with `--gc-threshold=64k` (collections almost continuously), 15 of 15 on the GPU with the same threshold, 10 of 10 under `OBJECK_GC_VERIFY=1`, and 10 of 10 with `--jit=off`.

**Not covered.** The per-family sessions (`YoloSession`, `ResNetSession`, `Phi3Session`, ...) still run their inference unparked: they build their input from Objeck data and decode their output in the same native call, and were not changed. Use `Session` for anything that shares a process with garbage-heavy threads.

**Rebuilding the numbers.** `programs/tests/onnx_gpu/run_bench.sh` runs the whole matrix and prints the table rows; see its README.
