//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"
#include "Rtt_EmscriptenPlatformWin.h"
#include "../Rtt_EmscriptenFont.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

EmscriptenPlatformWin::EmscriptenPlatformWin(const char *resourceDir, const char *documentsDir, const char *temporaryDir,	const char *cachesDir, const char *systemCachesDir)
	:	Super(resourceDir, documentsDir, temporaryDir, cachesDir, systemCachesDir)
{
}

EmscriptenPlatformWin::~EmscriptenPlatformWin()
{
}

PlatformFont * EmscriptenPlatformWin::CreateFont( PlatformFont::SystemFont fontType, Rtt_Real size ) const
{
	return Rtt_NEW(fAllocator, EmscriptenFont(*fAllocator, fontType, size));
}

PlatformFont * EmscriptenPlatformWin::CreateFont( const char *fontName, Rtt_Real size ) const
{
	bool isBold = false;
	return Rtt_NEW( fAllocator, EmscriptenFont( *fAllocator, fontName, size, isBold ) );
}

} // namespace Rtt
