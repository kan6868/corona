//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"
#include "Rtt_Runtime.h"
#include "Rtt_LuaContext.h"
#include "Core/Rtt_Types.h"
#include "Rtt_EmscriptenContext.h"
#include "Rtt_EmscriptenPlatform.h"
#include "Rtt_EmscriptenRuntimeDelegate.h"
#include "Rtt_LuaFile.h"
#include "Core/Rtt_FileSystem.h"
#include "Rtt_Archive.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_DisplayDefaults.h"
#include "Rtt_KeyName.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef WIN32
	#define strncasecmp _strnicmp
	#define strcasecmp _stricmp
#endif

// #define Rtt_DEBUG_TOUCH 1

#if defined(EMSCRIPTEN)
extern "C"
{
	extern int jsContextInit(int fWidth, int fHeight, int fOrientation);
	extern int jsContextGetWindowWidth();
	extern int jsContextGetWindowHeight();
	extern void jsContextUnlockAudio();
	extern void jsContextSyncFS();
	extern void jsContextResizeNativeObjects();
	extern void jsContextSetContentTransform(float x, float y, float sx, float sy);
	extern void jsContextGetCanvasArea(double* out);
	extern int jsContextMountFS();
	extern int jsContextGetIntModuleItem(const char* name);
	extern int jsContextLoadFonts(const char* name, void* buf, int size);
	extern void jsContextSetClearColor(int r, int g, int b, int a);
	extern void jsContextConfig(int w, int h);
}
#else
	static int appWidth, appHeight;
	int jsContextInit(int w, int h, int fOrientation) { appWidth = w; appHeight = h; return 0; }
	int jsContextGetWindowWidth() { return appWidth; }
	int jsContextGetWindowHeight() { return appHeight; }
	void jsContextUnlockAudio() {}
	void jsContextSyncFS() {}
	void jsContextResizeNativeObjects() {}
	void jsContextSetContentTransform(float x, float y, float sx, float sy) {}
	int jsContextMountFS() { return 1; }
	int jsContextGetIntModuleItem(const char* name) { return 1; }
	int jsContextLoadFonts(const char* name, void* buf, int size)  { return 0; }
	void jsContextSetClearColor(int r, int g, int b, int a) {}
	void jsContextConfig(int w, int h) {}
#endif

namespace Rtt
{
	// Finger id used for the mouse's touch events. Browser touch identifiers are small numbers starting at 0.
	static const SDL_FingerID kMouseFingerId = 0x7FFF0000;

	// A touch that ends within this many canvas pixels of where it began is also a "tap"
	static const float kTapSlop = 3.0f;

	// Largest canvas side in pixels. Browsers silently clamp larger WebGL drawing buffers,
	// which would make the canvas size and the drawing buffer size disagree.
	static const double kMaxCanvasPixels = 8192.0;

	static bool IsSideways(DeviceOrientation::Type orientation)
	{
		return orientation == DeviceOrientation::kSidewaysRight || orientation == DeviceOrientation::kSidewaysLeft;
	}

	// Space the canvas may use (CSS pixels) and the canvas pixels per CSS pixel (see jsContextGetCanvasArea)
	static void GetCanvasArea(double* width, double* height, double* pixelRatio)
	{
#if defined(EMSCRIPTEN)
		double out[3] = { 0, 0, 1 };
		jsContextGetCanvasArea(out);
		*width = out[0];
		*height = out[1];
		*pixelRatio = out[2] > 0 ? out[2] : 1.0;
#else
		*width = jsContextGetWindowWidth();
		*height = jsContextGetWindowHeight();
		*pixelRatio = 1.0;
#endif
	}

	// Called on every browser animation frame (requestAnimationFrame: 60, 120, 144 Hz... depending on the display).
	// Runs the app at the fps from config.lua instead: a tick is due every 1000 / fps milliseconds.
	// The deadline advances by exactly one frame per tick (not "now"), so 60 fps on a 144 Hz display averages
	// 60 ticks per second, and 30 fps on a 60 Hz display runs every other frame.
	static double s_nextTick = 0.0;
	void TimerTickShim(void *userdata)
	{
		CoronaAppContext *context = (CoronaAppContext*) userdata;
		int fps = context->getFPS();
		if (fps <= 0)
		{
			fps = 30;
		}
		const double frameMs = 1000.0 / (double) fps;

#if defined(EMSCRIPTEN)
		const double now = emscripten_get_now();	// milliseconds, sub-millisecond precision
#else
		const double now = (double) Rtt_AbsoluteToMilliseconds(Rtt_GetAbsoluteTime());
#endif

		// First call, or the page was hidden / stalled for a while: start counting from now, no catch-up burst
		if (s_nextTick <= 0.0 || now - s_nextTick > 4.0 * frameMs)
		{
			s_nextTick = now;
		}

		// Animation frame timestamps jitter by about a millisecond; allow 1 ms early so fps = 60 on a 60 Hz
		// display never skips a frame.
		if (now + 1.0 < s_nextTick)
		{
			return;
		}

		s_nextTick += frameMs;
		context->TimerTick();
	}

	//
	// MouseListener: touch, multitouch and tap events
	//

	MouseListener::MouseListener(Runtime &runtime)
		: fRuntime(runtime)
	{
	}

	void MouseListener::Dispatch(TouchEvent& t, bool notifyMultitouch) const
	{
		if (notifyMultitouch)
		{
			MultitouchEvent t2(&t, 1);
			DispatchEvent(t2);
		}
		else
		{
			DispatchEvent(t);
		}
	}

