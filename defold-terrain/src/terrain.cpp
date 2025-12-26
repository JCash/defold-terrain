#include <assert.h>
#include <dmsdk/dlib/math.h>
#include <dmsdk/dlib/time.h>
#include "terrain_private.h"
#include "loader_file.h"
#include "terrain.h"
#include "noise.h"
#include "rng.h"

namespace dmTerrain
{

static const dmhash_t VERTEX_STREAM_NAME_POSITION = dmHashString64("position");
static const dmhash_t VERTEX_STREAM_NAME_NORMAL = dmHashString64("normal");
static const dmhash_t VERTEX_STREAM_NAME_TEXCOORD = dmHashString64("texcoord");
static const dmhash_t VERTEX_STREAM_NAME_COLOR = dmHashString64("color");

static float HEIGHT_SCALE = 256.0f;
static float UNSIGNED_TO_HEIGHT_FACTOR = HEIGHT_SCALE / 65535.0f;

int PATCH_SIZES[NUM_LOD_LEVELS];

static void TerrainThread(void* ctx);
static void UpdatePatches(HTerrain terrain, Vector3 camera_pos);

int GetPatchSize(int lod)
{
    return PATCH_SIZES[lod];
}

void WorldToPatchCoord(const Vector3& pos, uint32_t lod, int xz[2])
{
    float size = GetPatchSize(lod);
    xz[0] = (int)floorf(pos.getX() / size);
    xz[1] = (int)floorf(pos.getZ() / size);
}

Vector3 PatchToWorldCoord(int xz[2], uint32_t lod)
{
    float size = GetPatchSize(lod);
    return Vector3(xz[0] * size, 0, xz[1] * size);
}


// https://stackoverflow.com/a/34960913/468516
// hartmann/gribbs
// void extract_planes_from_projmat(
//         const float mat[4][4],
//         float left[4], float right[4], float top[4], float bottom[4],
//         float near[4], float far[4])
// {
//     for (int i = 4; i--; ) left[i]      = mat[i][3] + mat[i][0];
//     for (int i = 4; i--; ) right[i]     = mat[i][3] - mat[i][0];
//     for (int i = 4; i--; ) bottom[i]    = mat[i][3] + mat[i][1];
//     for (int i = 4; i--; ) top[i]       = mat[i][3] - mat[i][1];
//     for (int i = 4; i--; ) near[i]      = mat[i][3] + mat[i][2];
//     for (int i = 4; i--; ) far[i]       = mat[i][3] - mat[i][2];
// }

static inline void SetVertex(const Vector3& p, const Vector3& n, const uint8_t* c,
                            float* positions, float* normals, uint8_t* colors)
{
    positions[0] = p.getX();
    positions[1] = p.getY();
    positions[2] = p.getZ();

    normals[0] = n.getX();
    normals[1] = n.getY();
    normals[2] = n.getZ();

    colors[0] = c[0];
    colors[1] = c[1];
    colors[2] = c[2];
}

static void GetStreams(dmBuffer::HBuffer buffer,
                float*& positions, uint32_t& positions_stride,
                float*& normals, uint32_t& normals_stride,
                uint8_t*& colors, uint32_t& colors_stride)
{

    void* out_bytes = 0;
    uint32_t out_size = 0;
    dmBuffer::GetBytes(buffer, &out_bytes, &out_size);
    //printf("Buffer is %u bytes (%u mb)\n", out_size, out_size/(1024*1024));

    uint32_t positions_count; uint32_t positions_components;
    uint32_t normals_count; uint32_t normals_components;
    uint32_t colors_count; uint32_t colors_components;

    dmBuffer::Result r = dmBuffer::GetStream(buffer, VERTEX_STREAM_NAME_POSITION, (void**)&positions, &positions_count, &positions_components, &positions_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", dmHashReverseSafe64(VERTEX_STREAM_NAME_NORMAL), dmBuffer::GetResultString(r), r);
        return;
    }
    r = dmBuffer::GetStream(buffer, VERTEX_STREAM_NAME_NORMAL, (void**)&normals, &normals_count, &normals_components, &normals_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", dmHashReverseSafe64(VERTEX_STREAM_NAME_NORMAL), dmBuffer::GetResultString(r), r);
        return;
    }
    r = dmBuffer::GetStream(buffer, VERTEX_STREAM_NAME_COLOR, (void**)&colors, &colors_count, &colors_components, &colors_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", dmHashReverseSafe64(VERTEX_STREAM_NAME_COLOR), dmBuffer::GetResultString(r), r);
        return;
    }
}

// static void FillFlatBuffer(uint32_t patch_size, float* positions, uint32_t positions_stride,
//                                                 float* normals, uint32_t normals_stride,
//                                                 float* texcoords, uint32_t texcoords_stride,
//                                                 uint8_t* colors, uint32_t colors_stride)
// {
//     float scale = 1;
//     for (uint32_t x = 0; x <= patch_size-1; ++x)
//     {
//         for (uint32_t z = 0; z <= patch_size-1; ++z)
//         {
//             Vector3 v0 = Vector3(  x, 0, z) * scale;
//             Vector3 v1 = Vector3(  x, 0, z+1) * scale;
//             Vector3 v2 = Vector3(x+1, 0, z+1) * scale;
//             Vector3 v3 = Vector3(x+1, 0, z) * scale;

//             Vector3 n0 = Vector3(0,1,0);
//             Vector3 n1 = Vector3(0,1,0);
//             Vector3 n2 = Vector3(0,1,0);
//             Vector3 n3 = Vector3(0,1,0);
//             Vector3 uv0 = Vector3(0,1,0);
//             Vector3 uv1 = Vector3(0,1,0);
//             Vector3 uv2 = Vector3(0,1,0);
//             Vector3 uv3 = Vector3(0,1,0);
//             Vector4 col0 = Vector4(1,1,1,1);
//             Vector4 col1 = Vector4(1,1,1,1);
//             Vector4 col2 = Vector4(1,1,1,1);
//             Vector4 col3 = Vector4(1,1,1,1);

//             #define INCREMENT_STRIDE() positions += positions_stride; normals += normals_stride; texcoords += texcoords_stride; colors += colors_stride;

//             SetVertex(v0, n0, uv0, col0, positions, normals, texcoords, colors); INCREMENT_STRIDE();
//             SetVertex(v1, n1, uv1, col1, positions, normals, texcoords, colors); INCREMENT_STRIDE();
//             SetVertex(v2, n2, uv2, col2, positions, normals, texcoords, colors); INCREMENT_STRIDE();

//             SetVertex(v2, n2, uv2, col2, positions, normals, texcoords, colors); INCREMENT_STRIDE();
//             SetVertex(v3, n3, uv3, col3, positions, normals, texcoords, colors); INCREMENT_STRIDE();
//             SetVertex(v0, n0, uv0, col0, positions, normals, texcoords, colors); INCREMENT_STRIDE();

//             #undef INCREMENT_STRIDE
//         }
//     }
// }

static float GenerateHeight(uint32_t seed, float x, float z)
{
    int num_octaves = 6;
    float frequency = 1.5f;
    float lacunarity = 1.2f;
    float amplitude = 0.5f;
    float gain = 0.5f;

    return dmNoise::Fbm_2D(seed, x, z, frequency, lacunarity, amplitude, gain, num_octaves);
}

static inline float Clampf(float a, float b, float v)
{
    return v < a ? a : (v > b ? b : v);
}

static inline int Clampi(int a, int b, int v)
{
    return v < a ? a : (v > b ? b : v);
}

// Coord range (-1,-1), (patch_size+1, patch_size+1)
static float GetHeight(TerrainPatch* patch, int x, int z)
{
    x++;
    z++;
    uint32_t patch_size = GetPatchSize(patch->m_Lod);
    // x = Clampi(0, patch_size+1, x);
    // z = Clampi(0, patch_size+1, z);

    uint32_t idx = z * (patch_size+3) + x;
    uint32_t uh = patch->m_Heightmap[idx];
    float h = uh * UNSIGNED_TO_HEIGHT_FACTOR;

//printf("get height %u %u:  %f  uh: %u   idx: %u\n", x, z, h, uh, idx);
    return h;
}

static Vector3 GetNormal(TerrainPatch* patch, int x, int z)
{
    float x_a = GetHeight(patch, x-1, z);
    float x_b = GetHeight(patch, x+1, z);
    float z_a = GetHeight(patch, x, z-1);
    float z_b = GetHeight(patch, x, z+1);

    float xz_scale = 1.0f;
    Vector3 n(x_b - x_a, 2 * xz_scale, z_b - z_a);
    return normalize(n);
}

struct TimerScope
{
    TimerScope(const char* name) {
        m_Name = name;
        m_TimeStart = dmTime::GetTime();
    }
    ~TimerScope() {
        uint64_t time_end = dmTime::GetTime();
        printf("Scope '%s' took %f ms\n", m_Name, (time_end - m_TimeStart)/1000.0f);
    }
    const char* m_Name;
    uint64_t m_TimeStart;
};

static bool GeneratePatchHeights(TerrainPatch* patch)
{
    TimerScope tscope(__FUNCTION__);

    uint32_t seed = patch->m_HeightSeed;
    float wx = patch->m_XZ[0];
    float wz = patch->m_XZ[1];

    int patch_size = GetPatchSize(0);

    int num_verts = patch_size+1;
    float oo_patch_size_f = 1.0f / patch_size;

    int size = num_verts+2; // we have an extra border in order to get correct normal values
    if (patch->m_Heightmap == 0)
        patch->m_Heightmap = new uint16_t[size * size];

    patch->m_HeightMin = 65535;
    patch->m_HeightMax = 0;


    for (int x = -1; x < num_verts+1; ++x)
    {
        for (int z = -1; z < num_verts+1; ++z)
        {
            float u = x * oo_patch_size_f;
            float v = z * oo_patch_size_f;
            float h = GenerateHeight(seed, wx + u, wz + v);
            h = Clampf(0.0f, 1.0f, h);

            //printf("x/z %d %d  uv: %f %f   h: %f\n", x, z, u, v, h * HEIGHT_SCALE);

            // if (patch->m_XZ[0] == 0 && patch->m_XZ[1] == 0)
            // {
            //     h = 1.0f;
            // }
            // else if (patch->m_XZ[0] == -1 && patch->m_XZ[1] == 0)
            // {
            //     h = x / (float)(size-1);
            // }
            // else if (patch->m_XZ[0] == 1 && patch->m_XZ[1] == 0)
            // {
            //     h = 1.0f - x / (float)(size-1);
            // }
            // else if (patch->m_XZ[0] == 0 && patch->m_XZ[1] == -1)
            // {
            //     h = z / (float)(size-1);
            // }
            // else if (patch->m_XZ[0] == 0 && patch->m_XZ[1] == 1)
            // {
            //     h = 1.0f - z / (float)(size-1);
            // }


            uint16_t uh = (uint16_t)(h * 65535);
            patch->m_Heightmap[(z+1) * size + (x+1)] = uh;

            //if (z == -1)
            //    printf("height: %f  uh: %u   idx: %u  u/v: %f %f\n", h, uh, z * size + x, u, v);

            if (uh < patch->m_HeightMin)
                patch->m_HeightMin = uh;

            if (uh > patch->m_HeightMax)
                patch->m_HeightMax = uh;
        }
    }

    //dmAtomicStore32(&patch->m_IsDataLoaded, 1);

    //printf("XZ: %d %d\n", patch->m_XZ[0], patch->m_XZ[1]);
    //printf("HEIGHT min/max: %f %f\n", height_min, height_max);
    return true;
}

static bool GenerateVertexData(TerrainPatch* patch)
{
    TimerScope tscope(__FUNCTION__);

    float* positions; uint32_t positions_stride;
    float* normals; uint32_t normals_stride;
    uint8_t* colors; uint32_t colors_stride;
    GetStreams(patch->m_Buffer,
                positions, positions_stride,
                normals, normals_stride,
                colors, colors_stride);


    uint32_t patch_size = GetPatchSize(0);
    float oo_patch_size_f = 1.0f / patch_size;
    uint32_t step_size = 1;
    float wx = patch->m_XZ[0];
    float wz = patch->m_XZ[1];

    //printf("******************************************\n");
    //printf("XZ: %d %d  patch_size: %u\n", patch->m_XZ[0], patch->m_XZ[1], patch_size);

    uint32_t world_size = 65536;

    float scale = 1;
    for (uint32_t x = 0; x <= patch_size-1; ++x)
    {
        for (uint32_t z = 0; z <= patch_size-1; ++z)
        {
            uint32_t x0 = x;
            uint32_t x1 = x + step_size;
            uint32_t z0 = z;
            uint32_t z1 = z + step_size;

            float u0 = x * oo_patch_size_f;
            float u1 = (x+1) * oo_patch_size_f;
            float v0 = z * oo_patch_size_f;
            float v1 = (z+1) * oo_patch_size_f;

            //printf("uv: %f %f  %f %f\n", u0, v0, u1, v1);

            float h0 = GetHeight(patch, x,     z);
            float h1 = GetHeight(patch, x,     z + 1);
            float h2 = GetHeight(patch, x + 1, z + 1);
            float h3 = GetHeight(patch, x + 1, z);

            Vector3 p0 = Vector3(x0, h0, z0) * scale;
            Vector3 p1 = Vector3(x0, h1, z1) * scale;
            Vector3 p2 = Vector3(x1, h2, z1) * scale;
            Vector3 p3 = Vector3(x1, h3, z0) * scale;

            Vector3 n0 = GetNormal(patch, x,     z);
            Vector3 n1 = GetNormal(patch, x,     z + 1);
            Vector3 n2 = GetNormal(patch, x + 1, z + 1);
            Vector3 n3 = GetNormal(patch, x + 1, z);

            uint8_t col0[3] = {255,255,255};
            uint8_t col1[3] = {255,255,255};
            uint8_t col2[3] = {255,255,255};
            uint8_t col3[3] = {255,255,255};

            #define INCREMENT_STRIDE() positions += positions_stride; normals += normals_stride; colors += colors_stride;

            SetVertex(p0, n0, col0, positions, normals, colors); INCREMENT_STRIDE();
            SetVertex(p1, n1, col1, positions, normals, colors); INCREMENT_STRIDE();
            SetVertex(p2, n2, col2, positions, normals, colors); INCREMENT_STRIDE();

            SetVertex(p2, n2, col2, positions, normals, colors); INCREMENT_STRIDE();
            SetVertex(p3, n3, col3, positions, normals, colors); INCREMENT_STRIDE();
            SetVertex(p0, n0, col0, positions, normals, colors); INCREMENT_STRIDE();

            #undef INCREMENT_STRIDE
        }
    }

    return true;
}

static void CreateBuffer(dmBuffer::HBuffer* buffer, uint32_t num_steps)
{
    dmBuffer::StreamDeclaration streams_decl[] = {
        {VERTEX_STREAM_NAME_POSITION, dmBuffer::VALUE_TYPE_FLOAT32, 3},
        {VERTEX_STREAM_NAME_NORMAL, dmBuffer::VALUE_TYPE_FLOAT32, 3},
        {VERTEX_STREAM_NAME_COLOR, dmBuffer::VALUE_TYPE_UINT8, 3},
    };

    // num triangles: num_quads * num_triangles per quad * num vertices per triangle
    uint32_t element_count = (num_steps*num_steps) * 2 * 3;

    dmBuffer::Result r = dmBuffer::Create(element_count, streams_decl, sizeof(streams_decl)/sizeof(dmBuffer::StreamDeclaration), buffer);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to create buffer: %s (%d)", dmBuffer::GetResultString(r), r);
        return;
    }
}

