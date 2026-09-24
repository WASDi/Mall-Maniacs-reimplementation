#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "audio_sdl2.h"
#include "custom_helpers.h"

/* SDL2 queue-mode audio sink. Open honors the obtained sample format,
 * channel count and rate; sound.c converts its scratch to that format.
 * When no device can be opened the game keeps running silent
 * (g_bSoundMute path in sound.c). */

static SDL_AudioDeviceID s_dev;
static int s_haveDevice;
static int s_rate;
static int s_chans;

int audioInit(void)
{
    SDL_AudioSpec want, got;
    if (s_haveDevice) return 1;
    memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 2048;
    want.callback = NULL; /* queue mode: sound.c pushes rendered frames */
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (!s_dev) {
        appLog("[audio] no device (%s); silent", SDL_GetError());
        s_haveDevice = 0;
        return 0;
    }
    s_rate = got.freq;
    s_chans = got.channels;
    s_haveDevice = 1;
    SDL_PauseAudioDevice(s_dev, 0);
    appLog("[audio] device %dHz ch=%d", s_rate, s_chans);
    return 1;
}

void audioShutdown(void)
{
    if (s_haveDevice) {
        SDL_PauseAudioDevice(s_dev, 1);
        SDL_CloseAudioDevice(s_dev);
        s_haveDevice = 0;
    }
}

int audioHaveDevice(void) { return s_haveDevice; }
int audioDeviceRate(void) { return s_haveDevice ? s_rate : 44100; }
int audioDeviceChannels(void) { return s_haveDevice ? s_chans : 2; }

int audioQueuePush(const void *data, unsigned int bytes)
{
    if (!s_haveDevice || !data || !bytes) return 0;
    return SDL_QueueAudio(s_dev, data, bytes) == 0;
}

unsigned int audioQueuedBytes(void)
{
    if (!s_haveDevice) return 0;
    return SDL_GetQueuedAudioSize(s_dev);
}

void audioQueueClear(void)
{
    if (s_haveDevice) SDL_ClearQueuedAudio(s_dev);
}

/* Offline mixer core for unit tests: resample signed 8-bit mono to
 * stereo 16-bit without touching SDL. pitch is per-mille ratio
 * (1000 = sample default). */
int audioMixOffline(const unsigned char *pcm8, int nSamples, int sampleRate,
                    unsigned int volume, int pitch,
                    short *outStereo, int outFrames, int outRate)
{
    double step, pos = 0;
    float gain;
    if (!pcm8 || !outStereo || nSamples <= 0 || outFrames <= 0 || outRate <= 0) return 0;
    if (volume > 0xffff) volume = 0xffff;
    gain = (float)volume / 65535.0f;
    step = (sampleRate > 0) ? (double)sampleRate / (double)outRate : 1.0;
    if (pitch != 0) step *= (double)pitch / 1000.0;
    if (step <= 0) step = 1.0;
    for (int f = 0; f < outFrames; f++) {
        int idx = (int)pos;
        float s = 0;
        if (idx < nSamples) s = ((int)((signed char)pcm8[idx])) / 128.0f * gain;
        if (s > 1) s = 1;
        if (s < -1) s = -1;
        outStereo[f * 2 + 0] = (short)(s * 32767);
        outStereo[f * 2 + 1] = (short)(s * 32767);
        pos += step;
    }
    return 1;
}
