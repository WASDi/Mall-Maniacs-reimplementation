#ifndef AUDIO_SDL2_H
#define AUDIO_SDL2_H

/* SDL2 audio sink (Phase 4). One SDL2 queue-mode device; the retained
 * software mix-core (sound.c) renders voices into staging regions that
 * are queued here. The obtained device format/rate/channels are honored
 * (convert as required); nothing assumes 44.1 kHz stereo. Silent when no
 * device is available (documented safe state).
 *
 * Port note (deviation): mixing stays frame-tick driven through
 * sndMixTick, as the original @0x439050 ring was; there is no audio-
 * thread callback yet, so emitter/voice state needs no cross-thread
 * synchronization by construction. A callback-owned mixer is future work
 * (see docs/17-cross-platform.md). */

int audioInit(void);
void audioShutdown(void);
int audioHaveDevice(void);
int audioDeviceRate(void);
int audioDeviceChannels(void);

/* Queue rendered PCM frames (stereo 16-bit native) to the device. */
int audioQueuePush(const void *data, unsigned int bytes);
unsigned int audioQueuedBytes(void);
void audioQueueClear(void);

/* For offline mixer-core unit tests (no SDL device needed). */
int audioMixOffline(const unsigned char *pcm8, int nSamples, int sampleRate,
                    unsigned int volume, int pitch,
                    short *outStereo, int outFrames, int outRate);

#endif /* AUDIO_SDL2_H */