	void MouseListener::TouchDown(float x, float y, SDL_FingerID fid)
	{
		bool notifyMultitouch = fRuntime.Platform().GetDevice().DoesNotify(MPlatformDevice::kMultitouchEvent);

		// One touch at a time unless multitouch is on
		if (fStartPoint.find(fid) != fStartPoint.end() || (notifyMultitouch == false && fStartPoint.size() > 0))
		{
			return;
		}

		fStartPoint[fid] = pt(x, y);

		TouchEvent t(x, y, x, y, TouchEvent::kBegan);
		t.SetId((void*) (uintptr_t) (fid + 1));	// must not be 0

#if Rtt_DEBUG_TOUCH
		printf("TouchDown %d(%g, %g)\n", (int) fid, x, y);
#endif
		Dispatch(t, notifyMultitouch);
	}

	void MouseListener::TouchMoved(float x, float y, SDL_FingerID fid)
	{
		std::map<SDL_FingerID, pt>::const_iterator it = fStartPoint.find(fid);
		if (it == fStartPoint.end())
		{
			return;
		}

		bool notifyMultitouch = fRuntime.Platform().GetDevice().DoesNotify(MPlatformDevice::kMultitouchEvent);
		TouchEvent t(x, y, it->second.x, it->second.y, TouchEvent::kMoved);
		t.SetId((void*) (uintptr_t) (fid + 1));
		Dispatch(t, notifyMultitouch);
	}

	void MouseListener::TouchUp(float x, float y, SDL_FingerID fid)
	{
		std::map<SDL_FingerID, pt>::iterator it = fStartPoint.find(fid);
		if (it == fStartPoint.end())
		{
			return;
		}
		const pt start = it->second;
		fStartPoint.erase(it);

		bool notifyMultitouch = fRuntime.Platform().GetDevice().DoesNotify(MPlatformDevice::kMultitouchEvent);
		TouchEvent t(x, y, start.x, start.y, TouchEvent::kEnded);
		t.SetId((void*) (uintptr_t) (fid + 1));

#if Rtt_DEBUG_TOUCH
		printf("TouchUp %d(%g, %g)\n", (int) fid, x, y);
#endif
		Dispatch(t, notifyMultitouch);

		// "tap" when released where it was pressed
		if (fabsf(start.x - x) <= kTapSlop && fabsf(start.y - y) <= kTapSlop)
		{
			TapEvent tap(start.x, start.y, 1);
			DispatchEvent(tap);
		}
	}

	void MouseListener::TouchCancelled(float x, float y, SDL_FingerID fid)
	{
		std::map<SDL_FingerID, pt>::iterator it = fStartPoint.find(fid);
		if (it == fStartPoint.end())
		{
			return;
		}
		const pt start = it->second;
		fStartPoint.erase(it);

		bool notifyMultitouch = fRuntime.Platform().GetDevice().DoesNotify(MPlatformDevice::kMultitouchEvent);
		TouchEvent t(x, y, start.x, start.y, TouchEvent::kCancelled);
		t.SetId((void*) (uintptr_t) (fid + 1));
		Dispatch(t, notifyMultitouch);
	}

	void MouseListener::DispatchEvent(const MEvent& e) const
	{
		fRuntime.DispatchEvent(e);
	}

	//
	// KeyListener
	//

