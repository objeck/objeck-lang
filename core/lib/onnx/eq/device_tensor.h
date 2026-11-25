#pragma once

//
// Device tensors and IO binding.
//
// A normal Session->Run copies every input from host memory to the device and every
// output back, and allocates for both, on every call. For a model that stays on the
// GPU between calls (a video pipeline, a decoder loop, a benchmark of the model
// itself) those copies and allocations are most of the cost. IO binding removes
// them: a DeviceTensor is a tensor that lives in the execution provider's memory,
// uploaded once, bound to a model input or output by name, and read back only when
// the program asks. BoundSession::Run then runs the model on what is already there.
//
// Where the memory is depends on the session's provider: ep=cuda and ep=tensorrt
// allocate on the GPU, ep=cpu allocates ordinary host memory (so the same code, and
// the same bit-identical-to-Run check, runs on a machine without a GPU). Any other
// provider is refused by name rather than given memory it cannot use.
//
// This file needs no CUDA headers and no CUDA at build time. The few CUDA runtime
// calls it makes (a copy, a stream) are looked up from the cudart library the ONNX
// Runtime CUDA provider already loaded, at the first use; where there is none, a
// device tensor on a CUDA session cannot be created and says why.
//

#include <vector>
#include <memory>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

//
// The CUDA runtime, by name.
//
struct CudaRuntime {
   bool ok;
   std::string error;
   int (*SetDevice)(int);
   int (*MemcpyAsync)(void*, const void*, size_t, int, void*);
   int (*StreamCreateWithFlags)(void**, unsigned int);
   int (*StreamSynchronize)(void*);
   const char* (*GetErrorString)(int);

   CudaRuntime() : ok(false), SetDevice(nullptr), MemcpyAsync(nullptr), StreamCreateWithFlags(nullptr),
                   StreamSynchronize(nullptr), GetErrorString(nullptr) {}
};

static void* cuda_symbol(void* module, const char* name) {
#ifdef _WIN32
   return (void*)GetProcAddress((HMODULE)module, name);
#else
   return dlsym(module, name);
#endif
}

static CudaRuntime& cuda_runtime() {
   static CudaRuntime runtime;
   static std::once_flag once;
   std::call_once(once, []() {
      void* module = nullptr;
#ifdef _WIN32
      static const char* const names[] = { "cudart64_12.dll", "cudart64_13.dll", "cudart64_110.dll" };
      for(size_t i = 0; i < sizeof(names) / sizeof(names[0]) && !module; ++i) {
         module = (void*)LoadLibraryA(names[i]);
      }
#else
      static const char* const names[] = { "libcudart.so.12", "libcudart.so.13", "libcudart.so.11.0", "libcudart.so" };
      for(size_t i = 0; i < sizeof(names) / sizeof(names[0]) && !module; ++i) {
         module = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
      }
#endif
      if(!module) {
         runtime.error = "the CUDA runtime library (cudart) could not be loaded";
         return;
      }

      runtime.SetDevice = (int (*)(int))cuda_symbol(module, "cudaSetDevice");
      runtime.MemcpyAsync = (int (*)(void*, const void*, size_t, int, void*))cuda_symbol(module, "cudaMemcpyAsync");
      runtime.StreamCreateWithFlags = (int (*)(void**, unsigned int))cuda_symbol(module, "cudaStreamCreateWithFlags");
      runtime.StreamSynchronize = (int (*)(void*))cuda_symbol(module, "cudaStreamSynchronize");
      runtime.GetErrorString = (const char* (*)(int))cuda_symbol(module, "cudaGetErrorString");
      if(!runtime.SetDevice || !runtime.MemcpyAsync || !runtime.StreamCreateWithFlags ||
         !runtime.StreamSynchronize || !runtime.GetErrorString) {
         runtime.error = "the CUDA runtime library is missing an expected entry point";
         // coverity[leaked_storage] kept loaded on purpose, like the success path below
         return;
      }
      runtime.ok = true;
      // coverity[leaked_storage] the CUDA runtime stays loaded for the process lifetime; the entry points above point into it
   });
   return runtime;
}

