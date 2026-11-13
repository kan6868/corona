/*
 * plugin.box3d — Lua binding cho Box3D (https://github.com/erincatto/box3d, MIT) dùng trong Solar2D.
 *
 * Chỉ dùng Lua C API, không cần API Corona hay OpenGL:
 *   Windows (Simulator Win32): link với lua.dll của Simulator qua import library (tools/build.py tạo).
 *   Android: link với liblua.so (stub chỉ mang tên, hàm thật lấy từ app Solar2D lúc chạy).
 *
 * Quy ước:
 *   - Toạ độ theo mét, hệ tay phải, Y hướng lên (khớp với plugin.s3d).
 *   - Góc và vận tốc góc người dùng nhập theo ĐỘ (độ/giây), giống Solar2D và s3d. Bên trong đổi sang radian.
 *   - Quaternion trả về/nhận vào dạng 4 số x, y, z, w.
 *   - Box3D chạy 1 luồng (workerCount = 1) trừ khi newWorld{ workers = n }.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "box3d/box3d.h"
#include "box3d/collision.h"
#include "box3d/math_functions.h"

#if defined( _WIN32 )
  #define B3P_EXPORT __declspec( dllexport )
#else
  #define B3P_EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

#define B3P_VERSION "0.1.0"

#define MT_WORLD "box3d.World"
#define MT_BODY  "box3d.Body"
#define MT_SHAPE "box3d.Shape"
#define MT_JOINT "box3d.Joint"

static const float kDeg = 0.017453292519943295f;

/* ------------------------------------------------------------------------------------------ */
/* Kiểu dữ liệu                                                                                */
/* ------------------------------------------------------------------------------------------ */

typedef struct World
{
    b3WorldId id;
    int subSteps;
    int alive;
    int bodiesRef;        /* bảng Lua: ref -> body userdata (giữ body sống, tra từ userData của Box3D) */
    b3MeshData ** meshes; /* mesh Box3D chỉ tham chiếu, phải giữ tới khi huỷ world */
    int meshCount, meshCap;
} World;

typedef struct Body
{
    b3BodyId id;
    int ref;  /* khoá trong bảng bodies của world */
    World * world;
} Body;

typedef struct Shape
{
    b3ShapeId id;
} Shape;

typedef struct Joint
{
    b3JointId id;
    int type; /* b3JointType */
} Joint;

/* ------------------------------------------------------------------------------------------ */
/* Tiện ích đọc tham số                                                                        */
/* ------------------------------------------------------------------------------------------ */

static float optfield( lua_State * L, int t, const char * k, float def )
{
    float v = def;

    if (t == 0 || !lua_istable( L, t )) return def;
    lua_getfield( L, t, k );
    if (lua_isnumber( L, -1 )) v = (float)lua_tonumber( L, -1 );
    lua_pop( L, 1 );
    return v;
}

static int optbool( lua_State * L, int t, const char * k, int def )
{
    int v = def;

    if (t == 0 || !lua_istable( L, t )) return def;
    lua_getfield( L, t, k );
    if (!lua_isnil( L, -1 )) v = lua_toboolean( L, -1 );
    lua_pop( L, 1 );
    return v;
}

/* đọc { x, y, z } ở field k của bảng t; trả 1 nếu có */
static int optvec3( lua_State * L, int t, const char * k, b3Vec3 * out )
{
    int ok = 0;

    if (t == 0 || !lua_istable( L, t )) return 0;
    lua_getfield( L, t, k );
    if (lua_istable( L, -1 ))
    {
        int i;
        float v[3] = { 0, 0, 0 };

        for (i = 0; i < 3; ++i)
        {
            lua_rawgeti( L, -1, i + 1 );
            v[i] = (float)lua_tonumber( L, -1 );
            lua_pop( L, 1 );
        }
        out->x = v[0]; out->y = v[1]; out->z = v[2];
        ok = 1;
    }
    lua_pop( L, 1 );
    return ok;
}

static b3Vec3 checkvec3( lua_State * L, int i )
{
    b3Vec3 v;

    v.x = (float)luaL_checknumber( L, i );
    v.y = (float)luaL_checknumber( L, i + 1 );
    v.z = (float)luaL_checknumber( L, i + 2 );
    return v;
}

static void pushvec3( lua_State * L, b3Vec3 v )
{
    lua_pushnumber( L, v.x );
    lua_pushnumber( L, v.y );
    lua_pushnumber( L, v.z );
}

static void pushquat( lua_State * L, b3Quat q )
{
    lua_pushnumber( L, q.v.x );
    lua_pushnumber( L, q.v.y );
    lua_pushnumber( L, q.v.z );
    lua_pushnumber( L, q.s );
}

/* Quaternion từ góc Euler (độ) theo thứ tự của s3d: R = Ry(yaw) * Rx(pitch) * Rz(roll) */
static b3Quat quat_from_euler( float rx, float ry, float rz )
{
    b3Quat qx = b3MakeQuatFromAxisAngle( b3Vec3_axisX, rx * kDeg );
    b3Quat qy = b3MakeQuatFromAxisAngle( b3Vec3_axisY, ry * kDeg );
    b3Quat qz = b3MakeQuatFromAxisAngle( b3Vec3_axisZ, rz * kDeg );

    return b3NormalizeQuat( b3MulQuat( b3MulQuat( qy, qx ), qz ) );
}

/* opts.rotation = { x, y, z, w } (quaternion) hoặc opts.angles = { rx, ry, rz } (độ) */
static int optrotation( lua_State * L, int t, b3Quat * out )
{
    int ok = 0;

    if (t == 0 || !lua_istable( L, t )) return 0;
    lua_getfield( L, t, "rotation" );
    if (lua_istable( L, -1 ))
    {
        float v[4] = { 0, 0, 0, 1 };
        int i;

        for (i = 0; i < 4; ++i)
        {
            lua_rawgeti( L, -1, i + 1 );
            if (lua_isnumber( L, -1 )) v[i] = (float)lua_tonumber( L, -1 );
            lua_pop( L, 1 );
        }
        out->v.x = v[0]; out->v.y = v[1]; out->v.z = v[2]; out->s = v[3];
        *out = b3NormalizeQuat( *out );
        ok = 1;
    }
    lua_pop( L, 1 );
    if (!ok)
    {
        b3Vec3 e;

        if (optvec3( L, t, "angles", &e ))
        {
            *out = quat_from_euler( e.x, e.y, e.z );
            ok = 1;
        }
    }
    return ok;
}

/* ------------------------------------------------------------------------------------------ */
/* Userdata                                                                                    */
/* ------------------------------------------------------------------------------------------ */

static World * checkworld( lua_State * L, int i )
{
    World * w = (World *)luaL_checkudata( L, i, MT_WORLD );

    if (!w->alive) luaL_error( L, "box3d: world đã bị huỷ" );
    return w;
}

static Body * checkbody( lua_State * L, int i )
{
    Body * b = (Body *)luaL_checkudata( L, i, MT_BODY );

    if (!b->world || !b->world->alive || !b3Body_IsValid( b->id )) luaL_error( L, "box3d: body không còn hợp lệ" );
    return b;
}

static Shape * checkshape( lua_State * L, int i )
{
    Shape * s = (Shape *)luaL_checkudata( L, i, MT_SHAPE );

    if (!b3Shape_IsValid( s->id )) luaL_error( L, "box3d: shape không còn hợp lệ" );
    return s;
}

static Joint * checkjoint( lua_State * L, int i )
{
    Joint * j = (Joint *)luaL_checkudata( L, i, MT_JOINT );

    if (!b3Joint_IsValid( j->id )) luaL_error( L, "box3d: joint không còn hợp lệ" );
    return j;
}

/* Đẩy body userdata ứng với b3BodyId (tra qua userData = ref trong bảng bodies của world) */
static int pushbody( lua_State * L, World * w, b3BodyId id )
{
    intptr_t ref;

    if (!b3Body_IsValid( id )) { lua_pushnil( L ); return 0; }
    ref = (intptr_t)b3Body_GetUserData( id );
    if (ref <= 0) { lua_pushnil( L ); return 0; }
    lua_rawgeti( L, LUA_REGISTRYINDEX, w->bodiesRef );
    lua_rawgeti( L, -1, (int)ref );
    lua_remove( L, -2 );
    return 1;
}

