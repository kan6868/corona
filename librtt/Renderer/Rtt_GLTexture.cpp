//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Config.h"

#include "Renderer/Rtt_GLTexture.h"

#include "Renderer/Rtt_GL.h"
#include "Renderer/Rtt_Texture.h"
#include "Core/Rtt_Assert.h"

#include "Rtt_Profiling.h"

#include <string.h>

// ----------------------------------------------------------------------------

#define ENABLE_DEBUG_PRINT    0

#if ENABLE_DEBUG_PRINT
    #define DEBUG_PRINT( ... ) Rtt_LogException( __VA_ARGS__ );
#else
    #define DEBUG_PRINT( ... )
#endif


#if !defined(Rtt_OPENGLES) && !defined(GL_ABGR_EXT)
#define GL_ABGR_EXT 0x8000
#endif

// Mipmaps for linear-filtered image textures. Without them, an image drawn at less than half its size samples
// only the 4 nearest texels, so thin lines break up and edges turn jagged (very visible in small web canvases on
// desktop, where devicePixelRatio is 1). Enabled for the web build only for now; define it to 1 to try elsewhere.
#ifndef Rtt_GL_TEXTURE_MIPMAPS
    #if defined( Rtt_EMSCRIPTEN_ENV )
        #define Rtt_GL_TEXTURE_MIPMAPS 1
    #else
        #define Rtt_GL_TEXTURE_MIPMAPS 0
    #endif
#endif
// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
    using namespace Rtt;

    void getFormatTokens( Texture::Format format, GLint& internalFormat, GLenum& sourceFormat, GLenum& sourceType )
    {
        switch( format )
        {
            case Texture::kAlpha:        internalFormat = GL_ALPHA;        sourceFormat = GL_ALPHA;        sourceType = GL_UNSIGNED_BYTE; break;
#if defined( Rtt_MetalANGLE)
            case Texture::kLuminance:   internalFormat = GL_RED_EXT;    sourceFormat = GL_RED_EXT;        sourceType = GL_UNSIGNED_BYTE; break;
#else
            case Texture::kLuminance:    internalFormat = GL_LUMINANCE;    sourceFormat = GL_LUMINANCE;    sourceType = GL_UNSIGNED_BYTE; break;
#endif
            case Texture::kRGB:            internalFormat = GL_RGB;        sourceFormat = GL_RGB;            sourceType = GL_UNSIGNED_BYTE; break;
            case Texture::kRGBA:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
#if defined( Rtt_WIN_PHONE_ENV )
            case Texture::kBGRA:        internalFormat = GL_BGRA_EXT;    sourceFormat = GL_BGRA_EXT;        sourceType = GL_UNSIGNED_BYTE; break;
#elif !defined( Rtt_OPENGLES )
            case Texture::kARGB:        internalFormat = GL_RGBA8;        sourceFormat = GL_BGRA;            sourceType = GL_UNSIGNED_INT_8_8_8_8_REV; break;
            case Texture::kBGRA:        internalFormat = GL_RGBA8;        sourceFormat = GL_BGRA;            sourceType = GL_UNSIGNED_INT_8_8_8_8; break;
            #ifdef GL_ABGR_EXT
            case Texture::kABGR:
                internalFormat = GL_ABGR_EXT;
                sourceFormat = GL_ABGR_EXT;
                sourceType = GL_UNSIGNED_BYTE;
                break;
            #endif
#else
            // NOTE: These are not available on OpenGL-ES
            // case Texture::kARGB:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
            // case Texture::kBGRA:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
            // case Texture::kABGR:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
#endif
#ifdef Rtt_NXS_ENV
            case Texture::kLuminanceAlpha:        internalFormat = GL_LUMINANCE_ALPHA;    sourceFormat = GL_LUMINANCE_ALPHA;        sourceType = GL_UNSIGNED_BYTE; break;
#endif
            default: Rtt_ASSERT_NOT_REACHED();
        }
    }

    void getFilterTokens( Texture::Filter filter, GLenum& minFilter, GLenum& magFilter )
    {
        switch( filter )
        {
            case Texture::kNearest:    minFilter = GL_NEAREST;    magFilter = GL_NEAREST;    break;
            case Texture::kLinear:    minFilter = GL_LINEAR;    magFilter = GL_LINEAR;    break;
            default: Rtt_ASSERT_NOT_REACHED();
        }
    }