	KeyListener::KeyListener(Runtime &runtime)
		: fRuntime(runtime)
	{
		// SDL key name -> Corona key name
		fKeyName["A"] = KeyName::kA;
		fKeyName["B"] = KeyName::kB;
		fKeyName["C"] = KeyName::kC;
		fKeyName["D"] = KeyName::kD;
		fKeyName["E"] = KeyName::kE;
		fKeyName["F"] = KeyName::kF;
		fKeyName["G"] = KeyName::kG;
		fKeyName["H"] = KeyName::kH;
		fKeyName["I"] = KeyName::kI;
		fKeyName["J"] = KeyName::kJ;
		fKeyName["K"] = KeyName::kK;
		fKeyName["L"] = KeyName::kL;
		fKeyName["M"] = KeyName::kM;
		fKeyName["N"] = KeyName::kN;
		fKeyName["O"] = KeyName::kO;
		fKeyName["P"] = KeyName::kP;
		fKeyName["Q"] = KeyName::kQ;
		fKeyName["R"] = KeyName::kR;
		fKeyName["S"] = KeyName::kS;
		fKeyName["T"] = KeyName::kT;
		fKeyName["U"] = KeyName::kU;
		fKeyName["V"] = KeyName::kV;
		fKeyName["W"] = KeyName::kW;
		fKeyName["X"] = KeyName::kX;
		fKeyName["Y"] = KeyName::kY;
		fKeyName["Z"] = KeyName::kZ;
		fKeyName["0"] = KeyName::k0;
		fKeyName["1"] = KeyName::k1;
		fKeyName["2"] = KeyName::k2;
		fKeyName["3"] = KeyName::k3;
		fKeyName["4"] = KeyName::k4;
		fKeyName["5"] = KeyName::k5;
		fKeyName["6"] = KeyName::k6;
		fKeyName["7"] = KeyName::k7;
		fKeyName["8"] = KeyName::k8;
		fKeyName["9"] = KeyName::k9;
		fKeyName["Keypad 0"] = KeyName::kNumPad0;
		fKeyName["Keypad 1"] = KeyName::kNumPad1;
		fKeyName["Keypad 2"] = KeyName::kNumPad2;
		fKeyName["Keypad 3"] = KeyName::kNumPad3;
		fKeyName["Keypad 4"] = KeyName::kNumPad4;
		fKeyName["Keypad 5"] = KeyName::kNumPad5;
		fKeyName["Keypad 6"] = KeyName::kNumPad6;
		fKeyName["Keypad 7"] = KeyName::kNumPad7;
		fKeyName["Keypad 8"] = KeyName::kNumPad8;
		fKeyName["Keypad 9"] = KeyName::kNumPad9;
		fKeyName["Keypad /"] = KeyName::kNumPadDivide;
		fKeyName["Keypad *"] = KeyName::kNumPadMultiply;
		fKeyName["Keypad -"] = KeyName::kNumPadSubtract;
		fKeyName["Keypad +"] = KeyName::kNumPadAdd;
		fKeyName["Keypad Enter"] = KeyName::kNumPadEnter;
		fKeyName["Keypad ,"] = KeyName::kNumPadComma;
		fKeyName["Keypad ."] = KeyName::kNumPadPeriod;
		fKeyName["Keypad ("] = KeyName::kNumPadLeftParentheses;
		fKeyName["Keypad )"] = KeyName::kNumPadRightParentheses;
		fKeyName["Left Alt"] = KeyName::kLeftAlt;
		fKeyName["Right Alt"] = KeyName::kRightAlt;
		fKeyName["Left Ctrl"] = KeyName::kLeftControl;
		fKeyName["Right Ctrl"] = KeyName::kRightControl;
		fKeyName["Left Shift"] = KeyName::kLeftShift;
		fKeyName["Right Shift"] = KeyName::kRightShift;
		fKeyName["Left Windows"] = KeyName::kLeftCommand;
		fKeyName["Right Windows"] = KeyName::kRightCommand;
		fKeyName["Left GUI"] = KeyName::kLeftCommand;		// web
		fKeyName["Right GUI"] = KeyName::kRightCommand;		// web
		fKeyName["'"] = KeyName::kApostrophe;
		fKeyName["Backspace"] = KeyName::kDeleteBack;
		fKeyName["Home"] = KeyName::kHome;
		fKeyName["/"] = KeyName::kForwardSlash;
		fKeyName["\\"] = KeyName::kBackSlash;
		fKeyName["Numlock"] = KeyName::kNumLock;
		fKeyName["ScrollLock"] = KeyName::kScrollLock;
		fKeyName["Pause"] = KeyName::kMediaPause;
		fKeyName["Up"] = KeyName::kUp;
		fKeyName["Down"] = KeyName::kDown;
		fKeyName["Left"] = KeyName::kLeft;
		fKeyName["Right"] = KeyName::kRight;
		fKeyName["End"] = KeyName::kEnd;
		fKeyName["PageUp"] = KeyName::kPageUp;
		fKeyName["PageDown"] = KeyName::kPageDown;
		fKeyName["Insert"] = KeyName::kInsert;
		fKeyName["Delete"] = KeyName::kDeleteForward;
		fKeyName["="] = KeyName::kPlus;
		fKeyName["-"] = KeyName::kMinus;
		fKeyName["F1"] = KeyName::kF1;
		fKeyName["F2"] = KeyName::kF2;
		fKeyName["F3"] = KeyName::kF3;
		fKeyName["F4"] = KeyName::kF4;
		fKeyName["F5"] = KeyName::kF5;
		fKeyName["F6"] = KeyName::kF6;
		fKeyName["F7"] = KeyName::kF7;
		fKeyName["F8"] = KeyName::kF8;
		fKeyName["F9"] = KeyName::kF9;
		fKeyName["F10"] = KeyName::kF10;
		fKeyName["F11"] = KeyName::kF11;
		fKeyName["F12"] = KeyName::kF12;
		fKeyName["F13"] = KeyName::kF13;
		fKeyName["F14"] = KeyName::kF14;
		fKeyName["F15"] = KeyName::kF15;
		fKeyName["F16"] = KeyName::kF16;
		fKeyName["F17"] = KeyName::kF17;
		fKeyName["F18"] = KeyName::kF18;
		fKeyName["F19"] = KeyName::kF19;
		fKeyName["F20"] = KeyName::kF20;
		fKeyName["F21"] = KeyName::kF21;
		fKeyName["F22"] = KeyName::kF22;
		fKeyName["F23"] = KeyName::kF23;
		fKeyName["F24"] = KeyName::kF24;
		fKeyName["`"] = "`";
		fKeyName[";"] = ";";
		fKeyName[","] = ",";
		fKeyName["."] = KeyName::kPeriod;
		fKeyName["["] = KeyName::kLeftBracket;
		fKeyName["]"] = KeyName::kRightBracket;
		fKeyName["Tab"] = KeyName::kTab;
		fKeyName["Return"] = KeyName::kEnter;
		fKeyName["CapsLock"] = KeyName::kCapsLock;
		fKeyName["Escape"] = KeyName::kEscape;
		fKeyName["PrintScreen"] = KeyName::kPrintScreen;
		fKeyName["VolumeUp"] = KeyName::kVolumeUp;
		fKeyName["VolumeDown"] = KeyName::kVolumeDown;
		fKeyName["VolumeMute"] = KeyName::kVolumeMute;
		fKeyName["ZoomIn"] = KeyName::kZoomIn;
		fKeyName["ZoomOut"] = KeyName::kZoomOut;
		fKeyName["Break"] = KeyName::kBreak;
		fKeyName["Menu"] = KeyName::kMenu;
		fKeyName["Application"] = KeyName::kMenu;		// web
		fKeyName["Space"] = KeyName::kSpace;
	}

	void KeyListener::notifyKeyEvent(const SDL_KeyboardEvent& e, bool down)
	{
		RttSDL_Keymod mod = RttSDL_KeyMod(e);
		bool isShiftDown = (mod & RttSDL_KMOD_SHIFT) != 0;
		bool isCtrlDown = (mod & RttSDL_KMOD_CTRL) != 0;
		bool isAltDown = (mod & RttSDL_KMOD_ALT) != 0;
		bool isCommandDown = (mod & RttSDL_KMOD_GUI) != 0;

		SDL_Keycode key = RttSDL_KeyCode(e);
		const char* sdlKeyName = SDL_GetKeyName(key);
		std::map<std::string, std::string>::const_iterator it = fKeyName.find(sdlKeyName ? sdlKeyName : "");
		const char* keyName = it == fKeyName.end() ? KeyName::kUnknown : it->second.c_str();

		KeyEvent ke(NULL, down ? KeyEvent::kDown : KeyEvent::kUp, keyName, (S32) key, isShiftDown, isAltDown, isCtrlDown, isCommandDown);
		fRuntime.DispatchEvent(ke);
	}

