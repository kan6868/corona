/*
 * s3d_gles2.h
 * Minimal declarations of the OpenGL ES 2.0 functions plugin s3d uses (types match Khronos GLES2/gl2.h).
 * Used for Android builds without the NDK (zig): define S3D_OWN_GLES2 instead of #include <GLES2/gl2.h>.
 * The GL_* constants are defined by the plugin itself in s3d.c.
 */
#ifndef S3D_GLES2_H
#define S3D_GLES2_H

#ifdef __cplusplus
extern "C" {
#endif

void glActiveTexture( unsigned int texture );
void glBindFramebuffer( unsigned int target, unsigned int framebuffer );
void glBindRenderbuffer( unsigned int target, unsigned int renderbuffer );
void glBindTexture( unsigned int target, unsigned int texture );
void glClear( unsigned int mask );
void glClearColor( float r, float g, float b, float a );
void glClearStencil( int s );
void glCullFace( unsigned int mode );
void glDeleteRenderbuffers( int n, const unsigned int * renderbuffers );
void glDepthFunc( unsigned int func );
void glDepthMask( unsigned char flag );
void glDisable( unsigned int cap );
void glEnable( unsigned int cap );
void glFramebufferRenderbuffer( unsigned int target, unsigned int attachment, unsigned int rbtarget, unsigned int renderbuffer );
void glFramebufferTexture2D( unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level );
void glFrontFace( unsigned int mode );
void glGenFramebuffers( int n, unsigned int * framebuffers );
void glGenRenderbuffers( int n, unsigned int * renderbuffers );
void glGenTextures( int n, unsigned int * textures );
unsigned int glGetError( void );
void glGetFloatv( unsigned int pname, float * data );
void glGetIntegerv( unsigned int pname, int * data );
int glGetUniformLocation( unsigned int program, const char * name );
unsigned char glIsProgram( unsigned int program );
void glPolygonOffset( float factor, float units );
void glRenderbufferStorage( unsigned int target, unsigned int internalformat, int width, int height );
void glStencilFunc( unsigned int func, int ref, unsigned int mask );
void glStencilMask( unsigned int mask );
void glStencilOp( unsigned int fail, unsigned int zfail, unsigned int zpass );
void glTexImage2D( unsigned int target, int level, int internalformat, int width, int height, int border,
    unsigned int format, unsigned int type, const void * pixels );
void glTexParameteri( unsigned int target, unsigned int pname, int param );
void glUniform1i( int location, int v0 );
void glUseProgram( unsigned int program );
void glViewport( int x, int y, int width, int height );

#ifdef __cplusplus
}
#endif

#endif
