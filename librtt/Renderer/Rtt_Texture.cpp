//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_Texture.h"

#include "Core/Rtt_Assert.h"
#include "Renderer/Rtt_GL.h"
// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

Texture::Texture( Rtt_Allocator* allocator )
:	Super( allocator ),
	fIsRetina( false ),
	fIsTarget( false )
{
}

Texture::~Texture()
{
}

CPUResource::ResourceType 
Texture::GetType() const
{
	return CPUResource::kTexture;
}

void 
Texture::Allocate()
{
}

void 
Texture::Deallocate()
{
}

Texture::Wrap
Texture::GetWrapX() const
{
	return kClampToEdge;
}

Texture::Wrap
Texture::GetWrapY() const
{
	return kClampToEdge;
}

size_t 
Texture::GetSizeInBytes() const
{
	Format format = GetFormat();
	U32 w = GetWidth();
	U32 h = GetHeight();

	switch(format)
	{
		case kLuminance:	return w * h * 1;
		case kRGB:			return w * h * 3;
		case kRGBA:			return w * h * 4;
		case kBGRA:			return w * h * 4;
		case kABGR:			return w * h * 4;
		case kARGB:			return w * h * 4;
		default:			return 0;
	}
}

U8
Texture::GetByteAlignment(const U32 &w, const U32 &h, const Format &format) const
{
	int bytesPerPixel = 4;

	switch (format) {
		case GL_ALPHA:
		case GL_LUMINANCE: bytesPerPixel = 1; break;
		case GL_RGB: bytesPerPixel = 3; break;
		case GL_RGBA: bytesPerPixel = 4; break;
	}

	const U8 bytes = w * h * bytesPerPixel;

	if (bytes % 4 == 0)
		return 4;

	if (bytes % 2 == 0)
		return 2;

	return 1;
}

const U8*
Texture::GetData() const
{
	return NULL;
}

void
Texture::ReleaseData()
{
}

void
Texture::SetFilter( Filter newValue )
{
	// Must implement in derived class
	Rtt_ASSERT_NOT_REACHED();
}

void
Texture::SetWrapX( Wrap newValue )
{
	// Must implement in derived class
	Rtt_ASSERT_NOT_REACHED();
}

void
Texture::SetWrapY( Wrap newValue )
{
	// Must implement in derived class
	Rtt_ASSERT_NOT_REACHED();
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
