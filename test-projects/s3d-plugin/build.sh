#!/bin/sh
# Builds plugin.s3d on Linux / macOS / Git Bash. Same commands and options as build.bat:
#   ./build.sh [all|win|android|package|install|clean] [--publisher ID] [--abis ...] [--android-tool ndk|zig] [--ndk DIR]
cd "$(dirname "$0")" || exit 1
if command -v python3 >/dev/null 2>&1; then
    exec python3 tools/build.py "$@"
fi
exec python tools/build.py "$@"
