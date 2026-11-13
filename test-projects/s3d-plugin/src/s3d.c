/*
 * s3d.c - a small 3D plugin for Solar2D, built only on the public native APIs (no engine changes).
 *
 * How it works:
 *   - A mesh is display.newMesh{ mode = "triangles", hasZ = true } ("indexed" meshes are not used: they live
 *     on the GPU, so geometry writers are never called for them).
 *   - The "filter.s3d.*" effects carry the "s3d" data type:
 *       before(): installs geometry writers for "position" (multiplies by the model matrix) and "userData"
 *       (transformed normal), enables the depth/cull state block, draws, issues the camera/light uniform command, disables the state block.
 *   - Shell transform "s3d": a_Position becomes a vec3, gl_Position = u_S3D_ViewProj * vec4(a_Position, 1).
 *   - Matrix math uses cglm.
 *
 * Build:
 *   Windows (Simulator / Win32): define S3D_DYNAMIC; every Lua/Corona/GL function is resolved at run time
 *   (GetProcAddress on already loaded modules), so neither lua.lib nor CoronaLabs.Corona.Native.lib is needed.
 *   Android: no S3D_DYNAMIC; links directly against liblua.so, libcorona.so and libGLESv2.so.
 */

#define CGLM_ALL_UNALIGNED
#include <cglm/cglm.h>

#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include "lua.h"
#include "lauxlib.h"
#include "s3d_corona.h"

#define S3D_VERSION "0.5.0"

/* ========================================================================== */
/* Function binding: resolved at run time on Windows, linked directly on Android            */
/* ========================================================================== */

#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE  0x0B44
#define GL_BACK       0x0405
#define GL_CCW        0x0901
#define GL_CW         0x0900
#define GL_LEQUAL     0x0203

#ifdef S3D_DYNAMIC

#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>

#define S3D_EXPORT __declspec(dllexport)
#define GLAPIENTRY_ __stdcall

static void * s3d_find_symbol( const char * name )
{
    HMODULE mods[1024];
    DWORD needed = 0, i;
    void * p = (void *)GetProcAddress( GetModuleHandleA( NULL ), name );

    if (p) return p;

    if (K32EnumProcessModules( GetCurrentProcess(), mods, sizeof( mods ), &needed ))
    {
        DWORD n = needed / sizeof( HMODULE );

        if (n > 1024) n = 1024;
        for (i = 0; i < n; ++i)
        {
            p = (void *)GetProcAddress( mods[i], name );
            if (p) return p;
        }
    }
    return NULL;
}

/* --- Corona --- */
#define S3D_DECL_PTR( ret, name, params ) typedef ret ( * name##_t ) params; static name##_t p_##name;
S3D_CORONA_FUNCS( S3D_DECL_PTR )
#define CORONA( name ) p_##name

/* --- OpenGL 1.1 (opengl32.dll) --- */
typedef void ( GLAPIENTRY_ * glEnable_t )( unsigned int );
typedef void ( GLAPIENTRY_ * glDepthFunc_t )( unsigned int );
typedef void ( GLAPIENTRY_ * glDepthMask_t )( unsigned char );
typedef void ( GLAPIENTRY_ * glCullFace_t )( unsigned int );
typedef void ( GLAPIENTRY_ * glFrontFace_t )( unsigned int );
typedef void ( GLAPIENTRY_ * glGetIntegerv_t )( unsigned int, int * );
typedef void ( GLAPIENTRY_ * glClear_t )( unsigned int );
typedef unsigned int ( GLAPIENTRY_ * glGetError_t )( void );
static glGetError_t p_glGetError;
typedef void ( GLAPIENTRY_ * glPolygonOffset_t )( float, float );
static glPolygonOffset_t p_glPolygonOffset;
typedef void ( GLAPIENTRY_ * glStencilFunc_t )( unsigned int, int, unsigned int );
typedef void ( GLAPIENTRY_ * glStencilOp_t )( unsigned int, unsigned int, unsigned int );
typedef void ( GLAPIENTRY_ * glStencilMask_t )( unsigned int );
typedef void ( GLAPIENTRY_ * glClearStencil_t )( int );
static glStencilFunc_t p_glStencilFunc;
static glStencilOp_t p_glStencilOp;
static glStencilMask_t p_glStencilMask;
static glClearStencil_t p_glClearStencil;
static glEnable_t p_glEnable, p_glDisable;
static glDepthFunc_t p_glDepthFunc;
static glDepthMask_t p_glDepthMask;
static glCullFace_t p_glCullFace;
static glFrontFace_t p_glFrontFace;
static glGetIntegerv_t p_glGetIntegerv;
static glClear_t p_glClear;

/* OpenGL 2.0+/3.0 (FBO): fetched with wglGetProcAddress once a context exists, i.e. on the render thread */
typedef void ( GLAPIENTRY_ * glGenRenderbuffers_t )( int, unsigned int * );
typedef void ( GLAPIENTRY_ * glBindRenderbuffer_t )( unsigned int, unsigned int );
typedef void ( GLAPIENTRY_ * glRenderbufferStorage_t )( unsigned int, unsigned int, int, int );
typedef void ( GLAPIENTRY_ * glFramebufferRenderbuffer_t )( unsigned int, unsigned int, unsigned int, unsigned int );
typedef void ( GLAPIENTRY_ * glDeleteRenderbuffers_t )( int, const unsigned int * );
static glGenRenderbuffers_t p_glGenRenderbuffers;
static glBindRenderbuffer_t p_glBindRenderbuffer;
static glRenderbufferStorage_t p_glRenderbufferStorage;
static glFramebufferRenderbuffer_t p_glFramebufferRenderbuffer;
static glDeleteRenderbuffers_t p_glDeleteRenderbuffers;

/* OpenGL 1.1 (opengl32.dll), used by the shadow map */
typedef void ( GLAPIENTRY_ * glGenTextures_t )( int, unsigned int * );
typedef void ( GLAPIENTRY_ * glBindTexture_t )( unsigned int, unsigned int );
typedef void ( GLAPIENTRY_ * glTexImage2D_t )( unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void * );
typedef void ( GLAPIENTRY_ * glTexParameteri_t )( unsigned int, unsigned int, int );
typedef void ( GLAPIENTRY_ * glViewport_t )( int, int, int, int );
typedef void ( GLAPIENTRY_ * glClearColor_t )( float, float, float, float );
typedef void ( GLAPIENTRY_ * glGetFloatv_t )( unsigned int, float * );
static glGenTextures_t p_glGenTextures;
static glBindTexture_t p_glBindTexture;
static glTexImage2D_t p_glTexImage2D;
static glTexParameteri_t p_glTexParameteri;
static glViewport_t p_glViewport;
static glClearColor_t p_glClearColor;
static glGetFloatv_t p_glGetFloatv;

/* OpenGL 2.0 / 3.0 (wglGetProcAddress) */
typedef void ( GLAPIENTRY_ * glGenFramebuffers_t )( int, unsigned int * );
typedef void ( GLAPIENTRY_ * glBindFramebuffer_t )( unsigned int, unsigned int );
typedef void ( GLAPIENTRY_ * glFramebufferTexture2D_t )( unsigned int, unsigned int, unsigned int, unsigned int, int );
typedef void ( GLAPIENTRY_ * glActiveTexture_t )( unsigned int );
typedef int ( GLAPIENTRY_ * glGetUniformLocation_t )( unsigned int, const char * );
typedef void ( GLAPIENTRY_ * glUniform1i_t )( int, int );
typedef unsigned int ( GLAPIENTRY_ * glCheckFramebufferStatus_t )( unsigned int );
static glCheckFramebufferStatus_t p_glCheckFramebufferStatus;
static glGenFramebuffers_t p_glGenFramebuffers;
static glBindFramebuffer_t p_glBindFramebuffer;
static glFramebufferTexture2D_t p_glFramebufferTexture2D;
static glActiveTexture_t p_glActiveTexture;
static glGetUniformLocation_t p_glGetUniformLocation;
static glUniform1i_t p_glUniform1i;
typedef unsigned char ( GLAPIENTRY_ * glIsProgram_t )( unsigned int );
typedef void ( GLAPIENTRY_ * glUseProgram_t )( unsigned int );
static glIsProgram_t p_glIsProgram;
static glUseProgram_t p_glUseProgram;

