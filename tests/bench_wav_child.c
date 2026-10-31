#include "apu_wav.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(int argc, char **argv)
{
    int16_t samples[512];
    FILE *ready;
    int i;

    if (argc != 3)
        return 2;
    if (strcmp(argv[1], "-") != 0) {
        if (!apu_wav_open(argv[1], 0))
            return 2;
        for (i = 0; i < 512; i++)
            samples[i] = (int16_t)(i + 1);
        apu_wav_write(samples, 256);
    }
    ready = fopen(argv[2], "wb");
    if (!ready)
        return 3;
    fputs("ready\n", ready);
    fclose(ready);
    for (;;)
        Sleep(50);
}
