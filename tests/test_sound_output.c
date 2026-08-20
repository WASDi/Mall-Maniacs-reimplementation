#include <string.h>

#include "../src/sound.c"

int main(void)
{
    short scratch[2] = { 0x1234, (short)-0x2345 };
    unsigned short output[2] = { 0, 0 };
    unsigned short silence[2] = { 0, 0 };

    g_pMixScratch = scratch;
    g_nMixScratchReady = 0;
    g_nMixFormat = 0x10;
    g_nMix16Bit = 1;
    g_nMixBufferBytes = 1; /* original signed 16-bit PCM selector */

    sndMixScratchToBuffer(output, 1, 0);
    if (output[0] != (unsigned short)scratch[0]) return 1;
    if (output[1] != (unsigned short)scratch[1]) return 2;

    g_nMixScratchReady = 1;
    sndMixScratchToBuffer(silence, 1, 0);
    if (silence[0] != 0 || silence[1] != 0) return 3;

    g_nMixBufferBytes = 0;
    silence[0] = silence[1] = 0;
    sndMixScratchToBuffer(silence, 1, 0);
    if (silence[0] != 0x8000 || silence[1] != 0x8000) return 4;

    {
        unsigned char silence8[2] = { 0, 0 };
        g_nMixFormat = 8;
        g_nMixBufferBytes = 1;
        sndMixScratchToBuffer(silence8, 1, 0);
        if (silence8[0] != 0 || silence8[1] != 0) return 5;

        g_nMixBufferBytes = 0;
        silence8[0] = silence8[1] = 0;
        sndMixScratchToBuffer(silence8, 1, 0);
        if (silence8[0] != 0x80 || silence8[1] != 0x80) return 6;
    }
    return 0;
}