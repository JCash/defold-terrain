#pragma once
#include <dmsdk/sdk.h>

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
        dmBuffer::HBuffer   m_Buffer;
        Vector3             m_Position;
        int                 m_XZ[2];
        uint32_t            m_Lod:4;
        uint32_t            m_IsLoaded:1;
        uint32_t            :27;
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