static void pushshape( lua_State * L, b3ShapeId id )
{
    Shape * s = (Shape *)lua_newuserdata( L, sizeof( Shape ) );

    s->id = id;
    luaL_getmetatable( L, MT_SHAPE );
    lua_setmetatable( L, -2 );
}

/* body: __index tra method trước, rồi tới bảng riêng (env) để người dùng gắn field: body.obj = mesh */
static int body_index( lua_State * L )
{
    lua_getmetatable( L, 1 );
    lua_getfield( L, -1, "__methods" );
    lua_pushvalue( L, 2 );
    lua_rawget( L, -2 );
    if (!lua_isnil( L, -1 )) return 1;
    lua_pop( L, 3 );
    lua_getfenv( L, 1 );
    lua_pushvalue( L, 2 );
    lua_rawget( L, -2 );
    return 1;
}

static int body_newindex( lua_State * L )
{
    lua_getfenv( L, 1 );
    lua_pushvalue( L, 2 );
    lua_pushvalue( L, 3 );
    lua_rawset( L, -3 );
    return 0;
}

/* ------------------------------------------------------------------------------------------ */
/* World                                                                                       */
/* ------------------------------------------------------------------------------------------ */

/* box3d.newWorld{ gravity = { 0, -10, 0 }, subSteps = 4, workers = 1, enableSleep = true, enableContinuous = true } */
static int l_newWorld( lua_State * L )
{
    int t = lua_istable( L, 1 ) ? 1 : 0;
    b3WorldDef def = b3DefaultWorldDef();
    World * w;
    b3Vec3 g;

    if (optvec3( L, t, "gravity", &g )) def.gravity = g;
    def.workerCount = (uint32_t)optfield( L, t, "workers", 1 );
    if (def.workerCount < 1) def.workerCount = 1;
    def.enableSleep = optbool( L, t, "enableSleep", def.enableSleep );
    def.enableContinuous = optbool( L, t, "enableContinuous", def.enableContinuous );

    w = (World *)lua_newuserdata( L, sizeof( World ) );
    memset( w, 0, sizeof( *w ) );
    w->id = b3CreateWorld( &def );
    w->subSteps = (int)optfield( L, t, "subSteps", 4 );
    if (w->subSteps < 1) w->subSteps = 1;
    w->alive = 1;
    lua_newtable( L );
    w->bodiesRef = luaL_ref( L, LUA_REGISTRYINDEX );
    luaL_getmetatable( L, MT_WORLD );
    lua_setmetatable( L, -2 );
    return 1;
}

static void world_free( lua_State * L, World * w )
{
    int i;

    if (!w->alive) return;
    w->alive = 0;
    b3DestroyWorld( w->id );
    for (i = 0; i < w->meshCount; ++i) b3DestroyMesh( w->meshes[i] );
    free( w->meshes );
    w->meshes = NULL;
    w->meshCount = w->meshCap = 0;
    luaL_unref( L, LUA_REGISTRYINDEX, w->bodiesRef );
    w->bodiesRef = LUA_NOREF;
}

static int w_destroy( lua_State * L )
{
    World * w = (World *)luaL_checkudata( L, 1, MT_WORLD );

    world_free( L, w );
    return 0;
}

/* world:step( dt [, subSteps] ) */
static int w_step( lua_State * L )
{
    World * w = checkworld( L, 1 );
    float dt = (float)luaL_checknumber( L, 2 );
    int sub = (int)luaL_optnumber( L, 3, w->subSteps );

    if (dt > 0.0f) b3World_Step( w->id, dt, sub < 1 ? 1 : sub );
    return 0;
}

static int w_setGravity( lua_State * L )
{
    World * w = checkworld( L, 1 );

    b3World_SetGravity( w->id, checkvec3( L, 2 ) );
    return 0;
}

static int w_getGravity( lua_State * L )
{
    World * w = checkworld( L, 1 );

    pushvec3( L, b3World_GetGravity( w->id ) );
    return 3;
}

static int w_getAwakeBodyCount( lua_State * L )
{
    World * w = checkworld( L, 1 );

    lua_pushnumber( L, b3World_GetAwakeBodyCount( w->id ) );
    return 1;
}

static int w_enableSleeping( lua_State * L )
{
    World * w = checkworld( L, 1 );

    b3World_EnableSleeping( w->id, lua_toboolean( L, 2 ) );
    return 0;
}

static b3BodyType parse_body_type( lua_State * L, int t )
{
    b3BodyType type = b3_dynamicBody;

    if (t == 0) return type;
    lua_getfield( L, t, "type" );
    if (lua_isstring( L, -1 ))
    {
        const char * s = lua_tostring( L, -1 );

        if (0 == strcmp( s, "static" )) type = b3_staticBody;
        else if (0 == strcmp( s, "kinematic" )) type = b3_kinematicBody;
        else if (0 == strcmp( s, "dynamic" )) type = b3_dynamicBody;
        else luaL_error( L, "box3d: type phải là \"static\", \"kinematic\" hoặc \"dynamic\"" );
    }
    lua_pop( L, 1 );
    return type;
}

/* world:newBody{ type, position, rotation | angles, linearVelocity, angularVelocity (độ/s),
                  linearDamping, angularDamping, gravityScale, bullet, enableSleep, awake, lockRotation, name } */
static int w_newBody( lua_State * L )
{
    World * w = checkworld( L, 1 );
    int t = lua_istable( L, 2 ) ? 2 : 0;
    b3BodyDef def = b3DefaultBodyDef();
    b3Vec3 v;
    Body * b;

    def.type = parse_body_type( L, t );
    if (optvec3( L, t, "position", &v )) def.position = b3ToPos( v );
    optrotation( L, t, &def.rotation );
    if (optvec3( L, t, "linearVelocity", &v )) def.linearVelocity = v;
    if (optvec3( L, t, "angularVelocity", &v )) def.angularVelocity = b3MulSV( kDeg, v );
    def.linearDamping = optfield( L, t, "linearDamping", def.linearDamping );
    def.angularDamping = optfield( L, t, "angularDamping", def.angularDamping );
    def.gravityScale = optfield( L, t, "gravityScale", def.gravityScale );
    def.isBullet = optbool( L, t, "bullet", def.isBullet );
    def.enableSleep = optbool( L, t, "enableSleep", def.enableSleep );
    def.isAwake = optbool( L, t, "awake", def.isAwake );
    if (optbool( L, t, "lockRotation", 0 ))
    {
        def.motionLocks.angularX = def.motionLocks.angularY = def.motionLocks.angularZ = true;
    }

    b = (Body *)lua_newuserdata( L, sizeof( Body ) );
    memset( b, 0, sizeof( *b ) );
    b->world = w;
    luaL_getmetatable( L, MT_BODY );
    lua_setmetatable( L, -2 );
    lua_newtable( L ); /* bảng field riêng; giữ world sống */
    lua_pushvalue( L, 1 );
    lua_setfield( L, -2, "world" );
    lua_setfenv( L, -2 );

    lua_rawgeti( L, LUA_REGISTRYINDEX, w->bodiesRef );
    lua_pushvalue( L, -2 );
    b->ref = luaL_ref( L, -2 );
    lua_pop( L, 1 );

    def.userData = (void *)(intptr_t)b->ref;
    b->id = b3CreateBody( w->id, &def );
    return 1;
}