/* --- Lua 5.1 (lua.dll): redefine the functions we use and forward them through pointers --- */
#define S3D_LUA_FUNCS(X) \
    X( int, lua_gettop, ( lua_State * L ), ( L ) ) \
    X( void, lua_settop, ( lua_State * L, int i ), ( L, i ) ) \
    X( void, lua_pushvalue, ( lua_State * L, int i ), ( L, i ) ) \
    X( int, lua_type, ( lua_State * L, int i ), ( L, i ) ) \
    X( int, lua_isnumber, ( lua_State * L, int i ), ( L, i ) ) \
    X( lua_Number, lua_tonumber, ( lua_State * L, int i ), ( L, i ) ) \
    X( int, lua_toboolean, ( lua_State * L, int i ), ( L, i ) ) \
    X( const char *, lua_tolstring, ( lua_State * L, int i, size_t * len ), ( L, i, len ) ) \
    X( size_t, lua_objlen, ( lua_State * L, int i ), ( L, i ) ) \
    X( void *, lua_touserdata, ( lua_State * L, int i ), ( L, i ) ) \
    X( void, lua_pushnil, ( lua_State * L ), ( L ) ) \
    X( void, lua_pushnumber, ( lua_State * L, lua_Number n ), ( L, n ) ) \
    X( void, lua_pushstring, ( lua_State * L, const char * s ), ( L, s ) ) \
    X( void, lua_pushboolean, ( lua_State * L, int b ), ( L, b ) ) \
    X( void, lua_pushcclosure, ( lua_State * L, lua_CFunction f, int n ), ( L, f, n ) ) \
    X( void, lua_createtable, ( lua_State * L, int a, int b ), ( L, a, b ) ) \
    X( void *, lua_newuserdata, ( lua_State * L, size_t sz ), ( L, sz ) ) \
    X( void, lua_getfield, ( lua_State * L, int i, const char * k ), ( L, i, k ) ) \
    X( void, lua_setfield, ( lua_State * L, int i, const char * k ), ( L, i, k ) ) \
    X( void, lua_rawgeti, ( lua_State * L, int i, int n ), ( L, i, n ) ) \
    X( void, lua_call, ( lua_State * L, int a, int r ), ( L, a, r ) ) \
    X( int, lua_error, ( lua_State * L ), ( L ) ) \
    X( int, luaL_loadbuffer, ( lua_State * L, const char * b, size_t sz, const char * name ), ( L, b, sz, name ) )

#define S3D_LUA_PTR( ret, name, params, args ) typedef ret ( * name##_t ) params; static name##_t p_##name;
S3D_LUA_FUNCS( S3D_LUA_PTR )
#define S3D_LUA_STUB( ret, name, params, args ) ret name params { return ( ret ) p_##name args; }
#define S3D_LUA_STUB_VOID( ret, name, params, args ) ret name params { p_##name args; }

int lua_gettop( lua_State * L ) { return p_lua_gettop( L ); }
void lua_settop( lua_State * L, int i ) { p_lua_settop( L, i ); }
void lua_pushvalue( lua_State * L, int i ) { p_lua_pushvalue( L, i ); }
int lua_type( lua_State * L, int i ) { return p_lua_type( L, i ); }
int lua_isnumber( lua_State * L, int i ) { return p_lua_isnumber( L, i ); }
lua_Number lua_tonumber( lua_State * L, int i ) { return p_lua_tonumber( L, i ); }
int lua_toboolean( lua_State * L, int i ) { return p_lua_toboolean( L, i ); }
const char * lua_tolstring( lua_State * L, int i, size_t * len ) { return p_lua_tolstring( L, i, len ); }
size_t lua_objlen( lua_State * L, int i ) { return p_lua_objlen( L, i ); }
void * lua_touserdata( lua_State * L, int i ) { return p_lua_touserdata( L, i ); }
void lua_pushnil( lua_State * L ) { p_lua_pushnil( L ); }
void lua_pushnumber( lua_State * L, lua_Number n ) { p_lua_pushnumber( L, n ); }
void lua_pushstring( lua_State * L, const char * s ) { p_lua_pushstring( L, s ); }
void lua_pushboolean( lua_State * L, int b ) { p_lua_pushboolean( L, b ); }
void lua_pushcclosure( lua_State * L, lua_CFunction f, int n ) { p_lua_pushcclosure( L, f, n ); }
void lua_createtable( lua_State * L, int a, int b ) { p_lua_createtable( L, a, b ); }
void * lua_newuserdata( lua_State * L, size_t sz ) { return p_lua_newuserdata( L, sz ); }
void lua_getfield( lua_State * L, int i, const char * k ) { p_lua_getfield( L, i, k ); }
void lua_setfield( lua_State * L, int i, const char * k ) { p_lua_setfield( L, i, k ); }
void lua_rawgeti( lua_State * L, int i, int n ) { p_lua_rawgeti( L, i, n ); }
void lua_call( lua_State * L, int a, int r ) { p_lua_call( L, a, r ); }
int lua_error( lua_State * L ) { return p_lua_error( L ); }
int luaL_loadbuffer( lua_State * L, const char * b, size_t sz, const char * name ) { return p_luaL_loadbuffer( L, b, sz, name ); }

static const char * s3d_bind_all( void )
{
    HMODULE gl;

#define S3D_BIND_CORONA( ret, name, params ) p_##name = ( name##_t )s3d_find_symbol( #name ); if (!p_##name) return #name;
    S3D_CORONA_FUNCS( S3D_BIND_CORONA )
#define S3D_BIND_LUA( ret, name, params, args ) p_##name = ( name##_t )s3d_find_symbol( #name ); if (!p_##name) return #name;
    S3D_LUA_FUNCS( S3D_BIND_LUA )

    gl = GetModuleHandleA( "opengl32.dll" );
    if (!gl) gl = LoadLibraryA( "opengl32.dll" );
    if (!gl) return "opengl32.dll";
    p_glEnable = (glEnable_t)GetProcAddress( gl, "glEnable" );
    p_glDisable = (glEnable_t)GetProcAddress( gl, "glDisable" );
    p_glDepthFunc = (glDepthFunc_t)GetProcAddress( gl, "glDepthFunc" );
    p_glDepthMask = (glDepthMask_t)GetProcAddress( gl, "glDepthMask" );
    p_glCullFace = (glCullFace_t)GetProcAddress( gl, "glCullFace" );
    p_glFrontFace = (glFrontFace_t)GetProcAddress( gl, "glFrontFace" );
    p_glGetIntegerv = (glGetIntegerv_t)GetProcAddress( gl, "glGetIntegerv" );
    p_glClear = (glClear_t)GetProcAddress( gl, "glClear" );
    p_glGetError = (glGetError_t)GetProcAddress( gl, "glGetError" );
    p_glPolygonOffset = (glPolygonOffset_t)GetProcAddress( gl, "glPolygonOffset" );
    p_glStencilFunc = (glStencilFunc_t)GetProcAddress( gl, "glStencilFunc" );
    p_glStencilOp = (glStencilOp_t)GetProcAddress( gl, "glStencilOp" );
    p_glStencilMask = (glStencilMask_t)GetProcAddress( gl, "glStencilMask" );
    p_glClearStencil = (glClearStencil_t)GetProcAddress( gl, "glClearStencil" );
    p_glGenTextures = (glGenTextures_t)GetProcAddress( gl, "glGenTextures" );
    p_glBindTexture = (glBindTexture_t)GetProcAddress( gl, "glBindTexture" );
    p_glTexImage2D = (glTexImage2D_t)GetProcAddress( gl, "glTexImage2D" );
    p_glTexParameteri = (glTexParameteri_t)GetProcAddress( gl, "glTexParameteri" );
    p_glViewport = (glViewport_t)GetProcAddress( gl, "glViewport" );
    p_glClearColor = (glClearColor_t)GetProcAddress( gl, "glClearColor" );
    p_glGetFloatv = (glGetFloatv_t)GetProcAddress( gl, "glGetFloatv" );
    if (!p_glEnable || !p_glDisable || !p_glDepthFunc || !p_glDepthMask || !p_glCullFace || !p_glFrontFace
        || !p_glGetIntegerv || !p_glClear || !p_glGetError || !p_glPolygonOffset
        || !p_glStencilFunc || !p_glStencilOp || !p_glStencilMask || !p_glClearStencil
        || !p_glGenTextures || !p_glBindTexture || !p_glTexImage2D || !p_glTexParameteri
        || !p_glViewport || !p_glClearColor || !p_glGetFloatv) return "glEnable";
    return NULL;
}

#define GLCALL( name ) p_##name

#else /* direct linking (Android, iOS, Windows builds with import libraries) */

#define S3D_PROTO( ret, name, params ) extern ret name params;
S3D_CORONA_FUNCS( S3D_PROTO )
#if defined( S3D_OWN_GLES2 )
  #include "s3d_gles2.h" /* builds without the NDK */
#elif defined( __ANDROID__ )
  #include <GLES2/gl2.h>
#elif defined( __APPLE__ )
  #include <OpenGLES/ES2/gl.h>
#else
  #include <GL/gl.h>
#endif

#if defined( _WIN32 )
  #define S3D_EXPORT __declspec(dllexport)
#else
  #define S3D_EXPORT __attribute__((visibility("default")))
#endif

#define CORONA( name ) name
#define GLCALL( name ) name
static const char * s3d_bind_all( void ) { return NULL; }

#endif

/* ========================================================================== */
/* Lua helpers                                                                  */
/* ========================================================================== */

static int s3d_error( lua_State * L, const char * fmt, ... )
{
    char buf[512];
    va_list ap;

    va_start( ap, fmt );
    vsnprintf( buf, sizeof( buf ), fmt, ap );
    va_end( ap );
    lua_pushstring( L, buf );
    return lua_error( L );
}

static float s3d_optnum( lua_State * L, int i, float def )
{
    return lua_isnumber( L, i ) ? (float)lua_tonumber( L, i ) : def;
}

/* t[n] (1-based) of the table at absolute index idx */
static float s3d_tablenum( lua_State * L, int idx, int n, float def )
{
    float v;

    lua_rawgeti( L, idx, n );
    v = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : def;
    lua_settop( L, -2 );
    return v;
}

/* ========================================================================== */
/* Global state                                                                 */
/* ========================================================================== */

/* Sent to the render thread every time a 3D object is drawn (placed right before the draw). */
typedef struct FrameUniforms {
    float viewProj[16];
    float lightDir[4];   /* xyz: direction TOWARDS the light (normalized), w: ambient */
    float lightColor[4]; /* rgb, a: unused */
    float fog[4];        /* rgb: color, a: 1 = enabled */
    float fogRange[4];   /* x: start, y: end (distance to the camera) */
    float camPos[4];     /* CPU side only (fog) */
} FrameUniforms;

