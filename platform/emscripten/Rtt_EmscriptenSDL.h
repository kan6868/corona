//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

// One include for SDL 2 and SDL 3.
// build_web.py passes -DRtt_SDL_MAJOR=2 or 3 (with -sUSE_SDL=2 or 3); without it, SDL 2 is used.
// The RttSDL_* names below cover the API and event differences the HTML5 platform needs, so the
// rest of the code has a single code path.

#pragma once

#ifndef Rtt_SDL_MAJOR
	#define Rtt_SDL_MAJOR 2
#endif

#if Rtt_SDL_MAJOR >= 3

#include <SDL3/SDL.h>

// Events
#define RttSDL_EVENT_FINGER_DOWN		SDL_EVENT_FINGER_DOWN
#define RttSDL_EVENT_FINGER_UP			SDL_EVENT_FINGER_UP
#define RttSDL_EVENT_FINGER_MOTION		SDL_EVENT_FINGER_MOTION
#define RttSDL_EVENT_MOUSE_DOWN			SDL_EVENT_MOUSE_BUTTON_DOWN
#define RttSDL_EVENT_MOUSE_UP			SDL_EVENT_MOUSE_BUTTON_UP
#define RttSDL_EVENT_MOUSE_MOTION		SDL_EVENT_MOUSE_MOTION
#define RttSDL_EVENT_MOUSE_WHEEL		SDL_EVENT_MOUSE_WHEEL
#define RttSDL_EVENT_KEY_DOWN			SDL_EVENT_KEY_DOWN
#define RttSDL_EVENT_KEY_UP				SDL_EVENT_KEY_UP
#define RttSDL_EVENT_TEXT_INPUT			SDL_EVENT_TEXT_INPUT

#define RttSDL_KMOD_SHIFT	SDL_KMOD_SHIFT
#define RttSDL_KMOD_CTRL	SDL_KMOD_CTRL
#define RttSDL_KMOD_ALT		SDL_KMOD_ALT
#define RttSDL_KMOD_GUI		SDL_KMOD_GUI

typedef SDL_Keymod RttSDL_Keymod;

inline SDL_FingerID RttSDL_FingerID(const SDL_TouchFingerEvent& e) { return e.fingerID; }
inline SDL_Keycode RttSDL_KeyCode(const SDL_KeyboardEvent& e) { return e.key; }
inline RttSDL_Keymod RttSDL_KeyMod(const SDL_KeyboardEvent& e) { return e.mod; }
inline bool RttSDL_KeyRepeat(const SDL_KeyboardEvent& e) { return e.repeat; }

inline bool RttSDL_Init(Uint32 flags) { return SDL_Init(flags); }

inline void RttSDL_GetVersion(int* major, int* minor, int* patch)
{
	int v = SDL_GetVersion();
	*major = SDL_VERSIONNUM_MAJOR(v);
	*minor = SDL_VERSIONNUM_MINOR(v);
	*patch = SDL_VERSIONNUM_MICRO(v);
}

inline SDL_Window* RttSDL_CreateWindow(const char* title, int w, int h, Uint64 flags)
{
	return SDL_CreateWindow(title, w, h, flags);
}

// Mouse buttons currently down
inline Uint32 RttSDL_MouseButtons() { return SDL_GetMouseState(NULL, NULL); }
#define RttSDL_BUTTON_MASK(b) SDL_BUTTON_MASK(b)

inline void RttSDL_GetMousePosition(float* x, float* y) { SDL_GetMouseState(x, y); }

// Wheel: amount scrolled away from the user (> 0) or toward the user (< 0)
inline float RttSDL_WheelX(const SDL_MouseWheelEvent& e) { return e.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.x : e.x; }
inline float RttSDL_WheelY(const SDL_MouseWheelEvent& e) { return e.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.y : e.y; }

inline void RttSDL_SetCursorVisible(bool visible) { if (visible) SDL_ShowCursor(); else SDL_HideCursor(); }
inline bool RttSDL_IsCursorVisible() { return SDL_CursorVisible(); }

