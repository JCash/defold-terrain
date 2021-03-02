#include "noise.h"
#include <memory.h>
#include <limits.h>
#include <stdio.h>
#include <math.h>

// #define JC_NOISE_IMPLEMENTATION
// #include "jc_noise.h"


// https://www.pcg-random.org/using-pcg-c-basic.html
// Noise based RNG
// https://www.gdcvault.com/play/1024365/Math-for-Game-Programmers-Noise


namespace dmNoise
{
    static inline float Clampf(float a, float b, float v)
    {
        return v < a ? a : (v > b ? b : v);
    }

    static inline int Clampi(int a, int b, int v)
    {
        return v < a ? a : (v > b ? b : v);
    }

    static inline float Smoothstep(float a, float b, float v)
    {
        float t = Clampf(0.0f, 1.0f, (v - a)/(b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    static inline float Mix(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    // Squirrel3: https://www.gdcvault.com/play/1024365/Math-for-Game-Programmers-Noise
    // by @Squirrel
    uint32_t Noise_Squirrel3(int x, uint32_t seed)
    {
        const uint32_t PRIME1 = 0xB5297A4D;
        const uint32_t PRIME2 = 0x68E31DA4;
        const uint32_t PRIME3 = 0x1B56C4E9;

        uint32_t h = (uint32_t)x;
        h *= PRIME1;
        h += seed;
        h ^= (h >> 8);
        h += PRIME2;
        h ^= (h << 8);
        h *= PRIME3;
        h ^= (h >> 8);
        return h;
    }

    // https://github.com/Cyan4973/xxHash
    static const uint32_t XXH_PRIME32_1 = 2654435761U;
    static const uint32_t XXH_PRIME32_2 = 2246822519U;
    static const uint32_t XXH_PRIME32_3 = 3266489917U;
    static const uint32_t XXH_PRIME32_4 =  668265263U;
    static const uint32_t XXH_PRIME32_5 =  374761393U;

    static uint32_t Noise_xxH32(int x, uint32_t seed)
    {
        uint32_t h32 = x + XXH_PRIME32_5;
        h32 += seed;
        h32 ^= h32 >> 15;
        h32 *= XXH_PRIME32_2;
        h32 ^= h32 >> 13;
        h32 *= XXH_PRIME32_3;
        h32 ^= h32 >> 16;
        return h32;
    }

    uint32_t Noise1D(int x, uint32_t seed)
    {
        //return Noise_Squirrel3(x, seed);
        return Noise_xxH32(x, seed);
    }

    uint32_t Noise2D(int x, int y, uint32_t seed)
    {
        return Noise1D( x + (XXH_PRIME32_4 * y), seed);
    }

    uint32_t Noise3D(int x, int y, int z, uint32_t seed)
    {
        return Noise1D( x + (XXH_PRIME32_4 * y) + (XXH_PRIME32_1 * z), seed);
    }

    static uint32_t FloatToUint32(float f)
    {
        uint32_t xx;
        memcpy(&xx, &f, sizeof(f));
        return xx;
    }

    float Noise2Df(float x, float y, uint32_t seed)
    {
        // Based on Morgan McGuire @morgan3d
        // https://www.shadertoy.com/view/4dS3Wd
        int xi = floorf(x);
        int yi = floorf(y);
        float fracx = x - xi;
        float fracy = y - yi;
        float h0 = (Noise2D(xi+0, yi+0, seed) / (float)UINT_MAX);// * 2.0f - 1.0f;
        float h1 = (Noise2D(xi+1, yi+0, seed) / (float)UINT_MAX);// * 2.0f - 1.0f;
        float h2 = (Noise2D(xi+0, yi+1, seed) / (float)UINT_MAX);// * 2.0f - 1.0f;
        float h3 = (Noise2D(xi+1, yi+1, seed) / (float)UINT_MAX);// * 2.0f - 1.0f;

        float tx = fracx * fracx * (3.0f - 2.0f * fracx);
        float ty = fracy * fracy * (3.0f - 2.0f * fracy);

        return Mix(h0, h1, tx) + (h2 - h0) * ty * (1.0f - tx) + (h3 - h1) * tx * ty;
    }

    // static void printBits(uint32_t x)
    // {
    //     printf("X: 0x%08x\n", x);
    //     for (int bit = 31; bit >= 0; bit--) {
    //         uint32_t set = (1 << bit) & x;
    //         printf("%d", set?1:0);
    //     }
    //     printf("\n");
    // }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Fbm
    // https://www.iquilezles.org/www/articles/fbm/fbm.htm
    // https://www.shadertoy.com/view/4ttSWf by Inigo Quilez
    // https://www.shadertoy.com/view/4dS3Wd by Morgan McGuire
    // float Fbm_2D(uint32_t seed, float x, float y, float frequency, float lacunarity, float amplitude, float gain, int num_octaves)
    // {
    //     float sum = 0.0f;
    //     float sum_amp = 0.0f;
    //     for(int i = 0; i < num_octaves; ++i)
    //     {
    //         sum += Noise2Df(x*frequency, y*frequency, seed) * amplitude;
    //         frequency *= lacunarity;
    //         sum_amp += amplitude;
    //         amplitude *= gain;
    //     }
    //     return sum;// / sum_amp;

    //     // // H= [0, 1] -> G [0.5, 1.0]
    //     // float G = 0.5f;// exp2(-H); // gain
    //     // //float f = 1.0;
    //     // float f = frequency;
    //     // float a = 1.0;
    //     // float t = 0.0;
    //     // for( int i=0; i<num_octaves; i++ )
    //     // {
    //     //     t += a * Noise2Df(f*x, f*y, seed);
    //     //     f *= 2.0f;
    //     //     a *= G;
    //     // }
    //     // return t;
    // }

    float Fbm_2D(uint32_t seed, float x, float y, float frequency, float lacunarity, float amplitude, float gain, int num_octaves)
    {
        float sum = 0.0f;
        for(int i = 0; i < num_octaves; ++i)
        {
            sum += Noise2Df(x, y, seed) * amplitude;
            x *= frequency;
            y *= frequency;
            frequency *= lacunarity;
            amplitude *= gain;
        }
        return sum;
    }

// void Perturb1(int w, int h, float* noisef)
// {
//     int modify_type = g_NoiseParams.noise_modify_type;
//     for( int y = 0; y < w; ++y )
//     {
//         for( int x = 0; x < h; ++x )
//         {
//             jcn_real qx = fbm( x, y );
//             jcn_real qy = fbm( x + g_NoiseParams.perturb1_a1, y + g_NoiseParams.perturb1_a2 );
//             float n = fbm(x + g_NoiseParams.perturb1_scale * qx, y + g_NoiseParams.perturb1_scale * qy );
//             noisef[y*w + x] = ModifyValue(n, modify_type);
//         }
//     }
// }

}