#pragma once
#include <dmsdk/sdk.h>
#include <dmsdk/dlib/align.h>
#include <dmsdk/dlib/array.h>
#include <dmsdk/dlib/atomic.h>
#include <dmsdk/dlib/message.h>
#include <dmsdk/dlib/thread.h>
#include <dmsdk/dlib/condition_variable.h>

#include <dmsdk/gamesys/components/comp_collection_proxy.h>
#include <dmsdk/gamesys/components/comp_factory.h>

#include "terrain.h"
#include "rng.h"

namespace dmTerrain {

    const uint32_t NUM_LOD_LEVELS = 1; // Todo: make this configurable
    const uint32_t NUM_PATCHES = 9;
    const uint32_t NUM_TOTAL_PATCHES = NUM_LOD_LEVELS * NUM_PATCHES;

    struct DM_ALIGNED(16) TerrainPatchLod
    {
        TerrainPatch    m_Patches[NUM_PATCHES];
        int             m_CameraXZ[2]; // The camera pos in patch space
    };

    struct FlushCommand
    {
        dmGameObject::HInstance m_Instance;
        dmVMath::Point3         m_Position;
        dmhash_t                m_BufferPathHash;
        int32_atomic_t*         m_DataState;
    };

    struct DM_ALIGNED(16) TerrainWorld
    {
        Matrix4 m_View;     // View matrix
        Matrix4 m_Proj;     // Used for frustum culling (later on)
        Vector3 m_CameraPos;// Camera position
        Vector3 m_CameraDir;// Camera dir

        TerrainPatchLod m_Terrain[NUM_LOD_LEVELS];

        dmRng::Rng m_Rng;

        int32_atomic_t      m_ThreadActive;
        dmThread::Thread    m_Thread;
        dmMutex::HMutex     m_ThreadMutex;
        dmConditionVariable::HConditionVariable m_ThreadCondition;

        // Game object
        HResourceFactory                m_Factory;

        dmGameObject::HCollection       m_Collection;
        dmGameObject::HInstance         m_FactoryInstance;

        dmGameSystem::HFactoryWorld     m_PatchFactoryWorld;
        dmGameSystem::HFactoryComponent m_PatchFactory;

        // Main thread synchronization
        dmArray<FlushCommand>           m_FlushCommands;

        void* m_LoaderContext;

        void (*m_Callback)(TerrainEvents event, TerrainPatch* patch);
    };

    typedef TerrainWorld* HTerrain;

}
