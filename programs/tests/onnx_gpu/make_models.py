#!/usr/bin/env python3
"""Makes the half-precision and 8-bit variants of an FP32 image model, for the GPU benchmarks.

    pip install onnx onnxruntime onnxconverter-common numpy sympy
    python make_models.py resnet34.onnx out_dir [batch]

Writes, beside each other in out_dir:

    <name>_fp16.onnx       every weight and activation FP16, FP16 inputs and outputs
    <name>_fp16_io32.onnx  the same inside, FP32 inputs and outputs (a Cast at each edge): a
                           drop-in replacement for the FP32 model
    <name>_int8_qdq.onnx   static 8-bit quantization, QDQ format (QuantizeLinear/DequantizeLinear
                           pairs around Conv and MatMul): the format TensorRT and the CPU provider
                           execute as real int8 kernels

A model older than opset 13 is upgraded first (the ONNX model zoo's ResNet is opset 7; per-channel
quantization needs 13). The 8-bit model is calibrated on 8 synthetic batches so the script runs
anywhere. THAT IS ENOUGH FOR SPEED MEASUREMENTS AND NOT FOR ACCURACY: calibrate a model you intend
to ship on a few hundred representative images (replace `CalibrationData`).
"""
import os
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, version_converter
from onnxconverter_common import float16
from onnxruntime.quantization import CalibrationDataReader, QuantFormat, QuantType, quantize_static
from onnxruntime.quantization.shape_inference import quant_pre_process


def input_spec(model):
    initializers = {x.name for x in model.graph.initializer}
    graph_input = [i for i in model.graph.input if i.name not in initializers][0]
    dims = []
    for d in graph_input.type.tensor_type.shape.dim:
        dims.append(d.dim_value if d.dim_value > 0 else None)
    return graph_input.name, dims


def with_float32_edges(model):
    """Wraps an FP16-in, FP16-out model in Casts so it takes and returns FP32."""
    graph = model.graph
    initializers = {x.name for x in graph.initializer}
    inputs = [i for i in graph.input if i.name not in initializers]
    nodes = []
    for i in inputs:
        outer, inner = i.name, i.name + "__fp16"
        shape = [d.dim_value if d.dim_value > 0 else (d.dim_param or None) for d in i.type.tensor_type.shape.dim]
        for node in graph.node:
            for k, n in enumerate(node.input):
                if n == outer:
                    node.input[k] = inner
        nodes.append(helper.make_node("Cast", [outer], [inner], to=TensorProto.FLOAT16, name="cast_in_" + outer))
        i.type.tensor_type.elem_type = TensorProto.FLOAT
    tail = []
    for o in graph.output:
        outer, inner = o.name, o.name + "__fp16"
        for node in graph.node:
            for k, n in enumerate(node.output):
                if n == outer:
                    node.output[k] = inner
            for k, n in enumerate(node.input):
                if n == outer:
                    node.input[k] = inner
        tail.append(helper.make_node("Cast", [inner], [outer], to=TensorProto.FLOAT, name="cast_out_" + outer))
        o.type.tensor_type.elem_type = TensorProto.FLOAT
    new_nodes = nodes + list(graph.node) + tail
    del graph.node[:]
    graph.node.extend(new_nodes)
    return model


class CalibrationData(CalibrationDataReader):
    """Replace with real, representative inputs for a model you ship."""

    def __init__(self, name, shape, count):
        rng = np.random.default_rng(0)
        self.items = iter([{name: rng.random(shape, dtype=np.float32)} for _ in range(count)])

    def get_next(self):
        return next(self.items, None)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    source, out_dir = sys.argv[1], sys.argv[2]
    batch = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(source))[0]

    model = onnx.load(source)
    opset = [o.version for o in model.opset_import if o.domain in ("", "ai.onnx")][0]
    if opset < 13:
        model = version_converter.convert_version(model, 13)
        model.ir_version = max(model.ir_version, 7)
        print("upgraded opset", opset, "-> 13")
    name, dims = input_spec(model)
    shape = [batch if d is None else d for d in dims]
    print("input", name, shape)

    upgraded = os.path.join(out_dir, stem + "_opset13.onnx")
    onnx.save(model, upgraded)

    half = float16.convert_float_to_float16(model, keep_io_types=False)
    path = os.path.join(out_dir, stem + "_fp16.onnx")
    onnx.save(half, path)
    print("wrote", path, os.path.getsize(path) // 1024, "KiB")

    wrapped = with_float32_edges(float16.convert_float_to_float16(model, keep_io_types=False))
    path = os.path.join(out_dir, stem + "_fp16_io32.onnx")
    onnx.checker.check_model(wrapped)
    onnx.save(wrapped, path)
    print("wrote", path, os.path.getsize(path) // 1024, "KiB")

    prepared = os.path.join(out_dir, stem + "_prep.onnx")
    quant_pre_process(upgraded, prepared)
    path = os.path.join(out_dir, stem + "_int8_qdq.onnx")
    quantize_static(prepared, path, CalibrationData(name, shape, 8),
                    quant_format=QuantFormat.QDQ, per_channel=True, op_types_to_quantize=["Conv"],
                    weight_type=QuantType.QInt8, activation_type=QuantType.QInt8,
                    # symmetric ranges and an unquantized bias are what TensorRT's explicit
                    # quantization path accepts; the other providers run the same model
                    extra_options={"ActivationSymmetric": True, "WeightSymmetric": True, "QuantizeBias": False})
    os.remove(prepared)
    os.remove(upgraded)
    print("wrote", path, os.path.getsize(path) // 1024, "KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