	//
	// CoronaAppContext
	//

	CoronaAppContext::CoronaAppContext(const char* pathToApp)
		: fRuntime(NULL)
		, fRuntimeDelegate(new EmscriptenRuntimeDelegate())
		, fMouseListener(NULL)
		, fKeyListener(NULL)
		, fPathToApp(pathToApp)
		, fDocumentsDir(pathToApp)
		, fWidth(320)
		, fHeight(480)
		, fCssWidth(0)
		, fCssHeight(0)
		, fPixelWidth(0)
		, fPixelHeight(0)
		, fPixelRatio(1)
		, fPlatform(NULL)
		, fOrientation(DeviceOrientation::kUpright)
		, fAppState(MOUNT_IDBFS)
		, fWindow(NULL)
		, fMode("maximized")
		, fFullscreenRequested(false)
	{
#ifdef EMSCRIPTEN
		fPathToApp = "/";
#endif
		fDocumentsDir += DIR_SEPARATOR;
		fDocumentsDir += "documentsDir";

		// for debugging
#ifdef _WIN32
		_mkdir(fDocumentsDir.c_str());
#endif
	}

	CoronaAppContext::~CoronaAppContext()
	{
		ReleaseRuntime();
		delete fRuntimeDelegate;
		delete fPlatform;

		SDL_Quit();
	}

	void CoronaAppContext::ReleaseRuntime()
	{
		delete fMouseListener;
		delete fKeyListener;
		delete fRuntime;
		fMouseListener = NULL;
		fKeyListener = NULL;
		fRuntime = NULL;
	}

	bool CoronaAppContext::Initialize()
	{
		if (!RttSDL_Init(SDL_INIT_VIDEO))
		{
			Rtt_LogException("SDL_Init: %s\n", SDL_GetError());
		}

		int major, minor, patch;
		RttSDL_GetVersion(&major, &minor, &patch);
		printf("SDL %d.%d.%d\n", major, minor, patch);

#if defined(EMSCRIPTEN)
		emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, blurCallback);
		emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, true, focusCallback);
		emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, mouseupCallback);	// user gestures:
		emscripten_set_touchend_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, true, touchCallback);	// audio unlock, full screen
		emscripten_set_beforeunload_callback(this, beforeunloadCallback);

		// resourceDir, documentsDir, temporaryDir, cachesDir, systemCachesDir
		fPlatform = new EmscriptenPlatform(fPathToApp.c_str(), fDocumentsDir.c_str(), fPathToApp.c_str(), fPathToApp.c_str(), fPathToApp.c_str());
#else
		fPlatform = new EmscriptenPlatformWin(fPathToApp.c_str(), fDocumentsDir.c_str(), fPathToApp.c_str(), fPathToApp.c_str(), fPathToApp.c_str());
#endif

		fRuntime = new EmscriptenRuntime(*fPlatform, NULL);
		fRuntime->SetDelegate(fRuntimeDelegate);
		fRuntime->SetProperty(Runtime::kEmscriptenMaskSet, true);

		// For debugging, use main.lua if it exists in the app folder
		{
			std::string main_lua = fPathToApp;
			main_lua += DIR_SEPARATOR;
			main_lua += "main.lua";
			FILE* fi = fopen(main_lua.c_str(), "r");
			if (fi)
			{
				fclose(fi);
				fRuntime->SetProperty(Runtime::kEmscriptenMaskSet | Runtime::kIsApplicationNotArchived | Runtime::kShouldVerifyLicense, true);
			}
		}

		// build.settings
		std::string orientation;
		std::string title;
		int w = 0;
		int h = 0;
		fRuntime->readSettings(&w, &h, &orientation, &title, &fMode);

		if (orientation == "landscapeRight")
		{
			fOrientation = DeviceOrientation::kSidewaysRight;	// bottom of device is to the right
		}
		else if (orientation == "landscapeLeft")
		{
			fOrientation = DeviceOrientation::kSidewaysLeft;	// bottom of device is to the left
		}
		else if (orientation == "portraitUpsideDown")
		{
			fOrientation = DeviceOrientation::kUpsideDown;
		}
		else
		{
			fOrientation = DeviceOrientation::kUpright;		// "portrait", or not set
		}

		if (w > 0 && h > 0)
		{
			// defaultViewWidth / defaultViewHeight, as given
			fWidth = w;
			fHeight = h;
		}
		else if (IsSideways(fOrientation))
		{
			// default 320 x 480 view, turned for landscape
			Swap(fWidth, fHeight);
		}

		if (fMode != "maximized" && fMode != "fullscreen" && fMode != "normal")
		{
			if (!fMode.empty())
			{
				Rtt_LogException("WARNING: build.settings: unknown defaultMode '%s', using 'maximized'\n", fMode.c_str());
			}
			fMode = "maximized";
		}

		jsContextInit(fWidth, fHeight, fOrientation);

		// The window is created at the build.settings size and then sized to the page by UpdateCanvasSize().
		// Not SDL_WINDOW_RESIZABLE: SDL's own resize handler would then resize the canvas too.
		// Not high pixel density: window units are canvas pixels, so mouse and touch positions are in canvas
		// pixels like the surface size (see Rtt_EmscriptenScreenSurface).
		fWindow = RttSDL_CreateWindow(title.empty() ? NULL : title.c_str(), fWidth, fHeight, SDL_WINDOW_OPENGL);
		if (fWindow == NULL)
		{
			Rtt_LogException("SDL_CreateWindow: %s\n", SDL_GetError());
			return false;
		}

