#!/bin/sh
# Kept for compatibility: now just runs tools/build.py win (see build.bat / build.sh).
#   LUA_INCLUDE=<folder with lua.h> ./build_win_zig.sh
cd "$(dirname "$0")" || exit 1
exec python3 tools/build.py win "$@"