static std::string cuda_error_text(CudaRuntime& rt, int code, const char* what) {
   std::string text = what;
   text += ": ";
   text += rt.GetErrorString ? rt.GetErrorString(code) : "error";
   return text;
}

//
// One copy stream per thread, taken from a pool.
//
// A copy on the legacy default stream would serialise with every other thread's work
// on the device. Each thread gets its own non-blocking stream instead, so threads
// that each run their own BoundSession overlap their transfers. Streams are pooled,
// not destroyed with the thread: a thread that ends hands its stream back, and the
// pool itself is never destroyed, so no CUDA call is made during process teardown,
// when the runtime may already be gone.
//
struct StreamPool {
   std::mutex mutex;
   std::vector<std::pair<int, void*> > free_streams;   // (device, stream)
};

static StreamPool& stream_pool() {
   static StreamPool* pool = new StreamPool();
   return *pool;
}

struct StreamLease {
   std::vector<std::pair<int, void*> > held;
   ~StreamLease() {
      StreamPool& pool = stream_pool();
      std::lock_guard<std::mutex> guard(pool.mutex);
      for(size_t i = 0; i < held.size(); ++i) {
         pool.free_streams.push_back(held[i]);
      }
   }
};

static void* thread_stream(CudaRuntime& rt, int device_id, std::string& error) {
   static thread_local StreamLease lease;
   for(size_t i = 0; i < lease.held.size(); ++i) {
      if(lease.held[i].first == device_id) {
         return lease.held[i].second;
      }
   }

   void* stream = nullptr;
   {
      StreamPool& pool = stream_pool();
      std::lock_guard<std::mutex> guard(pool.mutex);
      for(size_t i = 0; i < pool.free_streams.size(); ++i) {
         if(pool.free_streams[i].first == device_id) {
            stream = pool.free_streams[i].second;
            pool.free_streams.erase(pool.free_streams.begin() + i);
            break;
         }
      }
   }

   if(!stream) {
      const int set = rt.SetDevice(device_id);
      if(set != 0) {
         error = cuda_error_text(rt, set, "cudaSetDevice");
         return nullptr;
      }
      const int created = rt.StreamCreateWithFlags(&stream, 1 /* cudaStreamNonBlocking */);
      if(created != 0) {
         error = cuda_error_text(rt, created, "cudaStreamCreate");
         return nullptr;
      }
   }

   lease.held.push_back(std::make_pair(device_id, stream));
   return stream;
}

// "Cuda" is the memory name ONNX Runtime gives both the CUDA and the TensorRT provider's device memory
static Ort::MemoryInfo make_memory_info(bool cuda, int device_id) {
   if(cuda) {
      return Ort::MemoryInfo("Cuda", OrtDeviceAllocator, device_id, OrtMemTypeDefault);
   }
   return Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
}

//
// A tensor in provider memory.
//
struct DeviceBuf {
   const Ort::Session* session;
   bool retained;
   bool cuda;
   int device_id;
   Ort::MemoryInfo mem;
   std::unique_ptr<Ort::Allocator> allocator;   // null when ORT allocated the value (a bound output)
   Ort::Value value;
   ONNXTensorElementDataType type;
   std::vector<int64_t> shape;
   size_t count;
   size_t bytes;
   std::vector<uint8_t> staging;                // reused host buffer: no per-call allocation

   DeviceBuf(const Ort::Session* s, bool is_cuda, int device) :
      session(s), retained(false), cuda(is_cuda), device_id(device), mem(make_memory_info(is_cuda, device)),
      value(nullptr), type(ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED), count(0), bytes(0) {}

   ~DeviceBuf() {
      // the value before the allocator that made it, the allocator before the session
      value = Ort::Value(nullptr);
      allocator.reset();
      if(retained) {
         registry_release(session);
      }
   }

   void* data() { return value.GetTensorMutableRawData(); }
};

static size_t elem_type_size(ONNXTensorElementDataType type) {
   switch(type) {
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   return 4;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:  return 8;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: return 2;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16: return 2;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:    return 1;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:   return 1;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:   return 2;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:  return 2;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   return 4;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:  return 4;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:   return 8;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:  return 8;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:    return 1;
   default:                                    return 0;
   }
}

