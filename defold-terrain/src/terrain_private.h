#pragma once
#include <dmsdk/sdk.h>
#include "terrain.h"
#include "rng.h"

namespace dmTerrain {

    struct TerrainPatchLod
    {
        TerrainPatch m_Patches[9];
    };

    enum TerrainWorkType
    {
        TERRAIN_WORK_LOAD,
    };

    struct TerrainWork
    {
        TerrainWorkType m_Type;
        TerrainPatch*   m_Patch;
        uint32_t        m_IsDone:1;
        uint32_t        m_Generate:1; // 1 = generate via noise
    };

    const uint32_t NUM_LOD_LEVELS = 1; // Todo: make this configurable

    struct TerrainWorld
    {
        TerrainPatchLod m_Terrain[NUM_LOD_LEVELS];
        Matrix4 m_View; // Camera position
        Matrix4 m_Proj; // Used for frustum culling (later on)

        dmRng::Rng m_Rng;

        dmArray<TerrainWork> m_Work;

        void* m_LoaderContext;

        void (*m_Callback)(TerrainEvents event, const TerrainPatch* patch);
    };

    typedef TerrainWorld* HTerrain;

}