#include <SDL2/SDL.h>
#include <stdio.h>

#define SAMPLE_RATE 22050
#define CHANNELS 1
#define SAMPLE_FORMAT AUDIO_S16LSB

// Global buffer pointer
Uint8 *audio_pos;
Uint32 audio_len;

void audio_callback(void *userdata, Uint8 *stream, int len) {
    if (audio_len == 0)
        return;

    len = (len > audio_len) ? audio_len : len;
    SDL_memcpy(stream, audio_pos, len);

    audio_pos += len;
    audio_len -= len;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL init error: %s\n", SDL_GetError());
        return 1;
    }

    // Load PCM file into memory
    FILE *f = fopen("audio.pcm", "rb");
    if (!f) {
        printf("Failed to open audio.pcm\n");
        return 1;
    }
    fseek(f, 0, SEEK_END);
    audio_len = ftell(f);
    rewind(f);

    audio_pos = (Uint8 *)malloc(audio_len);
    fread(audio_pos, 1, audio_len, f);
    fclose(f);

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.freq = SAMPLE_RATE;
    spec.format = SAMPLE_FORMAT;
    spec.channels = CHANNELS;
    spec.samples = 4096;
    spec.callback = audio_callback;

    if (SDL_OpenAudio(&spec, NULL) < 0) {
        printf("Failed to open audio: %s\n", SDL_GetError());
        return 1;
    }

    SDL_PauseAudio(0); // Start playback

    // Wait until playback is done
    while (audio_len > 0) {
        SDL_Delay(100);
    }

    SDL_CloseAudio();
    SDL_Quit();
    free(audio_pos);
    return 0;
}