#if Rtt_GL_TEXTURE_MIPMAPS
    bool isPowerOfTwo( U32 value )
    {
        return value > 0 && 0 == ( value & ( value - 1 ) );
    }

    // OpenGL ES 3 / WebGL 2 can build mipmaps for any size; ES 2 / WebGL 1 only for power-of-two sizes.
    bool canMipmapAnySize()
    {
        static int sResult = -1;
        if ( sResult < 0 )
        {
            const char* version = (const char*)glGetString( GL_VERSION );
            sResult = ( version && ( strstr( version, "OpenGL ES 3" ) || strstr( version, "WebGL 2" ) ) ) ? 1 : 0;
        }
        return 1 == sResult;
    }

    // Only image textures with real data: render targets (snapshots, canvas textures) change every frame,
    // and "nearest" filtering (pixel art) must stay sharp.
    bool shouldMipmap( Texture* texture, const U8* data, U32 w, U32 h )
    {
        if ( ! data || texture->IsTarget() ) { return false; }
        if ( CPUResource::kTexture != texture->GetType() ) { return false; }
        if ( Texture::kLinear != texture->GetFilter() ) { return false; }
        if ( w < 2 && h < 2 ) { return false; }
        return canMipmapAnySize() || ( isPowerOfTwo( w ) && isPowerOfTwo( h ) );
    }

    // Builds the mipmap chain of the bound texture; returns true when the chain is usable.
    bool generateMipmaps()
    {
        while ( GL_NO_ERROR != glGetError() ) {} // drop stale errors
        glGenerateMipmap( GL_TEXTURE_2D );
        return GL_NO_ERROR == glGetError();
    }
#endif

    GLenum convertWrapToken( Texture::Wrap wrap )
    {
        GLenum result = GL_CLAMP_TO_EDGE;

        switch( wrap )
        {
            case Texture::kClampToEdge:        result = GL_CLAMP_TO_EDGE; break;
            case Texture::kRepeat:            result = GL_REPEAT; break;
            case Texture::kMirroredRepeat:    result = GL_MIRRORED_REPEAT; break;
            default: Rtt_ASSERT_NOT_REACHED();
        }

        return result;
    }
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

GLint CalculateOptimalAlignment(U32 width, GLenum format)
{
    int bytesPerPixel = 4;
    
    switch(format) {
        case GL_ALPHA:
#if defined( Rtt_MetalANGLE)
        case GL_RED_EXT:
#else
        case GL_LUMINANCE:
#endif
            bytesPerPixel = 1;
            break;
            
        case GL_RGB:
            bytesPerPixel = 3;
            break;
            
        
        #if defined(GL_ABGR_EXT)
        case GL_ABGR_EXT:
        #endif
        case GL_RGBA:
            bytesPerPixel = 4;
            break;
            
#ifdef Rtt_NXS_ENV
        case GL_LUMINANCE_ALPHA:
            bytesPerPixel = 2;
            break;
#endif
        default: // Default
            bytesPerPixel = 4;
            break;
    }

    const U32 rowBytes = width * bytesPerPixel;
   
    if(rowBytes % 8 == 0) return 8;
    if(rowBytes % 4 == 0) return 4;
    if(rowBytes % 2 == 0) return 2;
    
    return 1; // No alignment
}