inline void RttSDL_SetKeyboardEventsEnabled(bool enabled)
{
	SDL_SetEventEnabled(SDL_EVENT_TEXT_INPUT, enabled);
	SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, enabled);
	SDL_SetEventEnabled(SDL_EVENT_KEY_UP, enabled);
}

#else	// SDL 2

#include <SDL2/SDL.h>

#define RttSDL_EVENT_FINGER_DOWN		SDL_FINGERDOWN
#define RttSDL_EVENT_FINGER_UP			SDL_FINGERUP
#define RttSDL_EVENT_FINGER_MOTION		SDL_FINGERMOTION
#define RttSDL_EVENT_MOUSE_DOWN			SDL_MOUSEBUTTONDOWN
#define RttSDL_EVENT_MOUSE_UP			SDL_MOUSEBUTTONUP
#define RttSDL_EVENT_MOUSE_MOTION		SDL_MOUSEMOTION
#define RttSDL_EVENT_MOUSE_WHEEL		SDL_MOUSEWHEEL
#define RttSDL_EVENT_KEY_DOWN			SDL_KEYDOWN
#define RttSDL_EVENT_KEY_UP				SDL_KEYUP
#define RttSDL_EVENT_TEXT_INPUT			SDL_TEXTINPUT

#define RttSDL_KMOD_SHIFT	KMOD_SHIFT
#define RttSDL_KMOD_CTRL	KMOD_CTRL
#define RttSDL_KMOD_ALT		KMOD_ALT
#define RttSDL_KMOD_GUI		KMOD_GUI

typedef Uint16 RttSDL_Keymod;

inline SDL_FingerID RttSDL_FingerID(const SDL_TouchFingerEvent& e) { return e.fingerId; }
inline SDL_Keycode RttSDL_KeyCode(const SDL_KeyboardEvent& e) { return e.keysym.sym; }
inline RttSDL_Keymod RttSDL_KeyMod(const SDL_KeyboardEvent& e) { return e.keysym.mod; }
inline bool RttSDL_KeyRepeat(const SDL_KeyboardEvent& e) { return e.repeat != 0; }

inline bool RttSDL_Init(Uint32 flags) { return SDL_Init(flags) == 0; }

inline void RttSDL_GetVersion(int* major, int* minor, int* patch)
{
	SDL_version v;
	SDL_GetVersion(&v);
	*major = v.major;
	*minor = v.minor;
	*patch = v.patch;
}

inline SDL_Window* RttSDL_CreateWindow(const char* title, int w, int h, Uint32 flags)
{
	return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
}

inline Uint32 RttSDL_MouseButtons() { return SDL_GetMouseState(NULL, NULL); }
#define RttSDL_BUTTON_MASK(b) SDL_BUTTON(b)

inline void RttSDL_GetMousePosition(float* x, float* y)
{
	int ix = 0, iy = 0;
	SDL_GetMouseState(&ix, &iy);
	*x = (float) ix;
	*y = (float) iy;
}

inline float RttSDL_WheelX(const SDL_MouseWheelEvent& e) { return (float) (e.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.x : e.x); }
inline float RttSDL_WheelY(const SDL_MouseWheelEvent& e) { return (float) (e.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.y : e.y); }

inline void RttSDL_SetCursorVisible(bool visible) { SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE); }
inline bool RttSDL_IsCursorVisible() { return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE; }

inline void RttSDL_SetKeyboardEventsEnabled(bool enabled)
{
	SDL_EventState(SDL_TEXTINPUT, enabled ? SDL_ENABLE : SDL_DISABLE);
	SDL_EventState(SDL_KEYDOWN, enabled ? SDL_ENABLE : SDL_DISABLE);
	SDL_EventState(SDL_KEYUP, enabled ? SDL_ENABLE : SDL_DISABLE);
}

#endif

// Both versions

inline bool RttSDL_IsTouchMouse(Uint32 which) { return which == (Uint32) SDL_TOUCH_MOUSEID; }

// Window size in canvas pixels. The window is created without high pixel density, so window units are pixels.
inline void RttSDL_GetWindowPixelSize(SDL_Window* window, int* w, int* h)
{
	*w = 0;
	*h = 0;
	if (window)
	{
		SDL_GetWindowSize(window, w, h);
	}
}
