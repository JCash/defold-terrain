#include <dmsdk/dlib/log.h>
#include "terrain_private.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

namespace dmTerrain
{
    struct TerrainFileLoader
    {
        FILE* m_File;
        uint32_t m_ImageSize;
        uint32_t m_BytesPerPixel; // 2 for 16 bit height data
    };

    // Assumes raw format, square size, no header
    static bool GuessSize(const char* path, int filesize, uint32_t* bpp, uint32_t* size)
    {
        const char* end = strrchr(path, '.');

        *bpp = 0;
        if (end != 0)
        {
            if (strcmp(end, ".r16") == 0)
                *bpp = 2;
            else if (strcmp(end, ".r32") == 0)
                *bpp = 4;
        }

        if (*bpp != 0)
        {
            *size = sqrt(filesize / *bpp);
        } else
        {
            for (int i = 1; i <= 4; ++i)
            {
                *bpp = i;
                *size = sqrt(filesize / i);
                if ( (*size * *size * *bpp) == filesize )
                {
                    break;
                }

            }
        }

        uint32_t total_size = (*bpp * *size * *size);
        //dmLogWarning("MAWE: bpp: %u  size: %u  -> total size: %u   filesize: %u", *bpp, *size, total_size, filesize);
        return filesize == total_size;
    }

    void* RawFileLoader_Init(const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (!f)
        {
            dmLogError("Failed to open '%s' for reading.", path);
            return 0;
        }

        // guess size
        fseek(f, 0, SEEK_END);
        uint32_t filesize = (uint32_t)ftell(f);

        uint32_t bpp;
        uint32_t size;
        if (!GuessSize(path, filesize, &bpp, &size)) {
            dmLogError("Failed to guess image size of '%s' from file size %u", path, filesize);
            fclose(f);
            return 0;
        }

        fseek(f, 0, SEEK_SET);

        dmLogInfo("Loaded '%s': bpp: %u  size: %u", path, bpp, size);

        TerrainFileLoader* loader = new TerrainFileLoader;
        loader->m_File = f;
        loader->m_BytesPerPixel = bpp;
        loader->m_ImageSize = size;

        return loader;
    }

    void RawFileLoader_Exit(void* ctx)
    {
        if (ctx) {
            assert(ctx);
            TerrainFileLoader* loader = (TerrainFileLoader*)ctx;
            fclose(loader->m_File);
            delete loader;
        }
    }

    void RawFileLoader_Load(void* ctx, struct TerrainPatch* patch)
    {
        assert(ctx);
        TerrainFileLoader* loader = (TerrainFileLoader*)ctx;

    }


}