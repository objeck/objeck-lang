# programs/tests/onnx_gpu

Benchmarks for `API.Onnx` on the GPU. They need a GPU, a GPU build of ONNX Runtime and real models, so they are not part of the regression suite (that has `onnx_iobinding.obs` and friends, which run with no GPU and `SKIP:` cleanly where the provider is absent). The measured tables are in [docs/ai_sections/onnx_gpu.md](../../../docs/ai_sections/onnx_gpu.md).

| File | What it does |
|---|---|
| `bench_run.obs` | Times one model per run in several modes (`run`, `run_raw`, `bound_copy`, `bound_resident`, `bound_dynout`) and prints `RESULT|model|batch|ep|mode|iters|median_ms|p95_ms|min_ms|per_second` lines, plus a `CHECK` line comparing `run` with `bound_copy` |
| `run_bench.sh` | Runs `bench_run` over a list of batch sizes and prints markdown rows |

## Reproduce

1. A GPU ONNX Runtime. `Microsoft.ML.OnnxRuntime.Gpu` from NuGet (the `runtimes/<rid>/native` directory of the package) or the `onnxruntime-gpu` wheel from PyPI; check the SHA-512 of the package against the `.nupkg.sha512` beside it. CUDA 12 and cuDNN 9 must be installed; for `ep=tensorrt` also TensorRT 10.
2. `libobjk_onnx` linked to that runtime. Linux: `cd core/lib/onnx/eq && ORT_ROOT=/path/to/native ./build.sh cpu`, copy `libobjk_onnx.so` into the deploy's `lib/native`, and put the runtime directory first on `LD_LIBRARY_PATH`. Windows: copy the package's `onnxruntime.dll`, `onnxruntime_providers_cuda.dll` and `onnxruntime_providers_shared.dll` over the ones beside `libobjk_onnx.dll` and put the CUDA 12 and cuDNN 9 DLLs on `PATH`.
3. A model. The numbers in the docs use ResNet-34 (`resnet34.onnx`, the ONNX model zoo export, 87 MB, `data` `FP32[-1,3,224,224]`), a YOLO11 segmentation export (`yolov11.onnx`, 40 MB, 640x640) and DeepLabV3. Models are not stored in the repository, see [core/lib/onnx/MODELS.md](../../../core/lib/onnx/MODELS.md).
4. Run, with nothing else on the GPU:

```sh
export OBJECK_LIB_PATH=/path/to/deploy/lib
MODEL=resnet34.onnx EP=cuda BATCHES="1 8 32" ITERS=40 \
MODES=run,run_raw,bound_copy,bound_resident,bound_dynout ./run_bench.sh
```

To watch GPU memory over a long run, use `ITERS=10000` and `nvidia-smi --query-gpu=memory.used --format=csv -l 2` in another terminal: the process should plateau, and the card should return to its starting value when it exits.