typedef struct DepthState {
    unsigned char depthTest;
    unsigned char depthWrite;
    unsigned char cull;      /* 0 = none, 1 = cull back faces */
    unsigned char depthLess; /* 1 = GL_LESS (planar shadows: never blend twice) */
    unsigned char offset;    /* 1 = polygon offset towards the camera (shadow wins over the coplanar floor) */
    unsigned char pad[3];
} DepthState;

typedef struct StateCommand {
    DepthState s;
    unsigned int frame;
} StateCommand;

enum {
    kFlag_HasModel  = 1u << 0,
    kFlag_NoCull    = 1u << 1,
    kFlag_NoDepth   = 1u << 2,
    kFlag_NoDepthWr = 1u << 3,
    kFlag_Shadow    = 1u << 4,
    kFlag_NoLightU  = 1u << 5,  /* effect has no u_S3D_LightDir / u_S3D_LightColor */
    kFlag_NoFogU    = 1u << 6,  /* effect has no u_S3D_Fog / u_S3D_FogRange */
    kFlag_Caster    = 1u << 7,  /* copy drawn into the shadow map (camera = light) */
    kFlag_PassBegin = 1u << 8,  /* marker: switch to the shadow map FBO */
    kFlag_PassEnd   = 1u << 9,  /* marker: restore Solar2D's FBO */
    kFlag_NoShadowU = 1u << 10  /* effect does not sample the shadow map */
};

typedef struct UniformCommand {
    FrameUniforms f;
    float lightVP[16];
    float shadowParams[4]; /* x: enabled, y: bias, z: 1/size, w: darkness */
    unsigned int flags;
} UniformCommand;

typedef struct PassCommand {
    int begin;
    int size;
} PassCommand;

#define GL_LESS                0x0201
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_STENCIL_TEST        0x0B90
#define GL_EQUAL               0x0202
#define GL_KEEP                0x1E00
#define GL_INCR                0x1E02
#define GL_STENCIL_BUFFER_BIT  0x00000400
#define GL_STENCIL_ATTACHMENT  0x8D20
#define GL_DEPTH24_STENCIL8    0x88F0
#define GL_VIEWPORT            0x0BA2
#define GL_DEPTH_BUFFER_BIT    0x00000100
#define GL_FRAMEBUFFER         0x8D40
#define GL_RENDERBUFFER        0x8D41
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_DEPTH_ATTACHMENT    0x8D00
#define GL_DEPTH_COMPONENT16   0x81A5
#define GL_DEPTH_COMPONENT24   0x81A6
#define GL_COLOR_ATTACHMENT0   0x8CE0
#define GL_TEXTURE_2D          0x0DE1
#define GL_RGBA                0x1908
#define GL_UNSIGNED_BYTE       0x1401
#define GL_TEXTURE_MIN_FILTER  0x2801
#define GL_TEXTURE_MAG_FILTER  0x2800
#define GL_TEXTURE_WRAP_S      0x2802
#define GL_TEXTURE_WRAP_T      0x2803
#define GL_NEAREST             0x2600
#define GL_CLAMP_TO_EDGE       0x812F
#define GL_COLOR_BUFFER_BIT    0x00004000
#define GL_COLOR_CLEAR_VALUE   0x0C22
#define GL_ACTIVE_TEXTURE      0x84E0
#define GL_TEXTURE0            0x84C0
#define GL_CURRENT_PROGRAM     0x8B8D
#define GL_TEXTURE_BINDING_2D  0x8069
#define S3D_SHADOW_UNIT        7

/* The extraSpace of each effect instance (allocated and zeroed by Solar2D). It must not hold heap pointers
   that need freeing, because Solar2D may memcpy an instance when cloning it. */
typedef struct Instance {
    float model[16];
    float normalMat[9];   /* inverse-transpose 3x3 of the model matrix */
    const float * normals;   /* points into a Lua userdata kept alive by the display object */
    unsigned int normalCount;
    unsigned int flags;
    /* frame animation (loadOBJAnimation): position / normal = lerp( A, B, morphT ); buffers are owned by Lua */
    const float * morphPA;
    const float * morphPB;
    const float * morphNA;
    const float * morphNB;
    unsigned int morphCount; /* number of vertices in the buffers */
    float morphT;
} Instance;

static FrameUniforms gFrame;
static unsigned long gStateBlockID, gStateCmdID, gUniformCmdID;
static const DepthState kDepthOff = { 0, 1, 0, 0, 0, { 0, 0, 0 } };
static unsigned int gFrameId = 1;
static float gShadowMat[16];
static float gShadowPlaneY = 0.0f;

/* shadow map */
static unsigned long gPassCmdID;
static float gLightVP[16];
static float gShadowParams[4] = { 0, 0.003f, 1.0f / 1024, 0.6f };
static int gShadowSize = 1024;
static float gShadowCenter[3] = { 0, 0, 0 };
static float gShadowArea = 10, gShadowDist = 30;
static unsigned int gShadowFbo, gShadowTex, gShadowRb;
static int gShadowAlloc;
static int gSavedFbo, gSavedViewport[4];
static float gSavedClear[4];


/* ========================================================================== */
/* Commands that run on the render thread                                       */
/* ========================================================================== */

/* --- Solar2D FBOs (snapshot, canvas) have no depth buffer: the plugin attaches its own renderbuffer --- */
typedef struct FboDepth {
    int fbo;
    unsigned int rb;
    int w, h;
    unsigned int frame;
    int hasStencil;
} FboDepth;

#define S3D_MAX_FBOS 16
static FboDepth gFbos[S3D_MAX_FBOS];

static int s3d_gl_fbo_procs( void )
{
#ifdef S3D_DYNAMIC
    static int tried = 0;

    if (!tried)
    {
        HMODULE gl = GetModuleHandleA( "opengl32.dll" );
        typedef PROC ( WINAPI * wglGetProcAddress_t )( LPCSTR );
        wglGetProcAddress_t getProc = gl ? (wglGetProcAddress_t)GetProcAddress( gl, "wglGetProcAddress" ) : NULL;

        tried = 1;
        if (getProc)
        {
#define S3D_GLPROC( name ) p_##name = ( name##_t )getProc( #name ); if (!p_##name) p_##name = ( name##_t )getProc( #name "EXT" );
            S3D_GLPROC( glGenRenderbuffers )
            S3D_GLPROC( glBindRenderbuffer )
            S3D_GLPROC( glRenderbufferStorage )
            S3D_GLPROC( glFramebufferRenderbuffer )
            S3D_GLPROC( glDeleteRenderbuffers )
            S3D_GLPROC( glGenFramebuffers )
            S3D_GLPROC( glBindFramebuffer )
            S3D_GLPROC( glFramebufferTexture2D )
            S3D_GLPROC( glActiveTexture )
            S3D_GLPROC( glGetUniformLocation )
            S3D_GLPROC( glUniform1i )
            S3D_GLPROC( glCheckFramebufferStatus )
            S3D_GLPROC( glIsProgram )
            S3D_GLPROC( glUseProgram )
        }
    }
    return p_glGenRenderbuffers && p_glBindRenderbuffer && p_glRenderbufferStorage && p_glFramebufferRenderbuffer && p_glDeleteRenderbuffers
        && p_glGenFramebuffers && p_glBindFramebuffer && p_glFramebufferTexture2D && p_glActiveTexture
        && p_glGetUniformLocation && p_glUniform1i && p_glIsProgram && p_glUseProgram;
#else
    return 1;
#endif
}

/* Drawing into an FBO: make sure it has depth and clear the depth once per frame. Returns 1 when in an FBO. */
static int s3d_prepare_fbo_depth( unsigned int frame )
{
    int fbo = 0, vp[4] = { 0, 0, 0, 0 }, i, slot = -1, oldest = 0;
    FboDepth * e;

    GLCALL( glGetIntegerv )( GL_FRAMEBUFFER_BINDING, &fbo );
    if (0 == fbo) return 0;
    if (gShadowFbo && (unsigned int)fbo == gShadowFbo) return 1; /* shadow pass: depth is managed by the plugin */
    if (!s3d_gl_fbo_procs()) return 1;

    GLCALL( glGetIntegerv )( GL_VIEWPORT, vp );
    for (i = 0; i < S3D_MAX_FBOS; ++i)
    {
        if (gFbos[i].fbo == fbo) { slot = i; break; }
        if (gFbos[i].frame < gFbos[oldest].frame) oldest = i;
    }
    if (slot < 0)
    {
        slot = oldest;
        if (gFbos[slot].rb) GLCALL( glDeleteRenderbuffers )( 1, &gFbos[slot].rb );
        memset( &gFbos[slot], 0, sizeof( FboDepth ) );
        gFbos[slot].fbo = fbo;
    }
    e = &gFbos[slot];

    if (e->frame == frame && e->rb) return 1; /* already prepared this frame */

    if (!e->rb || e->w != vp[2] || e->h != vp[3])
    {
        if (e->rb) GLCALL( glDeleteRenderbuffers )( 1, &e->rb );
        GLCALL( glGenRenderbuffers )( 1, &e->rb );
        GLCALL( glBindRenderbuffer )( GL_RENDERBUFFER, e->rb );
        while (GLCALL( glGetError )()) {} /* drop stale errors */
        /* prefer depth 24 + stencil 8 (stencil keeps planar shadows from blending twice), then depth 24, then depth 16 */
        e->hasStencil = 1;
        GLCALL( glRenderbufferStorage )( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, vp[2], vp[3] );
        if (GLCALL( glGetError )())
        {
            e->hasStencil = 0;
            GLCALL( glRenderbufferStorage )( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, vp[2], vp[3] );
            if (GLCALL( glGetError )())
            {
                GLCALL( glRenderbufferStorage )( GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, vp[2], vp[3] );
            }
        }
        GLCALL( glBindRenderbuffer )( GL_RENDERBUFFER, 0 );
        e->w = vp[2]; e->h = vp[3];
    }
    /* re-attach every frame: Solar2D may have reused the FBO id */
    GLCALL( glFramebufferRenderbuffer )( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, e->rb );
    GLCALL( glFramebufferRenderbuffer )( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, e->hasStencil ? e->rb : 0 );
    GLCALL( glDepthMask )( 1 );
    GLCALL( glStencilMask )( 0xFF );
    GLCALL( glClearStencil )( 0 );
    GLCALL( glClear )( GL_DEPTH_BUFFER_BIT | ( e->hasStencil ? GL_STENCIL_BUFFER_BIT : 0 ) );
    e->frame = frame;
    return 1;
}

