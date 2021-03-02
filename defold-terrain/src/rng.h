#pragma once
#include <stdint.h>

namespace dmRng
{
    struct Rng
    {
        uint32_t m_Seed;
        uint32_t m_Position;
    };

    void     Init(Rng* rng, uint32_t seed);

    uint32_t RandU32(Rng* rng);
    float    RandF32_01(Rng* rng);
    float    RandF32_Range(Rng* rng, float min, float max);

    // Helpers
    float U32toFloat(uint32_t ui);
}