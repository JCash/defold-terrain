#include "terrain_private.h"
#include "terrain.h"
#include <assert.h>

namespace dmTerrain
{

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

static inline void SetVertex(uint32_t index, const Vector3& p, const Vector3& n, const Vector3& uv, const Vector4& c,
                            float* positions, float* normals, float* texcoords, float* colors)
{
    positions[index*3+0] = p.getX();
    positions[index*3+1] = p.getY();
    positions[index*3+2] = p.getZ();

    normals[index*3+0] = n.getX();
    normals[index*3+1] = n.getY();
    normals[index*3+2] = n.getZ();

    texcoords[index*2+0] = uv.getX();
    texcoords[index*2+1] = uv.getY();

    colors[index*4+0] = c.getX();
    colors[index*4+1] = c.getY();
    colors[index*4+2] = c.getZ();
    colors[index*4+3] = c.getW();
}

static void CreateBuffer(dmBuffer::HBuffer* buffer, uint32_t patch_size)
{
    dmBuffer::StreamDeclaration streams_decl[] = {
        {dmHashString64("position"), dmBuffer::VALUE_TYPE_FLOAT32, 3},
        {dmHashString64("normal"), dmBuffer::VALUE_TYPE_FLOAT32, 3},
        {dmHashString64("texcoord"), dmBuffer::VALUE_TYPE_FLOAT32, 2},
        {dmHashString64("color"), dmBuffer::VALUE_TYPE_FLOAT32, 4},
    };

    // num triangles: num_quads * num_triangles per quad * num vertices per triangle
    uint32_t element_count = (patch_size*patch_size) * 2 * 3;

    dmLogWarning("patch_size: %u", patch_size);
    dmLogWarning("element_count: %u", element_count);

    dmBuffer::Result r = dmBuffer::Create(element_count, streams_decl, sizeof(streams_decl)/sizeof(dmBuffer::StreamDeclaration), buffer);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to create buffer: %s (%d)", dmBuffer::GetResultString(r), r);
        return;
    }

    // Add debug info
    float* positions; uint32_t positions_count; uint32_t positions_components; uint32_t positions_stride;
    float* normals; uint32_t normals_count; uint32_t normals_components; uint32_t normals_stride;
    float* texcoords; uint32_t texcoords_count; uint32_t texcoords_components; uint32_t texcoords_stride;
    float* colors; uint32_t colors_count; uint32_t colors_components; uint32_t colors_stride;

    r = dmBuffer::GetStream(*buffer, streams_decl[0].m_Name, (void**)&positions, &positions_count, &positions_components, &positions_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", "position", dmBuffer::GetResultString(r), r);
        return;
    }
    r = dmBuffer::GetStream(*buffer, streams_decl[1].m_Name, (void**)&normals, &normals_count, &normals_components, &normals_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", "normal", dmBuffer::GetResultString(r), r);
        return;
    }
    r = dmBuffer::GetStream(*buffer, streams_decl[2].m_Name, (void**)&texcoords, &texcoords_count, &texcoords_components, &texcoords_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", "texcoord", dmBuffer::GetResultString(r), r);
        return;
    }
    r = dmBuffer::GetStream(*buffer, streams_decl[3].m_Name, (void**)&colors, &colors_count, &colors_components, &colors_stride);
    if (r != dmBuffer::RESULT_OK)
    {
        dmLogError("Failed to get stream '%s': %s (%d)", "color", dmBuffer::GetResultString(r), r);
        return;
    }

    float scale = 1;