#if defined(EMSCRIPTEN)
		// Ask for WebGL 2 (GLES 3) first: it can build mipmaps for textures of any size (see Rtt_GLTexture.cpp).
		// Browsers without WebGL 2 get WebGL 1. Needs -sMAX_WEBGL_VERSION=2 at link time.
		// The context version must be set BEFORE SDL_GL_CreateContext.
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GLContext glContext = SDL_GL_CreateContext(fWindow);
		if (glContext == NULL)
		{
			Rtt_Log("WebGL 2 is not available, using WebGL 1\n");
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			glContext = SDL_GL_CreateContext(fWindow);
		}
		if (glContext == NULL)
		{
			Rtt_LogException("WebGL is not available: %s\n", SDL_GetError());
			return false;
		}
#else
		SDL_GL_CreateContext(fWindow);
#endif
		SDL_GL_SetSwapInterval(1);		// vsync
		fPlatform->setWindow(fWindow, fOrientation);

#if defined(Rtt_EMSCRIPTEN_ENV) && defined(WIN32)
		glewExperimental = GL_TRUE;
		glewInit();
#endif

		// Final canvas size before the app starts, so the display is set up for it
		UpdateCanvasSize();

		fMouseListener = new MouseListener(*fRuntime);
		fKeyListener = new KeyListener(*fRuntime);

		if (Runtime::kSuccess != fRuntime->LoadApplication(Runtime::kHTML5LaunchOption, fOrientation))
		{
			return false;
		}

		// pass config.lua's content size to JS
		int contentWidth = fRuntimeDelegate->fContentWidth;
		int contentHeight = fRuntimeDelegate->fContentHeight;
		if (IsSideways(fOrientation))
		{
			Swap(contentWidth, contentHeight);
		}
		jsContextConfig(contentWidth, contentHeight);

		fRuntime->BeginRunLoop();

		DisplayDefaults& defaults = fRuntime->GetDisplay().GetDefaults();
		ColorUnion c;
		c.pixel = defaults.GetClearColor();
		jsContextSetClearColor(c.rgba.r, c.rgba.g, c.rgba.b, c.rgba.a);

		// Native objects created by main.lua are placed now that the content transform is known
		UpdateContentTransform();

		return true;
	}

	//
	// Canvas size
	//
	// The canvas fills the browser window (the iframe it runs in, the screen in full screen, or the area from
	// Module.getCanvasAreaSize(); see jsContextGetCanvasArea) in the "maximized" and "fullscreen" modes. In the "normal" mode it is defaultViewWidth x defaultViewHeight CSS pixels, scaled down
	// to fit. The drawing buffer is the CSS size times devicePixelRatio, so the app is drawn at the display's
	// resolution, and one canvas pixel is exactly 1 / devicePixelRatio CSS pixels (no resampling blur).
	//
	// Fitting the content into the canvas (letterBox, zoomEven, zoomStretch, adaptive, none) is done by the
	// engine, as on the other platforms. The canvas used to take the content's aspect ratio as well, which drew
	// the letterbox bars twice (inside the canvas and around it) and made zoomEven and adaptive behave differently
	// from devices.
	//
	// The size is checked every frame, which catches window resizes, page zoom, a move to a screen with another
	// pixel ratio, rotation and full screen.
	//
	// Returns true when the size changed.
	bool CoronaAppContext::UpdateCanvasSize()
	{
		if (fWindow == NULL)
		{
			return false;
		}

		double areaWidth, areaHeight, ratio;
		GetCanvasArea(&areaWidth, &areaHeight, &ratio);
		if (areaWidth < 1 || areaHeight < 1)
		{
			return false;	// hidden (display: none, or an iframe being laid out): keep the current size
		}

		double cssWidth = areaWidth;
		double cssHeight = areaHeight;
		if (fMode == "normal")
		{
			double fit = Min(areaWidth / fWidth, areaHeight / fHeight);
			double scale = Min(1.0, fit);
			cssWidth = fWidth * scale;
			cssHeight = fHeight * scale;
		}

		double largest = Max(cssWidth, cssHeight) * ratio;
		if (largest > kMaxCanvasPixels)
		{
			ratio *= kMaxCanvasPixels / largest;
		}

		int pixelWidth = Max(1, (int) floor(cssWidth * ratio + 0.5));
		int pixelHeight = Max(1, (int) floor(cssHeight * ratio + 0.5));
		if (pixelWidth == fPixelWidth && pixelHeight == fPixelHeight && ratio == fPixelRatio)
		{
			return false;
		}

		fPixelWidth = pixelWidth;
		fPixelHeight = pixelHeight;
		fPixelRatio = ratio;
		fCssWidth = pixelWidth / ratio;
		fCssHeight = pixelHeight / ratio;

		SDL_SetWindowSize(fWindow, pixelWidth, pixelHeight);	// drawing buffer
#if defined(EMSCRIPTEN)
		emscripten_set_element_css_size("#canvas", fCssWidth, fCssHeight);
#endif
		fPlatform->setPixelRatio((float) ratio);
		return true;
	}

	void CoronaAppContext::OnCanvasResized()
	{
		if (fRuntime == NULL)
		{
			return;
		}

		fRuntime->WindowSizeChanged();
		fRuntime->RestartRenderer(fOrientation);
		fRuntime->GetDisplay().Invalidate();
		fRuntime->DispatchEvent(ResizeEvent());

		UpdateContentTransform();
	}

	// Tells JS where content coordinates are on the page, for native objects (text fields, video...):
	// cssX = x + contentX * sx, cssY = y + contentY * sy, relative to the canvas' top left corner.
	void CoronaAppContext::UpdateContentTransform()
	{
		if (fRuntime == NULL)
		{
			return;
		}

		float x = 0, y = 0, w = 1, h = 1;
		fRuntime->GetDisplay().ContentToScreenUnrounded(x, y, w, h);	// canvas pixels

		const float r = (float) fPixelRatio;
		jsContextSetContentTransform(x / r, y / r, w / r, h / r);
		jsContextResizeNativeObjects();
	}

