#pragma once

//
// Raw element access on a device tensor, and the half-precision helpers.
//
// Upload and Download convert through doubles, which is exact for every type this
// layer carries and cheap enough, but a program that works in bit patterns (a
// float16 or bfloat16 pipeline, an int8 one) wants the elements as they sit in
// memory: no rounding it did not choose, and a quarter of the traffic.
//
// Included after device_tensor.h, whose DeviceBuf and copy routines it uses.
//

// The pattern of one element, in the low bytes of an Objeck Int
static inline void store_pattern(uint8_t* dst, size_t element_size, int64_t pattern) {
   const uint64_t bits = (uint64_t)pattern;
   for(size_t b = 0; b < element_size; ++b) {
      dst[b] = (uint8_t)(bits >> (8 * b));
   }
}

// The pattern of one element, zero-extended (a 16-bit float's bits are never negative)
static inline int64_t load_pattern(const uint8_t* src, size_t element_size) {
   uint64_t bits = 0;
   for(size_t b = 0; b < element_size; ++b) {
      bits |= (uint64_t)src[b] << (8 * b);
   }
   return (int64_t)bits;
}

//   0: out Int   1 on success
//   1: in  Int   handle
//   2: in  Int[] one pattern per element: the low 8, 16, 32 or 64 bits are stored,
//                by the tensor's element size
static void device_upload_bits(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   DeviceBuf* buf = (DeviceBuf*)APITools_GetIntValue(context, 1);
   size_t* array = arg_array(context, 2);
   if(!buf || !array) {
      say("upload needs a device tensor and patterns");
      return;
   }
   if(APITools_GetArraySize(array) != buf->count) {
      std::wcerr << L">>> ONNX: upload holds " << APITools_GetArraySize(array) << L" element(s) but the device tensor has "
                 << buf->count << L". <<<" << std::endl;
      return;
   }

   try {
      const size_t element_size = elem_type_size(buf->type);
      const int64_t* patterns = reinterpret_cast<const int64_t*>(array + ARRAY_HEADER_OFFSET);
      buf->staging.resize(buf->bytes);
      for(size_t i = 0; i < buf->count; ++i) {
         store_pattern(buf->staging.data() + i * element_size, element_size, patterns[i]);
      }

      std::string error;
      if(!device_copy(buf, buf->staging.data(), true, buf->bytes, error)) {
         say("upload failed: " + error);
         return;
      }
      APITools_SetIntValue(context, 0, 1);
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: upload failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: out Int   1 on success
//   1: in  Int   handle
//   2: in  Int[] receives one pattern per element
static void device_download_bits(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   DeviceBuf* buf = (DeviceBuf*)APITools_GetIntValue(context, 1);
   size_t* array = arg_array(context, 2);
   if(!buf || !array) {
      say("download needs a device tensor and an Int[] to fill");
      return;
   }
   if(APITools_GetArraySize(array) != buf->count) {
      std::wcerr << L">>> ONNX: the Int[] holds " << APITools_GetArraySize(array) << L" element(s) but the device tensor has "
                 << buf->count << L". <<<" << std::endl;
      return;
   }

   try {
      buf->staging.resize(buf->bytes);
      std::string error;
      if(!device_copy(buf, buf->staging.data(), false, buf->bytes, error)) {
         say("download failed: " + error);
         return;
      }

      const size_t element_size = elem_type_size(buf->type);
      int64_t* patterns = reinterpret_cast<int64_t*>(array + ARRAY_HEADER_OFFSET);
      for(size_t i = 0; i < buf->count; ++i) {
         patterns[i] = load_pattern(buf->staging.data() + i * element_size, element_size);
      }
      APITools_SetIntValue(context, 0, 1);
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: download failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: out Int   1 on success
//   1: in  Int   0 = binary16 (FP16), 1 = bfloat16 (BF16)
//   2: in  Int   0 = pack Float[] into bit patterns in Int[], 1 = unpack Int[] into Float[]
//   3: in  Float[] or Int[]   the source
//   4: in  Int[] or Float[]   the destination, the same length
static void convert_float16(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   const size_t kind = APITools_GetIntValue(context, 1);
   const size_t direction = APITools_GetIntValue(context, 2);
   size_t* source = arg_array(context, 3);
   size_t* target = arg_array(context, 4);
   if(!source || !target || kind > 1 || direction > 1) {
      say("a conversion needs a kind, a direction, a source and a destination");
      return;
   }

   const size_t count = APITools_GetArraySize(source);
   if(APITools_GetArraySize(target) != count) {
      std::wcerr << L">>> ONNX: the destination holds " << APITools_GetArraySize(target)
                 << L" element(s) but the source has " << count << L". <<<" << std::endl;
      return;
   }

   if(direction == 0) {
      const double* values = reinterpret_cast<const double*>(source + ARRAY_HEADER_OFFSET);
      int64_t* bits = reinterpret_cast<int64_t*>(target + ARRAY_HEADER_OFFSET);
      for(size_t i = 0; i < count; ++i) {
         bits[i] = (int64_t)(kind == 0 ? f64_to_f16(values[i]) : f64_to_bf16(values[i]));
      }
   }
   else {
      const int64_t* bits = reinterpret_cast<const int64_t*>(source + ARRAY_HEADER_OFFSET);
      double* values = reinterpret_cast<double*>(target + ARRAY_HEADER_OFFSET);
      for(size_t i = 0; i < count; ++i) {
         // only the low 16 bits are a half; the rest of the Int is ignored
         const uint16_t pattern = (uint16_t)((uint64_t)bits[i] & 0xFFFFu);
         values[i] = kind == 0 ? (double)half_to_float_u16(pattern) : (double)bf16_to_float_u16(pattern);
      }
   }
   APITools_SetIntValue(context, 0, 1);
}