static void PatchSetState(TerrainPatch* patch, PatchState state)
{
    if (state == PS_UNLOADED)
    {
        patch->m_XZ[0] = patch->m_XZ[1] = 100000;
    }

    dmAtomicStore32(&patch->m_DataState, 0);
    dmAtomicStore32(&patch->m_LuaCallback, 0);
    dmAtomicStore32(&patch->m_State, state);
}

static void PatchLoad(HTerrain terrain, TerrainPatch* patch, int x, int z)
{
    patch->m_XZ[0] = x;
    patch->m_XZ[1] = z;
    patch->m_Position = PatchToWorldCoord(patch->m_XZ, patch->m_Lod);

    PatchSetState(patch, PS_LOADING);

    printf("Loading %d, %d  %p\n", patch->m_XZ[0], patch->m_XZ[1], patch);
}

static void PatchUnload(HTerrain terrain, TerrainPatch* patch)
{
    PatchSetState(patch, PS_UNLOADING);
    printf("Unloading %d, %d  %p\n", patch->m_XZ[0], patch->m_XZ[1], patch);
}

// Return false when not finished. Return true when finished with this state
static bool DoPatchLoad(HTerrain terrain, TerrainPatch* patch)
{
    (void)terrain;

    if (patch->m_Generate)
    {
        int data_state = dmAtomicGet32(&patch->m_DataState);
        if (0 == data_state)
        {
            bool result = GeneratePatchHeights(patch);
            if (result)
                dmAtomicIncrement32(&patch->m_DataState);
            return false;
        }
        else if (1 == data_state)
        {
            bool result = GenerateVertexData(patch);
            if (result)
                dmAtomicIncrement32(&patch->m_DataState);
            return false;
        }
        return true;
    }

    dmBuffer::Result r = dmBuffer::ValidateBuffer(patch->m_Buffer);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to validate buffer: %s (%d)", dmBuffer::GetResultString(r), r);
        return true;
    }

    return true;
}

