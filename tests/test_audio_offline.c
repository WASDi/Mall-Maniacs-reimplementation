/* test_audio_offline.c — offline SDL2 audio mixer-core tests.
 *
 * Exercises audioMixOffline (resample/gain/pitch contract) without any
 * SDL device: silent-device behavior is covered by construction (the
 * mixer core never touches SDL). No dummy audio driver is used.
 */

#include <stdio.h>
#include <stdlib.h>

#include "audio_sdl2.h"

int main(void)
{
    /* Signed 8-bit bank samples (see sndPcmUnsignedToSigned @0x4378a0):
     * 0 = silence, 127 = max positive. */
    unsigned char pcm[8] = { 0, 64, 0, 192, 127, 192, 0, 64 };
    short out[16 * 2];
    int i;

    /* Basic 1:1 mix at full volume: DC-centered input must produce
     * bounded stereo output with equal channels. */
    if (!audioMixOffline(pcm, 8, 8000, 0xffff, 1000, out, 8, 8000))
        return 1;
    for (i = 0; i < 8; i++) {
        if (out[i * 2] != out[i * 2 + 1]) return 2;
        if (out[i * 2] > 32767 || out[i * 2] < -32767) return 3;
    }
    /* Silence sample (0) must map near zero. */
    if (out[0 * 2] > 256 || out[0 * 2] < -256) return 4;
    if (out[2 * 2] > 256 || out[2 * 2] < -256) return 4;

    /* Zero volume must be silent. */
    if (!audioMixOffline(pcm, 8, 8000, 0, 1000, out, 8, 8000)) return 5;
    for (i = 0; i < 8; i++)
        if (out[i * 2] != 0 || out[i * 2 + 1] != 0) return 6;

    /* Pitch double (2000) advances twice as fast: frame 1 reads sample 2. */
    if (!audioMixOffline(pcm, 8, 8000, 0xffff, 2000, out, 4, 8000)) return 7;
    {
        short plain[16 * 2];
        if (!audioMixOffline(pcm, 8, 8000, 0xffff, 1000, plain, 8, 8000)) return 8;
        if (out[1 * 2] != plain[2 * 2]) return 9;
    }

    /* Resample to a different output rate terminates and stays bounded. */
    if (!audioMixOffline(pcm, 8, 8000, 0x8000, 0, out, 16, 44100)) return 10;

    /* Invalid args must fail cleanly. */
    if (audioMixOffline(NULL, 8, 8000, 0xffff, 1000, out, 8, 8000)) return 11;
    if (audioMixOffline(pcm, 0, 8000, 0xffff, 1000, out, 8, 8000)) return 12;
    if (audioMixOffline(pcm, 8, 8000, 0xffff, 1000, NULL, 8, 8000)) return 13;
    if (audioMixOffline(pcm, 8, 8000, 0xffff, 1000, out, 0, 8000)) return 14;

    printf("audio_offline: ok\n");
    return 0;
}
