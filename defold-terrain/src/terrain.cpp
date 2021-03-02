#include <assert.h>
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
    float frequency = 1.0f;
    float lacunarity = 1.5f;
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

static void GeneratePatchHeights(TerrainPatch* patch)
{
    uint32_t seed = patch->m_HeightSeed;
    float wx = patch->m_XZ[0];
    float wz = patch->m_XZ[1];

    int patch_size = GetPatchSize(0);

    int num_verts = patch_size+1;
    float oo_patch_size_f = 1.0f / patch_size;

    int size = num_verts+2; // we have an extra border in order to get correct normal values
    patch->m_Heightmap = new uint16_t[size * size];

    patch->m_HeightMin = 65535;
    patch->m_HeightMax = 0;

    //printf("XZ: %d %d\n", patch->m_XZ[0], patch->m_XZ[1]);


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

    //printf("HEIGHT min/max: %f %f\n", height_min, height_max);
}

static void GenerateVertexData(TerrainPatch* patch)
{
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

    printf("******************************************\n");
    printf("XZ: %d %d  patch_size: %u\n", patch->m_XZ[0], patch->m_XZ[1], patch_size);

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


static bool PatchLoadGenerate(TerrainPatch* patch)
{
    int patch_size = GetPatchSize(0);

    if (patch->m_Generate)
    {
        if (patch->m_Heightmap == 0) {
            GeneratePatchHeights(patch);
            return false;
        }

        GenerateVertexData(patch);
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

    // Initialize work
    for (int lod = 0; lod < NUM_LOD_LEVELS; ++lod)
    {
        int camera_xz[2];
        WorldToPatchCoord(camera_pos, lod, camera_xz);

        for (int x = -1, i = 0; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z, ++i)
            {
                // if (x == -1 || x == 1)
                // {
                //     if (z == -1 || z == 1)
                //         continue;
                // }

                // if (!(x == -1 && z == 0))
                //     continue;

                TerrainPatch* patch = &terrain->m_Terrain[lod].m_Patches[i];

                dmRng::Init(&patch->m_Rng, dmRng::RandU32(&terrain->m_Rng));
                patch->m_HeightSeed = terrain_seed; // duplicate, but makes it easier to access on threads

                patch->m_Lod = lod;
                patch->m_XZ[0] = camera_xz[0] + x;
                patch->m_XZ[1] = camera_xz[1] + z;

                patch->m_IsLoaded = 0;
                patch->m_Position = PatchToWorldCoord(patch->m_XZ, lod);

                CreateBuffer(&patch->m_Buffer, patch_size);

                patch->m_Generate = 1;
                patch->m_Heightmap = 0;

                TerrainWork item;
                item.m_Type = TERRAIN_WORK_LOAD;
                item.m_Patch = patch;
                item.m_IsDone = 0;
                item.m_Generate = patch->m_Generate;

                if (terrain->m_Work.Full())
                    terrain->m_Work.OffsetCapacity(9*2);
                terrain->m_Work.Push(item);
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

    delete terrain;
}

static void FlushWorkQueue(HTerrain terrain, dmArray<TerrainWork>& commands)
{
    uint32_t size = commands.Size();
    for (uint32_t i = 0; i < size; ++i)
    {
        TerrainWork& cmd = commands[i];
        TerrainPatch* patch = cmd.m_Patch;

        if (cmd.m_Type == TERRAIN_WORK_LOAD)
        {
            if (patch->m_Generate) {
                cmd.m_IsDone = PatchLoadGenerate(patch);
            } else {
                //cmd.m_IsDone = true;
                //cmd.m_Type = fail?
            }
        }

        if (!cmd.m_IsDone)
            continue;

        if (cmd.m_Type == TERRAIN_WORK_LOAD)
        {
            terrain->m_Callback(TERRAIN_PATCH_SHOW, patch);
        }
        commands.EraseSwap(i);
        --i;
        --size;
    }
}


void Update(HTerrain terrain, const UpdateParams& params)
{
    Matrix4 prev_view = terrain->m_View;
    Vector3 prev_camera_pos = (prev_view.getCol(3) * -1).getXYZ();
    Vector3 camera_pos = (params.m_View.getCol(3) * -1).getXYZ();
    terrain->m_View = params.m_View;

    FlushWorkQueue(terrain, terrain->m_Work);
}


} // namespace