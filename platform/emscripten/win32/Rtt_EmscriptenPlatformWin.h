//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include <SDL2/SDL.h>
#include "../Rtt_EmscriptenPlatform.h"



namespace Rtt
{

	// ----------------------------------------------------------------------------

	class EmscriptenPlatformWin : public EmscriptenPlatform
	{
		Rtt_CLASS_NO_COPIES(EmscriptenPlatformWin)

	public:
		typedef EmscriptenPlatform Super;
		typedef EmscriptenPlatformWin Self;

		EmscriptenPlatformWin(const char *resourceDir, const char *documentsDir, const char *temporaryDir,
			const char *cachesDir, const char *systemCachesDir);
		virtual ~EmscriptenPlatformWin();

		virtual PlatformFont* CreateFont(PlatformFont::SystemFont fontType, Rtt_Real size) const override;
		virtual PlatformFont* CreateFont(const char *fontName, Rtt_Real size) const override;
	};

	// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