static bool DoPatchUnload(HTerrain terrain, TerrainPatch* patch)
{
    int data_state = dmAtomicGet32(&patch->m_DataState);
    if (0 == data_state)
    {
        DM_MUTEX_SCOPED_LOCK(terrain->m_ThreadMutex);

        terrain->m_Callback(TERRAIN_PATCH_HIDE, patch);

        dmAtomicIncrement32(&patch->m_DataState);
        return false;
    }

    int callback_done = dmAtomicGet32(&patch->m_LuaCallback);
    return callback_done != 0;
}


static void PatchDelete(TerrainPatch* patch)
{
    delete[] patch->m_Heightmap;
}

// // Coords in [-1,1] range (i.e. around the camera)
// static bool IsPatchOccupied(TerrainPatchLod* patch_lod, int x, int z)
// {
//     return patch_lod->m_PatchesOccupied[(z+1)*3 + (x+1)];
// }
// // Coords in [-1,1] range (i.e. around the camera)
// static void SetPatchOccupied(TerrainPatchLod* patch_lod, int x, int z, bool loaded)
// {
//     //printf("SetPatchOccupied %d %d: %d\n", x, z, (int)loaded);
//     patch_lod->m_PatchesOccupied[(z+1)*3 + (x+1)] = loaded;
// }