#if defined(EMSCRIPTEN)

	EM_BOOL CoronaAppContext::mouseupCallback(int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData)
	{
		CoronaAppContext* ctx = (CoronaAppContext*) userData;
		if (ctx)
		{
			ctx->OnUserGesture();
		}
		return EM_FALSE;
	}

	EM_BOOL CoronaAppContext::touchCallback(int eventType, const EmscriptenTouchEvent *touchEvent, void *userData)
	{
		CoronaAppContext* ctx = (CoronaAppContext*) userData;
		if (ctx)
		{
			ctx->OnUserGesture();
		}
		return EM_FALSE;
	}

	EM_BOOL CoronaAppContext::blurCallback(int eventType, const EmscriptenFocusEvent *focusEvent, void *userData)
	{
		// only the window's own event (id is empty), not the focus moving between page elements
		if (focusEvent->id[0] == 0)
		{
			CoronaAppContext* ctx = (CoronaAppContext*) userData;
			ctx->pause();
		}
		return EM_FALSE;
	}

	EM_BOOL CoronaAppContext::focusCallback(int eventType, const EmscriptenFocusEvent *focusEvent, void *userData)
	{
		if (focusEvent->id[0] == 0)
		{
			CoronaAppContext* ctx = (CoronaAppContext*) userData;
			ctx->resume();
		}
		return EM_FALSE;
	}

	const char* CoronaAppContext::beforeunloadCallback(int eventType, const void *reserved, void *userData)
	{
		jsContextSyncFS();

		CoronaAppContext* ctx = (CoronaAppContext*) userData;
		if (ctx->GetRuntime())
		{
			Rtt::jsSystemEvent ev("applicationExit");
			ctx->GetRuntime()->DispatchEvent(ev);
		}
		return NULL;
	}

