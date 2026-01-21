#pragma once
#include <dmsdk/sdk.h>
#include <dmsdk/dlib/atomic.h>
#include <dmsdk/resource/resource.h>
#include <dmsdk/gameobject/gameobject.h>

#include "rng.h"

namespace dmGameSystem
{
    struct BufferResource;
}

namespace dmTerrain {

    typedef Vectormath::Aos::Vector3 Vector3;
    typedef Vectormath::Aos::Matrix4 Matrix4;

    enum TerrainEvents
    {
        TERRAIN_PATCH_INIT,
        TERRAIN_PATCH_HIDE,
        TERRAIN_PATCH_SHOW,
    };

    enum PatchState
    {
        PS_UNLOADED,
        PS_LOADING,
        PS_LOADED,
        PS_UNLOADING,
    };

    struct TerrainPatch;

    struct DM_ALIGNED(16) TerrainPatch
    {
        Vector3                         m_Position;
        uint16_t*                       m_Heightmap;
        dmGameSystem::BufferResource*   m_Resource;
        dmhash_t                        m_PathHash; // Resource path hash
        dmGameObject::HInstance         m_Instance;
        dmGameObject::HComponent        m_MeshComponent;

        dmhash_t                        m_InstanceId; // The id of the spawned game object

        dmBuffer::HBuffer   m_Buffer;       // The buffer with all the vertex data
        dmRng::Rng          m_Rng;          // A random seed generator, seed derived from the world seed
        uint32_t            m_HeightSeed;   // The same for all patches, making it easy to query the height
        uint16_t            m_HeightMin;
        uint16_t            m_HeightMax;
        int                 m_XZ[2];        // Unit coords (world space). First patch is (0,0), second is (1,0)
        uint32_t            m_Id:8;         // A unique id to separate the patch from all the other patches.
        uint32_t            m_Lod:4;
        uint32_t            m_Generate:1;   // 0 = load from file, 1 = Generate through noise
        uint32_t            :19;

        // PatchState
        int32_atomic_t      m_State;
        int32_atomic_t      m_DataState;
        int32_atomic_t      m_LuaCallback;  // 1 = Lua callback occurred
    };

    typedef struct TerrainWorld* HTerrain;

    struct InitParams
    {
        int     m_BasePatchSize; // must be power of two
        Matrix4 m_View; // Camera position
        Matrix4 m_Proj; // Used for frustum culling (later on)

        HResourceFactory            m_Factory;
        dmGameObject::HCollection   m_Collection;
        dmMessage::URL              m_PatchFactoryUrl;

        void (*m_Callback)(TerrainEvents event, TerrainPatch* patch);
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

    void DebugPrint(HTerrain terrain);
}
