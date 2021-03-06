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
void extract_planes_from_projmat(
        const float mat[4][4],
        float left[4], float right[4], float top[4], float bottom[4],
        float near[4], float far[4])
{
    for (int i = 4; i--; ) left[i]      = mat[i][3] + mat[i][0];
    for (int i = 4; i--; ) right[i]     = mat[i][3] - mat[i][0];
    for (int i = 4; i--; ) bottom[i]    = mat[i][3] + mat[i][1];
    for (int i = 4; i--; ) top[i]       = mat[i][3] - mat[i][1];
    for (int i = 4; i--; ) near[i]      = mat[i][3] + mat[i][2];
    for (int i = 4; i--; ) far[i]       = mat[i][3] - mat[i][2];
}

static inline void SetVertex(const Vector3& p, const Vector3& n, const Vector3& uv, const Vector4& c,
                            float* positions, float* normals, float* texcoords, float* colors)
{
    positions[0] = p.getX();
    positions[1] = p.getY();
    positions[2] = p.getZ();

    normals[0] = n.getX();
    normals[1] = n.getY();
    normals[2] = n.getZ();

    texcoords[0] = uv.getX();
    texcoords[1] = uv.getY();

    colors[0] = c.getX();
    colors[1] = c.getY();
    colors[2] = c.getZ();
    colors[3] = c.getW();
}

static void GetStreams(dmBuffer::HBuffer buffer,
                float*& positions, uint32_t& positions_stride,
                float*& normals, uint32_t& normals_stride,
                float*& texcoords, uint32_t& texcoords_stride,
                float*& colors, uint32_t& colors_stride)
{
    uint32_t positions_count; uint32_t positions_components;
    uint32_t normals_count; uint32_t normals_components;
    uint32_t texcoords_count; uint32_t texcoords_components;
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
    r = dmBuffer::GetStream(buffer, VERTEX_STREAM_NAME_TEXCOORD, (void**)&texcoords, &texcoords_count, &texcoords_components, &texcoords_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", dmHashReverseSafe64(VERTEX_STREAM_NAME_TEXCOORD), dmBuffer::GetResultString(r), r);
        return;
    }
    r = dmBuffer::GetStream(buffer, VERTEX_STREAM_NAME_COLOR, (void**)&colors, &colors_count, &colors_components, &colors_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", dmHashReverseSafe64(VERTEX_STREAM_NAME_COLOR), dmBuffer::GetResultString(r), r);
        return;
    }
}