static void s3d_state_reader( const CoronaCommandBuffer * cb, const unsigned char * from, unsigned int size )
{
    StateCommand c;
    int inFbo = 0;

    (void)cb;
    if (size < sizeof( StateCommand )) return;
    memcpy( &c, from, sizeof( c ) );

    if (c.s.depthTest)
    {
        inFbo = s3d_prepare_fbo_depth( c.frame );
        GLCALL( glEnable )( GL_DEPTH_TEST );
        GLCALL( glDepthFunc )( c.s.depthLess ? GL_LESS : GL_LEQUAL );
    }
    else
    {
        GLCALL( glDisable )( GL_DEPTH_TEST );
    }
    GLCALL( glDepthMask )( c.s.depthWrite ? 1 : 0 );

    if (c.s.offset)
    {
        GLCALL( glEnable )( GL_POLYGON_OFFSET_FILL );
        GLCALL( glPolygonOffset )( -1.0f, -2.0f );
        /* each pixel receives a planar shadow only once per frame (when the framebuffer has stencil) */
        GLCALL( glEnable )( GL_STENCIL_TEST );
        GLCALL( glStencilMask )( 0xFF );
        GLCALL( glStencilFunc )( GL_EQUAL, 0, 0xFF );
        GLCALL( glStencilOp )( GL_KEEP, GL_KEEP, GL_INCR );
    }
    else
    {
        GLCALL( glDisable )( GL_POLYGON_OFFSET_FILL );
        GLCALL( glDisable )( GL_STENCIL_TEST );
    }

    if (c.s.cull)
    {
        if (!c.s.depthTest) GLCALL( glGetIntegerv )( GL_FRAMEBUFFER_BINDING, &inFbo );
        GLCALL( glEnable )( GL_CULL_FACE );
        /* inside an FBO the shader flips y, so the front-face winding flips too */
        GLCALL( glFrontFace )( inFbo ? GL_CW : GL_CCW );
        GLCALL( glCullFace )( GL_BACK );
    }
    else
    {
        GLCALL( glDisable )( GL_CULL_FACE );
    }
}

