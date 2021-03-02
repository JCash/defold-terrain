#pragma once

namespace dmTerrain
{
    void*   RawFileLoader_Init(const char* path);
    void    RawFileLoader_Exit(void* ctx);
    void    RawFileLoader_Load(void* ctx, struct TerrainPatch* patch);
}