HTerrain Create(const InitParams& params)
{
    assert(params.m_Callback != 0);

    int base_size = params.m_BasePatchSize;
    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        PATCH_SIZES[lod] = base_size;
        base_size *= 2;
    }

    HTerrain terrain = new TerrainWorld;

    terrain->m_Callback = params.m_Callback;
    terrain->m_View = params.m_View;
    terrain->m_Proj = params.m_Proj;

    uint32_t terrain_seed = 1234567;
    dmRng::Init(&terrain->m_Rng, terrain_seed);

    Vector3 camera_pos = (terrain->m_View.getCol(3) * -1).getXYZ();

    // Number of steps to divide
    int num_divides = GetPatchSize(0);

    // Initialize patches
    for (int lod = 0, id = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        TerrainPatchLod* patch_lod = &terrain->m_Terrain[lod];

        WorldToPatchCoord(camera_pos, lod, patch_lod->m_CameraXZ);

        for (int x = -1, i = 0; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z, ++i, ++id)
            {
                //SetPatchOccupied(patch_lod, x, z, false);

                TerrainPatch* patch = &patch_lod->m_Patches[i];
                memset(patch, 0, sizeof(*patch));

                patch->m_Id = id; // debug only
                patch->m_HeightSeed = terrain_seed; // duplicate, but makes it easier to access on threads
                patch->m_Lod = lod;
                patch->m_Generate = 1; // pass in option for this in the init function

                CreateBuffer(&patch->m_Buffer, num_divides);

                PatchSetState(patch, PS_UNLOADED);

                // dmRng::Init(&patch->m_Rng, dmRng::RandU32(&terrain->m_Rng));
            }
        }
    }

    terrain->m_LoaderContext = 0;
    //terrain->m_LoaderContext = RawFileLoader_Init("/Users/mawe/work/projects/users/mawe/defold-terrain/data/heightmap.r16");

    dmAtomicStore32(&terrain->m_ThreadActive, 1);
    terrain->m_ThreadMutex = dmMutex::New();
    terrain->m_Thread = dmThread::New(TerrainThread, 0x80000, terrain, "terrain");
    terrain->m_ThreadCondition = dmConditionVariable::New();

    return terrain;
}