static bool elem_type_from_name(const std::wstring& name, ONNXTensorElementDataType& type) {
   static const ONNXTensorElementDataType all[] = {
      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16, ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32,
      ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64, ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL
   };
   for(size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
      if(name == elem_type_name(all[i])) {
         type = all[i];
         return true;
      }
   }
   return false;
}

// 0 = host copy (CPU), 1 = host to device, 2 = device to host
static bool device_copy(DeviceBuf* buf, void* host, bool to_device, size_t bytes, std::string& error) {
   if(bytes == 0) {
      return true;
   }

   if(!buf->cuda) {
      if(to_device) {
         std::memcpy(buf->data(), host, bytes);
      }
      else {
         std::memcpy(host, buf->data(), bytes);
      }
      return true;
   }

   CudaRuntime& rt = cuda_runtime();
   if(!rt.ok) {
      error = rt.error;
      return false;
   }
   void* stream = thread_stream(rt, buf->device_id, error);
   if(!stream) {
      return false;
   }

   // the current device is per thread: this thread may never have touched this one
   int code = rt.SetDevice(buf->device_id);
   if(code != 0) {
      error = cuda_error_text(rt, code, "cudaSetDevice");
      return false;
   }

   // cudaMemcpyHostToDevice = 1, cudaMemcpyDeviceToHost = 2
   code = to_device ? rt.MemcpyAsync(buf->data(), host, bytes, 1, stream)
                    : rt.MemcpyAsync(host, buf->data(), bytes, 2, stream);
   if(code != 0) {
      error = cuda_error_text(rt, code, "cudaMemcpyAsync");
      return false;
   }
   code = rt.StreamSynchronize(stream);
   if(code != 0) {
      error = cuda_error_text(rt, code, "cudaStreamSynchronize");
      return false;
   }

   return true;
}

// The memory a provider's tensors live in, or false (with the reason) for one that has none here.
static bool device_kind_for(const SessionRecord& record, bool& cuda) {
   if(record.ep == "cuda" || record.ep == "tensorrt") {
      cuda = true;
      return true;
   }
   if(record.ep == "cpu") {
      cuda = false;
      return true;
   }
   return false;
}

static void say(const char* message) {
   std::wcerr << L">>> ONNX: " << BytesToUnicode(message) << L" <<<" << std::endl;
}

static void say(const std::string& message) {
   say(message.c_str());
}

template <typename T>
static void widen_elements(const void* src, double* dst, size_t count) {
   const T* typed = reinterpret_cast<const T*>(src);
   for(size_t i = 0; i < count; ++i) {
      dst[i] = (double)typed[i];
   }
}

// Host bytes of a tensor's element type to the doubles Objeck reads. Refuses an unsupported type by name.
static bool raw_to_doubles(ONNXTensorElementDataType type, const void* src, double* dst, size_t count,
                           const std::wstring& name) {
   switch(type) {
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:   widen_elements<float>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:  widen_elements<double>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:    widen_elements<int8_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:   widen_elements<int16_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:   widen_elements<int32_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:   widen_elements<int64_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:   widen_elements<uint8_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:  widen_elements<uint16_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:  widen_elements<uint32_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:  widen_elements<uint64_t>(src, dst, count); return true;
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL: {
      const uint8_t* typed = reinterpret_cast<const uint8_t*>(src);
      for(size_t i = 0; i < count; ++i) {
         dst[i] = typed[i] ? 1.0 : 0.0;
      }
      return true;
   }
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16: {
      const uint16_t* typed = reinterpret_cast<const uint16_t*>(src);
      for(size_t i = 0; i < count; ++i) {
         dst[i] = (double)half_to_float_u16(typed[i]);
      }
      return true;
   }
   case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16: {
      const uint16_t* typed = reinterpret_cast<const uint16_t*>(src);
      for(size_t i = 0; i < count; ++i) {
         dst[i] = (double)bf16_to_float_u16(typed[i]);
      }
      return true;
   }
   default:
      std::wcerr << L">>> ONNX: '" << name << L"' has element type " << elem_type_name(type)
                 << L", which this layer does not carry. <<<" << std::endl;
      return false;
   }
}

