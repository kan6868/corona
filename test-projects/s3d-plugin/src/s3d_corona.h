/*
 * s3d_corona.h
 * Minimal data types copied from librtt/Corona/CoronaGraphics.h (Solar2D, MIT),
 * enough for plugin s3d. Struct layouts are unchanged for ABI compatibility.
 */
#ifndef S3D_CORONA_H
#define S3D_CORONA_H

#ifndef lua_h
typedef struct lua_State lua_State;
#endif

typedef struct CoronaCommandBuffer CoronaCommandBuffer;
typedef struct CoronaRenderer CoronaRenderer;
typedef struct CoronaRenderData CoronaRenderData;
typedef struct CoronaShader CoronaShader;

/* ---- state block ---- */
typedef void (*CoronaStateBlockDirty)( const CoronaCommandBuffer * commandBuffer, const CoronaRenderer * renderer,
    const void * newContents, const void * oldContents, unsigned int size, int restore, void * userData );

typedef struct CoronaStateBlock {
    unsigned int blockSize;
    void * defaultContents;
    void * userData;
    CoronaStateBlockDirty stateDirty;
    CoronaStateBlockDirty defaultStateDirty;
    int dontHash;
} CoronaStateBlock;

/* ---- command ---- */
typedef void (*CoronaCommandReader)( const CoronaCommandBuffer * commandBuffer, const unsigned char * from, unsigned int size );
typedef void (*CoronaCommandWriter)( const CoronaCommandBuffer * commandBuffer, unsigned char * to, const void * data, unsigned int size );

typedef struct CoronaCommand {
    CoronaCommandReader reader;
    CoronaCommandWriter writer;
} CoronaCommand;

typedef struct CoronaWriteUniformParams {
    union {
        const void * data;
        unsigned long offset;
    } u;
    int useOffset;
} CoronaWriteUniformParams;

/* ---- geometry writer ---- */
typedef enum {
    kAttributeType_Byte,
    kAttributeType_Float,
    kAttributeType_Count
} CoronaGeometryAttributeType;

typedef struct CoronaGeometryMappingLayout {
    unsigned int count;
    unsigned int inStride;
    unsigned int outStride;
    CoronaGeometryAttributeType type;
} CoronaGeometryMappingLayout;

typedef void (*CoronaGeometryComponentWriter)( void * dest, const void * context, const CoronaGeometryMappingLayout * layout, unsigned int index, unsigned int n );

/* ---- shell transform ---- */
typedef struct CoronaEffectDetail {
    const char * name;
    const char * value;
} CoronaEffectDetail;

typedef struct CoronaShellTransformParamsExtensionHeader {
    struct CoronaShellTransformParamsExtensionHeader * next;
    unsigned short type;
} CoronaShellTransformParamsExtensionHeader;

typedef struct CoronaShellTransformParams {
    const char * type;
    const char ** sources;
    const char ** hints;
    const CoronaEffectDetail * details;
    const void * userData;
    unsigned int nsources;
    unsigned int ndetails;
    CoronaShellTransformParamsExtensionHeader * next;
} CoronaShellTransformParams;

typedef const char ** (*CoronaShellTransformBegin)( CoronaShellTransformParams * params, void * workSpaceData, void * key );
typedef void (*CoronaShellTransformFinish)( void * workSpaceData, void * key );
typedef void (*CoronaShellTransformStateCleanup)( void * key );

typedef struct CoronaShellTransform {
    unsigned long size;
    unsigned int workSpace;
    CoronaShellTransformBegin begin;
    CoronaShellTransformFinish finish;
    CoronaShellTransformStateCleanup cleanup;
    void * userData;
} CoronaShellTransform;

/* ---- effect data type ---- */
typedef void (*CoronaShaderDrawBookend)( const CoronaShader * shader, void * userData, const CoronaRenderer * renderer, const CoronaRenderData * renderData );

typedef struct CoronaShaderDrawParams {
    unsigned int ignoreOriginal;
    CoronaShaderDrawBookend before, after;
} CoronaShaderDrawParams;

typedef void (*CoronaShaderBind)( const CoronaRenderer * renderer, void * userData );
typedef void (*CoronaShaderDetach)( const CoronaShader * shader, void * userData );
typedef void (*CoronaShaderPrepare)( const CoronaShader * shader, void * userData, const CoronaRenderData * renderData, int w, int h, int mod );
typedef int (*CoronaEffectNameToIndex)( const char * name );
typedef int (*CoronaEffectGetData)( lua_State * L, int dataIndex, void * userData, int * hadError );
typedef int (*CoronaEffectSetData)( lua_State * L, int dataIndex, int valueIndex, void * userData, int * shouldInvalidate, int * hadError );

typedef struct CoronaEffectCallbacksExtensionHeader {
    struct CoronaObjectParamsHeader * next;
    unsigned short type;
} CoronaEffectCallbacksExtensionHeader;

typedef struct CoronaEffectCallbacks {
    unsigned long size;
    CoronaShaderBind shaderBind;
    CoronaShaderDetach shaderDetach;
    CoronaShaderPrepare prepare;
    CoronaShaderDrawParams drawParams;
    CoronaEffectNameToIndex getDataIndex;
    CoronaEffectGetData getData;
    CoronaEffectSetData setData;
    unsigned int extraSpace;
    CoronaEffectCallbacksExtensionHeader * next;
} CoronaEffectCallbacks;

/* ---- Solar2D functions the plugin uses: X(return type, name, parameters) ---- */
#define S3D_CORONA_FUNCS(X) \
    X( int, CoronaRendererRegisterStateBlock, ( lua_State * L, const CoronaStateBlock * block, unsigned long * blockID ) ) \
    X( int, CoronaRendererWriteStateBlock, ( const CoronaRenderer * renderer, unsigned long blockID, const void * data, unsigned int size ) ) \
    X( int, CoronaRendererRegisterCommand, ( lua_State * L, const CoronaCommand * command, unsigned long * commandID ) ) \
    X( int, CoronaRendererIssueCommand, ( const CoronaRenderer * renderer, unsigned long commandID, void * data, unsigned int size ) ) \
    X( int, CoronaCommandBufferWriteNamedUniform, ( const CoronaCommandBuffer * commandBuffer, const char * uniformName, const CoronaWriteUniformParams * params, unsigned int size ) ) \
    X( int, CoronaGeometrySetComponentWriter, ( const CoronaRenderer * renderer, const char * name, CoronaGeometryComponentWriter writer, const void * context, int update ) ) \
    X( int, CoronaShaderRegisterShellTransform, ( lua_State * L, const char * name, const CoronaShellTransform * transform ) ) \
    X( int, CoronaShaderRegisterEffectDataType, ( lua_State * L, const char * name, const CoronaEffectCallbacks * callbacks ) ) \
    X( int, CoronaShaderRawDraw, ( const CoronaShader * shader, const CoronaRenderData * renderData, const CoronaRenderer * renderer ) ) \
    X( void, CoronaRendererInvalidate, ( lua_State * L ) )

#endif
