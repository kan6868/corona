//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Rtt_EmscriptenPlatform.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class EmscriptenPlatformMac : public EmscriptenPlatform
{
	Rtt_CLASS_NO_COPIES( EmscriptenPlatformMac )

	public:
		typedef EmscriptenPlatform Super;
		typedef EmscriptenPlatformMac Self;

	public:
		EmscriptenPlatformMac( int width, int height );
		virtual ~EmscriptenPlatformMac();
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
