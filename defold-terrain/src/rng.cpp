#include "rng.h"
#include "noise.h"
#include <limits.h>

namespace dmRng
{
    float U32toFloat(uint32_t ui)
    {
        return ui / (float)UINT_MAX;
    }


    void Init(Rng* rng, uint32_t seed)
    {
        rng->m_Position = 0;
        rng->m_Seed = seed;
    }

    uint32_t RandU32(Rng* rng)
    {
        return dmNoise::Noise1D(rng->m_Position++, rng->m_Seed);
    }

    float RandF32_01(Rng* rng)
    {
        return U32toFloat(dmNoise::Noise1D(rng->m_Position++, rng->m_Seed));
    }

    float RandF32_Range(Rng* rng, float min, float max)
    {
        float f = RandF32_01(rng);
        return min + (max - min) * f;
    }
}