void Destroy(HTerrain terrain)
{
    if (terrain->m_LoaderContext)
        RawFileLoader_Exit(terrain->m_LoaderContext);

    // Exit the thread
    dmAtomicStore32(&terrain->m_ThreadActive, 0);

    dmMutex::Lock(terrain->m_ThreadMutex);
    dmConditionVariable::Signal(terrain->m_ThreadCondition);
    dmMutex::Unlock(terrain->m_ThreadMutex);

    // wait for it
    dmThread::Join(terrain->m_Thread);

    dmConditionVariable::Delete(terrain->m_ThreadCondition);
    dmMutex::Delete(terrain->m_ThreadMutex);

    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        for (int i = 0; i < NUM_PATCHES; ++i)
        {
            TerrainPatch* patch = &terrain->m_Terrain[lod].m_Patches[i];
            PatchDelete(patch);
        }
    }

    delete terrain;
}

static void TerrainThread(void* ctx)
{
    TerrainWorld* terrain = (TerrainWorld*)ctx;
    while (dmAtomicGet32(&terrain->m_ThreadActive))
    {
        // Lock and sleep until signaled there is jobs queued up
        // DM_MUTEX_SCOPED_LOCK(terrain->m_ThreadMutex);
        // while(terrain->m_Work.Empty() && terrain->m_ThreadActive)
        // {
        //     printf("Thread entering sleep...\n");
        //     fflush(stdout);
        //     dmConditionVariable::Wait(terrain->m_ThreadCondition, terrain->m_ThreadMutex);
        //     printf("Thread waking up!\n");
        //     fflush(stdout);
        // }
        // if (!terrain->m_ThreadActive)
        //     break;

        DM_MUTEX_SCOPED_LOCK(terrain->m_ThreadMutex);
        UpdatePatches(terrain, terrain->m_CameraPos);

        //FlushWorkQueue(terrain, terrain->m_Work);
    }

    printf("Thread exited!\n");
    fflush(stdout);
}

