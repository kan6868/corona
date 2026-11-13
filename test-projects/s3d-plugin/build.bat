@echo off
rem Builds plugin.s3d on Windows. Examples:
rem   build.bat                 build Windows + Android, package, install into the Solar2DPlugins folder
rem   build.bat win             build plugin.s3d.dll only (Simulator)
rem   build.bat android         build libplugin.s3d.so only (Android)
rem   build.bat package         create dist\Solar2DPlugins\...\data.tgz
rem   build.bat install         install the packages into Solar2D's plugin folder
rem Options: --publisher ID, --abis arm64-v8a,armeabi-v7a, --android-tool ndk/zig, --ndk DIR, --lua-include DIR
setlocal
cd /d "%~dp0"

where py >nul 2>nul
if errorlevel 1 goto try_python
py -3 tools\build.py %*
goto done

:try_python
where python >nul 2>nul
if errorlevel 1 goto no_python
python tools\build.py %*
goto done

:no_python
echo [s3d] ERROR: Python 3 not found. Install it from https://www.python.org/downloads/ (check "Add python.exe to PATH").
exit /b 1

:done
exit /b %errorlevel%
