#include <iostream>
#include <fstream>
#include <vector>
#include <lame/lame.h> 

#include "../../vm/lib_api.h"

extern "C" {
  //
  // initialize library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void load_lib(VMContext& context) {
  }

  //
  // release library
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void unload_lib() {
  }

  //
  // covert PCM to MP3 audio
  //
#ifdef _WIN32
  __declspec(dllexport)
#endif
  void lame_pcm_mp3(VMContext& context) {
    // get input
    size_t* input_array = (size_t*)APITools_GetArray(context, 1)[0];
    const long input_size = ((long)APITools_GetArraySize(input_array));
    const unsigned char* input = (unsigned char*)APITools_GetArray(input_array);

    // get output
    size_t* output_holder = APITools_GetArray(context, 0);

    const int sample_rate = 22050;  // Hz
    const int num_channels = 1;     // 1=mono, 2=stereo

    if(input_size <= 0) {
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    std::vector<short> pcm_data(static_cast<size_t>(input_size) / sizeof(short));
    std::memcpy(pcm_data.data(), input, static_cast<size_t>(input_size));

    lame_t lame_context = lame_init();
    lame_set_in_samplerate(lame_context, sample_rate);
    lame_set_num_channels(lame_context, num_channels);
    lame_set_VBR(lame_context, vbr_default);
    if(lame_init_params(lame_context) < 0) {
      lame_close(lame_context);
      APITools_SetIntValue(context, 0, 0);
      return;
    }

    const size_t pcm_samples_per_chunk = 1152 * 2;
    const size_t mp3_buffer_size = static_cast<size_t>(1.25 * pcm_samples_per_chunk + 7200);
    std::vector<unsigned char> mp3_buffer(mp3_buffer_size);

    // Reserve estimated capacity for mp3_output to avoid reallocations
    const size_t estimated_mp3_size = static_cast<size_t>(input_size * 1.25);

    // Output buffer for MP3 data
    std::vector<uint8_t> mp3_output;
    mp3_output.reserve(estimated_mp3_size);

    const size_t total_samples = pcm_data.size() / static_cast<size_t>(num_channels);
    const int mp3_buffer_capacity = static_cast<int>(mp3_buffer.size());

    size_t processed = 0;
    while(processed < total_samples) {
      const size_t chunk = min(pcm_samples_per_chunk, total_samples - processed);

      int encoded_bytes = 0;
      if(num_channels == 2) {
        encoded_bytes = lame_encode_buffer_interleaved(
          lame_context,
          &pcm_data[processed * num_channels],
          static_cast<int>(chunk),
          mp3_buffer.data(),
          mp3_buffer_capacity
        );
      }
      else {
        encoded_bytes = lame_encode_buffer(
          lame_context,
          &pcm_data[processed],
          nullptr,
          static_cast<int>(chunk),
          mp3_buffer.data(),
          mp3_buffer_capacity
        );
      }

      if(encoded_bytes < 0) {
        std::cerr << "LAME encoding error: " << encoded_bytes << "\n";
        break;
      }

      if(encoded_bytes > 0) {
        mp3_output.insert(mp3_output.end(), mp3_buffer.begin(), mp3_buffer.begin() + encoded_bytes);
      }

      processed += chunk;
    }

    const int flush_bytes = lame_encode_flush(lame_context, mp3_buffer.data(), mp3_buffer_capacity);
    if(flush_bytes > 0) {
      mp3_output.insert(mp3_output.end(), mp3_buffer.begin(), mp3_buffer.begin() + flush_bytes);
    }

    lame_close(lame_context);

    size_t* output_byte_array = APITools_MakeByteArray(context, mp3_output.size());

    unsigned char* output_byte_array_buffer = reinterpret_cast<unsigned char*>(output_byte_array + 3);
    memcpy(output_byte_array_buffer, mp3_output.data(), mp3_output.size() * sizeof(unsigned char));
    output_holder[0] = (size_t)output_byte_array;
  }
}