static inline int Sign(int value)
{
    return value > 0 ? 1 : -1;
}

static int ToIndex(int x, int z)
{
    int ix = x + 1;  // -1→0, 0→1, 1→2
    int iz = z + 1;  // -1→0, 0→1, 1→2
    return ix + 3 * iz;  // Row-major: 0-2 (z=-1), 3-5 (z=0), 6-8 (z=1)
}

static void ToXZ(int idx, int *out_x, int *out_z)
{
    int ix = idx % 3;   // 0..2
    int iz = idx / 3;   // 0..2

    *out_x = ix - 1;    // 0..2 -> -1..1
    *out_z = iz - 1;    // 0..2 -> -1..1
}

static int FindUnoccupied(const bool* occupied, int* out_x, int* out_z)
{
    for (int i = 0; i < NUM_PATCHES; ++i)
    {
        if (!occupied[i])
        {
            ToXZ(i, out_x, out_z);
            return i;
        }
    }
    return -1;
}

// mark patches as discarded
// Allow empty patches to load
static void UpdatePatches(HTerrain terrain, Vector3 camera_pos)
{
    // static int frame = 0;
    // frame++;

    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        TerrainPatchLod* patch_lod = &terrain->m_Terrain[lod];

        int camera_xz[2];
        {
            DM_MUTEX_SCOPED_LOCK(terrain->m_ThreadMutex);

            camera_xz[0] = patch_lod->m_CameraXZ[0];
            camera_xz[1] = patch_lod->m_CameraXZ[1];
        }

// TODO: Early out if the lod camera position hasn't moved

        // This let's us know if the patches directly surrounding the camera patch is occupied
        // Note that it doesn't guarantuee that the patch is available for use.
        bool occupied[NUM_PATCHES] = {false, false, false, false, false, false, false, false, false};
        bool some_empty = false;

        for (int i = 0; i < NUM_PATCHES; ++i)
        {
            TerrainPatch* patch = &patch_lod->m_Patches[i];

            int diffx = patch->m_XZ[0] - camera_xz[0];
            int diffz = patch->m_XZ[1] - camera_xz[1];
            if (dmMath::Abs(diffx) <= 1 && dmMath::Abs(diffz) <= 1)
            {
                int idx = ToIndex(diffx, diffz);
                occupied[idx] = true;
            }
            // else
            // {
            //     some_empty = true;
            // }
        }

        // if (some_empty)
        // {
        //     printf("occupied: ");
        //     for (int i = 0; i < NUM_PATCHES; ++i)
        //     {
        //         printf("%d ", occupied[i]);
        //     }
        //     printf(" (fr: %d)\n", frame);

        //     DebugPrint(terrain);
        // }

        for (int i = 0; i < NUM_PATCHES; ++i)
        {
            TerrainPatch* patch = &patch_lod->m_Patches[i];

            // Bounding box check

            // find out if it's more than one step away from the camera position
            int diffx = patch->m_XZ[0] - camera_xz[0];
            int diffz = patch->m_XZ[1] - camera_xz[1];

            // If the difference is positive, then the camera is moving in the positive direction
            // and the next patch position should be before the camera in that same direction
            int movex = dmMath::Abs(diffx) > 1 ? Sign(diffx) : 0;
            int movez = dmMath::Abs(diffz) > 1 ? Sign(diffz) : 0;

            // If the patch has moved away from the camera
            if (movex || movez)
            {
                int state = dmAtomicGet32(&patch->m_State);

                if (PS_UNLOADED == state) // It is free to load a new one
                {
                    // Time to load a new patch

                    // Find an unoccupied slot next to the camera
                    // TODO: Find an unoccupied slot in front of the camera first, as we want to load them first
                    int x, z;
                    int idx = FindUnoccupied(occupied, &x, &z);
                    if (idx >= 0)
                    {
                        occupied[idx] = true;
                        PatchLoad(terrain, patch, camera_xz[0] + x, camera_xz[1] + z);
                    }
                }
                else if (PS_LOADED == state)
                {
                    PatchUnload(terrain, patch);
                }
            }

            // update any loading/unloading state
            int state = dmAtomicGet32(&patch->m_State);

            if (PS_LOADING == state)
            {
                if (DoPatchLoad(terrain, patch))
                {
                    PatchSetState(patch, PS_LOADED);

                    DM_MUTEX_SCOPED_LOCK(terrain->m_ThreadMutex);
                    terrain->m_Callback(TERRAIN_PATCH_SHOW, patch);
                }
            }
            else if (PS_UNLOADING == state)
            {
                if (DoPatchUnload(terrain, patch))
                {
                    PatchSetState(patch, PS_UNLOADED);
                }
            }
        }
    }

    // for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    // {
    //     TerrainPatchLod* patch_lod = &terrain->m_Terrain[lod];

    //     // Since we want to prioritize the loading of the patch immediately in front of the camera
    //     // we want to sort the patches.
    //     // This index table allows us to iterate in a different order
    //     int idx[8*9] = {
    //         4, 5, 2, 8, 1, 7, 0, 3, 6,  // dir:  1, 0, 0  East
    //         4, 2, 1, 5, 0, 8, 3, 7, 6,  // dir:  1, 0,-1  NorthEast
    //         4, 1, 0, 2, 3, 5, 6, 7, 8,  // dir: 0, 0, -1  North
    //         4, 0, 1, 3, 6, 2, 7, 5, 8,  // dir: -1, 0,-1  NorthWest
    //         4, 3, 6, 0, 7, 1, 8, 5, 2,  // dir: -1, 0, 0  West
    //         4, 6, 3, 7, 0, 8, 1, 5, 2,  // dir: -1, 0, 1  SouthWest
    //         4, 7, 6, 8, 3, 5, 0, 1, 2,  // dir: 0, 0,  1  South
    //         4, 8, 7, 5, 6, 2, 3, 1, 0,  // dir:  1, 0, 1  SouthEast
    //     };

    //     const char* octantnames[8] = {
    //         "East",
    //         "NorthEast",
    //         "North",
    //         "NorthWest",
    //         "West",
    //         "SouthWest",
    //         "South",
    //         "SouthEast",
    //     };

    //     // figure out the quadrant that the camera direction is pointing at
    //     float camdirx = terrain->m_CameraDir.getX();
    //     float camdirz = -terrain->m_CameraDir.getZ(); // we want negative Z to be "north"
    //     float a = atan2f(camdirz, camdirx);
    //     int octant = int( 8 * a / (2*M_PI) + 8.5f ) % 8;

    //     //printf("octant: %s\n", octantnames[octant]);

    //     for (int i = 0; i < NUM_PATCHES; ++i)
    //     {
    //         int index = idx[octant * NUM_PATCHES + i];
    //         int x = index % 3 - 1;
    //         int z = index / 3 - 1;
    //         {
    //             if (!IsPatchOccupied(patch_lod, x, z))
    //             {
    //                 TerrainPatch* patch = AllocateFreePatch(terrain, lod);
    //                 if (!patch)
    //                     continue;

    //                 // move this patch
    //                 patch->m_XZ[0] = patch_lod->m_CameraXZ[0] + x;
    //                 patch->m_XZ[1] = patch_lod->m_CameraXZ[1] + z;

    //                 patch->m_Position = PatchToWorldCoord(patch->m_XZ, lod);

    //                 // printf("Loading patch: %d %d (%d %d)    pos: %.3f, %.3f  camera: %d %d\n",
    //                 //     patch->m_XZ[0], patch->m_XZ[1], x, z, patch->m_Position.getX(), patch->m_Position.getZ(),
    //                 //     patch_lod->m_CameraXZ[0], patch_lod->m_CameraXZ[1]);

    //                 PatchLoad(terrain, patch);

    //                 SetPatchOccupied(patch_lod, x, z, true);
    //             }
    //         }
    //     }
    // }
}