// Reads the tensor an element of a Float[] argument holds into a host buffer of the element type
static bool buffer_for_values(DeviceBuf* buf, const double* values, size_t n) {
   return fill_input_block(buf->staging, buf->type, values, n, L"device tensor");
}

//
// Entry points. Slot 0 is the result, set on every path (#1028).
//

//   0: out Int   handle, 0 on failure
//   1: in  Int   session
//   2: in  String element type (FP32, FP16, INT64 ...)
//   3: in  Int[] shape
static void device_new(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);
   const wchar_t* type_arg = APITools_GetStringValue(context, 2);
   size_t* shape_array = arg_array(context, 3);
   if(!session || !type_arg || !shape_array) {
      say("a device tensor needs an open session, an element type and a shape");
      return;
   }

   ONNXTensorElementDataType type;
   if(!elem_type_from_name(type_arg, type)) {
      std::wcerr << L">>> ONNX: element type '" << type_arg << L"' is not one a device tensor carries. <<<" << std::endl;
      return;
   }

   const size_t rank = APITools_GetArraySize(shape_array);
   const int64_t* dims = reinterpret_cast<const int64_t*>(shape_array + ARRAY_HEADER_OFFSET);
   std::vector<int64_t> shape(dims, dims + rank);
   size_t count = 1;
   for(size_t i = 0; i < rank; ++i) {
      if(shape[i] < 0) {
         say("a device tensor needs a concrete shape; a model's -1 means the caller chooses it");
         return;
      }
      count *= (size_t)shape[i];
   }
   if(rank == 0) {
      say("a device tensor needs at least one dimension");
      return;
   }

   SessionRecord record;
   if(!registry_find(session, record)) {
      say("this session is not known to the library (it is closing or was not opened here)");
      return;
   }
   bool cuda = false;
   if(!device_kind_for(record, cuda)) {
      say("device tensors are supported for ep=cuda, ep=tensorrt and ep=cpu; this session uses ep=" + record.ep);
      return;
   }

   try {
      std::unique_ptr<DeviceBuf> buf(new DeviceBuf(session, cuda, record.device_id));
      if(!registry_retain(session)) {
         say("this session is closing");
         return;
      }
      buf->retained = true;
      buf->allocator.reset(new Ort::Allocator(*session, buf->mem));
      buf->value = Ort::Value::CreateTensor(*buf->allocator, shape.data(), shape.size(), type);
      buf->type = type;
      buf->shape = shape;
      buf->count = count;
      buf->bytes = count * elem_type_size(type);

      APITools_SetIntValue(context, 0, (size_t)buf.release());
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: allocating a device tensor failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: in Int handle
static void device_free(VMContext& context) {
   DeviceBuf* buf = (DeviceBuf*)APITools_GetIntValue(context, 0);
   delete buf;
}

//   0: out Int   1 on success, 0 on failure
//   1: in  Int   handle
//   2: in  Float[] the elements, flat and row-major
static void device_upload(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   DeviceBuf* buf = (DeviceBuf*)APITools_GetIntValue(context, 1);
   size_t* array = arg_array(context, 2);
   if(!buf || !array) {
      say("upload needs a device tensor and values");
      return;
   }
   if(APITools_GetArraySize(array) != buf->count) {
      std::wcerr << L">>> ONNX: upload holds " << APITools_GetArraySize(array) << L" element(s) but the device tensor has "
                 << buf->count << L". <<<" << std::endl;
      return;
   }

   try {
      const double* values = reinterpret_cast<const double*>(array + ARRAY_HEADER_OFFSET);
      if(!buffer_for_values(buf, values, buf->count)) {
         return;
      }
      std::string error;
      bool copied = false;
      {
         APITools_Parked parked(context);   // the staging buffer is native memory
         copied = device_copy(buf, buf->staging.data(), true, buf->bytes, error);
      }
      if(!copied) {
         say("upload failed: " + error);
         return;
      }
      APITools_SetIntValue(context, 0, 1);
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: upload failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: out Int   1 on success, 0 on failure
//   1: in  Int   handle
//   2: in  Float[] receives the elements; must hold exactly the tensor's count
static void device_download(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   DeviceBuf* buf = (DeviceBuf*)APITools_GetIntValue(context, 1);
   size_t* array = arg_array(context, 2);
   if(!buf || !array) {
      say("download needs a device tensor and a Float[] to fill");
      return;
   }
   if(APITools_GetArraySize(array) != buf->count) {
      std::wcerr << L">>> ONNX: the Float[] holds " << APITools_GetArraySize(array) << L" element(s) but the device tensor has "
                 << buf->count << L". <<<" << std::endl;
      return;
   }

   try {
      buf->staging.resize(buf->bytes);
      std::string error;
      bool copied = false;
      {
         APITools_Parked parked(context);   // the staging buffer is native memory
         copied = device_copy(buf, buf->staging.data(), false, buf->bytes, error);
      }
      if(!copied) {
         say("download failed: " + error);
         return;
      }
      double* out = reinterpret_cast<double*>(array + ARRAY_HEADER_OFFSET);
      if(!raw_to_doubles(buf->type, buf->staging.data(), out, buf->count, L"device tensor")) {
         return;
      }
      APITools_SetIntValue(context, 0, 1);
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: download failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: out Int   the answer
//   1: in  Int   handle
//   2: in  Int   which: 0 element type code, 1 rank, 2 element count, 3 byte size,
//                       4 on the GPU (1) or the host (0), 10 + i the i-th dimension
static void device_meta(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   DeviceBuf* buf = (DeviceBuf*)APITools_GetIntValue(context, 1);
   const int64_t which = (int64_t)APITools_GetIntValue(context, 2);
   if(!buf) {
      return;
   }

   int64_t answer = 0;
   if(which == 0) {
      answer = (int64_t)buf->type;
   }
   else if(which == 1) {
      answer = (int64_t)buf->shape.size();
   }
   else if(which == 2) {
      answer = (int64_t)buf->count;
   }
   else if(which == 3) {
      answer = (int64_t)buf->bytes;
   }
   else if(which == 4) {
      answer = buf->cuda ? 1 : 0;
   }
   else if(which >= 10 && (size_t)(which - 10) < buf->shape.size()) {
      answer = buf->shape[(size_t)(which - 10)];
   }
   APITools_SetIntValue(context, 0, (size_t)answer);
}

//
// An IO binding: the model's inputs and outputs, by name, pointed at device tensors.
//
struct BoundSession {
   Ort::Session* session;
   bool retained;
   bool cuda;
   int device_id;
   Ort::IoBinding io;
   Ort::MemoryInfo mem;
   std::vector<std::string> output_names;   // in the order they were bound
   Ort::RunOptions run_options;

   BoundSession(Ort::Session* s, bool is_cuda, int device) :
      session(s), retained(false), cuda(is_cuda), device_id(device), io(*s),
      mem(make_memory_info(is_cuda, device)), run_options(nullptr) {}

   ~BoundSession() {
      if(retained) {
         registry_release(session);
      }
   }
};

//   0: out Int  handle, 0 on failure
//   1: in  Int  session
static void bound_new(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   Ort::Session* session = (Ort::Session*)APITools_GetIntValue(context, 1);
   if(!session) {
      say("an IO binding needs an open session");
      return;
   }

   SessionRecord record;
   if(!registry_find(session, record)) {
      say("this session is not known to the library (it is closing or was not opened here)");
      return;
   }
   bool cuda = false;
   if(!device_kind_for(record, cuda)) {
      say("IO binding is supported for ep=cuda, ep=tensorrt and ep=cpu; this session uses ep=" + record.ep);
      return;
   }

   try {
      std::unique_ptr<BoundSession> bound(new BoundSession(session, cuda, record.device_id));
      if(!registry_retain(session)) {
         say("this session is closing");
         return;
      }
      bound->retained = true;
      APITools_SetIntValue(context, 0, (size_t)bound.release());
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: creating an IO binding failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: in Int handle
static void bound_free(VMContext& context) {
   BoundSession* bound = (BoundSession*)APITools_GetIntValue(context, 0);
   delete bound;
}

//   0: out Int   1 on success
//   1: in  Int   bound handle
//   2: in  String model input or output name
//   3: in  Int   device tensor handle; 0 on an output = let ONNX Runtime allocate it on the device each run
//   4: in  Int   0 input, 1 output
static void bound_bind(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   BoundSession* bound = (BoundSession*)APITools_GetIntValue(context, 1);
   const wchar_t* name_arg = APITools_GetStringValue(context, 2);
   DeviceBuf* buf = (DeviceBuf*)APITools_GetIntValue(context, 3);
   const bool is_output = APITools_GetIntValue(context, 4) != 0;
   if(!bound || !name_arg) {
      say("bind needs an IO binding and a name");
      return;
   }
   if(!is_output && !buf) {
      say("an input must be bound to a device tensor");
      return;
   }
   if(buf && buf->cuda != bound->cuda) {
      say("the device tensor and the IO binding are on different kinds of device");
      return;
   }
   if(buf && buf->session != bound->session) {
      say("the device tensor was allocated for another session");
      return;
   }

   try {
      const std::string name = UnicodeToBytes(name_arg);
      if(!is_output) {
         bound->io.BindInput(name.c_str(), buf->value);
      }
      else if(buf) {
         bound->io.BindOutput(name.c_str(), buf->value);
         bound->output_names.push_back(name);
      }
      else {
         bound->io.BindOutput(name.c_str(), bound->mem);
         bound->output_names.push_back(name);
      }
      APITools_SetIntValue(context, 0, 1);
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: binding '" << name_arg << L"' failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: in Int bound handle
static void bound_clear(VMContext& context) {
   BoundSession* bound = (BoundSession*)APITools_GetIntValue(context, 0);
   if(bound) {
      bound->io.ClearBoundInputs();
      bound->io.ClearBoundOutputs();
      bound->output_names.clear();
   }
}

//   0: out Int   1 on success
//   1: in  Int   bound handle
static void bound_run(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   BoundSession* bound = (BoundSession*)APITools_GetIntValue(context, 1);
   if(!bound) {
      say("run needs an IO binding");
      return;
   }

   try {
      // The arguments are all read above, so this thread may park for the length of
      // the inference: a collection no longer waits for it (APITools_Parked). Nothing
      // the VM manages is touched in the scope.
      {
         APITools_Parked parked(context);
         bound->session->Run(bound->run_options, bound->io);
         bound->io.SynchronizeOutputs();
      }
      APITools_SetIntValue(context, 0, 1);
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: run failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}

//   0: out Int   device tensor handle for that output of the LAST run, 0 on failure
//   1: in  Int   bound handle
//   2: in  Int   output position, in the order the outputs were bound
static void bound_output(VMContext& context) {
   APITools_SetIntValue(context, 0, 0);

   BoundSession* bound = (BoundSession*)APITools_GetIntValue(context, 1);
   const size_t at = APITools_GetIntValue(context, 2);
   if(!bound || at >= bound->output_names.size()) {
      say("no such bound output");
      return;
   }

   try {
      std::vector<Ort::Value> values = bound->io.GetOutputValues();
      if(at >= values.size() || !values[at].IsTensor()) {
         say("that output is not a tensor, or the binding has not been run");
         return;
      }

      const Ort::TensorTypeAndShapeInfo info = values[at].GetTensorTypeAndShapeInfo();
      std::unique_ptr<DeviceBuf> buf(new DeviceBuf(bound->session, bound->cuda, bound->device_id));
      if(!registry_retain(bound->session)) {
         say("this session is closing");
         return;
      }
      buf->retained = true;
      buf->type = info.GetElementType();
      buf->shape = info.GetShape();
      buf->count = info.GetElementCount();
      buf->bytes = buf->count * elem_type_size(buf->type);
      buf->value = std::move(values[at]);

      APITools_SetIntValue(context, 0, (size_t)buf.release());
   }
   catch(const std::exception& e) {
      std::wcerr << L">>> ONNX: reading a bound output failed: " << BytesToUnicode(e.what()) << L" <<<" << std::endl;
   }
}
