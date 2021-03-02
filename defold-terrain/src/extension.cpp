
// include the Defold SDK
#include <dmsdk/sdk.h>
#include "terrain.h"

#define MODULE_NAME "terrain"

namespace dmTerrain {


struct TerrainCommand
{
    TerrainEvents m_Event;
    const TerrainPatch* m_Patch;
};

struct ExtensionContext
{
    dmScript::LuaCallbackInfo* m_Callback;
    HTerrain m_Terrain;
    dmArray<TerrainCommand> m_Commands;
};
ExtensionContext* g_TerrainWorld = 0;

// ****************************************************************************************************************************************************************
// callback functions

// Invoke the Lua callback
static void Terrain_PatchCallback(TerrainEvents event, const TerrainPatch* patch)
{
    ExtensionContext* world = g_TerrainWorld;
    if (!dmScript::IsCallbackValid(world->m_Callback))
    {
        dmLogWarning("No callback function set!");
        return;
    }

    if (!dmScript::SetupCallback(world->m_Callback))
    {
        return;
    }
    lua_State* L = dmScript::GetCallbackLuaContext(world->m_Callback);

    lua_pushnumber(L, (lua_Number)event);

    lua_newtable(L);

    lua_pushinteger(L, patch->m_XZ[0]);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, patch->m_XZ[1]);
    lua_setfield(L, -2, "z");
    lua_pushinteger(L, patch->m_Lod);
    lua_setfield(L, -2, "lod");

    dmScript::PushVector3(L, patch->m_Position);
    lua_setfield(L, -2, "position");

    dmScript::LuaHBuffer luabuf = {patch->m_Buffer, dmScript::OWNER_C};
    dmScript::PushBuffer(L, luabuf);
    lua_setfield(L, -2, "buffer");

    dmScript::PCall(L, 3, 0); // self + # user arguments

    dmScript::TeardownCallback(world->m_Callback);
}

// Callbacks from the terrain system
static void Terrain_Callback(TerrainEvents event, const TerrainPatch* patch)
{
    ExtensionContext* world = g_TerrainWorld;
    TerrainCommand cmd;
    cmd.m_Event = event;
    cmd.m_Patch = patch;

    if (world->m_Commands.Full())
        world->m_Commands.OffsetCapacity(9*2);
    world->m_Commands.Push(cmd);
}


static void FlushCommandQueue(dmArray<TerrainCommand>& commands)
{
    uint32_t size = commands.Size();
    for (uint32_t i = 0; i < size; ++i)
    {
        TerrainCommand& cmd = commands[i];
        Terrain_PatchCallback(cmd.m_Event, cmd.m_Patch);
    }
    commands.SetSize(0);
}


// ****************************************************************************************************************************************************************

static int Terrain_Init(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    ExtensionContext* world = g_TerrainWorld;

    world->m_Callback = dmScript::CreateCallback(L, 1);

    dmTerrain::InitParams init_params;
    init_params.m_Callback = Terrain_Callback;
    init_params.m_BasePatchSize = 512;

    Vector3 camera_pos = Vector3(0);
    if (lua_istable(L, 2))
    {
        lua_pushvalue(L, 2);

        lua_getfield(L, -1, "view");
        Matrix4* view = dmScript::ToMatrix4(L, -1);
        if (view) {
            init_params.m_View = *view;
            camera_pos = (view->getCol(3) * -1).getXYZ();
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "proj");
        Matrix4* proj = dmScript::ToMatrix4(L, -1);
        if (proj)
            init_params.m_Proj = *proj;
        lua_pop(L, 1);

        lua_pop(L, 1); // pop the table
    }

    world->m_Terrain = dmTerrain::Create(init_params);

    return 0;
}

static int Terrain_Exit(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    ExtensionContext* world = g_TerrainWorld;

    dmTerrain::Destroy(world->m_Terrain);

    dmScript::DestroyCallback(world->m_Callback);
    world->m_Callback = 0;

    return 0;
}




static int Terrain_Update(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    ExtensionContext* world = g_TerrainWorld;

    dmTerrain::UpdateParams update_params;
    update_params.m_Dt = (float)luaL_checknumber(L, 1); // not sure if needed. perhaps use for time slicing?

    if (lua_istable(L, 2))
    {
        lua_pushvalue(L, 2);

        lua_getfield(L, -1, "view");
        Matrix4* view = dmScript::ToMatrix4(L, -1);
        if (view) {
            update_params.m_View = *view;
        }
        lua_pop(L, 1);

        lua_getfield(L, -1, "proj");
        Matrix4* proj = dmScript::ToMatrix4(L, -1);
        if (proj)
            update_params.m_Proj = *proj;
        lua_pop(L, 1);

        lua_pop(L, 1); // pop the table
    }

    dmTerrain::Update(world->m_Terrain, update_params);

    FlushCommandQueue(world->m_Commands);

    return 0;
}

// Functions exposed to Lua
static const luaL_reg Module_methods[] =
{
    {"init", Terrain_Init},
    {"update", Terrain_Update},
    {"exit", Terrain_Exit},
    {0, 0}
};

static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);

    // Register lua names
    luaL_register(L, MODULE_NAME, Module_methods);

#define SETCONSTANT(name) \
        lua_pushnumber(L, (lua_Number) name); \
        lua_setfield(L, -2, #name);\

     SETCONSTANT(TERRAIN_PATCH_HIDE); // a patch is about to be moved
     SETCONSTANT(TERRAIN_PATCH_SHOW); // a patch is about to be shown

#undef SETCONSTANT

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

static dmExtension::Result AppInitialize(dmExtension::AppParams* params)
{
    printf("AppInitialize %s Extension\n", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result Initialize(dmExtension::Params* params)
{
    g_TerrainWorld = new ExtensionContext;
    LuaInit(params->m_L);
    printf("Registered %s Extension\n", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalize(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result Finalize(dmExtension::Params* params)
{
    delete g_TerrainWorld;
    g_TerrainWorld = 0;
    return dmExtension::RESULT_OK;
}

} // namespace

// Defold SDK uses a macro for setting up extension entry points:
//
// DM_DECLARE_EXTENSION(symbol, name, app_init, app_final, init, update, on_event, final)

//  is the C++ symbol that holds all relevant extension data.
// It must match the name field in the `ext.manifest`
DM_DECLARE_EXTENSION(TerrainExt, "Terrain", dmTerrain::AppInitialize, dmTerrain::AppFinalize, dmTerrain::Initialize, 0, 0, dmTerrain::Finalize)
