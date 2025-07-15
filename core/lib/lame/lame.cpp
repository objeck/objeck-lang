#include <iostream>
#include <fstream>
#include <vector>
#include <lame/lame.h>  // make sure libmp3lame is installed and in include path

int main(const int argc, const char* argv[]) {
  if(argc == 3) {
    const char* const input_pcm = argv[1];     // raw 16-bit signed little-endian PCM
    const char* const output_mp3 = argv[2];
    const int sample_rate = 22050;  // Hz
    const int num_channels = 1;     // 1=mono, 2=stereo

    // ----- Read input PCM file into memory -----
    std::ifstream pcm_file(input_pcm, std::ios::binary);
    if(!pcm_file) {
      std::cerr << "Failed to open input file\n";
      return 1;
    }

    pcm_file.seekg(0, std::ios::end);
    const std::streamsize pcm_size = pcm_file.tellg();
    pcm_file.seekg(0, std::ios::beg);

    if(pcm_size <= 0) {
      std::cerr << "Input file is empty or invalid size\n";
      return 1;
    }

    std::vector<short> pcm_data(static_cast<size_t>(pcm_size) / sizeof(short));
    pcm_file.read(reinterpret_cast<char*>(pcm_data.data()), pcm_size);
    pcm_file.close();

    lame_t lame_context = lame_init();
    lame_set_in_samplerate(lame_context, sample_rate);
    lame_set_num_channels(lame_context, num_channels);
    lame_set_VBR(lame_context, vbr_default);
    if(lame_init_params(lame_context) < 0) {
      std::cerr << "lame_init_params failed\n";
      lame_close(lame_context);
      return 1;
    }
    
    const size_t pcm_samples_per_chunk = 1152 * 2;
    const size_t mp3_buffer_size = static_cast<size_t>(1.25 * pcm_samples_per_chunk + 7200);
    std::vector<unsigned char> mp3_buffer(mp3_buffer_size);

    // Reserve estimated capacity for mp3_output to avoid reallocations
    const size_t estimated_mp3_size = static_cast<size_t>(pcm_size * 1.25);
    
    std::vector<uint8_t> mp3_output;
    mp3_output.reserve(estimated_mp3_size);

    const size_t total_samples = pcm_data.size() / static_cast<size_t>(num_channels);
    const int mp3_buffer_capacity = static_cast<int>(mp3_buffer.size());

    size_t processed = 0;
    while(processed < total_samples) {
      const size_t chunk = std::min(pcm_samples_per_chunk, total_samples - processed);

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

    // Optionally write vector to a file at the end if needed
    std::ofstream mp3_file(output_mp3, std::ios::binary);
    if(mp3_file) {
      mp3_file.write(reinterpret_cast<const char*>(mp3_output.data()), static_cast<std::streamsize>(mp3_output.size()));
      mp3_file.close();
    }

    std::wcout << L"Encoded: file=" << output_mp3 << L", bytes=" << mp3_output.size() << std::endl;

    return 0;
  }
}
