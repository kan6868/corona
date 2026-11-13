#!/bin/sh
# Build plugin.s3d trên Linux / macOS / Git Bash. Lệnh và tuỳ chọn giống build.bat:
#   ./build.sh [all|win|android|package|install|test|clean] [--publisher ID] [--abis ...] [--android-tool ndk|zig] [--ndk DIR]
cd "$(dirname "$0")" || exit 1
if command -v python3 >/dev/null 2>&1; then
    exec python3 tools/build.py "$@"
fi
exec python tools/build.py "$@"
