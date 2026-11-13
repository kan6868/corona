@echo off
rem Build plugin.box3d tren Windows. Vi du:
rem   build.bat                 build Windows + Android, dong goi, cai vao thu muc Solar2DPlugins
rem   build.bat win             chi build plugin.box3d.dll (Simulator)
rem   build.bat android         chi build libplugin.box3d.so (Android)
rem   build.bat test            chay 22 test (can Linux / WSL)
rem   build.bat package         tao dist\Solar2DPlugins\...\data.tgz
rem   build.bat install         cai goi da tao vao thu muc plugin cua Solar2D
rem Tuy chon: --publisher ID, --abis arm64-v8a,armeabi-v7a, --android-tool ndk/zig, --ndk DIR, --lua-include DIR
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
echo [s3d] LOI: khong tim thay Python 3. Cai tu https://www.python.org/downloads/ (chon "Add python.exe to PATH").
exit /b 1

:done
exit /b %errorlevel%