/* world:rayCast( ox, oy, oz, tx, ty, tz ): tia từ gốc o, độ dài = vector t. Trả về bảng hit gần nhất hoặc nil */
static int w_rayCast( lua_State * L )
{
    World * w = checkworld( L, 1 );
    b3Vec3 o = checkvec3( L, 2 ), d = checkvec3( L, 5 );
    b3RayResult r = b3World_CastRayClosest( w->id, b3ToPos( o ), d, b3DefaultQueryFilter() );
    b3Vec3 p;

    if (!r.hit) { lua_pushnil( L ); return 1; }
    p = b3ToVec3( r.point );
    lua_createtable( L, 0, 10 );
    pushbody( L, w, b3Shape_GetBody( r.shapeId ) ); lua_setfield( L, -2, "body" );
    pushshape( L, r.shapeId ); lua_setfield( L, -2, "shape" );
    lua_pushnumber( L, p.x ); lua_setfield( L, -2, "x" );
    lua_pushnumber( L, p.y ); lua_setfield( L, -2, "y" );
    lua_pushnumber( L, p.z ); lua_setfield( L, -2, "z" );
    lua_pushnumber( L, r.normal.x ); lua_setfield( L, -2, "nx" );
    lua_pushnumber( L, r.normal.y ); lua_setfield( L, -2, "ny" );
    lua_pushnumber( L, r.normal.z ); lua_setfield( L, -2, "nz" );
    lua_pushnumber( L, r.fraction ); lua_setfield( L, -2, "fraction" );
    return 1;
}

/* world:explode{ position = { x, y, z }, radius, falloff, impulsePerArea } */
static int w_explode( lua_State * L )
{
    World * w = checkworld( L, 1 );
    b3ExplosionDef def;
    b3Vec3 p = { 0, 0, 0 };

    luaL_checktype( L, 2, LUA_TTABLE );
    memset( &def, 0, sizeof( def ) );
    def.maskBits = UINT64_MAX;
    optvec3( L, 2, "position", &p );
    def.position = b3ToPos( p );
    def.radius = optfield( L, 2, "radius", 5 );
    def.falloff = optfield( L, 2, "falloff", 1 );
    def.impulsePerArea = optfield( L, 2, "impulsePerArea", 1000 ); /* N·s/m²; mật độ mặc định 1000 kg/m³ */
    b3World_Explode( w->id, &def );
    return 0;
}

static void push_pair( lua_State * L, World * w, b3ShapeId a, b3ShapeId b )
{
    lua_createtable( L, 0, 4 );
    pushbody( L, w, b3Shape_GetBody( a ) ); lua_setfield( L, -2, "bodyA" );
    pushbody( L, w, b3Shape_GetBody( b ) ); lua_setfield( L, -2, "bodyB" );
    pushshape( L, a ); lua_setfield( L, -2, "shapeA" );
    pushshape( L, b ); lua_setfield( L, -2, "shapeB" );
}

/* world:getContactEvents() -> { began = {...}, ended = {...}, hit = {...} } (sự kiện của lần step trước) */
static int w_getContactEvents( lua_State * L )
{
    World * w = checkworld( L, 1 );
    b3ContactEvents ev = b3World_GetContactEvents( w->id );
    int i;

    lua_createtable( L, 0, 3 );

    lua_createtable( L, ev.beginCount, 0 );
    for (i = 0; i < ev.beginCount; ++i)
    {
        if (!b3Shape_IsValid( ev.beginEvents[i].shapeIdA ) || !b3Shape_IsValid( ev.beginEvents[i].shapeIdB )) continue;
        push_pair( L, w, ev.beginEvents[i].shapeIdA, ev.beginEvents[i].shapeIdB );
        lua_rawseti( L, -2, (int)lua_objlen( L, -2 ) + 1 );
    }
    lua_setfield( L, -2, "began" );

    lua_createtable( L, ev.endCount, 0 );
    for (i = 0; i < ev.endCount; ++i)
    {
        if (!b3Shape_IsValid( ev.endEvents[i].shapeIdA ) || !b3Shape_IsValid( ev.endEvents[i].shapeIdB )) continue;
        push_pair( L, w, ev.endEvents[i].shapeIdA, ev.endEvents[i].shapeIdB );
        lua_rawseti( L, -2, (int)lua_objlen( L, -2 ) + 1 );
    }
    lua_setfield( L, -2, "ended" );

    lua_createtable( L, ev.hitCount, 0 );
    for (i = 0; i < ev.hitCount; ++i)
    {
        const b3ContactHitEvent * h = ev.hitEvents + i;
        b3Vec3 p = b3ToVec3( h->point );

        if (!b3Shape_IsValid( h->shapeIdA ) || !b3Shape_IsValid( h->shapeIdB )) continue;
        push_pair( L, w, h->shapeIdA, h->shapeIdB );
        lua_pushnumber( L, p.x ); lua_setfield( L, -2, "x" );
        lua_pushnumber( L, p.y ); lua_setfield( L, -2, "y" );
        lua_pushnumber( L, p.z ); lua_setfield( L, -2, "z" );
        lua_pushnumber( L, h->normal.x ); lua_setfield( L, -2, "nx" );
        lua_pushnumber( L, h->normal.y ); lua_setfield( L, -2, "ny" );
        lua_pushnumber( L, h->normal.z ); lua_setfield( L, -2, "nz" );
        lua_pushnumber( L, h->approachSpeed ); lua_setfield( L, -2, "speed" );
        lua_rawseti( L, -2, (int)lua_objlen( L, -2 ) + 1 );
    }
    lua_setfield( L, -2, "hit" );
    return 1;
}

/* world:getMoveEvents() -> mảng { body, x, y, z, qx, qy, qz, qw, sleep }: body đã di chuyển ở step trước.
   Đồng bộ hình bằng danh sách này rẻ hơn hỏi từng body. */
static int w_getMoveEvents( lua_State * L )
{
    World * w = checkworld( L, 1 );
    b3BodyEvents ev = b3World_GetBodyEvents( w->id );
    int i, n = 0;

    lua_createtable( L, ev.moveCount, 0 );
    for (i = 0; i < ev.moveCount; ++i)
    {
        const b3BodyMoveEvent * m = ev.moveEvents + i;
        b3Vec3 p = b3ToVec3( m->transform.p );
        intptr_t ref = (intptr_t)m->userData;

        if (ref <= 0) continue;
        lua_createtable( L, 0, 9 );
        lua_rawgeti( L, LUA_REGISTRYINDEX, w->bodiesRef );
        lua_rawgeti( L, -1, (int)ref );
        lua_remove( L, -2 );
        lua_setfield( L, -2, "body" );
        lua_pushnumber( L, p.x ); lua_setfield( L, -2, "x" );
        lua_pushnumber( L, p.y ); lua_setfield( L, -2, "y" );
        lua_pushnumber( L, p.z ); lua_setfield( L, -2, "z" );
        lua_pushnumber( L, m->transform.q.v.x ); lua_setfield( L, -2, "qx" );
        lua_pushnumber( L, m->transform.q.v.y ); lua_setfield( L, -2, "qy" );
        lua_pushnumber( L, m->transform.q.v.z ); lua_setfield( L, -2, "qz" );
        lua_pushnumber( L, m->transform.q.s ); lua_setfield( L, -2, "qw" );
        lua_pushboolean( L, m->fellAsleep ); lua_setfield( L, -2, "sleep" );
        lua_rawseti( L, -2, ++n );
    }
    return 1;
}

