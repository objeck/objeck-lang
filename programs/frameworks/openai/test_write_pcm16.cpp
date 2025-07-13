#include <SDL2/SDL.h>
#include <stdio.h>

#define SAMPLE_RATE 22050
#define CHANNELS 1
#define SAMPLE_FORMAT AUDIO_S16LSB
#define BUFFER_SIZE 4096

void audio_input_callback(void *userdata, Uint8 *stream, int len) {
    FILE *out = (FILE *)userdata;
    fwrite(stream, 1, len, out); // Save raw PCM data
}

int main() {
    SDL_Init(SDL_INIT_AUDIO);

    SDL_AudioSpec desiredSpec, obtainedSpec;
    SDL_zero(desiredSpec);

    desiredSpec.freq = SAMPLE_RATE;
    desiredSpec.format = SAMPLE_FORMAT;
    desiredSpec.channels = CHANNELS;
    desiredSpec.samples = BUFFER_SIZE;
    desiredSpec.callback = audio_input_callback;

    FILE *outFile = fopen("mic_output.pcm", "wb");
    if (!outFile) return 1;
    desiredSpec.userdata = outFile;

    int devcount = SDL_GetNumAudioDevices(SDL_TRUE);
    printf("Available input devices:\n");
    for (int i = 0; i < devcount; i++) {
        printf("  [%d] %s\n", i, SDL_GetAudioDeviceName(i, SDL_TRUE));
    }

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 1, &desiredSpec, &obtainedSpec, 0);
    if (!dev) {
        printf("Failed to open input device: %s\n", SDL_GetError());
        return 1;
    }

    SDL_PauseAudioDevice(dev, 0); // Start capturing
    printf("Recording... Press Ctrl+C to stop.\n");

    while (1) SDL_Delay(100); // Loop forever, or add logic to stop after N seconds

    // SDL_CloseAudioDevice(dev); // Unreachable here
    // fclose(outFile);
    // SDL_Quit();
}
