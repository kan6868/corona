# plugin.s3d - CMake for Android (NDK) and Windows (MSVC, optional).
#
# Android (from Android Studio / gradle externalNativeBuild, or the command line):
#   cmake -B build-android -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
#         -DSOLAR2D_LUA_INCLUDE=<folder with lua.h> -DSOLAR2D_ANDROID_LIBS=<folder with liblua.so, libcorona.so for this ABI>
#   cmake --build build-android
#   -> libplugin.s3d.so
#
# Windows: uses S3D_DYNAMIC (functions resolved at run time), no Solar2D libraries needed. Must be a 32-bit (Win32) build.
#   cmake -B build-win -A Win32 -DSOLAR2D_LUA_INCLUDE=<folder with lua.h>
#   cmake --build build-win --config Release
#   -> plugin.s3d.dll

cmake_minimum_required( VERSION 3.14 )
project( s3d C )

set( SOLAR2D_LUA_INCLUDE "" CACHE PATH "Folder with lua.h, luaconf.h, lauxlib.h (Lua 5.1)" )
set( SOLAR2D_ANDROID_LIBS "" CACHE PATH "Folder with liblua.so and libcorona.so (Android, per ABI)" )

include( FetchContent )
FetchContent_Declare( cglm GIT_REPOSITORY https://github.com/recp/cglm.git GIT_TAG v0.9.4 )
FetchContent_GetProperties( cglm )
if( NOT cglm_POPULATED )
    FetchContent_Populate( cglm ) # headers only
endif()

find_package( Python3 REQUIRED COMPONENTS Interpreter )
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/s3d_lua.h
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_lua.py ${CMAKE_CURRENT_SOURCE_DIR}/src/s3d.lua ${CMAKE_CURRENT_BINARY_DIR}/s3d_lua.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/s3d.lua )

add_library( plugin.s3d SHARED src/s3d.c ${CMAKE_CURRENT_BINARY_DIR}/s3d_lua.h )
set_target_properties( plugin.s3d PROPERTIES PREFIX "" C_STANDARD 99 )
target_include_directories( plugin.s3d PRIVATE ${CMAKE_CURRENT_BINARY_DIR} src ${cglm_SOURCE_DIR}/include ${SOLAR2D_LUA_INCLUDE} )

if( ANDROID )
    set_target_properties( plugin.s3d PROPERTIES PREFIX "lib" )
    target_link_libraries( plugin.s3d PRIVATE ${SOLAR2D_ANDROID_LIBS}/liblua.so ${SOLAR2D_ANDROID_LIBS}/libcorona.so GLESv2 log )
elseif( WIN32 )
    target_compile_definitions( plugin.s3d PRIVATE S3D_DYNAMIC _CRT_SECURE_NO_WARNINGS )
endif()