static float round_to_step(float x, float step)
{
    float scaled = x / step;
    float rounded = (floorf(scaled + 0.5f)) * step;
    return rounded;
}

static bool UpdateCameraPos(HTerrain terrain, dmVMath::Vector3& camera_pos)
{
    bool lods_need_update = false;
    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        TerrainPatchLod* patch_lod = &terrain->m_Terrain[lod];

        int camera_xz[2];
        WorldToPatchCoord(camera_pos, lod, camera_xz);

        int camera_diffx = camera_xz[0] - patch_lod->m_CameraXZ[0];
        int camera_diffz = camera_xz[1] - patch_lod->m_CameraXZ[1];

        bool camera_moved = (camera_diffx !=0) || (camera_diffz != 0);
        lods_need_update |= camera_moved;

        // if (camera_moved)
        // {
        //     dmLogWarning("Camera was at %d %d, moved to %d %d  diff: %d %d  sign: %d %d  pos: %.3f %.3f %.3f",
        //         patch_lod->m_CameraXZ[0], patch_lod->m_CameraXZ[1], camera_xz[0], camera_xz[1],
        //         camera_diffx, camera_diffz, Sign(camera_diffx), Sign(camera_diffz),
        //         camera_pos.getX(), camera_pos.getY(), camera_pos.getZ());
        // }
        if (camera_moved)
        {
            printf("Camera update: %d, %d\n", patch_lod->m_CameraXZ[0], patch_lod->m_CameraXZ[1]);
        }

        {
            DM_MUTEX_SCOPED_LOCK(terrain->m_ThreadMutex);
            patch_lod->m_CameraXZ[0] = camera_xz[0];
            patch_lod->m_CameraXZ[1] = camera_xz[1];
        }
    }
    return lods_need_update;
}

