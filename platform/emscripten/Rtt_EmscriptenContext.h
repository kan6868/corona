//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include <map>
#include "Rtt_Event.h"
#include "Core/Rtt_Types.h"
#include "Rtt_Runtime.h"
#include "Rtt_EmscriptenRuntimeDelegate.h"
#include "Core/Rtt_Math.h"
#include "Core/Rtt_Array.h"

#include "Rtt_EmscriptenSDL.h"

#if defined(EMSCRIPTEN)
	#include "emscripten/emscripten.h"
	#include "emscripten/html5.h"
#else
	#include <GL/glew.h>
	#include "Rtt_EmscriptenPlatformWin.h"
#endif

#if defined(EMSCRIPTEN)
	#define DIR_SEPARATOR "/"
#else
	#define DIR_SEPARATOR "\\"
#endif

namespace Rtt
{
	class EmscriptenPlatform;

	class EmscriptenRuntime : public Runtime
	{
	public:
		EmscriptenRuntime(const MPlatform& platform, MCallback *viewCallback = NULL)
			: Runtime(platform, viewCallback)
		{
		}

		// Reads build.settings: orientation.default and the web / html5 / window table
		void readSettings(int* w, int* h, std::string* orientation, std::string* title, std::string* mode);
		bool readTable(lua_State *L, const char* name, int* w, int* h, std::string* title, std::string* mode) const;
	};

	class KeyListener
	{
	public:
		KeyListener(Runtime &runtime);

		void notifyKeyEvent(const SDL_KeyboardEvent& e, bool down);

	private:
		Runtime& fRuntime;
		std::map<std::string, std::string> fKeyName;
	};

	// Turns mouse and finger input into "touch", multitouch and "tap" events. Coordinates are canvas pixels.
	class MouseListener
	{
	public:
		MouseListener(Runtime &runtime);

		void TouchDown(float x, float y, SDL_FingerID id);
		void TouchMoved(float x, float y, SDL_FingerID id);
		void TouchUp(float x, float y, SDL_FingerID id);
		void TouchCancelled(float x, float y, SDL_FingerID id);
		void DispatchEvent(const MEvent& e) const;

	private:
		struct pt
		{
			pt() : x(0), y(0) {}
			pt(float xx, float yy) : x(xx), y(yy) {}
			float x;
			float y;
		};

		void Dispatch(TouchEvent& t, bool notifyMultitouch) const;

		Runtime& fRuntime;
		std::map<SDL_FingerID, pt> fStartPoint;
	};

	// Immediately broadcast to "Runtime"
	class jsSystemEvent : public VirtualEvent
	{
	public:
		jsSystemEvent(const char* e)
			: fEventName(e)
		{}

		virtual const char* Name() const { return "system"; }
		virtual int Push(lua_State *L) const;

	private:
		std::string fEventName;
	};

	struct CoronaAppContext
	{
		CoronaAppContext(const char* pathToApp);
		~CoronaAppContext();

		// Startup steps, run one per frame by TimerTick() before the app runs
		enum AppState
		{
			MOUNT_IDBFS,
			WAIT_FOR_IDBFS_MOUNTED,
			COPY_DOCS,
			WAIT_FOR_SYNC,
			LOAD_FONTS,
			WAIT_FOR_FONTS,
			INIT_APP,
			RUN_APP,
			FAILED
		};

		bool Initialize();
		bool IsInitialized() const { return NULL != fRuntime; }
		void Start();
		bool ProcessEvent(SDL_Event& event);
		void enumerateFontFiles(const char* dir, std::vector<std::string>& fileList);
		bool TimerTick();

		Runtime* GetRuntime() { return fRuntime; }
		const Runtime *GetRuntime() const { return fRuntime; }

		MouseListener* GetMouseListener() { return fMouseListener; }
		const MouseListener *GetMouseListener() const { return fMouseListener; }
		KeyListener* GetKeyListener() { return fKeyListener; }
		const KeyListener *GetKeyListener() const { return fKeyListener; }
		void pause();
		void resume();
		int getFPS() const { return fRuntime ? fRuntime->GetFPS() : 30; }

		// Puts the canvas' parent element (#emscripten_border) in browser full screen.
		// Browsers allow it only from a user gesture (click, touch, key).
		void requestFullscreen();

#if defined(EMSCRIPTEN)
		static EM_BOOL mouseupCallback(int eventType, const EmscriptenMouseEvent *mouseEvent, void * userData);
		static EM_BOOL touchCallback(int eventType, const EmscriptenTouchEvent *touchEvent, void *userData);
		static EM_BOOL blurCallback(int eventType, const EmscriptenFocusEvent *focusEvent, void *userData);
		static EM_BOOL focusCallback(int eventType, const EmscriptenFocusEvent *focusEvent, void *userData);
		static const char* beforeunloadCallback(int eventType, const void *reserved, void *userData);
#endif

	private:
		void ReleaseRuntime();

		// Canvas size. See the comment above UpdateCanvasSize() in the .cpp file.
		bool UpdateCanvasSize();
		void OnCanvasResized();
		void UpdateContentTransform();
		void DispatchMouseEvent(MouseEvent::MouseEventType type, float x, float y, float scrollX, float scrollY);
		void OnUserGesture();

		EmscriptenRuntime* fRuntime;
		EmscriptenRuntimeDelegate* fRuntimeDelegate;
		MouseListener* fMouseListener;
		KeyListener* fKeyListener;
		std::string fPathToApp;
		std::string fDocumentsDir;

		// Window size from build.settings (defaultViewWidth / defaultViewHeight), used by the "normal" mode
		int fWidth;
		int fHeight;

		// Current canvas size: CSS pixels, canvas (drawing buffer) pixels and their ratio
		double fCssWidth;
		double fCssHeight;
		int fPixelWidth;
		int fPixelHeight;
		double fPixelRatio;

		EmscriptenPlatform* fPlatform;
		DeviceOrientation::Type fOrientation;
		AppState fAppState;
		SDL_Window* fWindow;
		std::string fMode;		// "maximized" (default), "fullscreen" or "normal"
		bool fFullscreenRequested;
	};

};