static void FillFlatBuffer(uint32_t patch_size, float* positions, uint32_t positions_stride,
                                                float* normals, uint32_t normals_stride,
                                                float* texcoords, uint32_t texcoords_stride,
                                                float* colors, uint32_t colors_stride)
{
    float scale = 1;
    for (uint32_t x = 0; x <= patch_size-1; ++x)
    {
        for (uint32_t z = 0; z <= patch_size-1; ++z)
        {
            Vector3 v0 = Vector3(  x, 0, z) * scale;
            Vector3 v1 = Vector3(  x, 0, z+1) * scale;
            Vector3 v2 = Vector3(x+1, 0, z+1) * scale;
            Vector3 v3 = Vector3(x+1, 0, z) * scale;

            Vector3 n0 = Vector3(0,1,0);
            Vector3 n1 = Vector3(0,1,0);
            Vector3 n2 = Vector3(0,1,0);
            Vector3 n3 = Vector3(0,1,0);
            Vector3 uv0 = Vector3(0,1,0);
            Vector3 uv1 = Vector3(0,1,0);
            Vector3 uv2 = Vector3(0,1,0);
            Vector3 uv3 = Vector3(0,1,0);
            Vector4 col0 = Vector4(1,1,1,1);
            Vector4 col1 = Vector4(1,1,1,1);
            Vector4 col2 = Vector4(1,1,1,1);
            Vector4 col3 = Vector4(1,1,1,1);

            #define INCREMENT_STRIDE() positions += positions_stride; normals += normals_stride; texcoords += texcoords_stride; colors += colors_stride;

            SetVertex(v0, n0, uv0, col0, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(v1, n1, uv1, col1, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(v2, n2, uv2, col2, positions, normals, texcoords, colors); INCREMENT_STRIDE();

            SetVertex(v2, n2, uv2, col2, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(v3, n3, uv3, col3, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(v0, n0, uv0, col0, positions, normals, texcoords, colors); INCREMENT_STRIDE();

            #undef INCREMENT_STRIDE
        }
    }
}

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
//printf("GetNormal %d %d\n", x, z);
    float x_a = GetHeight(patch, x-1, z);
    float x_b = GetHeight(patch, x+1, z);
    float z_a = GetHeight(patch, x, z-1);
    float z_b = GetHeight(patch, x, z+1);

    // float dx = x_b - x_a;
    // float dz = z_b - z_a;

    float xz_scale = 1.0f;
    Vector3 n(x_b - x_a, 2 * xz_scale, z_b - z_a);
// printf("h: %f %f %f %f\n", x_a, x_b, z_a, z_b);
// printf("n: %f %f %f\n", n.getX(), n.getY(), n.getZ());

    n = normalize(n);

 // printf("nn: %f %f %f\n", n.getX(), n.getY(), n.getZ());
 // printf("\n");

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

static void GeneratePatchHeights(TerrainPatch* patch)
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

    patch->m_IsDataLoaded = 1;
    printf("XZ: %d %d  height 0,0: %u \n", patch->m_XZ[0], patch->m_XZ[1], patch->m_Heightmap[0]);


    //printf("HEIGHT min/max: %f %f\n", height_min, height_max);
}

static void GenerateVertexData(TerrainPatch* patch)
{
    TimerScope tscope(__FUNCTION__);

    float* positions; uint32_t positions_stride;
    float* normals; uint32_t normals_stride;
    float* texcoords; uint32_t texcoords_stride;
    float* colors; uint32_t colors_stride;
    GetStreams(patch->m_Buffer,
                positions, positions_stride,
                normals, normals_stride,
                texcoords, texcoords_stride,
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

            Vector3 uv0 = Vector3(0,1,0);
            Vector3 uv1 = Vector3(0,1,0);
            Vector3 uv2 = Vector3(0,1,0);
            Vector3 uv3 = Vector3(0,1,0);
            Vector4 col0 = Vector4(1,1,1,1);
            Vector4 col1 = Vector4(1,1,1,1);
            Vector4 col2 = Vector4(1,1,1,1);
            Vector4 col3 = Vector4(1,1,1,1);

            #define INCREMENT_STRIDE() positions += positions_stride; normals += normals_stride; texcoords += texcoords_stride; colors += colors_stride;

            SetVertex(p0, n0, uv0, col0, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(p1, n1, uv1, col1, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(p2, n2, uv2, col2, positions, normals, texcoords, colors); INCREMENT_STRIDE();

            SetVertex(p2, n2, uv2, col2, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(p3, n3, uv3, col3, positions, normals, texcoords, colors); INCREMENT_STRIDE();
            SetVertex(p0, n0, uv0, col0, positions, normals, texcoords, colors); INCREMENT_STRIDE();

            #undef INCREMENT_STRIDE
        }
    }
}


static bool DoPatchLoad(TerrainPatch* patch)
{
    int patch_size = GetPatchSize(0);

    if (patch->m_Generate)
    {
        if (!patch->m_IsDataLoaded) {
            GeneratePatchHeights(patch);
            return false;
        }

        GenerateVertexData(patch);

        patch->m_IsLoading = 0;
        patch->m_IsLoaded = 1;
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

static void CreateBuffer(dmBuffer::HBuffer* buffer, uint32_t patch_size)
{
    dmBuffer::StreamDeclaration streams_decl[] = {
        {VERTEX_STREAM_NAME_POSITION, dmBuffer::VALUE_TYPE_FLOAT32, 3},
        {VERTEX_STREAM_NAME_NORMAL, dmBuffer::VALUE_TYPE_FLOAT32, 3},
        {VERTEX_STREAM_NAME_TEXCOORD, dmBuffer::VALUE_TYPE_FLOAT32, 2},
        {VERTEX_STREAM_NAME_COLOR, dmBuffer::VALUE_TYPE_FLOAT32, 4},
    };

    // num triangles: num_quads * num_triangles per quad * num vertices per triangle
    uint32_t element_count = (patch_size*patch_size) * 2 * 3;

    // dmLogWarning("patch_size: %u", patch_size);
    // dmLogWarning("element_count: %u", element_count);

    dmBuffer::Result r = dmBuffer::Create(element_count, streams_decl, sizeof(streams_decl)/sizeof(dmBuffer::StreamDeclaration), buffer);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to create buffer: %s (%d)", dmBuffer::GetResultString(r), r);
        return;
    }
}

static void PutWork(HTerrain terrain, TerrainWork item)
{
    if (terrain->m_Work.Full())
        terrain->m_Work.OffsetCapacity(9*2);
    terrain->m_Work.Push(item);
}

void PatchLoad(HTerrain terrain, TerrainPatch* patch)
{
    TerrainWork item;
    item.m_Type = TERRAIN_WORK_LOAD;
    item.m_Patch = patch;
    item.m_IsDone = 0;
    item.m_Generate = patch->m_Generate;
    patch->m_IsLoading = 1;
    patch->m_IsLoaded = 0;
    patch->m_Delete = 0;
    PutWork(terrain, item);
}


void PatchUnload(HTerrain terrain, TerrainPatch* patch)
{
    TerrainWork item;
    item.m_Type = TERRAIN_WORK_UNLOAD;
    item.m_Patch = patch;
    item.m_IsDone = 0;
    item.m_Generate = patch->m_Generate;
    PutWork(terrain, item);
}

static bool DoPatchUnload(HTerrain terrain, TerrainPatch* patch)
{
    patch->m_IsDataLoaded = 0;
    patch->m_IsLoaded = 0;
    patch->m_Delete = 0;
    return true;
}


static void PatchDelete(TerrainPatch* patch)
{
    delete[] patch->m_Heightmap;
}

// Coords in [-1,1] range (i.e. around the camera)
static bool IsPatchOccupied(TerrainPatchLod* patch_lod, int x, int z)
{
    return patch_lod->m_PatchesOccupied[(z+1)*3 + (x+1)];
}
// Coords in [-1,1] range (i.e. around the camera)
static void SetPatchOccupied(TerrainPatchLod* patch_lod, int x, int z, bool loaded)
{
    //printf("SetPatchOccupied %d %d: %d\n", x, z, (int)loaded);
    patch_lod->m_PatchesOccupied[(z+1)*3 + (x+1)] = loaded;
}

HTerrain Create(const InitParams& params)
{
    assert(params.m_Callback != 0);

//printf("HACK: params.m_BasePatchSize: %u\n", params.m_BasePatchSize);

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

    int patch_size = GetPatchSize(0);

    // Initialize patches
    for (int lod = 0, id = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        TerrainPatchLod* patch_lod = &terrain->m_Terrain[lod];

        WorldToPatchCoord(camera_pos, lod, patch_lod->m_CameraXZ);

        for (int x = -1, i = 0; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z, ++i, ++id)
            {
                SetPatchOccupied(patch_lod, x, z, false);

                TerrainPatch* patch = &terrain->m_Terrain[lod].m_Patches[i];
                patch->m_Id = id;
                patch->m_HeightSeed = terrain_seed; // duplicate, but makes it easier to access on threads
                patch->m_Lod = lod;
                patch->m_IsLoaded = 0;
                patch->m_Delete = 0;
                patch->m_IsLoading = 0;
                patch->m_IsDataLoaded = 0;
                patch->m_Heightmap = 0;

                patch->m_Generate = 1; // pass in option for this in the init function

                CreateBuffer(&patch->m_Buffer, patch_size);

                // patch->m_XZ[0] = patch_lod->m_CameraXZ[0] + x;
                // patch->m_XZ[1] = patch_lod->m_CameraXZ[1] + z;

                // patch->m_Position = PatchToWorldCoord(patch->m_XZ, lod);

                // dmRng::Init(&patch->m_Rng, dmRng::RandU32(&terrain->m_Rng));
            }
        }
    }

    terrain->m_LoaderContext = 0;
    //terrain->m_LoaderContext = RawFileLoader_Init("/Users/mawe/work/projects/users/mawe/defold-terrain/data/heightmap.r16");

    return terrain;
}


void Destroy(HTerrain terrain)
{
    // TODO: Cancel loading
    // TODO: Post hide events?

    if (terrain->m_LoaderContext)
        RawFileLoader_Exit(terrain->m_LoaderContext);

    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        for (int i = 0; i < 9; ++i)
        {
            TerrainPatch* patch = &terrain->m_Terrain[lod].m_Patches[i];
            PatchDelete(patch);
        }
    }

    delete terrain;
}

static void FlushWorkQueue(HTerrain terrain, dmArray<TerrainWork>& commands)
{
    uint32_t size = commands.Size();
    for (uint32_t i = 0; i < size; ++i)
    {
        TerrainWork& cmd = commands[i];
        TerrainPatch* patch = cmd.m_Patch;

        bool isloaded = patch->m_IsLoaded;

        if (cmd.m_Type == TERRAIN_WORK_LOAD)
        {
            cmd.m_IsDone = DoPatchLoad(patch);
            if (cmd.m_IsDone && patch->m_Delete)
            {
                patch->m_IsLoaded = 0; // don't trigger a callback
                PatchUnload(terrain, patch);
            }
        }
        else if (cmd.m_Type == TERRAIN_WORK_UNLOAD)
        {
            cmd.m_IsDone = DoPatchUnload(terrain, patch);
        }

        if (!cmd.m_IsDone)
            continue;

        // Change in state, do the callbacks
        if (isloaded != patch->m_IsLoaded)
        {
            if (patch->m_IsLoaded)
                terrain->m_Callback(TERRAIN_PATCH_SHOW, patch);
            else
                terrain->m_Callback(TERRAIN_PATCH_HIDE, patch);
        }

        commands.EraseSwap(i);
        --i;
        --size;
    }
}

static inline bool IsPatchFree(TerrainPatch* patch)
{
    return !patch->m_IsLoading && !patch->m_IsLoaded;
}

static TerrainPatch* FindFreePatch(HTerrain terrain, int lod)
{
    for (int i = 0; i < 9; ++i)
    {
        TerrainPatch* patch = &terrain->m_Terrain[lod].m_Patches[i];
        if (IsPatchFree(patch))
            return patch;
    }
    return 0;
}

static inline int Sign(int value)
{
    return value > 0 ? 1 : -1;
}

// update camera position
// mark patches as discarded
// Allow empty patches to load
static void UpdatePatches(HTerrain terrain, Vector3 camera_pos)
{
    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        TerrainPatchLod* patch_lod = &terrain->m_Terrain[lod];

        int camera_xz[2];
        WorldToPatchCoord(camera_pos, lod, camera_xz);

        int camera_diffx = camera_xz[0] - patch_lod->m_CameraXZ[0];
        int camera_diffz = camera_xz[1] - patch_lod->m_CameraXZ[1];

        bool camera_moved = camera_diffx!=0 || camera_diffz!= 0;

        // if (!camera_moved)
        //     continue;

        // if (camera_moved)
        // {
        //     dmLogWarning("Camera was at %d %d, moved to %d %d  diff: %d %d  sign: %d %d  pos: %.3f %.3f %.3f",
        //         patch_lod->m_CameraXZ[0], patch_lod->m_CameraXZ[1], camera_xz[0], camera_xz[1],
        //         camera_diffx, camera_diffz, Sign(camera_diffx), Sign(camera_diffz),
        //         camera_pos.getX(), camera_pos.getY(), camera_pos.getZ());
        // }

        patch_lod->m_CameraXZ[0] = camera_xz[0];
        patch_lod->m_CameraXZ[1] = camera_xz[1];

        // Mark the new slots as missing
        if (camera_diffx != 0)
        {
            SetPatchOccupied(patch_lod, Sign(camera_diffx), -1, false);
            SetPatchOccupied(patch_lod, Sign(camera_diffx),  0, false);
            SetPatchOccupied(patch_lod, Sign(camera_diffx),  1, false);
        }
        if (camera_diffz != 0)
        {
            SetPatchOccupied(patch_lod, -1, Sign(camera_diffz), false);
            SetPatchOccupied(patch_lod,  0, Sign(camera_diffz), false);
            SetPatchOccupied(patch_lod,  1, Sign(camera_diffz), false);
        }

        // if (camera_diffx || camera_diffz)
        // {
        //     printf("     x-1  x+0  x+1\n");
        //     printf("z-1  %2d  %2d  %2d\n", IsPatchOccupied(patch_lod, -1, -1), IsPatchOccupied(patch_lod, 0, -1), IsPatchOccupied(patch_lod, 1, -1));
        //     printf("z+0  %2d  %2d  %2d\n", IsPatchOccupied(patch_lod, -1,  0), IsPatchOccupied(patch_lod, 0,  0), IsPatchOccupied(patch_lod, 1,  0));
        //     printf("z+1  %2d  %2d  %2d\n", IsPatchOccupied(patch_lod, -1, +1), IsPatchOccupied(patch_lod, 0, +1), IsPatchOccupied(patch_lod, 1, +1));
        // }

        for (int i = 0; i < 9; ++i)
        {
            TerrainPatch* patch = &patch_lod->m_Patches[i];

            // Bounding box check

            // find out if it's more than one step away from the camera position
            int diffx = patch->m_XZ[0] - camera_xz[0];
            int diffz = patch->m_XZ[1] - camera_xz[1];

            // the the difference is positive, then the camera is moving in the positive direction
            // and the next patch position should be before the camera in that same direction
            int movex = dmMath::Abs(diffx) > 1 ? Sign(diffx) : 0;
            int movez = dmMath::Abs(diffz) > 1 ? Sign(diffz) : 0;

            if (movex || movez)
            {
                if (patch->m_IsLoading)
                {
                    patch->m_Delete = 1; // might possibly do an early out of the patch loader
                    continue;
                }
                else if (patch->m_IsLoaded)
                {
                    PatchUnload(terrain, patch);
                    continue;
                }
            }
        }
    }

    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        TerrainPatchLod* patch_lod = &terrain->m_Terrain[lod];

        for (int z = -1; z <= 1; ++z)
        {
            for (int x = -1; x <= 1; ++x)
            {
                if (!IsPatchOccupied(patch_lod, x, z))
                {
                    TerrainPatch* patch = FindFreePatch(terrain, lod);
                    if (patch)
                    {
                        // move this patch
                        int newx = patch_lod->m_CameraXZ[0] + x;
                        int newz = patch_lod->m_CameraXZ[1] + z;

                        patch->m_XZ[0] = newx;
                        patch->m_XZ[1] = newz;

                        patch->m_Position = PatchToWorldCoord(patch->m_XZ, lod);

                        // printf("Loading patch: %d %d (%d %d)    pos: %.3f, %.3f  camera: %d %d\n",
                        //     patch->m_XZ[0], patch->m_XZ[1], x, z, patch->m_Position.getX(), patch->m_Position.getZ(),
                        //     patch_lod->m_CameraXZ[0], patch_lod->m_CameraXZ[1]);

                        PatchLoad(terrain, patch);

                        SetPatchOccupied(patch_lod, x, z, true);
                    }
                }
            }
        }
    }
}


void Update(HTerrain terrain, const UpdateParams& params)
{
    terrain->m_View = params.m_View;

    Matrix4 invView = inverse(params.m_View);
    terrain->m_CameraPos = invView.getCol(3).getXYZ();

    UpdatePatches(terrain, terrain->m_CameraPos);

    FlushWorkQueue(terrain, terrain->m_Work);
}


} // namespace