void
GLTexture::Create( CPUResource* resource )
{
    Rtt_ASSERT( CPUResource::kTexture == resource->GetType() || CPUResource::kVideoTexture == resource->GetType() );
    Texture* texture = static_cast<Texture*>( resource );

    SUMMED_TIMING( gltc, "Texture GPU Resource: Create" );

    GLuint name = 0;
    glGenTextures( 1, &name );
    fHandle = NameToHandle( name );
    GL_CHECK_ERROR();

    GLenum minFilter;
    GLenum magFilter;
    getFilterTokens( texture->GetFilter(), minFilter, magFilter );

    glBindTexture( GL_TEXTURE_2D, name );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter );
    GL_CHECK_ERROR();

    GLenum wrapS = convertWrapToken( texture->GetWrapX() );
    GLenum wrapT = convertWrapToken( texture->GetWrapY() );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT );
    GL_CHECK_ERROR();

    GLint internalFormat;
    GLenum format;
    GLenum type;
    Texture::Format textureFormat = texture->GetFormat();
    getFormatTokens( textureFormat, internalFormat, format, type );

    const U32 w = texture->GetWidth();
    const U32 h = texture->GetHeight();
    const U8* data = texture->GetData();
    {
//#if defined( Rtt_EMSCRIPTEN_ENV )
//        glPixelStorei( GL_UNPACK_ALIGNMENT, texture->GetByteAlignment() );
//        GL_CHECK_ERROR();
//#endif
        glPixelStorei(GL_UNPACK_ALIGNMENT, CalculateOptimalAlignment(w, internalFormat));
        GL_CHECK_ERROR();

        // It is valid to pass a NULL pointer, so allocation is done either way
        glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data );
        GL_CHECK_ERROR();
        
        fCachedFormat = internalFormat;
        fCachedWidth = w;
        fCachedHeight = h;

        fHasMipmaps = false;
#if Rtt_GL_TEXTURE_MIPMAPS
        if ( shouldMipmap( texture, data, w, h ) && generateMipmaps() )
        {
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
            fHasMipmaps = true;
        }
#endif
    }
    texture->ReleaseData();

    DEBUG_PRINT( "%s : OpenGL name: %d\n",
                    Rtt_FUNCTION,
                    name );
}

void
GLTexture::Update( CPUResource* resource )
{
    Rtt_ASSERT( CPUResource::kTexture == resource->GetType() );
    Texture* texture = static_cast<Texture*>( resource );

    SUMMED_TIMING( gltu, "Texture GPU Resource: Update" );

    const U8* data = texture->GetData();
    if( data )
    {
        const U32 w = texture->GetWidth();
        const U32 h = texture->GetHeight();
        GLint internalFormat;
        GLenum format;
        GLenum type;
        getFormatTokens( texture->GetFormat(), internalFormat, format, type );

        glBindTexture( GL_TEXTURE_2D, GetName() );

        glPixelStorei(GL_UNPACK_ALIGNMENT, CalculateOptimalAlignment(w, internalFormat));
        GL_CHECK_ERROR();

        if (internalFormat == fCachedFormat && w == fCachedWidth && h == fCachedHeight )
        {
            glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data );
        }
        else
        {
            glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data );
            fCachedFormat = internalFormat;
            fCachedWidth = w;
            fCachedHeight = h;
        }
        GL_CHECK_ERROR();

#if Rtt_GL_TEXTURE_MIPMAPS
        // New pixels: rebuild the chain, or fall back to plain linear if the new size cannot have one
        if ( shouldMipmap( texture, data, w, h ) && generateMipmaps() )
        {
            if ( ! fHasMipmaps )
            {
                glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
                fHasMipmaps = true;
            }
        }
        else if ( fHasMipmaps )
        {
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            fHasMipmaps = false;
        }
#endif
    }
    texture->ReleaseData();
}

void
GLTexture::Destroy()
{
    GLuint name = GetName();
    if ( 0 != name )
    {
        glDeleteTextures( 1, &name );
        GL_CHECK_ERROR();
        fHandle = 0;
    }

    DEBUG_PRINT( "%s : OpenGL name: %d\n",
                    Rtt_FUNCTION,
                    name );
}

void
GLTexture::Bind( U32 unit )
{
    glActiveTexture( GL_TEXTURE0 + unit );
    glBindTexture( GL_TEXTURE_2D, GetName() );
    GL_CHECK_ERROR();
}

GLuint
GLTexture::GetName()
{
    return HandleToName( fHandle );
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
