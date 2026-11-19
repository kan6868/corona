//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Rtt_PlatformSurface.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

class EmscriptenPlatform;

class EmscriptenScreenSurface : public PlatformSurface
{
	Rtt_CLASS_NO_COPIES(EmscriptenScreenSurface)

	public:
		EmscriptenScreenSurface();
		virtual ~EmscriptenScreenSurface();

		virtual void SetCurrent() const;
		virtual void Flush() const;
		virtual S32 Width() const;
		virtual S32 Height() const;
		virtual DeviceOrientation::Type GetOrientation() const;
		virtual S32 DeviceWidth() const;
		virtual S32 DeviceHeight() const;

		// "adaptive" content size: the canvas size in CSS pixels (about 160 per inch on phones, like dp / points)
		virtual S32 AdaptiveWidth() const;
		virtual S32 AdaptiveHeight() const;

		void setWindow(SDL_Window *window, DeviceOrientation::Type orientation) { fWindow = window; fOrientation = orientation; }
		void setPixelRatio(float ratio) { fPixelRatio = ratio > 0 ? ratio : 1.0f; }
		void getWindowSize(int* w, int* h);

	private:
		SDL_Window* fWindow;
		DeviceOrientation::Type fOrientation;
		float fPixelRatio;		// canvas pixels per CSS pixel
};

class EmscriptenOffscreenSurface : public PlatformSurface
{
	public:
		EmscriptenOffscreenSurface(const PlatformSurface& parent);
		virtual ~EmscriptenOffscreenSurface();

		virtual void SetCurrent() const;
		virtual void Flush() const;
		virtual S32 Width() const;
		virtual S32 Height() const;

	private:
		S32 fWidth;
		S32 fHeight;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
