#pragma once
#include <dmsdk/sdk.h>
#include <dmsdk/dlib/align.h>
#include "terrain.h"
#include "rng.h"

namespace dmTerrain {

    struct DM_ALIGNED(16) TerrainPatchLod
    {
        TerrainPatch m_Patches[9];

        int m_CameraXZ[2]; // The camera pos in patch space
        bool m_PatchesOccupied[9];
    };

    enum TerrainWorkType
    {
        TERRAIN_WORK_LOAD,
        TERRAIN_WORK_UNLOAD,
        TERRAIN_WORK_RELOAD,
    };

    struct TerrainWork
    {
        TerrainWorkType m_Type;
        TerrainPatch*   m_Patch;
        uint32_t        m_IsDone:1;
        uint32_t        m_Generate:1; // 1 = generate via noise
    };

    const uint32_t NUM_LOD_LEVELS = 1; // Todo: make this configurable

    struct DM_ALIGNED(16) TerrainWorld
    {
        Matrix4 m_View;     // View matrix
        Matrix4 m_Proj;     // Used for frustum culling (later on)
        Vector3 m_CameraPos;// Camera position

        TerrainPatchLod m_Terrain[NUM_LOD_LEVELS];

        dmRng::Rng m_Rng;

        dmArray<TerrainWork> m_Work;

        void* m_LoaderContext;

        void (*m_Callback)(TerrainEvents event, const TerrainPatch* patch);
    };

    typedef TerrainWorld* HTerrain;

}