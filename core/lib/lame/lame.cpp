#include <iostream>
#include <fstream>
#include <vector>
#include <lame/lame.h>  // make sure libmp3lame is installed and in include path

int main() {
    // ----- Settings -----
    const char* inputPCM = "input.pcm";     // raw 16-bit signed little-endian PCM
    const char* outputMP3 = "output.mp3";
    int sampleRate = 44100;  // Hz
    int channels = 1;        // 1=mono, 2=stereo

    // ----- Read input PCM file into memory -----
    std::ifstream pcmFile(inputPCM, std::ios::binary);
    if (!pcmFile) {
        std::cerr << "Failed to open input file\n";
        return 1;
    }

    // get size
    pcmFile.seekg(0, std::ios::end);
    std::streamsize pcmSize = pcmFile.tellg();
    pcmFile.seekg(0, std::ios::beg);

    std::vector<short> pcmData(pcmSize / sizeof(short));
    pcmFile.read(reinterpret_cast<char*>(pcmData.data()), pcmSize);
    pcmFile.close();

    // ----- Initialize LAME encoder -----
    lame_t lame = lame_init();
    lame_set_in_samplerate(lame, sampleRate);
    lame_set_num_channels(lame, channels);
    lame_set_VBR(lame, vbr_default);  // variable bitrate, good quality
    if (lame_init_params(lame) < 0) {
        std::cerr << "lame_init_params failed\n";
        lame_close(lame);
        return 1;
    }

    // ----- Prepare output buffer -----
    std::ofstream mp3File(outputMP3, std::ios::binary);
    if (!mp3File) {
        std::cerr << "Failed to open output MP3 file\n";
        lame_close(lame);
        return 1;
    }

    const size_t PCM_SAMPLES_PER_CHUNK = 1152 * 2; // LAME prefers multiples of 1152
    const size_t MP3_BUFFER_SIZE = 1.25 * PCM_SAMPLES_PER_CHUNK + 7200; // recommended
    std::vector<unsigned char> mp3Buffer(MP3_BUFFER_SIZE);

    // ----- Encode PCM samples in chunks -----
    size_t totalSamples = pcmData.size() / channels;
    size_t processed = 0;
    while (processed < totalSamples) {
        size_t chunk = std::min(PCM_SAMPLES_PER_CHUNK, totalSamples - processed);

        int encodedBytes = 0;
        if (channels == 2) {
            // stereo: interleaved samples
            encodedBytes = lame_encode_buffer_interleaved(
                lame,
                &pcmData[processed * channels],
                (int)chunk,
                mp3Buffer.data(),
                (int)mp3Buffer.size()
            );
        } else {
            // mono
            encodedBytes = lame_encode_buffer(
                lame,
                &pcmData[processed], // left channel
                nullptr,              // no right channel
                (int)chunk,
                mp3Buffer.data(),
                (int)mp3Buffer.size()
            );
        }

        if (encodedBytes < 0) {
            std::cerr << "LAME encoding error: " << encodedBytes << "\n";
            break;
        }

        if (encodedBytes > 0) {
            mp3File.write(reinterpret_cast<char*>(mp3Buffer.data()), encodedBytes);
        }

        processed += chunk;
    }

    // ----- Flush and close -----
    int flushBytes = lame_encode_flush(lame, mp3Buffer.data(), (int)mp3Buffer.size());
    if (flushBytes > 0) {
        mp3File.write(reinterpret_cast<char*>(mp3Buffer.data()), flushBytes);
    }

    mp3File.close();
    lame_close(lame);

    std::cout << "Encoding complete: " << outputMP3 << "\n";
    return 0;
}