/* (Re)create the shadow map FBO: RGBA8 texture holding packed depth + a depth renderbuffer */
static int s3d_ensure_shadow_target( int size )
{
    if (!s3d_gl_fbo_procs()) return 0;
    if (gShadowFbo && gShadowAlloc == size) return 1;

    if (!gShadowFbo)
    {
        GLCALL( glGenFramebuffers )( 1, &gShadowFbo );
        GLCALL( glGenTextures )( 1, &gShadowTex );
        GLCALL( glGenRenderbuffers )( 1, &gShadowRb );
    }

    GLCALL( glBindTexture )( GL_TEXTURE_2D, gShadowTex );
    GLCALL( glTexImage2D )( GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
    GLCALL( glTexParameteri )( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    GLCALL( glTexParameteri )( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
    GLCALL( glTexParameteri )( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    GLCALL( glTexParameteri )( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

    GLCALL( glBindRenderbuffer )( GL_RENDERBUFFER, gShadowRb );
    while (GLCALL( glGetError )()) {}
    GLCALL( glRenderbufferStorage )( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size );
    if (GLCALL( glGetError )()) GLCALL( glRenderbufferStorage )( GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, size, size );
    GLCALL( glBindRenderbuffer )( GL_RENDERBUFFER, 0 );

    GLCALL( glBindFramebuffer )( GL_FRAMEBUFFER, gShadowFbo );
    GLCALL( glFramebufferTexture2D )( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gShadowTex, 0 );
    GLCALL( glFramebufferRenderbuffer )( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, gShadowRb );
    gShadowAlloc = size;
    return 1;
}

/*
 * Set u_S3D_ShadowMap = unit 7 on every program that has this sampler.
 * This cannot be done inside the uniform command: when it runs, the program of the next draw is not bound yet
 * (e.g. the object drawn right after the shadow pass would set it on the depth program). Sampler values are stored
 * per program, so each program only needs it once; programs are rescanned to catch newly created ones.
 */
#define S3D_MAX_PROGRAM_SCAN 2048
static unsigned char gProgramDone[S3D_MAX_PROGRAM_SCAN];

static unsigned int gProgramScanFrame; /* frames since init, used to throttle the scan */
static unsigned int gProgramMaxId;     /* largest program id seen so far */

static void s3d_assign_shadow_samplers( void )
{
    int prev = 0;
    unsigned int id, limit;
    int switched = 0;

    if (!s3d_gl_fbo_procs()) return;
    /* glIsProgram is not cheap on mobile drivers: scan every frame for the first 30 frames (Solar2D is creating
       programs then), then once every 30 frames, and only up to the largest id seen + 64 */
    ++gProgramScanFrame;
    if (gProgramScanFrame > 30 && ( gProgramScanFrame % 30 ) != 0) return;
    limit = gProgramMaxId + 64;
    if (limit < 256) limit = 256;
    if (limit > S3D_MAX_PROGRAM_SCAN) limit = S3D_MAX_PROGRAM_SCAN;
    while (GLCALL( glGetError )() != 0) {}
    GLCALL( glGetIntegerv )( GL_CURRENT_PROGRAM, &prev );
    for (id = 1; id < limit; ++id)
    {
        if (!GLCALL( glIsProgram )( id )) { gProgramDone[id] = 0; continue; }
        if (id > gProgramMaxId) gProgramMaxId = id;
        if (gProgramDone[id]) continue;
        gProgramDone[id] = 1;
        {
            int loc = GLCALL( glGetUniformLocation )( id, "u_S3D_ShadowMap" );

            if (GLCALL( glGetError )() != 0) { gProgramDone[id] = 0; continue; } /* not linked yet: retry next frame */
            if (loc < 0) continue;
            GLCALL( glUseProgram )( id );
            GLCALL( glUniform1i )( loc, S3D_SHADOW_UNIT );
            switched = 1;
        }
    }
    if (switched) GLCALL( glUseProgram )( (unsigned int)prev );
    while (GLCALL( glGetError )() != 0) {} /* ignore errors from programs that are not linked yet */
}

static void s3d_pass_reader( const CoronaCommandBuffer * cb, const unsigned char * from, unsigned int size )
{
    PassCommand pc;

    (void)cb;
    if (size < sizeof( PassCommand )) return;
    memcpy( &pc, from, sizeof( pc ) );
    if (!s3d_gl_fbo_procs()) return;

    if (pc.begin)
    {
        int activeUnit = GL_TEXTURE0, boundTex = 0;

        GLCALL( glGetIntegerv )( GL_FRAMEBUFFER_BINDING, &gSavedFbo );
        GLCALL( glGetIntegerv )( GL_VIEWPORT, gSavedViewport );
        GLCALL( glGetFloatv )( GL_COLOR_CLEAR_VALUE, gSavedClear );

        /* create the texture on the shadow map unit so Solar2D's bound textures are left alone */
        GLCALL( glGetIntegerv )( GL_ACTIVE_TEXTURE, &activeUnit );
        GLCALL( glActiveTexture )( GL_TEXTURE0 + S3D_SHADOW_UNIT );
        GLCALL( glGetIntegerv )( GL_TEXTURE_BINDING_2D, &boundTex );
        if (!s3d_ensure_shadow_target( pc.size ))
        {
            GLCALL( glActiveTexture )( (unsigned int)activeUnit );
            return;
        }
        GLCALL( glBindTexture )( GL_TEXTURE_2D, 0 ); /* do not sample the texture being written */
        GLCALL( glActiveTexture )( (unsigned int)activeUnit );

        GLCALL( glBindFramebuffer )( GL_FRAMEBUFFER, gShadowFbo );
        GLCALL( glViewport )( 0, 0, pc.size, pc.size );
        GLCALL( glClearColor )( 1, 1, 1, 1 ); /* farthest depth */
        GLCALL( glDepthMask )( 1 );
        GLCALL( glClear )( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        (void)boundTex;
    }
    else if (gShadowFbo)
    {
        int activeUnit = GL_TEXTURE0;

        GLCALL( glBindFramebuffer )( GL_FRAMEBUFFER, (unsigned int)gSavedFbo );
        GLCALL( glViewport )( gSavedViewport[0], gSavedViewport[1], gSavedViewport[2], gSavedViewport[3] );
        GLCALL( glClearColor )( gSavedClear[0], gSavedClear[1], gSavedClear[2], gSavedClear[3] );

        GLCALL( glGetIntegerv )( GL_ACTIVE_TEXTURE, &activeUnit );
        GLCALL( glActiveTexture )( GL_TEXTURE0 + S3D_SHADOW_UNIT );
        GLCALL( glBindTexture )( GL_TEXTURE_2D, gShadowTex );
        GLCALL( glActiveTexture )( (unsigned int)activeUnit );
        s3d_assign_shadow_samplers();
    }
}


static void s3d_uniform_reader( const CoronaCommandBuffer * cb, const unsigned char * from, unsigned int size )
{
    CoronaWriteUniformParams p;
    unsigned int flags;
    const unsigned char * f = from + offsetof( UniformCommand, f );

    if (size < sizeof( UniformCommand )) return;
    memcpy( &flags, from + offsetof( UniformCommand, flags ), sizeof( flags ) );
    p.useOffset = 0;
    /* copy drawn in the shadow pass: the "camera" is the light */
    p.u.data = ( flags & kFlag_Caster ) ? from + offsetof( UniformCommand, lightVP ) : f + offsetof( FrameUniforms, viewProj );
    CORONA( CoronaCommandBufferWriteNamedUniform )( cb, "u_S3D_ViewProj", &p, 16 * sizeof( float ) );
    if (( flags & kFlag_Caster ) || !( flags & kFlag_NoShadowU ))
    {
        p.u.data = from + offsetof( UniformCommand, lightVP );
        CORONA( CoronaCommandBufferWriteNamedUniform )( cb, "u_S3D_LightVP", &p, 16 * sizeof( float ) );
    }
    if (!( flags & kFlag_Caster ) && !( flags & kFlag_NoShadowU ))
    {
        p.u.data = from + offsetof( UniformCommand, shadowParams );
        CORONA( CoronaCommandBufferWriteNamedUniform )( cb, "u_S3D_ShadowParams", &p, 4 * sizeof( float ) );
    }
    if (!( flags & kFlag_NoLightU ))
    {
        p.u.data = f + offsetof( FrameUniforms, lightDir );
        CORONA( CoronaCommandBufferWriteNamedUniform )( cb, "u_S3D_LightDir", &p, 4 * sizeof( float ) );
        p.u.data = f + offsetof( FrameUniforms, lightColor );
        CORONA( CoronaCommandBufferWriteNamedUniform )( cb, "u_S3D_LightColor", &p, 4 * sizeof( float ) );
    }
    if (!( flags & kFlag_NoFogU ))
    {
        p.u.data = f + offsetof( FrameUniforms, fog );
        CORONA( CoronaCommandBufferWriteNamedUniform )( cb, "u_S3D_Fog", &p, 4 * sizeof( float ) );
        p.u.data = f + offsetof( FrameUniforms, fogRange );
        CORONA( CoronaCommandBufferWriteNamedUniform )( cb, "u_S3D_FogRange", &p, 4 * sizeof( float ) );
    }
}

/* State block handler: called while the command buffer is built; issues a command so the render thread changes GL state. */
static void s3d_state_dirty( const CoronaCommandBuffer * cb, const CoronaRenderer * renderer,
    const void * newContents, const void * oldContents, unsigned int size, int restore, void * userData )
{
    StateCommand c;

    (void)cb; (void)oldContents; (void)size; (void)restore; (void)userData;
    memcpy( &c.s, newContents, sizeof( DepthState ) );
    c.frame = gFrameId;
    CORONA( CoronaRendererIssueCommand )( renderer, gStateCmdID, &c, sizeof( c ) );
}

/* ========================================================================== */
/* Geometry writer                                                              */
/* ========================================================================== */

/* "position", update kind: dest already holds the original coordinates (x, y, z); multiply by the model matrix. */
static void s3d_position_writer( void * dest, const void * context, const CoronaGeometryMappingLayout * layout,
    unsigned int index, unsigned int n )
{
    const Instance * inst = (const Instance *)context;
    unsigned char * d = (unsigned char *)dest;
    unsigned int k;

    for (k = 0; k < n; ++k, d += layout->outStride)
    {
        float * p = (float *)d;
        unsigned int vi = index + k;
        vec4 in, out;

        if (inst->morphPA && vi < inst->morphCount)
        {
            const float * a = inst->morphPA + 3 * vi;
            const float * b = inst->morphPB + 3 * vi;
            float t = inst->morphT;

            in[0] = a[0] + ( b[0] - a[0] ) * t;
            in[1] = a[1] + ( b[1] - a[1] ) * t;
            in[2] = a[2] + ( b[2] - a[2] ) * t;
        }
        else
        {
            in[0] = p[0]; in[1] = p[1]; in[2] = p[2];
        }
        in[3] = 1.0f;
        if (inst->flags & kFlag_HasModel)
        {
            glm_mat4_mulv( *(mat4 *)inst->model, in, out );
        }
        else
        {
            glm_vec4_copy( in, out );
        }

        if (inst->flags & kFlag_Shadow)
        {
            /* project onto the plane y = planeY along the light direction, lifted slightly to avoid z-fighting with the floor */
            vec4 s;

            glm_mat4_mulv( *(mat4 *)gShadowMat, out, s );
            if (s[3] != 0.0f) glm_vec4_scale( s, 1.0f / s[3], s );
            out[0] = s[0]; out[1] = gShadowPlaneY + 0.0005f; /* polygon offset handles the rest */ out[2] = s[2];
        }

        p[0] = out[0]; p[1] = out[1]; p[2] = out[2];

    }
}

/* "userData" (ux, uy, uz, uw): transformed normal; the shader reads it through CoronaVertexUserData.xyz */
static void s3d_normal_writer( void * dest, const void * context, const CoronaGeometryMappingLayout * layout,
    unsigned int index, unsigned int n )
{
    const Instance * inst = (const Instance *)context;
    unsigned char * d = (unsigned char *)dest;
    unsigned int k;

    for (k = 0; k < n; ++k, d += layout->outStride)
    {
        float * u = (float *)d;
        unsigned int vi = index + k;
        vec3 nIn = { 0.0f, 0.0f, 1.0f }, nOut;

        if (inst->morphNA && vi < inst->morphCount)
        {
            const float * a = inst->morphNA + 3 * vi;
            const float * b = inst->morphNB + 3 * vi;
            float t = inst->morphT;

            nIn[0] = a[0] + ( b[0] - a[0] ) * t;
            nIn[1] = a[1] + ( b[1] - a[1] ) * t;
            nIn[2] = a[2] + ( b[2] - a[2] ) * t;
            glm_vec3_normalize( nIn );
        }
        else if (inst->normals && vi < inst->normalCount)
        {
            nIn[0] = inst->normals[3 * vi];
            nIn[1] = inst->normals[3 * vi + 1];
            nIn[2] = inst->normals[3 * vi + 2];
        }
        if (inst->flags & kFlag_HasModel)
        {
            glm_mat3_mulv( *(mat3 *)inst->normalMat, nIn, nOut );
        }
        else
        {
            glm_vec3_copy( nIn, nOut );
        }
        u[0] = nOut[0]; u[1] = nOut[1]; u[2] = nOut[2];
    }
}

/* ========================================================================== */
/* Effect data type                                                             */
/* ========================================================================== */

static void s3d_before( const CoronaShader * shader, void * userData, const CoronaRenderer * renderer, const CoronaRenderData * renderData )
{
    Instance * inst = (Instance *)userData;
    DepthState on;

    if (inst->flags & ( kFlag_PassBegin | kFlag_PassEnd ))
    {
        /* Marker: draw a degenerate triangle so Solar2D flushes pending draws, then switch the FBO */
        PassCommand pc;

        CORONA( CoronaRendererWriteStateBlock )( renderer, gStateBlockID, &kDepthOff, sizeof( kDepthOff ) );
        CORONA( CoronaShaderRawDraw )( shader, renderData, renderer );
        if (!gShadowParams[0]) return;
        pc.begin = ( inst->flags & kFlag_PassBegin ) ? 1 : 0;
        pc.size = gShadowSize;
        CORONA( CoronaRendererIssueCommand )( renderer, gPassCmdID, &pc, sizeof( pc ) );
        return;
    }
    if (( inst->flags & kFlag_Caster ) && !gShadowParams[0]) return; /* shadow map is off */

    on.depthTest = ( inst->flags & kFlag_NoDepth ) ? 0 : 1;
    on.depthWrite = ( inst->flags & kFlag_NoDepthWr ) ? 0 : 1;
    on.cull = ( inst->flags & kFlag_NoCull ) ? 0 : 1;
    on.depthLess = ( inst->flags & kFlag_Shadow ) ? 1 : 0;
    on.offset = on.depthLess;
    on.pad[0] = on.pad[1] = on.pad[2] = 0;

    /* writers only apply inside before(), so the object must be drawn here (ignoreOriginal = 1) */
    CORONA( CoronaGeometrySetComponentWriter )( renderer, "position", s3d_position_writer, inst, 1 );
    CORONA( CoronaGeometrySetComponentWriter )( renderer, "userData", s3d_normal_writer, inst, 1 );
    CORONA( CoronaRendererWriteStateBlock )( renderer, gStateBlockID, &on, sizeof( on ) );

    CORONA( CoronaShaderRawDraw )( shader, renderData, renderer );

    /* After RawDraw the object's shader is bound and the draw is still waiting to be flushed, so the uniforms arrive in time. */
    {
        UniformCommand uc;

        uc.f = gFrame;
        memcpy( uc.lightVP, gLightVP, sizeof( uc.lightVP ) );
        memcpy( uc.shadowParams, gShadowParams, sizeof( uc.shadowParams ) );
        uc.flags = inst->flags;
        CORONA( CoronaRendererIssueCommand )( renderer, gUniformCmdID, &uc, sizeof( uc ) );
    }

    /* restore the default state so 2D objects drawn later are not affected */
    CORONA( CoronaRendererWriteStateBlock )( renderer, gStateBlockID, &kDepthOff, sizeof( kDepthOff ) );
}

enum { kData_TRS = 0, kData_Normals, kData_Flags, kData_Matrix, kData_Pose, kData_Morph };

static int s3d_data_index( const char * name )
{
    if (0 == strcmp( name, "s3dTRS" )) return kData_TRS;
    if (0 == strcmp( name, "s3dNormals" )) return kData_Normals;
    if (0 == strcmp( name, "s3dFlags" )) return kData_Flags;
    if (0 == strcmp( name, "s3dMatrix" )) return kData_Matrix;
    if (0 == strcmp( name, "s3dPose" )) return kData_Pose;
    if (0 == strcmp( name, "s3dMorph" )) return kData_Morph;
    return -1;
}

static void s3d_update_normal_matrix( Instance * inst )
{
    mat3 m3;

    glm_mat4_pick3( *(mat4 *)inst->model, m3 );
    glm_mat3_inv( m3, m3 );
    glm_mat3_transpose( m3 );
    memcpy( inst->normalMat, m3, sizeof( inst->normalMat ) );
}

static int s3d_get_data( lua_State * L, int dataIndex, void * userData, int * hadError )
{
    const Instance * inst = (const Instance *)userData;

    (void)hadError;
    if (kData_Flags == dataIndex)
    {
        lua_pushnumber( L, inst->flags );
        return 1;
    }
    return 0;
}

static int s3d_set_data( lua_State * L, int dataIndex, int valueIndex, void * userData, int * shouldInvalidate, int * hadError )
{
    Instance * inst = (Instance *)userData;

    if (valueIndex < 0) valueIndex = lua_gettop( L ) + valueIndex + 1;

    switch (dataIndex)
    {
    case kData_TRS: /* { x, y, z, rx, ry, rz (degrees), sx, sy, sz } */
        if (LUA_TTABLE != lua_type( L, valueIndex ))
        {
            *hadError = 1;
            lua_pushstring( L, "s3dTRS must be a table of 9 numbers" );
            return 1;
        }
        else
        {
            vec3 t, r, s;
            mat4 m, rot;

            t[0] = s3d_tablenum( L, valueIndex, 1, 0 ); t[1] = s3d_tablenum( L, valueIndex, 2, 0 ); t[2] = s3d_tablenum( L, valueIndex, 3, 0 );
            r[0] = glm_rad( s3d_tablenum( L, valueIndex, 4, 0 ) );
            r[1] = glm_rad( s3d_tablenum( L, valueIndex, 5, 0 ) );
            r[2] = glm_rad( s3d_tablenum( L, valueIndex, 6, 0 ) );
            s[0] = s3d_tablenum( L, valueIndex, 7, 1 ); s[1] = s3d_tablenum( L, valueIndex, 8, 1 ); s[2] = s3d_tablenum( L, valueIndex, 9, 1 );

            /* model = T * R(yaw y, pitch x, roll z) * S */
            glm_translate_make( m, t );
            glm_euler_yxz( r, rot );
            glm_mat4_mul( m, rot, m );
            glm_scale( m, s );
            memcpy( inst->model, m, sizeof( inst->model ) );
            inst->flags |= kFlag_HasModel;
            s3d_update_normal_matrix( inst );
        }
        break;
    case kData_Pose: /* { x, y, z, qx, qy, qz, qw, sx, sy, sz }: rotation as a quaternion (e.g. from physics) */
        if (LUA_TTABLE != lua_type( L, valueIndex ))
        {
            *hadError = 1;
            lua_pushstring( L, "s3dPose must be a table of 10 numbers" );
            return 1;
        }
        else
        {
            vec3 t, s;
            versor q;
            mat4 m, rot;

            t[0] = s3d_tablenum( L, valueIndex, 1, 0 ); t[1] = s3d_tablenum( L, valueIndex, 2, 0 ); t[2] = s3d_tablenum( L, valueIndex, 3, 0 );
            q[0] = s3d_tablenum( L, valueIndex, 4, 0 ); q[1] = s3d_tablenum( L, valueIndex, 5, 0 );
            q[2] = s3d_tablenum( L, valueIndex, 6, 0 ); q[3] = s3d_tablenum( L, valueIndex, 7, 1 );
            s[0] = s3d_tablenum( L, valueIndex, 8, 1 ); s[1] = s3d_tablenum( L, valueIndex, 9, 1 ); s[2] = s3d_tablenum( L, valueIndex, 10, 1 );
            glm_quat_normalize( q );
            glm_translate_make( m, t );
            glm_quat_mat4( q, rot );
            glm_mat4_mul( m, rot, m );
            glm_scale( m, s );
            memcpy( inst->model, m, sizeof( inst->model ) );
            inst->flags |= kFlag_HasModel;
            s3d_update_normal_matrix( inst );
        }
        break;
    case kData_Matrix: /* 16 numbers, column-major */
        if (LUA_TTABLE != lua_type( L, valueIndex ))
        {
            *hadError = 1;
            lua_pushstring( L, "s3dMatrix must be a table of 16 numbers" );
            return 1;
        }
        else
        {
            int i;

            for (i = 0; i < 16; ++i) inst->model[i] = s3d_tablenum( L, valueIndex, i + 1, ( i % 5 ) ? 0.0f : 1.0f );
            inst->flags |= kFlag_HasModel;
            s3d_update_normal_matrix( inst );
        }
        break;
    case kData_Morph: /* { posA, posB, t, nrmA, nrmB } (buffers from s3d.newBuffer) or false to turn it off */
        if (LUA_TTABLE != lua_type( L, valueIndex ))
        {
            inst->morphPA = inst->morphPB = inst->morphNA = inst->morphNB = NULL;
            inst->morphCount = 0;
        }
        else
        {
            const float * pa;
            const float * pb;
            unsigned int na, nb;

            lua_rawgeti( L, valueIndex, 1 ); pa = (const float *)lua_touserdata( L, -1 ); na = pa ? (unsigned int)( lua_objlen( L, -1 ) / ( 3 * sizeof( float ) ) ) : 0; lua_pop( L, 1 );
            lua_rawgeti( L, valueIndex, 2 ); pb = (const float *)lua_touserdata( L, -1 ); nb = pb ? (unsigned int)( lua_objlen( L, -1 ) / ( 3 * sizeof( float ) ) ) : 0; lua_pop( L, 1 );
            if (!pa || !pb || na != nb)
            {
                *hadError = 1;
                lua_pushstring( L, "s3dMorph: needs two position buffers with the same vertex count" );
                return 1;
            }
            inst->morphPA = pa;
            inst->morphPB = pb;
            inst->morphCount = na;
            inst->morphT = s3d_tablenum( L, valueIndex, 3, 0 );
            lua_rawgeti( L, valueIndex, 4 ); inst->morphNA = (const float *)lua_touserdata( L, -1 ); lua_pop( L, 1 );
            lua_rawgeti( L, valueIndex, 5 ); inst->morphNB = (const float *)lua_touserdata( L, -1 ); lua_pop( L, 1 );
            if (!inst->morphNA || !inst->morphNB) inst->morphNA = inst->morphNB = NULL;
        }
        break;
    case kData_Normals: /* userdata from s3d.newBuffer */
        inst->normals = (const float *)lua_touserdata( L, valueIndex );
        inst->normalCount = inst->normals ? (unsigned int)( lua_objlen( L, valueIndex ) / ( 3 * sizeof( float ) ) ) : 0;
        break;
    case kData_Flags:
        inst->flags = ( inst->flags & kFlag_HasModel ) | ( (unsigned int)lua_tonumber( L, valueIndex ) & ~kFlag_HasModel );
        break;
    default:
        return 0;
    }

    *shouldInvalidate = 1;
    return 1;
}

/* ========================================================================== */
/* Shell transform                                                              */
/* ========================================================================== */

/* replace the first occurrence of `from` with `to`; returns a new (malloc'ed) string or NULL */
static char * s3d_replace( const char * src, const char * from, const char * to )
{
    const char * at = strstr( src, from );
    size_t a, lf, lt, ls;
    char * out;

    if (!at) return NULL;
    a = (size_t)( at - src ); lf = strlen( from ); lt = strlen( to ); ls = strlen( src );
    out = (char *)malloc( ls - lf + lt + 1 );
    if (!out) return NULL;
    memcpy( out, src, a );
    memcpy( out + a, to, lt );
    memcpy( out + a + lt, at + lf, ls - a - lf + 1 );
    return out;
}

static char * s3d_transform_vertex( const char * src )
{
    /* When drawing into an FBO (snapshot/canvas), Solar2D's 2D projection flips y (m[1][1] > 0): flip to match */
    /* Light space (shadow map) is not flipped: casters (ViewProj == LightVP) draw straight into the plugin's FBO */
    static const char * kPosition = "gl_Position = u_S3D_ViewProj * vec4( a_Position, 1.0 );\n"
        "    bool s3dCaster = u_S3D_ViewProj[0] == u_S3D_LightVP[0] && u_S3D_ViewProj[3] == u_S3D_LightVP[3];\n"
        "    if ( !s3dCaster && u_ViewProjectionMatrix[1][1] > 0.0 ) { gl_Position.y = -gl_Position.y; }\n"
        "    v_S3D_ShadowPos = u_S3D_LightVP * vec4( a_Position, 1.0 );\n"
        "    v_S3D_Depth = gl_Position.w;";
    char * a, * b, * c, * d, * gp, * semi;
    size_t pre, total;

    a = s3d_replace( src, "attribute vec2 a_Position;", "attribute vec3 a_Position;" );
    if (!a) return NULL;
    b = s3d_replace( a, "VertexKernel( a_Position )", "VertexKernel( a_Position.xy )" );
    free( a );
    if (!b) return NULL;
    c = s3d_replace( b, "void main()", "" ); /* make sure main exists */
    if (!c) { free( b ); return NULL; }
    free( c );

    /* insert the uniform declarations right before "void main()" */
    c = s3d_replace( b, "void main()", "uniform P_DEFAULT mat4 u_S3D_ViewProj;\nuniform P_DEFAULT mat4 u_S3D_LightVP;\n"
        "varying P_DEFAULT vec4 v_S3D_ShadowPos;\nvarying P_DEFAULT float v_S3D_Depth;\nvoid main()" );
    free( b );
    if (!c) return NULL;

    /* replace the final gl_Position assignment */
    gp = NULL;
    {
        char * p = strstr( c, "gl_Position" );

        while (p) { gp = p; p = strstr( p + 1, "gl_Position" ); }
    }
    if (!gp) { free( c ); return NULL; }
    semi = strchr( gp, ';' );
    if (!semi) { free( c ); return NULL; }
    pre = (size_t)( gp - c );
    total = pre + strlen( kPosition ) + strlen( semi + 1 ) + 1;
    d = (char *)malloc( total );
    if (!d) { free( c ); return NULL; }
    memcpy( d, c, pre );
    strcpy( d + pre, kPosition );
    strcat( d + pre, semi + 1 );
    free( c );
    return d;
}

static const char ** s3d_shell_begin( CoronaShellTransformParams * params, void * workSpace, void * key )
{
    char ** slot = (char **)workSpace;
    unsigned int i;

    (void)key;
    *slot = NULL;
    if (0 == strcmp( params->type, "fragment" ))
    {
        for (i = 0; i < params->nsources; ++i)
        {
            if (params->hints[i] && 0 == strcmp( params->hints[i], "fragmentSource" ))
            {
                char * out = s3d_replace( params->sources[i], "void main()", "varying P_DEFAULT vec4 v_S3D_ShadowPos;\nvarying P_DEFAULT float v_S3D_Depth;\nvoid main()" );

                if (out) { *slot = out; params->sources[i] = out; }
                break;
            }
        }
        return params->sources;
    }
    if (0 != strcmp( params->type, "vertex" )) return params->sources;

    for (i = 0; i < params->nsources; ++i)
    {
        if (params->hints[i] && 0 == strcmp( params->hints[i], "vertexSource" ))
        {
            char * out = s3d_transform_vertex( params->sources[i] );

            if (out)
            {
                *slot = out;
                params->sources[i] = out;
            }
            else
            {
                printf( "[s3d] shell transform: unrecognized shell shader (backend is not OpenGL?)\n" );
            }
            break;
        }
    }
    return params->sources;
}

static void s3d_shell_finish( void * workSpace, void * key )
{
    char ** slot = (char **)workSpace;

    (void)key;
    free( *slot );
    *slot = NULL;
}

/* ========================================================================== */
/* Lua functions                                                                */
/* ========================================================================== */

static CoronaEffectCallbacks gCallbacks;
static CoronaShellTransform gShell;

/* s3d._init(): registers the shell transform, data type, state block and commands. Call before defineEffect. */
static int s3d_l_init( lua_State * L )
{
    CoronaStateBlock block;
    CoronaCommand cmd;

    memset( &gShell, 0, sizeof( gShell ) );
    gShell.size = sizeof( CoronaShellTransform );
    gShell.workSpace = sizeof( char * );
    gShell.begin = s3d_shell_begin;
    gShell.finish = s3d_shell_finish;
    if (!CORONA( CoronaShaderRegisterShellTransform )( L, "s3d", &gShell ))
    {
        /* already registered in this Lua state: skip */
    }

    memset( &gCallbacks, 0, sizeof( gCallbacks ) );
    gCallbacks.size = sizeof( CoronaEffectCallbacks );
    gCallbacks.drawParams.ignoreOriginal = 1;
    gCallbacks.drawParams.before = s3d_before;
    gCallbacks.getDataIndex = s3d_data_index;
    gCallbacks.getData = s3d_get_data;
    gCallbacks.setData = s3d_set_data;
    gCallbacks.extraSpace = sizeof( Instance );
    CORONA( CoronaShaderRegisterEffectDataType )( L, "s3d", &gCallbacks );

    memset( &cmd, 0, sizeof( cmd ) );
    cmd.reader = s3d_state_reader;
    if (!CORONA( CoronaRendererRegisterCommand )( L, &cmd, &gStateCmdID )) return s3d_error( L, "s3d: could not register the state command" );

    cmd.reader = s3d_uniform_reader;
    if (!CORONA( CoronaRendererRegisterCommand )( L, &cmd, &gUniformCmdID )) return s3d_error( L, "s3d: could not register the uniform command" );

    cmd.reader = s3d_pass_reader;
    if (!CORONA( CoronaRendererRegisterCommand )( L, &cmd, &gPassCmdID )) return s3d_error( L, "s3d: could not register the shadow pass command" );

    /* new Lua state (relaunch): GL objects from the old context may be gone */
    gShadowFbo = gShadowTex = gShadowRb = 0;
    memset( gProgramDone, 0, sizeof( gProgramDone ) );
    gProgramScanFrame = 0;
    gProgramMaxId = 0;
    gShadowAlloc = 0;
    memset( gFbos, 0, sizeof( gFbos ) );

    memset( &block, 0, sizeof( block ) );
    block.blockSize = sizeof( DepthState );
    block.defaultContents = (void *)&kDepthOff;
    block.stateDirty = s3d_state_dirty;
    if (!CORONA( CoronaRendererRegisterStateBlock )( L, &block, &gStateBlockID )) return s3d_error( L, "s3d: could not register the state block" );

    lua_pushboolean( L, 1 );
    return 1;
}

/* Planar shadow matrix for a directional light: M = (P.L) I - L P^T, P = (0, 1, 0, -y), L = (direction to the light, 0) */
static void s3d_update_shadow_matrix( void )
{
    float P[4], Lv[4], dot;
    int r, c;

    P[0] = 0; P[1] = 1; P[2] = 0; P[3] = -gShadowPlaneY;
    Lv[0] = gFrame.lightDir[0]; Lv[1] = gFrame.lightDir[1]; Lv[2] = gFrame.lightDir[2]; Lv[3] = 0;
    if (Lv[1] < 0.05f) Lv[1] = 0.05f; /* light below the plane: avoid dividing by zero */
    dot = P[0] * Lv[0] + P[1] * Lv[1] + P[2] * Lv[2] + P[3] * Lv[3];
    for (c = 0; c < 4; ++c)
        for (r = 0; r < 4; ++r)
            gShadowMat[c * 4 + r] = ( r == c ? dot : 0.0f ) - Lv[r] * P[c]; /* column-major */
}

/* Light matrix for the shadow map: ortho around the center, looking along the light direction */
static void s3d_update_light_vp( void )
{
    vec3 dir, eye, center, up = { 0, 1, 0 };
    mat4 view, proj, vp;

    dir[0] = gFrame.lightDir[0]; dir[1] = gFrame.lightDir[1]; dir[2] = gFrame.lightDir[2]; /* direction to the light */
    if (glm_vec3_norm( dir ) < 1e-6f) { dir[0] = 0; dir[1] = 1; dir[2] = 0; }
    glm_vec3_normalize( dir );
    if (fabsf( dir[1] ) > 0.99f) { up[0] = 0; up[1] = 0; up[2] = 1; }
    glm_vec3_copy( gShadowCenter, center );
    glm_vec3_scale( dir, gShadowDist, eye );
    glm_vec3_add( center, eye, eye );
    glm_lookat( eye, center, up, view );
    glm_ortho( -gShadowArea, gShadowArea, -gShadowArea, gShadowArea, 0.05f, gShadowDist * 2.0f, proj );
    glm_mat4_mul( proj, view, vp );
    memcpy( gLightVP, vp, sizeof( gLightVP ) );
}

/* s3d._setShadowMap( enabled, size, area, cx, cy, cz, distance, bias, darkness ) */
static int s3d_l_set_shadow_map( lua_State * L )
{
    int size = (int)s3d_optnum( L, 2, 1024 );

    if (size < 64) size = 64;
    if (size > 4096) size = 4096;
    gShadowSize = size;
    gShadowArea = s3d_optnum( L, 3, 10 );
    gShadowCenter[0] = s3d_optnum( L, 4, 0 ); gShadowCenter[1] = s3d_optnum( L, 5, 0 ); gShadowCenter[2] = s3d_optnum( L, 6, 0 );
    gShadowDist = s3d_optnum( L, 7, 30 );
    {
        /* x: 0 = off, 1/2/3 = filter quality (1, 4, 9 texture reads) */
        int quality = (int)s3d_optnum( L, 10, 3 );

        if (quality < 1) quality = 1;
        if (quality > 3) quality = 3;
        gShadowParams[0] = lua_toboolean( L, 1 ) ? (float)quality : 0.0f;
    }
    gShadowParams[1] = s3d_optnum( L, 8, 0.003f );
    gShadowParams[2] = 1.0f / (float)size;
    gShadowParams[3] = s3d_optnum( L, 9, 0.6f );
    s3d_update_light_vp();
    CORONA( CoronaRendererInvalidate )( L );
    return 0;
}

/* s3d._setCamera( ex, ey, ez, tx, ty, tz, ux, uy, uz, fovDeg, aspect, near, far ) */
static int s3d_l_set_camera( lua_State * L )
{
    vec3 eye, target, up;
    mat4 view, proj, vp;

    eye[0] = s3d_optnum( L, 1, 0 ); eye[1] = s3d_optnum( L, 2, 0 ); eye[2] = s3d_optnum( L, 3, 5 );
    target[0] = s3d_optnum( L, 4, 0 ); target[1] = s3d_optnum( L, 5, 0 ); target[2] = s3d_optnum( L, 6, 0 );
    up[0] = s3d_optnum( L, 7, 0 ); up[1] = s3d_optnum( L, 8, 1 ); up[2] = s3d_optnum( L, 9, 0 );

    glm_lookat( eye, target, up, view );
    glm_perspective( glm_rad( s3d_optnum( L, 10, 60 ) ), s3d_optnum( L, 11, 1 ), s3d_optnum( L, 12, 0.1f ), s3d_optnum( L, 13, 100 ), proj );
    glm_mat4_mul( proj, view, vp );
    memcpy( gFrame.viewProj, vp, sizeof( gFrame.viewProj ) );
    gFrame.camPos[0] = eye[0]; gFrame.camPos[1] = eye[1]; gFrame.camPos[2] = eye[2]; gFrame.camPos[3] = 1.0f;

    CORONA( CoronaRendererInvalidate )( L );
    return 0;
}

/* s3d._setLight( dx, dy, dz, ambient, r, g, b ): (dx, dy, dz) is the direction the light travels */
static int s3d_l_set_light( lua_State * L )
{
    vec3 d;

    d[0] = -s3d_optnum( L, 1, -0.4f ); d[1] = -s3d_optnum( L, 2, -1.0f ); d[2] = -s3d_optnum( L, 3, -0.6f );
    glm_vec3_normalize( d );
    gFrame.lightDir[0] = d[0]; gFrame.lightDir[1] = d[1]; gFrame.lightDir[2] = d[2];
    gFrame.lightDir[3] = s3d_optnum( L, 4, 0.3f );
    gFrame.lightColor[0] = s3d_optnum( L, 5, 1 );
    gFrame.lightColor[1] = s3d_optnum( L, 6, 1 );
    gFrame.lightColor[2] = s3d_optnum( L, 7, 1 );
    gFrame.lightColor[3] = 1;
    s3d_update_shadow_matrix();
    s3d_update_light_vp();

    CORONA( CoronaRendererInvalidate )( L );
    return 0;
}

/* s3d._setFog( r, g, b, start, end, enabled ) */
static int s3d_l_set_fog( lua_State * L )
{
    gFrame.fog[0] = s3d_optnum( L, 1, 0.7f );
    gFrame.fog[1] = s3d_optnum( L, 2, 0.8f );
    gFrame.fog[2] = s3d_optnum( L, 3, 0.9f );
    gFrame.fogRange[0] = s3d_optnum( L, 4, 10 );
    gFrame.fogRange[1] = s3d_optnum( L, 5, 40 );
    gFrame.fog[3] = lua_toboolean( L, 6 ) ? 1.0f : 0.0f;
    CORONA( CoronaRendererInvalidate )( L );
    return 0;
}

/* s3d._setShadowPlane( y ) */
static int s3d_l_set_shadow_plane( lua_State * L )
{
    gShadowPlaneY = s3d_optnum( L, 1, 0 );
    s3d_update_shadow_matrix();
    CORONA( CoronaRendererInvalidate )( L );
    return 0;
}

/* s3d._beginFrame(): called every enterFrame, so we know when an FBO's depth must be cleared */
static int s3d_l_begin_frame( lua_State * L )
{
    (void)L;
    ++gFrameId;
    return 0;
}

/* s3d._newBuffer( { numbers... } ) -> userdata float[] */
static int s3d_l_new_buffer( lua_State * L )
{
    size_t n, i;
    float * buf;

    if (LUA_TTABLE != lua_type( L, 1 )) return s3d_error( L, "s3d.newBuffer: expected a table of numbers" );
    n = lua_objlen( L, 1 );
    buf = (float *)lua_newuserdata( L, ( n ? n : 1 ) * sizeof( float ) );
    for (i = 0; i < n; ++i) buf[i] = s3d_tablenum( L, 1, (int)i + 1, 0 );
    return 1;
}

/* s3d._project( x, y, z ) -> sx, sy (NDC -1..1), depth ; handy for touch / raycasts */
static int s3d_l_project( lua_State * L )
{
    vec4 p, out;

    p[0] = s3d_optnum( L, 1, 0 ); p[1] = s3d_optnum( L, 2, 0 ); p[2] = s3d_optnum( L, 3, 0 ); p[3] = 1;
    glm_mat4_mulv( *(mat4 *)gFrame.viewProj, p, out );
    if (out[3] == 0.0f) return 0;
    lua_pushnumber( L, out[0] / out[3] );
    lua_pushnumber( L, out[1] / out[3] );
    lua_pushnumber( L, out[2] / out[3] );
    return 3;
}

/* s3d._unproject( ndcX, ndcY ) -> ox, oy, oz, dx, dy, dz : ray from the camera through a screen point */
static int s3d_l_unproject( lua_State * L )
{
    mat4 inv;
    vec4 a, b, pa, pb;
    vec3 dir;

    glm_mat4_inv( *(mat4 *)gFrame.viewProj, inv );
    a[0] = b[0] = s3d_optnum( L, 1, 0 );
    a[1] = b[1] = s3d_optnum( L, 2, 0 );
    a[2] = -1; b[2] = 1; a[3] = b[3] = 1;
    glm_mat4_mulv( inv, a, pa );
    glm_mat4_mulv( inv, b, pb );
    glm_vec4_scale( pa, 1.0f / pa[3], pa );
    glm_vec4_scale( pb, 1.0f / pb[3], pb );
    glm_vec3_sub( pb, pa, dir );
    glm_vec3_normalize( dir );
    lua_pushnumber( L, pa[0] ); lua_pushnumber( L, pa[1] ); lua_pushnumber( L, pa[2] );
    lua_pushnumber( L, dir[0] ); lua_pushnumber( L, dir[1] ); lua_pushnumber( L, dir[2] );
    return 6;
}

/* ========================================================================== */
/* Entry point                                                                  */
/* ========================================================================== */

#include "s3d_lua.h" /* const char kS3DLua[]: the Lua half of the plugin */

S3D_EXPORT int luaopen_plugin_s3d( lua_State * L )
{
    const char * missing = s3d_bind_all();

    if (missing)
    {
#ifdef S3D_DYNAMIC
        if (p_lua_pushstring && p_lua_error)
        {
            char buf[256];

            snprintf( buf, sizeof( buf ), "plugin.s3d: function '%s' not found in the process", missing );
            p_lua_pushstring( L, buf );
            return p_lua_error( L );
        }
#endif
        return 0;
    }

    if (0 != luaL_loadbuffer( L, kS3DLua, sizeof( kS3DLua ) - 1, "=plugin.s3d" ))
    {
        return lua_error( L );
    }

    /* the Lua half is a function that takes the table of C functions and returns the module */
    lua_createtable( L, 0, 8 );
    lua_pushcclosure( L, s3d_l_init, 0 ); lua_setfield( L, -2, "init" );
    lua_pushcclosure( L, s3d_l_set_camera, 0 ); lua_setfield( L, -2, "setCamera" );
    lua_pushcclosure( L, s3d_l_set_light, 0 ); lua_setfield( L, -2, "setLight" );
    lua_pushcclosure( L, s3d_l_new_buffer, 0 ); lua_setfield( L, -2, "newBuffer" );
    lua_pushcclosure( L, s3d_l_project, 0 ); lua_setfield( L, -2, "project" );
    lua_pushcclosure( L, s3d_l_unproject, 0 ); lua_setfield( L, -2, "unproject" );
    lua_pushcclosure( L, s3d_l_set_fog, 0 ); lua_setfield( L, -2, "setFog" );
    lua_pushcclosure( L, s3d_l_set_shadow_plane, 0 ); lua_setfield( L, -2, "setShadowPlane" );
    lua_pushcclosure( L, s3d_l_begin_frame, 0 ); lua_setfield( L, -2, "beginFrame" );
    lua_pushcclosure( L, s3d_l_set_shadow_map, 0 ); lua_setfield( L, -2, "setShadowMap" );
    lua_pushstring( L, S3D_VERSION ); lua_setfield( L, -2, "version" );
    lua_call( L, 1, 1 );
    return 1;
}