    uint32_t index = 0;
    for (uint32_t x = 0; x < patch_size; ++x)
    {
        for (uint32_t z = 0; z < patch_size; ++z)
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
            Vector4 col0 = Vector4(1,0,0,1);
            Vector4 col1 = Vector4(0,1,0,1);
            Vector4 col2 = Vector4(0,0,1,1);
            Vector4 col3 = Vector4(1,1,1,1);

            SetVertex(index+0, v0, n0, uv0, col0, positions, normals, texcoords, colors);
            SetVertex(index+1, v1, n1, uv1, col1, positions, normals, texcoords, colors);
            SetVertex(index+2, v2, n2, uv2, col2, positions, normals, texcoords, colors);

            SetVertex(index+3, v2, n2, uv2, col2, positions, normals, texcoords, colors);
            SetVertex(index+4, v3, n3, uv3, col3, positions, normals, texcoords, colors);
            SetVertex(index+5, v0, n0, uv0, col0, positions, normals, texcoords, colors);
        }
    }

/*
        local stream_position = buffer.get_stream(newbuf, "position")
    local stream_normal = buffer.get_stream(newbuf, "normal")
    local stream_texcoord = buffer.get_stream(newbuf, "texcoord")
    local stream_color = buffer.get_stream(newbuf, "color")

    local index = 0
    for x=0,size-1 do
        for z=0,size-1 do
            -- topleft, bottom left, bottom right, top right
            local v0 = vmath.vector3(  x, 0, z) * scale
            local v1 = vmath.vector3(  x, 0, z+1) * scale
            local v2 = vmath.vector3(x+1, 0, z+1) * scale
            local v3 = vmath.vector3(x+1, 0, z) * scale

            local n0 = vmath.vector3(0,1,0)
            local n1 = vmath.vector3(0,1,0)
            local n2 = vmath.vector3(0,1,0)
            local n3 = vmath.vector3(0,1,0)

            local uv0 = vmath.vector3(0,1,0)
            local uv1 = vmath.vector3(0,1,0)
            local uv2 = vmath.vector3(0,1,0)
            local uv3 = vmath.vector3(0,1,0)

            local col0 = vmath.vector4(1,1,1,1)
            local col1 = vmath.vector4(1,1,1,1)
            local col2 = vmath.vector4(1,1,1,1)
            local col3 = vmath.vector4(1,1,1,1)

            set_vertex(index+0, v0, n0, uv0, col0, stream_position, stream_normal, stream_texcoord, stream_color)
            set_vertex(index+1, v1, n1, uv1, col1, stream_position, stream_normal, stream_texcoord, stream_color)
            set_vertex(index+2, v2, n2, uv2, col2, stream_position, stream_normal, stream_texcoord, stream_color)

            set_vertex(index+3, v2, n2, uv2, col2, stream_position, stream_normal, stream_texcoord, stream_color)
            set_vertex(index+4, v3, n3, uv3, col3, stream_position, stream_normal, stream_texcoord, stream_color)
            set_vertex(index+5, v0, n0, uv0, col0, stream_position, stream_normal, stream_texcoord, stream_color)
            index = index + 6
        end
    end
    */

}


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
                TerrainPatch* patch = &terrain->m_Terrain[lod].m_Patches[i];
                patch->m_Lod = lod;
                patch->m_XZ[0] = camera_xz[0] + x;
                patch->m_XZ[1] = camera_xz[1] + z;
                patch->m_IsLoaded = 0;
                patch->m_Position = PatchToWorldCoord(patch->m_XZ, lod);
                CreateBuffer(&patch->m_Buffer, patch_size);

                TerrainWork item;
                item.m_Type = TERRAIN_WORK_LOAD;
                item.m_Patch = patch;
                item.m_IsDone = 1;

                if (terrain->m_Work.Full())
                    terrain->m_Work.OffsetCapacity(9*2);
                terrain->m_Work.Push(item);
            }
        }
    }

    return terrain;
}


void Destroy(HTerrain terrain)
{
    // TODO: Cancel loading
    // TODO: Post hide events?
    delete terrain;
}


static void FlushWorkQueue(HTerrain terrain, dmArray<TerrainWork>& commands)
{
    uint32_t size = commands.Size();
    for (uint32_t i = 0; i < size; ++i)
    {
        TerrainWork& cmd = commands[i];
        if (!cmd.m_IsDone)
            continue;

        TerrainPatch* patch = cmd.m_Patch;

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