#endif

	// Browsers let a page start audio and enter full screen only from a user gesture
	void CoronaAppContext::OnUserGesture()
	{
		jsContextUnlockAudio();

		// "fullscreen" mode: full screen on the first click / touch
		if (fMode == "fullscreen" && !fFullscreenRequested)
		{
			fFullscreenRequested = true;
			requestFullscreen();
		}
	}

	void CoronaAppContext::requestFullscreen()
	{
#ifdef EMSCRIPTEN
		// Default strategy: the browser only makes the element full screen. The canvas is then resized by
		// UpdateCanvasSize() like for any other size change; emscripten's scaling strategies would resize it too.
		EmscriptenFullscreenStrategy s;
		memset(&s, 0, sizeof(s));
		s.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_DEFAULT;
		s.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_NONE;
		s.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
		EMSCRIPTEN_RESULT rc = emscripten_request_fullscreen_strategy("#emscripten_border", EM_TRUE, &s);
		if (rc != EMSCRIPTEN_RESULT_SUCCESS && rc != EMSCRIPTEN_RESULT_DEFERRED)
		{
			Rtt_Log("Full screen request failed (%d)\n", (int) rc);
		}
#endif
	}

	void CoronaAppContext::Start()
	{
#if defined(EMSCRIPTEN)
		emscripten_set_main_loop_arg(&TimerTickShim, this, 0, 1);	// never returns
#else
		bool closeApp = false;
		while (closeApp == false)
		{
			closeApp = TimerTick();
			SDL_Delay(30);		// hack, 30FPS for debugging
		}
#endif
	}

	void CoronaAppContext::pause()
	{
		if (fRuntime && fRuntime->IsSuspended() == false)
		{
			fRuntime->Suspend();

			// The page may never get "beforeunload" (mobile browsers, closed tabs): save documentsDir now
			jsContextSyncFS();
		}
	}

	void CoronaAppContext::resume()
	{
		if (fRuntime && fRuntime->IsSuspended())
		{
			fRuntime->Resume();
		}
	}

	void CoronaAppContext::DispatchMouseEvent(MouseEvent::MouseEventType type, float x, float y, float scrollX, float scrollY)
	{
		Uint32 buttons = RttSDL_MouseButtons();
		bool isPrimaryDown = (buttons & RttSDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
		bool isSecondaryDown = (buttons & RttSDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;
		bool isMiddleDown = (buttons & RttSDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) != 0;

		RttSDL_Keymod mod = (RttSDL_Keymod) SDL_GetModState();
		bool isShiftDown = (mod & RttSDL_KMOD_SHIFT) != 0;
		bool isAltDown = (mod & RttSDL_KMOD_ALT) != 0;
		bool isCtrlDown = (mod & RttSDL_KMOD_CTRL) != 0;
		bool isCommandDown = (mod & RttSDL_KMOD_GUI) != 0;

		if (type == MouseEvent::kMove && (isPrimaryDown || isSecondaryDown || isMiddleDown))
		{
			type = MouseEvent::kDrag;
		}

		MouseEvent e(type, x, y, Rtt_FloatToReal(scrollX), Rtt_FloatToReal(scrollY), 0,
			isPrimaryDown, isSecondaryDown, isMiddleDown, isShiftDown, isAltDown, isCtrlDown, isCommandDown);
		fRuntime->DispatchEvent(e);
	}

	bool CoronaAppContext::ProcessEvent(SDL_Event& event)
	{
		switch (event.type)
		{
		// Fingers. SDL also turns them into mouse events (which == SDL_TOUCH_MOUSEID); those are skipped below.
		case RttSDL_EVENT_FINGER_DOWN:
		case RttSDL_EVENT_FINGER_UP:
		case RttSDL_EVENT_FINGER_MOTION:
#if Rtt_SDL_MAJOR >= 3
		case SDL_EVENT_FINGER_CANCELED:
#endif
		{
			const SDL_TouchFingerEvent& f = event.tfinger;
			int w, h;
			RttSDL_GetWindowPixelSize(fWindow, &w, &h);
			float x = f.x * w;
			float y = f.y * h;
			SDL_FingerID id = RttSDL_FingerID(f);
			if (event.type == RttSDL_EVENT_FINGER_DOWN)
			{
				GetMouseListener()->TouchDown(x, y, id);
			}
			else if (event.type == RttSDL_EVENT_FINGER_UP)
			{
				GetMouseListener()->TouchUp(x, y, id);
			}
			else if (event.type == RttSDL_EVENT_FINGER_MOTION)
			{
				GetMouseListener()->TouchMoved(x, y, id);
			}
			else
			{
				GetMouseListener()->TouchCancelled(x, y, id);
			}
			break;
		}

		// Mouse: "mouse" events for every button; "touch" events for the left button only
		case RttSDL_EVENT_MOUSE_DOWN:
		case RttSDL_EVENT_MOUSE_UP:
		{
			const SDL_MouseButtonEvent& b = event.button;
			if (RttSDL_IsTouchMouse(b.which))
			{
				break;
			}
			float x = (float) b.x;
			float y = (float) b.y;
			bool down = (event.type == RttSDL_EVENT_MOUSE_DOWN);
			DispatchMouseEvent(down ? MouseEvent::kDown : MouseEvent::kUp, x, y, 0, 0);
			if (b.button == SDL_BUTTON_LEFT)
			{
				if (down)
				{
					GetMouseListener()->TouchDown(x, y, kMouseFingerId);
				}
				else
				{
					GetMouseListener()->TouchUp(x, y, kMouseFingerId);
				}
			}
			break;
		}

		case RttSDL_EVENT_MOUSE_MOTION:
		{
			const SDL_MouseMotionEvent& m = event.motion;
			if (RttSDL_IsTouchMouse(m.which))
			{
				break;
			}
			float x = (float) m.x;
			float y = (float) m.y;
			DispatchMouseEvent(MouseEvent::kMove, x, y, 0, 0);
			GetMouseListener()->TouchMoved(x, y, kMouseFingerId);	// ignored unless the left button is down
			break;
		}

		case RttSDL_EVENT_MOUSE_WHEEL:
		{
			const SDL_MouseWheelEvent& wheel = event.wheel;
			if (RttSDL_IsTouchMouse(wheel.which))
			{
				break;
			}

			// Like the Windows simulator: 3 lines of ~16 px per wheel notch, in content units, positive = down / right
			const float kPixelsPerNotch = 3.0f * 16.0f * (float) fPixelRatio;
			const Display& display = fRuntime->GetDisplay();
			float scrollX = RttSDL_WheelX(wheel) * kPixelsPerNotch * Rtt_RealToFloat(display.GetSx());
			float scrollY = -RttSDL_WheelY(wheel) * kPixelsPerNotch * Rtt_RealToFloat(display.GetSy());

			float x, y;
			RttSDL_GetMousePosition(&x, &y);
			DispatchMouseEvent(MouseEvent::kScroll, x, y, scrollX, scrollY);
			break;
		}

		case RttSDL_EVENT_KEY_DOWN:
		case RttSDL_EVENT_KEY_UP:
			if (!RttSDL_KeyRepeat(event.key))
			{
				GetKeyListener()->notifyKeyEvent(event.key, event.type == RttSDL_EVENT_KEY_DOWN);
			}
			break;

		// Window state (the page was hidden or shown). Size changes are handled by UpdateCanvasSize().
#if Rtt_SDL_MAJOR >= 3
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_RESTORED:
			resume();
			break;
		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_MINIMIZED:
			pause();
			break;
#else
		case SDL_WINDOWEVENT:
			switch (event.window.event)
			{
			case SDL_WINDOWEVENT_SHOWN:
			case SDL_WINDOWEVENT_RESTORED:
				resume();
				break;
			case SDL_WINDOWEVENT_HIDDEN:
			case SDL_WINDOWEVENT_MINIMIZED:
				pause();
				break;
			default:
				break;
			}
			break;
#endif

		default:
			break;
		}
		return false;
	}

	static bool HasFontExtension(const std::string& name)
	{
		size_t len = name.size();
		return len > 4 && (strcasecmp(name.c_str() + len - 4, ".ttf") == 0 || strcasecmp(name.c_str() + len - 4, ".otf") == 0);
	}

	void CoronaAppContext::enumerateFontFiles(const char* dir, std::vector<std::string>& files)
	{
		std::vector<std::string> fileList = Rtt_ListFiles(dir);
		for (size_t i = 0; i < fileList.size(); i++)
		{
			const std::string& name = fileList[i];
			if (Rtt_IsDirectory(name.c_str()))
			{
				// skip emscripten's system folders
				if (name != "//proc" && name != "//dev" && name != "//tmp" && name != "//home")
				{
					enumerateFontFiles(name.c_str(), files);
				}
			}
			else if (HasFontExtension(name))
			{
				files.push_back(name);
			}
		}
	}

	bool CoronaAppContext::TimerTick()
	{
		switch (fAppState)
		{
		case MOUNT_IDBFS:
		{
			// mount IDBFS on documentsDir and load it
			int rc = jsContextMountFS();
			fAppState = (rc == 1) ? WAIT_FOR_IDBFS_MOUNTED : LOAD_FONTS;
			break;
		}

		case WAIT_FOR_IDBFS_MOUNTED:
			if (jsContextGetIntModuleItem("documentsDirLoaded") == 1)
			{
				fAppState = COPY_DOCS;
			}
			break;

		case COPY_DOCS:
		{
			// First start: copy the app's .db files to documentsDir. ".installed" marks that it was done.
			std::string installed = fDocumentsDir;
			installed += DIR_SEPARATOR;
			installed += ".installed";
			if (Rtt_FileExists(installed.c_str()))
			{
				fAppState = LOAD_FONTS;
				break;
			}

			FILE* f = fopen(installed.c_str(), "w");
			if (f == NULL)
			{
				Rtt_LogException("Failed to create %s\n", installed.c_str());
				fAppState = LOAD_FONTS;
				break;
			}
			fclose(f);

			int copiedFiles = 0;
			std::vector<std::string> files = Rtt_ListFiles(fPathToApp.c_str());
			for (size_t i = 0; i < files.size(); i++)
			{
				const std::string& path = files[i];
				size_t len = path.size();
				if (len > 3 && path.compare(len - 3, 3, ".db") == 0)
				{
					size_t slash = path.rfind(DIR_SEPARATOR);
					std::string dst = fDocumentsDir;
					dst += (slash == std::string::npos) ? (DIR_SEPARATOR + path) : path.substr(slash);
					Rtt_CopyFile(path.c_str(), dst.c_str());
					copiedFiles++;
				}
			}

			// sync the marker and the databases; nothing else to wait for if there are none
			jsContextSyncFS();
			fAppState = copiedFiles > 0 ? WAIT_FOR_SYNC : LOAD_FONTS;
			break;
		}

		case WAIT_FOR_SYNC:
			if (jsContextGetIntModuleItem("idbfsSynced") == 1)
			{
				fAppState = LOAD_FONTS;
			}
			break;

		case LOAD_FONTS:
		{
			// Register the app's .ttf / .otf files with the browser (FontFace), for text rendering
			int loadingFonts = 0;
			std::vector<std::string> fileList;
			enumerateFontFiles(fPathToApp.c_str(), fileList);
			for (size_t i = 0; i < fileList.size(); i++)
			{
				FILE* fi = fopen(fileList[i].c_str(), "rb");
				if (fi == NULL)
				{
					continue;
				}
				fseek(fi, 0, SEEK_END);
				long size = ftell(fi);
				fseek(fi, 0, SEEK_SET);
				if (size > 0)
				{
					std::vector<char> buf(size);
					if (fread(&buf[0], 1, size, fi) == (size_t) size)
					{
						loadingFonts += jsContextLoadFonts(fileList[i].c_str(), &buf[0], (int) size);
					}
				}
				fclose(fi);
			}
			fAppState = loadingFonts > 0 ? WAIT_FOR_FONTS : INIT_APP;
			break;
		}

		case WAIT_FOR_FONTS:
#ifndef WIN32
			if (jsContextGetIntModuleItem("loadingFonts") == 0)
#endif
			{
				fAppState = INIT_APP;
			}
			break;

		case INIT_APP:
			if (Initialize())
			{
				fAppState = RUN_APP;
			}
			else
			{
				Rtt_LogException("The app failed to start\n");
				ReleaseRuntime();
				fAppState = FAILED;
			}
			break;

		case RUN_APP:
		{
			bool closeApp = false;
			SDL_Event event;
			while (closeApp == false && SDL_PollEvent(&event))
			{
				closeApp = ProcessEvent(event);
			}

			if (UpdateCanvasSize())
			{
				OnCanvasResized();
			}

			if (fRuntime->IsSuspended() == false)
			{
				(*fRuntime)();
			}
			return closeApp;
		}

		case FAILED:
		default:
			break;
		}
		return false;
	}

	bool EmscriptenRuntime::readTable(lua_State *L, const char* table, int* w, int* h, std::string* title, std::string* mode) const
	{
		bool rc = false;
		int top = lua_gettop(L);

		lua_getfield(L, -1, table);
		if (lua_istable(L, -1))
		{
			rc = true;
			lua_getfield(L, -1, "defaultViewWidth");
			if (lua_isnumber(L, -1))
			{
				*w = (int) lua_tointeger(L, -1);
			}
			lua_pop(L, 1);

			lua_getfield(L, -1, "defaultViewHeight");
			if (lua_isnumber(L, -1))
			{
				*h = (int) lua_tointeger(L, -1);
			}
			lua_pop(L, 1);

			lua_getfield(L, -1, "defaultMode");
			if (lua_type(L, -1) == LUA_TSTRING)
			{
				*mode = lua_tostring(L, -1);
			}
			lua_pop(L, 1);

			lua_getfield(L, -1, "titleText");
			if (lua_istable(L, -1))
			{
				lua_getfield(L, -1, "default");
				if (lua_type(L, -1) == LUA_TSTRING)
				{
					*title = lua_tostring(L, -1);
				}
				lua_pop(L, 1);		// default
			}
			lua_pop(L, 1);		// titleText
		}

		lua_settop(L, top);
		return rc;
	}

	void EmscriptenRuntime::readSettings(int* w, int* h, std::string* orientation, std::string* title, std::string* mode)
	{
		Rtt_ASSERT(w != NULL && h != NULL);

		lua_State *L = VMContext().L();
		const MPlatform& p = Platform();
		int top = lua_gettop(L);

		const char kBuildSettings[] = "build.settings";
		String filePath(&p.GetAllocator());
		p.PathForFile(kBuildSettings, MPlatform::kResourceDir, MPlatform::kTestFileExists, filePath);
		const char *path = filePath.GetString();
		int status = -1;
		if (path)
		{
			status = VMContext().DoFile(path, 0, true);
		}

		if (status == 0)
		{
			lua_getglobal(L, "settings");
			if (lua_istable(L, -1))
			{
				lua_getfield(L, -1, "orientation");
				if (lua_istable(L, -1))
				{
					lua_getfield(L, -1, "default");
					if (lua_type(L, -1) == LUA_TSTRING)
					{
						*orientation = lua_tostring(L, -1);
					}
					lua_pop(L, 1);
				}
				lua_pop(L, 1);		// orientation

				// the "web" table, else "html5", else "window"
				if (!readTable(L, "web", w, h, title, mode) && !readTable(L, "html5", w, h, title, mode))
				{
					readTable(L, "window", w, h, title, mode);
				}
			}
		}
		else
		{
			Rtt_LogException("No build.settings file\n");
		}
		lua_settop(L, top);
	}

	int jsSystemEvent::Push(lua_State *L) const
	{
		if (Rtt_VERIFY(VirtualEvent::Push(L)))
		{
			lua_pushstring(L, fEventName.c_str());
			lua_setfield(L, -2, kTypeKey);
		}
		return 1;
	}

}
