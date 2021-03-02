#include <stdio.h>
#include <limits.h>
#include <math.h>
#include "noise.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"


int IMG_SIZE = 128;
int SEED = 1234567;

int PATCH_X = -1;
int PATCH_Z = 0;

int idx(int x, int y, int size)
{
    return y * size * 3 + x * 3;
}

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

float Noise2Df(float x, float y, uint32_t seed)
{
    // Based on Morgan McGuire @morgan3d
    // https://www.shadertoy.com/view/4dS3Wd
    int xi = floorf(x);
    int yi = floorf(y);
    float fracx = x - xi;
    float fracy = y - yi;
    float h0 = (dmNoise::Noise2D(xi+0, yi+0, seed) / (float)UINT_MAX);
    float h1 = (dmNoise::Noise2D(xi+1, yi+0, seed) / (float)UINT_MAX);
    float h2 = (dmNoise::Noise2D(xi+0, yi+1, seed) / (float)UINT_MAX);
    float h3 = (dmNoise::Noise2D(xi+1, yi+1, seed) / (float)UINT_MAX);

    // float x1 = Mix(h0, h1, Smoothstep(0.0f, 1.0f, fracx));
    // float x2 = Mix(h2, h3, Smoothstep(0.0f, 1.0f, fracx));
    // float v = Mix(x1, x2, Smoothstep(0.0f, 1.0f, fracy));


    float tx = fracx * fracx * (3.0f - 2.0f * fracx);
    float ty = fracy * fracy * (3.0f - 2.0f * fracy);

    float v = Mix(h0, h1, tx) + (h2 - h0) * ty * (1.0f - tx) + (h3 - h1) * tx * ty;

    // Simple 2D lerp using smoothstep envelope between the values.
    // return vec3(mix(
            //          mix(a, b, smoothstep(0.0, 1.0, f.x)),
            //          mix(c, d, smoothstep(0.0, 1.0, f.x)),
    //          smoothstep(0.0, 1.0, f.y)));

    // if (v < 0) {
    //     printf("FBM: xy: %.2f %.2f  fbm: %f\n", x, y, v);
    // }

    printf("xy: %.2f %.2f  frac: %.3f %.3f  h: %.3f %.3f %.3f %.3f", x, y, fracx, fracy, h0, h1, h2, h3);
    //printf(" tx/y: %.3f %.3f  fbm: %f", tx, ty, v);
    //printf(" x1/x2: %.3f %.3f  fbm: %f", x1, x2, v);
    printf("\n");

    return v;
}

void MakeNoise(uint8_t* buffer, int size)
{
    for (int z = 0; z < size; ++z)
    {
        float v = z / (float)size;
        for (int x = 0; x < size; ++x)
        {
            float u = x / (float)size;
            float n = Noise2Df(PATCH_X + u, PATCH_Z + v, SEED);

            printf("uv: %.3f %.3f  noise: %f\n\n", u, v, n);
            int i = idx(x,z,size);
            buffer[i+0] = (uint8_t)(n * 255.0f);
            buffer[i+1] = buffer[i+2] = buffer[i+0];
        }
    }
}

float Fbm_2D_1(uint32_t seed, float x, float y, float frequency, float lacunarity, float amplitude, float gain, int num_octaves)
{
    float sum = 0.0f;
    for(int i = 0; i < num_octaves; ++i)
    {
        sum += Noise2Df(x*frequency, y*frequency, seed) * amplitude;
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return sum;
}

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

float Fbm_2D_(uint32_t seed, float x, float y, float frequency, float lacunarity, float amplitude, float gain, int num_octaves)
{
    float v = 0.0f;
    float shift = 100.0f;
    //vec2 shift = vec2(100.0);
    // Rotate to reduce axial bias
    // mat2 rot = mat2(cos(0.5), sin(0.5),
    //                 -sin(0.5), cos(0.50));
    for (int i = 0; i < num_octaves; ++i) {
        //v += a * noise(_st);
        v += amplitude * Noise2Df(x, y, seed);
        //_st = rot * _st * 2.0 + shift;
        x = x * frequency + shift;
        y = y * frequency + shift;
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return v;
}


void MakeFbm(uint8_t* buffer, int size)
{
    int num_octaves = 6;
    float frequency = 1.0f;
    float lacunarity = 1.5f;
    float amplitude = 0.5f;
    float gain = 0.5f;

    float max_n = -10000.0f;
    float min_n = 10000.0f;

    for (int z = 0; z < size; ++z)
    {
        float v = z / (float)size;
        for (int x = 0; x < size; ++x)
        {
            float u = x / (float)size;
            float n = Fbm_2D(SEED, PATCH_X + u, PATCH_Z + v, frequency, lacunarity, amplitude, gain, num_octaves);

            printf("uv: %.3f %.3f  fbm: %f\n", PATCH_X + u, PATCH_Z + v, n);

            if (n < min_n)
                min_n = n;
            if (n > max_n)
                max_n = n;

            int i = idx(x,z,size);
            buffer[i+0] = (uint8_t)(n * 255.0f);
            buffer[i+1] = buffer[i+2] = buffer[i+0];
        }
    }
    printf("min/max: %.3f %.3f\n", min_n, max_n);
}


int main(int argc, char const *argv[])
{
    int size = IMG_SIZE;
    uint8_t* noise = new uint8_t[size*size*3];
    memset(noise, 0, size*size*3);

    uint8_t* fbmout = new uint8_t[size*size*3];
    memset(fbmout, 0, size*size*3);

    //MakeNoise(noise, size);

    MakeFbm(fbmout, size);

    const char* noisepath = "noise.tga";
    stbi_write_tga(noisepath, size, size, 3, noise);
    printf("Wrote %s\n", noisepath);

    const char* fbmpath = "fbm.tga";
    stbi_write_tga(fbmpath, size, size, 3, fbmout);
    printf("Wrote %s\n", fbmpath);


    delete[] noise;
    return 0;
}