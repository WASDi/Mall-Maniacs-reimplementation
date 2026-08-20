#include <string.h>

typedef struct TestSndSample {
    unsigned char *pData;
    int            nSamples;
    int            nLoopStart;
    int            nLoopEnd;
    unsigned char  envProfile[64];
    int            nBits;
    int            nSampleRate;
    int            nEnvScale;
    int            nEnvShift;
} TestSndSample;

int sndBuildEnvProfile(void *pSample);

int main(void)
{
    TestSndSample sample;
    unsigned char data[65];
    int i;

    memset(&sample, 0, sizeof(sample));
    memset(data, 0, sizeof(data));
    memset(sample.envProfile, 0x5a, sizeof(sample.envProfile));
    for (i = 0; i < 65; i++) data[i] = (unsigned char)(i & 1);

    sample.pData = data;
    sample.nSamples = 65;
    sample.nBits = 8;
    if (sndBuildEnvProfile(&sample) != 1) return 1;
    if ((unsigned int)sample.nEnvScale != 0x80000000U || sample.nEnvShift != 7) return 2;
    for (i = 0; i < 64; i++) {
        if (sample.envProfile[i] != 128) return 3;
    }
    return 0;
}