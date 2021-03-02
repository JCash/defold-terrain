#pragma once
#include <stdint.h>

namespace dmNoise
{

    uint32_t Noise1D(int x, uint32_t seed);
    uint32_t Noise2D(int x, int y, uint32_t seed);
    uint32_t Noise3D(int x, int y, int z, uint32_t seed);
    float Noise2Df(float x, float y, uint32_t seed);

    float Fbm_2D(uint32_t seed, float x, float y, float frequency, float lacunarity, float amplitude, float gain, int num_octaves);
}