/* world:getBodies() -> mảng mọi body còn sống */
static int w_getBodies( lua_State * L )
{
    World * w = checkworld( L, 1 );
    int n = 0;

    lua_newtable( L );
    lua_rawgeti( L, LUA_REGISTRYINDEX, w->bodiesRef );
    lua_pushnil( L );
    while (lua_next( L, -2 ))
    {
        if (lua_isuserdata( L, -1 ))
        {
            lua_pushvalue( L, -1 );
            lua_rawseti( L, -5, ++n );
        }
        lua_pop( L, 1 );
    }
    lua_pop( L, 1 );
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* Joint                                                                                       */
/* ------------------------------------------------------------------------------------------ */

/* Dựng local frame cho joint từ điểm neo và trục trong toạ độ thế giới.
   localAxis: trục của joint trong frame (revolute/cone: z, prismatic: x). */
static void joint_frames( lua_State * L, int t, b3JointDef * base, b3Vec3 localAxis, World ** worldOut )
{
    Body * a;
    Body * b;
    b3Vec3 anchor = { 0, 0, 0 }, axis = { 0, 0, 0 };
    b3Quat qW = b3Quat_identity;
    b3Quat qa, qb;

    lua_getfield( L, t, "bodyA" );
    a = checkbody( L, -1 );
    lua_pop( L, 1 );
    lua_getfield( L, t, "bodyB" );
    b = checkbody( L, -1 );
    lua_pop( L, 1 );
    if (a->world != b->world) luaL_error( L, "box3d: bodyA và bodyB phải cùng world" );
    *worldOut = a->world;

    if (!optvec3( L, t, "anchor", &anchor )) anchor = b3ToVec3( b3Body_GetPosition( b->id ) );
    if (optvec3( L, t, "axis", &axis ) && b3Length( axis ) > 1e-6f)
    {
        qW = b3ComputeQuatBetweenUnitVectors( localAxis, b3Normalize( axis ) );
    }
    qa = b3Body_GetRotation( a->id );
    qb = b3Body_GetRotation( b->id );
    base->bodyIdA = a->id;
    base->bodyIdB = b->id;
    base->localFrameA.p = b3Body_GetLocalPoint( a->id, b3ToPos( anchor ) );
    base->localFrameB.p = b3Body_GetLocalPoint( b->id, b3ToPos( anchor ) );
    base->localFrameA.q = b3InvMulQuat( qa, qW );
    base->localFrameB.q = b3InvMulQuat( qb, qW );
    base->collideConnected = optbool( L, t, "collideConnected", base->collideConnected );
}

static void pushjoint( lua_State * L, b3JointId id, int type )
{
    Joint * j = (Joint *)lua_newuserdata( L, sizeof( Joint ) );

    j->id = id;
    j->type = type;
    luaL_getmetatable( L, MT_JOINT );
    lua_setmetatable( L, -2 );
}

/* world:newRevoluteJoint{ bodyA, bodyB, anchor, axis, enableLimit, lowerAngle, upperAngle (độ),
                           enableMotor, motorSpeed (độ/s), maxMotorTorque, enableSpring, hertz, dampingRatio } */
static int w_newRevoluteJoint( lua_State * L )
{
    World * w;
    b3RevoluteJointDef def = b3DefaultRevoluteJointDef();

    luaL_checktype( L, 2, LUA_TTABLE );
    checkworld( L, 1 );
    joint_frames( L, 2, &def.base, b3Vec3_axisZ, &w );
    def.enableLimit = optbool( L, 2, "enableLimit", def.enableLimit );
    def.lowerAngle = optfield( L, 2, "lowerAngle", def.lowerAngle / kDeg ) * kDeg;
    def.upperAngle = optfield( L, 2, "upperAngle", def.upperAngle / kDeg ) * kDeg;
    def.enableMotor = optbool( L, 2, "enableMotor", def.enableMotor );
    def.motorSpeed = optfield( L, 2, "motorSpeed", 0 ) * kDeg;
    def.maxMotorTorque = optfield( L, 2, "maxMotorTorque", def.maxMotorTorque );
    def.enableSpring = optbool( L, 2, "enableSpring", def.enableSpring );
    def.hertz = optfield( L, 2, "hertz", def.hertz );
    def.dampingRatio = optfield( L, 2, "dampingRatio", def.dampingRatio );
    pushjoint( L, b3CreateRevoluteJoint( w->id, &def ), b3_revoluteJoint );
    return 1;
}

/* world:newSphericalJoint{ bodyA, bodyB, anchor, axis, enableConeLimit, coneAngle (độ),
                            enableTwistLimit, lowerTwistAngle, upperTwistAngle (độ), enableSpring, hertz, dampingRatio } */
static int w_newSphericalJoint( lua_State * L )
{
    World * w;
    b3SphericalJointDef def = b3DefaultSphericalJointDef();

    luaL_checktype( L, 2, LUA_TTABLE );
    checkworld( L, 1 );
    joint_frames( L, 2, &def.base, b3Vec3_axisZ, &w );
    def.enableConeLimit = optbool( L, 2, "enableConeLimit", def.enableConeLimit );
    def.coneAngle = optfield( L, 2, "coneAngle", def.coneAngle / kDeg ) * kDeg;
    def.enableTwistLimit = optbool( L, 2, "enableTwistLimit", def.enableTwistLimit );
    def.lowerTwistAngle = optfield( L, 2, "lowerTwistAngle", def.lowerTwistAngle / kDeg ) * kDeg;
    def.upperTwistAngle = optfield( L, 2, "upperTwistAngle", def.upperTwistAngle / kDeg ) * kDeg;
    def.enableSpring = optbool( L, 2, "enableSpring", def.enableSpring );
    def.hertz = optfield( L, 2, "hertz", def.hertz );
    def.dampingRatio = optfield( L, 2, "dampingRatio", def.dampingRatio );
    pushjoint( L, b3CreateSphericalJoint( w->id, &def ), b3_sphericalJoint );
    return 1;
}

/* world:newWeldJoint{ bodyA, bodyB, anchor, linearHertz, angularHertz, linearDampingRatio, angularDampingRatio } */
static int w_newWeldJoint( lua_State * L )
{
    World * w;
    b3WeldJointDef def = b3DefaultWeldJointDef();

    luaL_checktype( L, 2, LUA_TTABLE );
    checkworld( L, 1 );
    joint_frames( L, 2, &def.base, b3Vec3_axisZ, &w );
    def.linearHertz = optfield( L, 2, "linearHertz", def.linearHertz );
    def.angularHertz = optfield( L, 2, "angularHertz", def.angularHertz );
    def.linearDampingRatio = optfield( L, 2, "linearDampingRatio", def.linearDampingRatio );
    def.angularDampingRatio = optfield( L, 2, "angularDampingRatio", def.angularDampingRatio );
    pushjoint( L, b3CreateWeldJoint( w->id, &def ), b3_weldJoint );
    return 1;
}

/* world:newPrismaticJoint{ bodyA, bodyB, anchor, axis, enableLimit, lowerTranslation, upperTranslation,
                            enableMotor, motorSpeed, maxMotorForce, enableSpring, hertz, dampingRatio } */
static int w_newPrismaticJoint( lua_State * L )
{
    World * w;
    b3PrismaticJointDef def = b3DefaultPrismaticJointDef();

    luaL_checktype( L, 2, LUA_TTABLE );
    checkworld( L, 1 );
    joint_frames( L, 2, &def.base, b3Vec3_axisX, &w );
    def.enableLimit = optbool( L, 2, "enableLimit", def.enableLimit );
    def.lowerTranslation = optfield( L, 2, "lowerTranslation", def.lowerTranslation );
    def.upperTranslation = optfield( L, 2, "upperTranslation", def.upperTranslation );
    def.enableMotor = optbool( L, 2, "enableMotor", def.enableMotor );
    def.motorSpeed = optfield( L, 2, "motorSpeed", def.motorSpeed );
    def.maxMotorForce = optfield( L, 2, "maxMotorForce", def.maxMotorForce );
    def.enableSpring = optbool( L, 2, "enableSpring", def.enableSpring );
    def.hertz = optfield( L, 2, "hertz", def.hertz );
    def.dampingRatio = optfield( L, 2, "dampingRatio", def.dampingRatio );
    pushjoint( L, b3CreatePrismaticJoint( w->id, &def ), b3_prismaticJoint );
    return 1;
}

/* world:newDistanceJoint{ bodyA, bodyB, anchorA, anchorB (thế giới), length, enableSpring, hertz, dampingRatio,
                           enableLimit, minLength, maxLength, collideConnected } */
static int w_newDistanceJoint( lua_State * L )
{
    World * w = checkworld( L, 1 );
    b3DistanceJointDef def = b3DefaultDistanceJointDef();
    Body * a;
    Body * b;
    b3Vec3 pa, pb;

    luaL_checktype( L, 2, LUA_TTABLE );
    lua_getfield( L, 2, "bodyA" ); a = checkbody( L, -1 ); lua_pop( L, 1 );
    lua_getfield( L, 2, "bodyB" ); b = checkbody( L, -1 ); lua_pop( L, 1 );
    if (!optvec3( L, 2, "anchorA", &pa )) pa = b3ToVec3( b3Body_GetPosition( a->id ) );
    if (!optvec3( L, 2, "anchorB", &pb )) pb = b3ToVec3( b3Body_GetPosition( b->id ) );
    def.base.bodyIdA = a->id;
    def.base.bodyIdB = b->id;
    def.base.localFrameA.p = b3Body_GetLocalPoint( a->id, b3ToPos( pa ) );
    def.base.localFrameB.p = b3Body_GetLocalPoint( b->id, b3ToPos( pb ) );
    def.base.collideConnected = optbool( L, 2, "collideConnected", def.base.collideConnected );
    def.length = optfield( L, 2, "length", b3Length( b3Sub( pb, pa ) ) );
    def.enableSpring = optbool( L, 2, "enableSpring", def.enableSpring );
    def.hertz = optfield( L, 2, "hertz", def.hertz );
    def.dampingRatio = optfield( L, 2, "dampingRatio", def.dampingRatio );
    def.enableLimit = optbool( L, 2, "enableLimit", def.enableLimit );
    def.minLength = optfield( L, 2, "minLength", def.minLength );
    def.maxLength = optfield( L, 2, "maxLength", def.maxLength );
    (void)w;
    pushjoint( L, b3CreateDistanceJoint( w->id, &def ), b3_distanceJoint );
    return 1;
}

static int j_destroy( lua_State * L )
{
    Joint * j = (Joint *)luaL_checkudata( L, 1, MT_JOINT );

    if (b3Joint_IsValid( j->id )) b3DestroyJoint( j->id, true );
    return 0;
}

static int j_isValid( lua_State * L )
{
    Joint * j = (Joint *)luaL_checkudata( L, 1, MT_JOINT );

    lua_pushboolean( L, b3Joint_IsValid( j->id ) );
    return 1;
}

static int j_getType( lua_State * L )
{
    static const char * names[] = { "distance", "filter", "motor", "mover", "parallel", "pogo", "prismatic",
        "revolute", "spherical", "weld", "wheel" };
    Joint * j = checkjoint( L, 1 );
    int t = (int)b3Joint_GetType( j->id );
    const char * n = "unknown";

    switch (t)
    {
    case b3_distanceJoint: n = names[0]; break;
    case b3_prismaticJoint: n = names[6]; break;
    case b3_revoluteJoint: n = names[7]; break;
    case b3_sphericalJoint: n = names[8]; break;
    case b3_weldJoint: n = names[9]; break;
    default: break;
    }
    lua_pushstring( L, n );
    return 1;
}

/* joint:getAngle() (revolute, độ) */
static int j_getAngle( lua_State * L )
{
    Joint * j = checkjoint( L, 1 );

    if (j->type != b3_revoluteJoint) return luaL_error( L, "box3d: getAngle chỉ cho revolute joint" );
    lua_pushnumber( L, b3RevoluteJoint_GetAngle( j->id ) / kDeg );
    return 1;
}

/* joint:getTranslation() (prismatic) */
static int j_getTranslation( lua_State * L )
{
    Joint * j = checkjoint( L, 1 );

    if (j->type != b3_prismaticJoint) return luaL_error( L, "box3d: getTranslation chỉ cho prismatic joint" );
    lua_pushnumber( L, b3PrismaticJoint_GetTranslation( j->id ) );
    return 1;
}

/* joint:enableMotor( on [, speed [, maxTorqueOrForce]] ): revolute speed theo độ/s, prismatic theo m/s */
static int j_enableMotor( lua_State * L )
{
    Joint * j = checkjoint( L, 1 );
    int on = lua_toboolean( L, 2 );

    if (j->type == b3_revoluteJoint)
    {
        b3RevoluteJoint_EnableMotor( j->id, on );
        if (lua_isnumber( L, 3 )) b3RevoluteJoint_SetMotorSpeed( j->id, (float)lua_tonumber( L, 3 ) * kDeg );
        if (lua_isnumber( L, 4 )) b3RevoluteJoint_SetMaxMotorTorque( j->id, (float)lua_tonumber( L, 4 ) );
    }
    else if (j->type == b3_prismaticJoint)
    {
        b3PrismaticJoint_EnableMotor( j->id, on );
        if (lua_isnumber( L, 3 )) b3PrismaticJoint_SetMotorSpeed( j->id, (float)lua_tonumber( L, 3 ) );
        if (lua_isnumber( L, 4 )) b3PrismaticJoint_SetMaxMotorForce( j->id, (float)lua_tonumber( L, 4 ) );
    }
    else return luaL_error( L, "box3d: enableMotor chỉ cho revolute / prismatic joint" );
    b3Joint_WakeBodies( j->id );
    return 0;
}

/* joint:setLimits( lower, upper ): revolute theo độ, prismatic theo mét; bật luôn limit */
static int j_setLimits( lua_State * L )
{
    Joint * j = checkjoint( L, 1 );
    float lo = (float)luaL_checknumber( L, 2 ), hi = (float)luaL_checknumber( L, 3 );

    if (j->type == b3_revoluteJoint)
    {
        b3RevoluteJoint_SetLimits( j->id, lo * kDeg, hi * kDeg );
        b3RevoluteJoint_EnableLimit( j->id, true );
    }
    else if (j->type == b3_prismaticJoint)
    {
        b3PrismaticJoint_SetLimits( j->id, lo, hi );
        b3PrismaticJoint_EnableLimit( j->id, true );
    }
    else return luaL_error( L, "box3d: setLimits chỉ cho revolute / prismatic joint" );
    return 0;
}

/* joint:setLength( len ) (distance) */
static int j_setLength( lua_State * L )
{
    Joint * j = checkjoint( L, 1 );

    if (j->type != b3_distanceJoint) return luaL_error( L, "box3d: setLength chỉ cho distance joint" );
    b3DistanceJoint_SetLength( j->id, (float)luaL_checknumber( L, 2 ) );
    return 0;
}

/* joint:getConstraintForce() -> fx, fy, fz */
static int j_getConstraintForce( lua_State * L )
{
    Joint * j = checkjoint( L, 1 );

    pushvec3( L, b3Joint_GetConstraintForce( j->id ) );
    return 3;
}

/* ------------------------------------------------------------------------------------------ */
/* Body                                                                                        */
/* ------------------------------------------------------------------------------------------ */

static b3ShapeDef shape_def( lua_State * L, int t )
{
    b3ShapeDef def = b3DefaultShapeDef();

    def.density = optfield( L, t, "density", def.density );
    def.baseMaterial.friction = optfield( L, t, "friction", def.baseMaterial.friction );
    def.baseMaterial.restitution = optfield( L, t, "restitution", def.baseMaterial.restitution );
    def.baseMaterial.rollingResistance = optfield( L, t, "rollingResistance", def.baseMaterial.rollingResistance );
    def.isSensor = optbool( L, t, "sensor", def.isSensor );
    def.enableSensorEvents = optbool( L, t, "sensorEvents", def.isSensor ? true : def.enableSensorEvents );
    /* Box3D mặc định tắt contact event; bật sẵn như Box2D v3 để getContactEvents() có dữ liệu */
    def.enableContactEvents = optbool( L, t, "contactEvents", true );
    def.enableHitEvents = optbool( L, t, "hitEvents", def.enableHitEvents );
    if (t && lua_istable( L, t ))
    {
        lua_getfield( L, t, "categoryBits" );
        if (lua_isnumber( L, -1 )) def.filter.categoryBits = (uint64_t)lua_tonumber( L, -1 );
        lua_pop( L, 1 );
        lua_getfield( L, t, "maskBits" );
        if (lua_isnumber( L, -1 )) def.filter.maskBits = (uint64_t)lua_tonumber( L, -1 );
        lua_pop( L, 1 );
        lua_getfield( L, t, "groupIndex" );
        if (lua_isnumber( L, -1 )) def.filter.groupIndex = (int)lua_tonumber( L, -1 );
        lua_pop( L, 1 );
    }
    return def;
}

/* opts.offset + opts.rotation/angles -> transform cục bộ của shape */
static b3Transform shape_local( lua_State * L, int t )
{
    b3Transform x = b3Transform_identity;
    b3Vec3 o;

    if (optvec3( L, t, "offset", &o )) x.p = o;
    optrotation( L, t, &x.q );
    return x;
}

/* body:addBox( hx, hy, hz [, opts] ): nửa cạnh theo trục X, Y, Z */
static int b_addBox( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    float hx = (float)luaL_checknumber( L, 2 ), hy = (float)luaL_checknumber( L, 3 ), hz = (float)luaL_checknumber( L, 4 );
    int t = lua_istable( L, 5 ) ? 5 : 0;
    b3ShapeDef def = shape_def( L, t );
    b3Transform x = shape_local( L, t );
    b3BoxHull box = b3MakeTransformedBoxHull( hx, hy, hz, x );

    pushshape( L, b3CreateHullShape( b->id, &def, &box.base ) );
    return 1;
}

/* body:addSphere( radius [, opts] ), opts.offset = tâm */
static int b_addSphere( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    int t = lua_istable( L, 3 ) ? 3 : 0;
    b3ShapeDef def = shape_def( L, t );
    b3Sphere s;

    s.center = b3Vec3_zero;
    optvec3( L, t, "offset", &s.center );
    s.radius = (float)luaL_checknumber( L, 2 );
    pushshape( L, b3CreateSphereShape( b->id, &def, &s ) );
    return 1;
}

/* body:addCapsule( x1, y1, z1, x2, y2, z2, radius [, opts] ) */
static int b_addCapsule( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    int t = lua_istable( L, 9 ) ? 9 : 0;
    b3ShapeDef def = shape_def( L, t );
    b3Capsule c;

    c.center1 = checkvec3( L, 2 );
    c.center2 = checkvec3( L, 5 );
    c.radius = (float)luaL_checknumber( L, 8 );
    pushshape( L, b3CreateCapsuleShape( b->id, &def, &c ) );
    return 1;
}

static b3ShapeId create_hull_shape( lua_State * L, Body * b, b3HullData * hull, int t )
{
    b3ShapeDef def = shape_def( L, t );
    b3Transform x = shape_local( L, t );
    b3ShapeId id;

    if (!hull) luaL_error( L, "box3d: không tạo được convex hull (điểm thẳng hàng / quá nhiều điểm?)" );
    if (x.p.x != 0 || x.p.y != 0 || x.p.z != 0 || x.q.s != 1.0f)
    {
        b3Vec3 one = { 1, 1, 1 };

        id = b3CreateTransformedHullShape( b->id, &def, hull, x, one );
    }
    else
        id = b3CreateHullShape( b->id, &def, hull );
    b3DestroyHull( hull );
    return id;
}

/* đọc mảng điểm: phẳng { x1, y1, z1, x2, ... } hoặc lồng { { x, y, z }, ... } */
static b3Vec3 * read_points( lua_State * L, int i, int * count )
{
    int n = (int)lua_objlen( L, i ), k, m = 0;
    b3Vec3 * pts;

    luaL_checktype( L, i, LUA_TTABLE );
    lua_rawgeti( L, i, 1 );
    if (lua_istable( L, -1 ))
    {
        lua_pop( L, 1 );
        pts = (b3Vec3 *)malloc( sizeof( b3Vec3 ) * ( n > 0 ? n : 1 ) );
        for (k = 1; k <= n; ++k)
        {
            float v[3] = { 0, 0, 0 };
            int c;

            lua_rawgeti( L, i, k );
            for (c = 0; c < 3; ++c)
            {
                lua_rawgeti( L, -1, c + 1 );
                v[c] = (float)lua_tonumber( L, -1 );
                lua_pop( L, 1 );
            }
            lua_pop( L, 1 );
            pts[m].x = v[0]; pts[m].y = v[1]; pts[m].z = v[2];
            ++m;
        }
    }
    else
    {
        lua_pop( L, 1 );
        pts = (b3Vec3 *)malloc( sizeof( b3Vec3 ) * ( n / 3 + 1 ) );
        for (k = 0; k + 2 < n; k += 3)
        {
            lua_rawgeti( L, i, k + 1 ); pts[m].x = (float)lua_tonumber( L, -1 );
            lua_rawgeti( L, i, k + 2 ); pts[m].y = (float)lua_tonumber( L, -1 );
            lua_rawgeti( L, i, k + 3 ); pts[m].z = (float)lua_tonumber( L, -1 );
            lua_pop( L, 3 );
            ++m;
        }
    }
    *count = m;
    return pts;
}

/* body:addHull( points [, opts] ): bao lồi của các điểm (tối đa B3_MAX_HULL_VERTICES đỉnh giữ lại) */
static int b_addHull( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    int t = lua_istable( L, 3 ) ? 3 : 0;
    int n = 0;
    b3Vec3 * pts = read_points( L, 2, &n );
    b3HullData * hull;
    b3ShapeId id;

    if (n < 4) { free( pts ); return luaL_error( L, "box3d: addHull cần ít nhất 4 điểm" ); }
    hull = b3CreateHull( pts, n, (int)optfield( L, t, "maxVertices", 64 ) );
    free( pts );
    id = create_hull_shape( L, b, hull, t );
    pushshape( L, id );
    return 1;
}

/* body:addCylinder( height, radius [, sides [, opts]] ): trụ đứng theo Y, tâm ở gốc */
static int b_addCylinder( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    float h = (float)luaL_checknumber( L, 2 ), r = (float)luaL_checknumber( L, 3 );
    int sides = (int)luaL_optnumber( L, 4, 16 );
    int t = lua_istable( L, 5 ) ? 5 : 0;
    b3HullData * hull = b3CreateCylinder( h, r, -0.5f * h, sides < 3 ? 3 : sides );

    pushshape( L, create_hull_shape( L, b, hull, t ) );
    return 1;
}

/* body:addMesh( vertices, indices [, opts] ): lưới tam giác (nên dùng cho body static).
   vertices: phẳng { x, y, z, ... }; indices: phẳng, đánh số từ 1 như Lua. */
static int b_addMesh( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    int t = lua_istable( L, 4 ) ? 4 : 0;
    int nv = 0, ni, k;
    b3Vec3 * verts = read_points( L, 2, &nv );
    int32_t * idx;
    b3MeshDef md;
    b3MeshData * mesh;
    b3ShapeDef def;
    b3Vec3 scale = { 1, 1, 1 };

    luaL_checktype( L, 3, LUA_TTABLE );
    ni = (int)lua_objlen( L, 3 );
    if (ni < 3 || ni % 3) { free( verts ); return luaL_error( L, "box3d: addMesh cần số chỉ số là bội của 3" ); }
    idx = (int32_t *)malloc( sizeof( int32_t ) * ni );
    for (k = 0; k < ni; ++k)
    {
        lua_rawgeti( L, 3, k + 1 );
        idx[k] = (int32_t)lua_tonumber( L, -1 ) - 1;
        lua_pop( L, 1 );
        if (idx[k] < 0 || idx[k] >= nv) { free( verts ); free( idx ); return luaL_error( L, "box3d: chỉ số %d ngoài khoảng", k + 1 ); }
    }
    memset( &md, 0, sizeof( md ) );
    md.vertices = verts;
    md.stride = sizeof( b3Vec3 );
    md.indices = idx;
    md.vertexCount = nv;
    md.triangleCount = ni / 3;
    md.weldVertices = true;
    md.weldTolerance = 0.0001f;
    mesh = b3CreateMesh( &md, NULL, 0 );
    free( verts );
    free( idx );
    if (!mesh) return luaL_error( L, "box3d: không tạo được mesh" );

    if (b->world->meshCount == b->world->meshCap)
    {
        int cap = b->world->meshCap ? b->world->meshCap * 2 : 8;

        b->world->meshes = (b3MeshData **)realloc( b->world->meshes, sizeof( b3MeshData * ) * cap );
        b->world->meshCap = cap;
    }
    b->world->meshes[b->world->meshCount++] = mesh;

    def = shape_def( L, t );
    optvec3( L, t, "scale", &scale );
    pushshape( L, b3CreateMeshShape( b->id, &def, mesh, scale ) );
    return 1;
}

static int b_getPosition( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    pushvec3( L, b3ToVec3( b3Body_GetPosition( b->id ) ) );
    return 3;
}

static int b_getRotation( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    pushquat( L, b3Body_GetRotation( b->id ) );
    return 4;
}

/* body:getTransform() -> x, y, z, qx, qy, qz, qw */
static int b_getTransform( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    b3WorldTransform x = b3Body_GetTransform( b->id );

    pushvec3( L, b3ToVec3( x.p ) );
    pushquat( L, x.q );
    return 7;
}

/* body:setTransform( x, y, z [, qx, qy, qz, qw] ) */
static int b_setTransform( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    b3Vec3 p = checkvec3( L, 2 );
    b3Quat q = b3Body_GetRotation( b->id );

    if (lua_isnumber( L, 5 ))
    {
        q.v.x = (float)luaL_checknumber( L, 5 );
        q.v.y = (float)luaL_checknumber( L, 6 );
        q.v.z = (float)luaL_checknumber( L, 7 );
        q.s = (float)luaL_checknumber( L, 8 );
        q = b3NormalizeQuat( q );
    }
    b3Body_SetTransform( b->id, b3ToPos( p ), q );
    return 0;
}

static int b_getLinearVelocity( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    pushvec3( L, b3Body_GetLinearVelocity( b->id ) );
    return 3;
}

static int b_setLinearVelocity( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_SetLinearVelocity( b->id, checkvec3( L, 2 ) );
    return 0;
}

static int b_getAngularVelocity( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    pushvec3( L, b3MulSV( 1.0f / kDeg, b3Body_GetAngularVelocity( b->id ) ) );
    return 3;
}

static int b_setAngularVelocity( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_SetAngularVelocity( b->id, b3MulSV( kDeg, checkvec3( L, 2 ) ) );
    return 0;
}

/* body:applyForce( fx, fy, fz [, px, py, pz] ): không có điểm thì đặt vào khối tâm */
static int b_applyForce( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    b3Vec3 f = checkvec3( L, 2 );

    if (lua_isnumber( L, 5 )) b3Body_ApplyForce( b->id, f, b3ToPos( checkvec3( L, 5 ) ), true );
    else b3Body_ApplyForceToCenter( b->id, f, true );
    return 0;
}

static int b_applyImpulse( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    b3Vec3 f = checkvec3( L, 2 );

    if (lua_isnumber( L, 5 )) b3Body_ApplyLinearImpulse( b->id, f, b3ToPos( checkvec3( L, 5 ) ), true );
    else b3Body_ApplyLinearImpulseToCenter( b->id, f, true );
    return 0;
}

static int b_applyTorque( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_ApplyTorque( b->id, checkvec3( L, 2 ), true );
    return 0;
}

static int b_applyAngularImpulse( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_ApplyAngularImpulse( b->id, checkvec3( L, 2 ), true );
    return 0;
}

static int b_getMass( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    lua_pushnumber( L, b3Body_GetMass( b->id ) );
    return 1;
}

static int b_getType( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    b3BodyType t = b3Body_GetType( b->id );

    lua_pushstring( L, t == b3_staticBody ? "static" : ( t == b3_kinematicBody ? "kinematic" : "dynamic" ) );
    return 1;
}

static int b_setType( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    const char * s = luaL_checkstring( L, 2 );
    b3BodyType t = b3_dynamicBody;

    if (0 == strcmp( s, "static" )) t = b3_staticBody;
    else if (0 == strcmp( s, "kinematic" )) t = b3_kinematicBody;
    b3Body_SetType( b->id, t );
    return 0;
}

static int b_isAwake( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    lua_pushboolean( L, b3Body_IsAwake( b->id ) );
    return 1;
}

static int b_setAwake( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_SetAwake( b->id, lua_toboolean( L, 2 ) );
    return 0;
}

static int b_setBullet( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_SetBullet( b->id, lua_toboolean( L, 2 ) );
    return 0;
}

static int b_setGravityScale( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_SetGravityScale( b->id, (float)luaL_checknumber( L, 2 ) );
    return 0;
}

static int b_setDamping( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    b3Body_SetLinearDamping( b->id, (float)luaL_checknumber( L, 2 ) );
    if (lua_isnumber( L, 3 )) b3Body_SetAngularDamping( b->id, (float)lua_tonumber( L, 3 ) );
    return 0;
}

static int b_getWorldPoint( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    pushvec3( L, b3ToVec3( b3Body_GetWorldPoint( b->id, checkvec3( L, 2 ) ) ) );
    return 3;
}

static int b_getLocalPoint( lua_State * L )
{
    Body * b = checkbody( L, 1 );

    pushvec3( L, b3Body_GetLocalPoint( b->id, b3ToPos( checkvec3( L, 2 ) ) ) );
    return 3;
}

static int b_isValid( lua_State * L )
{
    Body * b = (Body *)luaL_checkudata( L, 1, MT_BODY );

    lua_pushboolean( L, b->world && b->world->alive && b3Body_IsValid( b->id ) );
    return 1;
}

static int b_destroy( lua_State * L )
{
    Body * b = (Body *)luaL_checkudata( L, 1, MT_BODY );

    if (b->world && b->world->alive && b3Body_IsValid( b->id ))
    {
        b3DestroyBody( b->id );
        lua_rawgeti( L, LUA_REGISTRYINDEX, b->world->bodiesRef );
        luaL_unref( L, -1, b->ref );
        lua_pop( L, 1 );
    }
    b->ref = 0;
    return 0;
}

static int b_getShapes( lua_State * L )
{
    Body * b = checkbody( L, 1 );
    int n = b3Body_GetShapeCount( b->id ), i;
    b3ShapeId * ids = (b3ShapeId *)malloc( sizeof( b3ShapeId ) * ( n > 0 ? n : 1 ) );

    n = b3Body_GetShapes( b->id, ids, n );
    lua_createtable( L, n, 0 );
    for (i = 0; i < n; ++i)
    {
        pushshape( L, ids[i] );
        lua_rawseti( L, -2, i + 1 );
    }
    free( ids );
    return 1;
}

static int b_tostring( lua_State * L )
{
    Body * b = (Body *)luaL_checkudata( L, 1, MT_BODY );

    lua_pushfstring( L, "box3d.Body(%d)", (int)b->id.index1 );
    return 1;
}

static int b_eq( lua_State * L )
{
    Body * a = (Body *)luaL_checkudata( L, 1, MT_BODY );
    Body * b = (Body *)luaL_checkudata( L, 2, MT_BODY );

    lua_pushboolean( L, B3_ID_EQUALS( a->id, b->id ) );
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* Shape                                                                                       */
/* ------------------------------------------------------------------------------------------ */

static int s_setFriction( lua_State * L ) { b3Shape_SetFriction( checkshape( L, 1 )->id, (float)luaL_checknumber( L, 2 ) ); return 0; }
static int s_setRestitution( lua_State * L ) { b3Shape_SetRestitution( checkshape( L, 1 )->id, (float)luaL_checknumber( L, 2 ) ); return 0; }
static int s_setDensity( lua_State * L ) { b3Shape_SetDensity( checkshape( L, 1 )->id, (float)luaL_checknumber( L, 2 ), true ); return 0; }
static int s_getFriction( lua_State * L ) { lua_pushnumber( L, b3Shape_GetFriction( checkshape( L, 1 )->id ) ); return 1; }
static int s_getRestitution( lua_State * L ) { lua_pushnumber( L, b3Shape_GetRestitution( checkshape( L, 1 )->id ) ); return 1; }
static int s_isSensor( lua_State * L ) { lua_pushboolean( L, b3Shape_IsSensor( checkshape( L, 1 )->id ) ); return 1; }
static int s_isValid( lua_State * L ) { lua_pushboolean( L, b3Shape_IsValid( ( (Shape *)luaL_checkudata( L, 1, MT_SHAPE ) )->id ) ); return 1; }

static int s_destroy( lua_State * L )
{
    Shape * s = (Shape *)luaL_checkudata( L, 1, MT_SHAPE );

    if (b3Shape_IsValid( s->id )) b3DestroyShape( s->id, true );
    return 0;
}

static int s_eq( lua_State * L )
{
    Shape * a = (Shape *)luaL_checkudata( L, 1, MT_SHAPE );
    Shape * b = (Shape *)luaL_checkudata( L, 2, MT_SHAPE );

    lua_pushboolean( L, B3_ID_EQUALS( a->id, b->id ) );
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* Tiện ích toán                                                                               */
/* ------------------------------------------------------------------------------------------ */

/* box3d.quatFromEuler( rx, ry, rz ) (độ, thứ tự Y * X * Z như s3d) -> qx, qy, qz, qw */
static int l_quatFromEuler( lua_State * L )
{
    pushquat( L, quat_from_euler( (float)luaL_checknumber( L, 1 ), (float)luaL_checknumber( L, 2 ), (float)luaL_checknumber( L, 3 ) ) );
    return 4;
}

/* box3d.quatFromAxisAngle( ax, ay, az, deg ) */
static int l_quatFromAxisAngle( lua_State * L )
{
    b3Vec3 a = checkvec3( L, 1 );

    pushquat( L, b3MakeQuatFromAxisAngle( b3Normalize( a ), (float)luaL_checknumber( L, 4 ) * kDeg ) );
    return 4;
}

/* box3d.quatMul( q1..., q2... ) */
static int l_quatMul( lua_State * L )
{
    b3Quat a, b;

    a.v = checkvec3( L, 1 ); a.s = (float)luaL_checknumber( L, 4 );
    b.v = checkvec3( L, 5 ); b.s = (float)luaL_checknumber( L, 8 );
    pushquat( L, b3MulQuat( a, b ) );
    return 4;
}

/* box3d.rotate( qx, qy, qz, qw, vx, vy, vz ) -> vector đã xoay */
static int l_rotate( lua_State * L )
{
    b3Quat q;

    q.v = checkvec3( L, 1 ); q.s = (float)luaL_checknumber( L, 4 );
    pushvec3( L, b3RotateVector( q, checkvec3( L, 5 ) ) );
    return 3;
}

/* ------------------------------------------------------------------------------------------ */
/* Đăng ký                                                                                     */
/* ------------------------------------------------------------------------------------------ */

static void newclass( lua_State * L, const char * name, const luaL_Reg * methods, const luaL_Reg * meta )
{
    luaL_newmetatable( L, name );
    lua_newtable( L );
    luaL_register( L, NULL, methods );
    if (0 == strcmp( name, MT_BODY ))
    {
        lua_setfield( L, -2, "__methods" );
        lua_pushcfunction( L, body_index ); lua_setfield( L, -2, "__index" );
        lua_pushcfunction( L, body_newindex ); lua_setfield( L, -2, "__newindex" );
    }
    else
    {
        lua_setfield( L, -2, "__index" );
    }
    if (meta) luaL_register( L, NULL, meta );
    lua_pop( L, 1 );
}

static int w_gc( lua_State * L )
{
    World * w = (World *)luaL_checkudata( L, 1, MT_WORLD );

    world_free( L, w );
    return 0;
}

static int w_tostring( lua_State * L )
{
    World * w = (World *)luaL_checkudata( L, 1, MT_WORLD );

    lua_pushfstring( L, "box3d.World(%s)", w->alive ? "alive" : "destroyed" );
    return 1;
}

B3P_EXPORT int luaopen_plugin_box3d( lua_State * L )
{
    static const luaL_Reg worldMethods[] = {
        { "step", w_step }, { "destroy", w_destroy }, { "setGravity", w_setGravity }, { "getGravity", w_getGravity },
        { "newBody", w_newBody }, { "rayCast", w_rayCast }, { "explode", w_explode },
        { "getContactEvents", w_getContactEvents }, { "getMoveEvents", w_getMoveEvents }, { "getBodies", w_getBodies },
        { "getAwakeBodyCount", w_getAwakeBodyCount }, { "enableSleeping", w_enableSleeping },
        { "newRevoluteJoint", w_newRevoluteJoint }, { "newSphericalJoint", w_newSphericalJoint },
        { "newWeldJoint", w_newWeldJoint }, { "newPrismaticJoint", w_newPrismaticJoint },
        { "newDistanceJoint", w_newDistanceJoint },
        { NULL, NULL } };
    static const luaL_Reg worldMeta[] = { { "__gc", w_gc }, { "__tostring", w_tostring }, { NULL, NULL } };
    static const luaL_Reg bodyMethods[] = {
        { "addBox", b_addBox }, { "addSphere", b_addSphere }, { "addCapsule", b_addCapsule }, { "addHull", b_addHull },
        { "addCylinder", b_addCylinder }, { "addMesh", b_addMesh },
        { "getPosition", b_getPosition }, { "getRotation", b_getRotation }, { "getTransform", b_getTransform },
        { "setTransform", b_setTransform }, { "getLinearVelocity", b_getLinearVelocity },
        { "setLinearVelocity", b_setLinearVelocity }, { "getAngularVelocity", b_getAngularVelocity },
        { "setAngularVelocity", b_setAngularVelocity }, { "applyForce", b_applyForce }, { "applyImpulse", b_applyImpulse },
        { "applyTorque", b_applyTorque }, { "applyAngularImpulse", b_applyAngularImpulse }, { "getMass", b_getMass },
        { "getType", b_getType }, { "setType", b_setType }, { "isAwake", b_isAwake }, { "setAwake", b_setAwake },
        { "setBullet", b_setBullet }, { "setGravityScale", b_setGravityScale }, { "setDamping", b_setDamping },
        { "getWorldPoint", b_getWorldPoint }, { "getLocalPoint", b_getLocalPoint }, { "getShapes", b_getShapes },
        { "isValid", b_isValid }, { "destroy", b_destroy },
        { NULL, NULL } };
    static const luaL_Reg bodyMeta[] = { { "__tostring", b_tostring }, { "__eq", b_eq }, { NULL, NULL } };
    static const luaL_Reg shapeMethods[] = {
        { "setFriction", s_setFriction }, { "setRestitution", s_setRestitution }, { "setDensity", s_setDensity },
        { "getFriction", s_getFriction }, { "getRestitution", s_getRestitution }, { "isSensor", s_isSensor },
        { "isValid", s_isValid }, { "destroy", s_destroy },
        { NULL, NULL } };
    static const luaL_Reg shapeMeta[] = { { "__eq", s_eq }, { NULL, NULL } };
    static const luaL_Reg jointMethods[] = {
        { "destroy", j_destroy }, { "isValid", j_isValid }, { "getType", j_getType }, { "getAngle", j_getAngle },
        { "getTranslation", j_getTranslation }, { "enableMotor", j_enableMotor }, { "setLimits", j_setLimits },
        { "setLength", j_setLength }, { "getConstraintForce", j_getConstraintForce },
        { NULL, NULL } };
    static const luaL_Reg lib[] = {
        { "newWorld", l_newWorld }, { "quatFromEuler", l_quatFromEuler }, { "quatFromAxisAngle", l_quatFromAxisAngle },
        { "quatMul", l_quatMul }, { "rotate", l_rotate },
        { NULL, NULL } };

    newclass( L, MT_WORLD, worldMethods, worldMeta );
    newclass( L, MT_BODY, bodyMethods, bodyMeta );
    newclass( L, MT_SHAPE, shapeMethods, shapeMeta );
    newclass( L, MT_JOINT, jointMethods, NULL );

    lua_newtable( L );
    luaL_register( L, NULL, lib );
    lua_pushstring( L, B3P_VERSION ); lua_setfield( L, -2, "version" );
    lua_pushstring( L, "box3d (erincatto/box3d)" ); lua_setfield( L, -2, "engine" );
    return 1;
}