void Update(HTerrain terrain, const UpdateParams& params)
{
    terrain->m_View = params.m_View;

    Matrix4 invView = inverse(params.m_View);
    terrain->m_CameraDir = -invView.getCol(2).getXYZ();

// TODO: This is just a small hack to make the loading/unloading a little bit more stable,
// when camera is passing boundaries. Should replace with more logic when exiting the current camera's patch.
    dmVMath::Vector3 pos = invView.getCol(3).getXYZ();
    pos = dmVMath::Vector3(round_to_step(pos.getX(), 0.05f),
                           round_to_step(pos.getY(), 0.05f),
                           round_to_step(pos.getZ(), 0.05f));

    if (UpdateCameraPos(terrain, pos))
    {
        if (terrain->m_Thread)
            dmConditionVariable::Signal(terrain->m_ThreadCondition); // wake up thread if it wasn't already working
    }

    // For single threaded systems
    if (!terrain->m_Thread)
    {
        UpdatePatches(terrain, terrain->m_CameraPos);
    }
}


void DebugPrint(HTerrain terrain)
{
    DM_MUTEX_SCOPED_LOCK(terrain->m_ThreadMutex);

    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        TerrainPatchLod* patchlod = &terrain->m_Terrain[lod];
        printf("LOD %d: cam x/z: %d %d\n", lod, patchlod->m_CameraXZ[0], patchlod->m_CameraXZ[1]);

        for (int i = 0; i < NUM_PATCHES; ++i)
        {
            TerrainPatch* patch = &terrain->m_Terrain[lod].m_Patches[i];
            printf("  p %d: x/z: %d, %d  s: %d  ds: %d lua: %d  p: %p\n", i, patch->m_XZ[0], patch->m_XZ[1],
                    dmAtomicGet32(&patch->m_State),
                    dmAtomicGet32(&patch->m_DataState),
                    dmAtomicGet32(&patch->m_LuaCallback),
                    patch);
        }
    }

}

} // namespace
