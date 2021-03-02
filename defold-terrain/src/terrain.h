#pragma once
#include <dmsdk/sdk.h>
#include "rng.h"

namespace dmTerrain {

    typedef Vectormath::Aos::Vector3 Vector3;
    typedef Vectormath::Aos::Vector4 Vector4;
    typedef Vectormath::Aos::Matrix4 Matrix4;

    enum TerrainEvents
    {
        TERRAIN_PATCH_HIDE,
        TERRAIN_PATCH_SHOW,
    };

    struct TerrainPatch
    {
        uint16_t*           m_Heightmap;
        dmBuffer::HBuffer   m_Buffer;   // The buffer with all the vertex data
        Vector3             m_Position;
        dmRng::Rng          m_Rng; // A random seed generator, seed derived from the world seed
        uint32_t            m_HeightSeed; // The same for all patches, making it easy to query the height
        uint16_t            m_HeightMin;
        uint16_t            m_HeightMax;
        int                 m_XZ[2]; // Unit coors. First patch is (0,0), second is (1,0)
        uint32_t            m_Lod:4;
        uint32_t            m_IsLoaded:1;
        uint32_t            m_Generate:1; // 0 = load from file, 1 = Generate through noise
        uint32_t            :26;
    };

    typedef struct TerrainWorld* HTerrain;

    struct InitParams
    {
        int     m_BasePatchSize; // must be power of two
        Matrix4 m_View; // Camera position
        Matrix4 m_Proj; // Used for frustum culling (later on)

        void (*m_Callback)(TerrainEvents event, const TerrainPatch* patch);
    };
    struct UpdateParams
    {
        float   m_Dt;
        Matrix4 m_View; // Camera position
        Matrix4 m_Proj; // Used for frustum culling (later on)
    };

    HTerrain Create(const InitParams& params);
    void Update(HTerrain terrain, const UpdateParams& params);
    void Destroy(HTerrain terrain);

    // Helper functions
    int GetPatchSize(int lod);
    void WorldToPatchCoord(const Vector3& pos, uint32_t lod, int xz[2]);
    Vector3 PatchToWorldCoord(int xz[2], uint32_t lod);
}