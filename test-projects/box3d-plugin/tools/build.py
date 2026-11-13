#!/usr/bin/env python3
"""
Build plugin.box3d (Box3D của Erin Catto, MIT) cho Windows (Simulator) và Android, đóng gói và cài vào
thư mục plugin local của Solar2D.

    python tools/build.py all            # win + android + package + install
    python tools/build.py win            # build/win32/plugin.box3d.dll
    python tools/build.py android        # build/android/jniLibs/<abi>/libplugin.box3d.so
    python tools/build.py package        # dist/plugin.box3d-win32.tgz, dist/plugin.box3d-android.tgz
                                         # + dist/Solar2DPlugins/<publisherId>/plugin.box3d/<platform>/data.tgz
    python tools/build.py install        # chép dist vào %APPDATA%\\Solar2DPlugins (hoặc $HOME/Solar2DPlugins)
    python tools/build.py test           # build bản máy host (gcc/clang) và chạy tests/run_tests.lua
    python tools/build.py clean

Tuỳ chọn: --lua-include DIR, --publisher ID (mặc định com.s3d), --abis LIST, --android-tool auto|ndk|zig,
--ndk DIR, --box3d DIR (mã nguồn Box3D có sẵn, bỏ qua bước tải), --jobs N.

Chỉ cần Python 3.8+ và git. zig tự cài qua "pip install ziglang" nếu chưa có.
Windows: plugin link với lua.dll của Simulator (import library tạo bằng "zig dlltool").
Android: plugin link với liblua.so rỗng chỉ mang tên; hàm thật lấy từ app Solar2D lúc chạy.
"""

import argparse
import concurrent.futures
import glob
import io
import os
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile

# Console Windows (cp1252/cp437) không in được tiếng Việt: chuyển sang UTF-8, ký tự lạ thì thay thế
for _stream in ( sys.stdout, sys.stderr ):
    try:
        _stream.reconfigure( encoding = "utf-8", errors = "replace" )
    except Exception:
        pass

ROOT = os.path.dirname( os.path.dirname( os.path.abspath( __file__ ) ) )
SRC = os.path.join( ROOT, "src" )
BUILD = os.path.join( ROOT, "build" )
DIST = os.path.join( ROOT, "dist" )
BOX3D_DIR = os.path.join( ROOT, "third_party", "box3d" )
BOX3D_REPO = "https://github.com/erincatto/box3d.git"
BOX3D_COMMIT = "a8ac09f364de4cb099b10b0e331e378c9f0d3638"  # 2026-11-11, đã chạy 22 test trong tests/
PLUGIN = "plugin.box3d"
LUAOPEN = "luaopen_plugin_box3d"

# Hàm Lua C API plugin dùng (cho import library của lua.dll trên Windows)
LUA_EXPORTS = """lua_checkstack lua_createtable lua_error lua_getfenv lua_getfield lua_getmetatable lua_gettop
lua_insert lua_isnumber lua_isstring lua_isuserdata lua_newuserdata lua_next lua_objlen lua_pushboolean
lua_pushcclosure lua_pushfstring lua_pushinteger lua_pushlightuserdata lua_pushlstring lua_pushnil lua_pushnumber
lua_pushstring lua_pushvalue lua_rawequal lua_rawget lua_rawgeti lua_rawset lua_rawseti lua_remove lua_replace
lua_setfenv lua_setfield lua_setmetatable lua_settop lua_toboolean lua_tointeger lua_tolstring lua_tonumber
lua_topointer lua_touserdata lua_type lua_typename luaL_argerror luaL_checkinteger luaL_checklstring
luaL_checknumber luaL_checkstack luaL_checktype luaL_checkudata luaL_error luaL_getmetafield luaL_newmetatable
luaL_optinteger luaL_optlstring luaL_optnumber luaL_ref luaL_register luaL_typerror luaL_unref luaL_where""".split()

# ABI Android -> (target zig, cờ thêm cho zig, target clang của NDK)
ANDROID_ABIS = {
    "arm64-v8a":   ( "aarch64-linux-musl", [], "aarch64-linux-android21" ),
    "armeabi-v7a": ( "arm-linux-musleabi", [ "-mcpu=cortex_a9", "-mfloat-abi=softfp", "-mthumb" ], "armv7a-linux-androideabi21" ),
    "x86_64":      ( "x86_64-linux-musl", [], "x86_64-linux-android21" ),
    "x86":         ( "x86-linux-musl", [], "i686-linux-android21" ),
}
# Thư viện plugin cần ở lúc chạy (Solar2D / Android đã nạp sẵn)
ANDROID_NEEDED = [ "liblua.so", "libm.so", "libdl.so", "libc.so" ]

METADATA_LUA = """local metadata =
{
    plugin =
    {
        format = "sharedLibrary",
        manifest = {},
    },
}

return metadata
"""


def log( *a ):
    print( "[box3d]", *a, flush = True )


def fail( msg ):
    print( "[box3d] LỖI: " + msg, file = sys.stderr, flush = True )
    sys.exit( 1 )


def run( cmd, **kw ):
    log( " ".join( '"%s"' % c if " " in c else c for c in cmd ) )
    r = subprocess.run( cmd, **kw )
    if r.returncode != 0:
        fail( "lệnh thất bại (mã %d)" % r.returncode )


# --------------------------------------------------------------------------------------------
# Chuẩn bị: cglm, phần Lua nhúng, zig
# --------------------------------------------------------------------------------------------

def ensure_box3d( args ):
    """Mã nguồn Box3D ở third_party/box3d, đúng commit đã kiểm thử."""
    global BOX3D_DIR
    if args.box3d:
        BOX3D_DIR = os.path.abspath( args.box3d )
    if os.path.isfile( os.path.join( BOX3D_DIR, "include", "box3d", "box3d.h" ) ):
        return
    if args.box3d:
        fail( "không thấy include/box3d/box3d.h trong " + BOX3D_DIR )
    os.makedirs( os.path.dirname( BOX3D_DIR ), exist_ok = True )
    if shutil.which( "git" ):
        log( "tải Box3D %s bằng git" % BOX3D_COMMIT[:10] )
        ok = subprocess.run( [ "git", "-c", "advice.detachedHead=false", "clone", "-q", BOX3D_REPO, BOX3D_DIR ] ).returncode == 0
        ok = ok and subprocess.run( [ "git", "-c", "advice.detachedHead=false", "-C", BOX3D_DIR, "checkout", "-q", BOX3D_COMMIT ] ).returncode == 0
        if ok:
            return
        shutil.rmtree( BOX3D_DIR, ignore_errors = True )
    log( "tải Box3D dạng zip" )
    url = "https://codeload.github.com/erincatto/box3d/zip/" + BOX3D_COMMIT
    try:
        data = urllib.request.urlopen( url, timeout = 120 ).read()
    except Exception as e:
        fail( "không tải được Box3D (%s). Tự clone %s vào %s" % ( e, BOX3D_REPO, BOX3D_DIR ) )
    with zipfile.ZipFile( io.BytesIO( data ) ) as z:
        top = z.namelist()[0].split( "/" )[0]
        tmp = BOX3D_DIR + "_tmp"
        z.extractall( tmp )
        shutil.move( os.path.join( tmp, top ), BOX3D_DIR )
        shutil.rmtree( tmp, ignore_errors = True )


def box3d_sources():
    return sorted( glob.glob( os.path.join( BOX3D_DIR, "src", "*.c" ) ) )


def find_lua_include( arg ):
    cands = [ arg ] if arg else []
    env = os.environ.get( "LUA_INCLUDE" )
    if env:
        cands.append( env )
    # plugin nằm trong <repo>/test-projects/box3d-plugin -> <repo>/external/lua-5.1.3/src
    cands.append( os.path.join( ROOT, "..", "..", "external", "lua-5.1.3", "src" ) )
    cands.append( os.path.join( ROOT, "third_party", "lua-5.1.3", "src" ) )
    for c in cands:
        if c and os.path.isfile( os.path.join( c, "lua.h" ) ) and os.path.isfile( os.path.join( c, "lauxlib.h" ) ):
            return os.path.abspath( c )
    fail( "không tìm thấy lua.h (Lua 5.1). Dùng --lua-include <thư mục external/lua-5.1.3/src của Solar2D>" )


def zig_cmd():
    try:
        import ziglang  # noqa: F401
    except ImportError:
        if shutil.which( "zig" ):
            return [ "zig" ]
        log( "cài zig: pip install ziglang" )
        r = subprocess.run( [ sys.executable, "-m", "pip", "install", "--user", "ziglang" ] )
        if r.returncode != 0:
            r = subprocess.run( [ sys.executable, "-m", "pip", "install", "--break-system-packages", "ziglang" ] )
        if r.returncode != 0:
            fail( "không cài được ziglang. Chạy: %s -m pip install ziglang" % sys.executable )
    return [ sys.executable, "-m", "ziglang" ]


def prepare( args ):
    ensure_box3d( args )
    return find_lua_include( args.lua_include )


def common_flags( lua_inc ):
    # NDEBUG cho cả Box3D lẫn binding: header Box3D gọi b3InternalAssert khi thiếu NDEBUG
    return [ "-O2", "-std=gnu17", "-DNDEBUG", "-I", os.path.join( BOX3D_DIR, "include" ), "-I", os.path.join( BOX3D_DIR, "src" ),
             "-I", lua_inc, "-I", SRC ]


def compile_all( cc, flags, obj_dir, jobs ):
    """Biên dịch Box3D + binding thành file .o (song song). Trả về danh sách .o"""
    os.makedirs( obj_dir, exist_ok = True )
    srcs = box3d_sources() + [ os.path.join( SRC, "box3d_plugin.c" ) ]
    outs = []

    def one( src ):
        obj = os.path.join( obj_dir, os.path.splitext( os.path.basename( src ) )[0] + ".o" )
        if os.path.isfile( obj ) and os.path.getmtime( obj ) >= os.path.getmtime( src ):
            return obj, None
        r = subprocess.run( cc + flags + [ "-c", "-o", obj, src ], stdout = subprocess.PIPE, stderr = subprocess.STDOUT, text = True )
        return obj, ( None if r.returncode == 0 else r.stdout )

    log( "biên dịch %d file (%s)" % ( len( srcs ), os.path.basename( obj_dir ) ) )
    with concurrent.futures.ThreadPoolExecutor( max_workers = args_jobs( jobs ) ) as ex:
        for obj, err in ex.map( one, srcs ):
            if err:
                fail( "biên dịch lỗi %s:\n%s" % ( obj, err ) )
            outs.append( obj )
    return outs


def args_jobs( jobs ):
    return jobs or max( 1, min( 8, os.cpu_count() or 2 ) )


# --------------------------------------------------------------------------------------------
# Windows
# --------------------------------------------------------------------------------------------

def build_win( args ):
    lua_inc = prepare( args )
    zig = zig_cmd()
    out_dir = os.path.join( BUILD, "win32" )
    os.makedirs( out_dir, exist_ok = True )
    # Simulator Windows là 32-bit: DLL bắt buộc x86
    objs = compile_all( zig + [ "cc", "-target", "x86-windows-gnu" ], common_flags( lua_inc ), os.path.join( BUILD, "obj", "win32" ), args.jobs )
    # import library cho lua.dll (hàm Lua cdecl, tên không trang trí)
    deff = os.path.join( out_dir, "lua.def" )
    with open( deff, "w" ) as f:
        f.write( "LIBRARY lua.dll\nEXPORTS\n" + "".join( "    %s\n" % n for n in LUA_EXPORTS ) )
    implib = os.path.join( out_dir, "liblua_dll.a" )
    run( zig + [ "dlltool", "-m", "i386", "-d", deff, "-l", implib, "-D", "lua.dll" ] )
    out = os.path.join( out_dir, "plugin.box3d.dll" )
    run( zig + [ "cc", "-target", "x86-windows-gnu", "-shared", "-o", out ] + objs + [ implib, "-Wl,--strip-all" ] )
    for junk in glob.glob( os.path.join( out_dir, "*.lib" ) ) + glob.glob( os.path.join( out_dir, "*.pdb" ) ):
        os.remove( junk )
    check_win_dll( out )
    log( "-> " + out )
    return out


def check_win_dll( path ):
    """DLL phải export luaopen_plugin_box3d và chỉ nhập hàm Lua từ lua.dll."""
    with open( path, "rb" ) as f:
        data = f.read()
    for needle in ( LUAOPEN.encode(), b"lua.dll" ):
        if needle not in data:
            fail( "%s thiếu %s" % ( path, needle.decode() ) )


# --------------------------------------------------------------------------------------------
# Android
# --------------------------------------------------------------------------------------------

def find_ndk( arg ):
    cands = [ arg ] if arg else []
    for k in ( "ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK" ):
        if os.environ.get( k ):
            cands.append( os.environ[k] )
    # NDK tải riêng, giải nén ở gốc ổ đĩa hoặc thư mục người dùng: D:\android-ndk-r28c, C:\android-ndk-r27...
    roots = [ os.path.expanduser( "~" ) ]
    if os.name == "nt":
        roots += [ "%s:\\" % d for d in "CDEFG" ]
        roots += [ os.path.join( "%s:\\" % d, "Android" ) for d in "CDE" ]
    else:
        roots += [ "/opt", "/usr/local" ]
    found = []
    for r in roots:
        found += glob.glob( os.path.join( r, "android-ndk-*" ) )
    cands += sorted( found, key = ndk_version_key, reverse = True )
    sdks = [ os.environ.get( k ) for k in ( "ANDROID_HOME", "ANDROID_SDK_ROOT" ) ]
    if os.environ.get( "LOCALAPPDATA" ):
        sdks.append( os.path.join( os.environ["LOCALAPPDATA"], "Android", "Sdk" ) )
    sdks.append( os.path.expanduser( "~/Android/Sdk" ) )
    sdks.append( os.path.expanduser( "~/Library/Android/sdk" ) )
    for sdk in sdks:
        if sdk and os.path.isdir( os.path.join( sdk, "ndk" ) ):
            vers = sorted( os.listdir( os.path.join( sdk, "ndk" ) ), key = lambda v: [ int( x ) if x.isdigit() else 0 for x in v.split( "." ) ] )
            cands += [ os.path.join( sdk, "ndk", v ) for v in reversed( vers ) ]
        if sdk and os.path.isdir( os.path.join( sdk, "ndk-bundle" ) ):
            cands.append( os.path.join( sdk, "ndk-bundle" ) )
    for c in cands:
        if c and ndk_clang( c ):
            return c
    return None


def ndk_version_key( path ):
    """android-ndk-r28c -> (28, 3); thư mục SDK ndk/28.2.13676358 -> (28, 2, ...)"""
    import re
    name = os.path.basename( path.rstrip( "\\/" ) )
    m = re.search( r"r(\d+)([a-z]?)", name )
    if m:
        return ( int( m.group( 1 ) ), ( ord( m.group( 2 ) ) - 96 ) if m.group( 2 ) else 0 )
    return tuple( int( x ) for x in re.findall( r"\d+", name ) ) or ( 0, )


def ndk_clang( ndk ):
    pre = os.path.join( ndk, "toolchains", "llvm", "prebuilt" )
    if not os.path.isdir( pre ):
        return None
    for host in os.listdir( pre ):
        for name in ( "clang.exe", "clang" ):
            p = os.path.join( pre, host, "bin", name )
            if os.path.isfile( p ):
                return p
    return None


def make_stubs( compile_so, stub_dir, names ):
    """Thư viện rỗng chỉ mang tên (soname): giúp plugin có DT_NEEDED đúng mà không cần lib thật."""
    os.makedirs( stub_dir, exist_ok = True )
    empty = os.path.join( stub_dir, "empty.c" )
    with open( empty, "w" ) as f:
        f.write( "/* stub */\n" )
    for lib in names:
        compile_so( empty, os.path.join( stub_dir, lib ), lib )
    return stub_dir


def build_android( args ):
    lua_inc = prepare( args )
    abis = [ a.strip() for a in args.abis.split( "," ) if a.strip() ]
    for a in abis:
        if a not in ANDROID_ABIS:
            fail( "ABI không hỗ trợ: %s (có: %s)" % ( a, ", ".join( ANDROID_ABIS ) ) )

    tool = args.android_tool
    ndk = None
    if tool in ( "auto", "ndk" ):
        ndk = find_ndk( args.ndk )
        if not ndk and tool == "ndk":
            fail( "không tìm thấy Android NDK. Cài qua Android Studio (SDK Manager > NDK) hoặc chỉ --ndk <thư mục>" )
    if ndk:
        clang = ndk_clang( ndk )
        log( "dùng NDK: " + ndk )
    else:
        zig = zig_cmd()
        log( "không có NDK, dùng zig (libc musl, chỉ dùng các hàm có ABI giống Bionic)" )

    out_root = os.path.join( BUILD, "android", "jniLibs" )
    outs = []
    for abi in abis:
        zig_target, zig_extra, ndk_target = ANDROID_ABIS[abi]
        if ndk:
            base = [ clang, "--target=" + ndk_target, "-fPIC" ]
            cflags = base + [ "-D__ANDROID__" ]
        else:
            base = zig + [ "cc", "-target", zig_target ] + zig_extra + [ "-fPIC" ]
            cflags = base + [ "-D__ANDROID__", "-fno-stack-protector", "-U_FORTIFY_SOURCE" ]

        stub_dir = os.path.join( BUILD, "android", "stubs", abi )

        def compile_so( src, out, soname, base = base ):
            cmd = base + [ "-shared", "-nostdlib", "-o", out, src, "-Wl,-soname," + soname ]
            r = subprocess.run( cmd, stdout = subprocess.DEVNULL, stderr = subprocess.PIPE, text = True )
            if r.returncode != 0:
                fail( "không tạo được stub %s:\n%s" % ( soname, r.stderr ) )

        make_stubs( compile_so, stub_dir, [ "liblua.so" ] if ndk else ANDROID_NEEDED )

        obj_dir = os.path.join( BUILD, "obj", ( "ndk-" if ndk else "zig-" ) + abi )
        objs = compile_all( cflags, common_flags( lua_inc ) + [ "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections" ], obj_dir, args.jobs )
        if not ndk and abi in ( "armeabi-v7a", "x86" ):
            # musl 32-bit gọi __clock_gettime64 (time64), Bionic không có: thêm hàm chuyển tiếp
            compat = os.path.join( obj_dir, "android_compat_arm32.o" )
            run( base + [ "-O2", "-c", "-o", compat, os.path.join( SRC, "android_compat_arm32.c" ) ] )
            objs.append( compat )

        out_dir = os.path.join( out_root, abi )
        os.makedirs( out_dir, exist_ok = True )
        out = os.path.join( out_dir, "libplugin.box3d.so" )
        if ndk:
            # NDK có sysroot thật: chỉ cần stub cho thư viện của Solar2D, còn libc/libm/GLESv2/log lấy từ NDK
            # (không dùng -nostdlib để còn builtins của clang: ARMv7 cần __aeabi_idiv, __aeabi_d2f...)
            libs = [ os.path.join( stub_dir, "liblua.so" ), "-lm", "-ldl" ]
            nostd = []
        else:
            libs = [ os.path.join( stub_dir, l ) for l in ANDROID_NEEDED ]
            nostd = [ "-nostdlib" ]
        # chỉ export luaopen_plugin_box3d: Solar2D nạp plugin với RTLD_GLOBAL, không được lộ hàm b3* hay memcpy... của runtime
        vscript = os.path.join( obj_dir, "exports.map" )
        with open( vscript, "w" ) as f:
            f.write( "{\n  global:\n    %s;\n  local:\n    *;\n};\n" % LUAOPEN )
        link = base + [ "-shared" ] + nostd + [ "-o", out ] + objs + libs + [
            "-Wl,-soname,libplugin.box3d.so",
            "-Wl,--no-undefined-version",
            "-Wl,--hash-style=both",            # Android < 6 cần DT_HASH
            "-Wl,-z,max-page-size=16384",       # Android 15+: trang 16 KB
            "-Wl,-z,noexecstack",
            "-Wl,--build-id",
            "-Wl,--version-script," + vscript,
            "-Wl,--gc-sections",
            "-Wl,--strip-all",
        ]
        if not ndk:
            link.insert( len( base ) + 1, "-Wl,--no-as-needed" )  # giữ DT_NEEDED của stub rỗng
        run( link )
        check_android_so( out, abi )
        outs.append( out )
        log( "-> " + out )
    return outs


def check_android_so( path, abi ):
    """Kiểm tra nhanh file ELF: đúng kiến trúc, có luaopen_plugin_box3d, có DT_NEEDED liblua."""
    with open( path, "rb" ) as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        fail( path + " không phải ELF" )
    machine = int.from_bytes( data[18:20], "little" )
    want = { "arm64-v8a": 183, "armeabi-v7a": 40, "x86_64": 62, "x86": 3 }[abi]
    if machine != want:
        fail( "%s: e_machine %d, cần %d" % ( path, machine, want ) )
    for needle in ( LUAOPEN.encode(), b"liblua.so" ):
        if needle not in data:
            fail( "%s thiếu %s" % ( path, needle.decode() ) )
    if abi in ( "armeabi-v7a", "x86" ) and b"__clock_gettime64" in data and elf_imports_symbol( data, "__clock_gettime64" ):
        fail( path + " cần __clock_gettime64 (musl time64) mà Bionic không có" )
    exported = elf_exports( data )
    if exported is not None and exported != [ LUAOPEN ]:
        fail( "%s export thừa symbol: %s" % ( path, ", ".join( exported[:10] ) ) )


def elf_imports_symbol( data, name ):
    """True nếu .dynsym có symbol `name` chưa định nghĩa (tức cần lấy từ thư viện khác)."""
    import struct
    try:
        is64 = data[4] == 2
        shoff = struct.unpack_from( "<Q" if is64 else "<I", data, 0x28 if is64 else 0x20 )[0]
        shentsize, shnum = struct.unpack_from( "<HH", data, 0x3A if is64 else 0x2E )
        secs = []
        for i in range( shnum ):
            o = shoff + i * shentsize
            f = struct.unpack_from( "<IIQQQQIIQQ" if is64 else "<IIIIIIIIII", data, o )
            secs.append( ( f[1], f[4], f[5], f[6], f[9] ) )
        for typ, off, size, link, entsize in secs:
            if typ != 11:
                continue
            stroff = secs[link][1]
            for k in range( 1, size // entsize ):
                e = off + k * entsize
                if is64:
                    nm, _, _, shndx = struct.unpack_from( "<IBBH", data, e )
                else:
                    nm, _, _, _, _, shndx = struct.unpack_from( "<IIIBBH", data, e )
                end = data.index( b"\0", stroff + nm )
                if shndx == 0 and data[stroff + nm:end].decode() == name:
                    return True
        return False
    except Exception:
        return False


def elf_exports( data ):
    """Danh sách symbol toàn cục đã định nghĩa trong .dynsym (None nếu không đọc được)."""
    import struct
    try:
        is64 = data[4] == 2
        if is64:
            shoff = struct.unpack_from( "<Q", data, 0x28 )[0]
            shentsize, shnum = struct.unpack_from( "<HH", data, 0x3A )
        else:
            shoff = struct.unpack_from( "<I", data, 0x20 )[0]
            shentsize, shnum = struct.unpack_from( "<HH", data, 0x2E )
        secs = []
        for i in range( shnum ):
            o = shoff + i * shentsize
            if is64:
                _, typ, _, _, off, size, link, _, _, entsize = struct.unpack_from( "<IIQQQQIIQQ", data, o )
            else:
                _, typ, _, _, off, size, link, _, _, entsize = struct.unpack_from( "<IIIIIIIIII", data, o )
            secs.append( ( typ, off, size, link, entsize ) )
        out = []
        for typ, off, size, link, entsize in secs:
            if typ != 11:  # SHT_DYNSYM
                continue
            stroff = secs[link][1]
            for k in range( 1, size // entsize ):
                e = off + k * entsize
                if is64:
                    name, info, _, shndx = struct.unpack_from( "<IBBH", data, e )
                else:
                    name, _, _, info, _, shndx = struct.unpack_from( "<IIIBBH", data, e )
                if shndx != 0 and ( info >> 4 ) in ( 1, 2 ):  # đã định nghĩa, GLOBAL/WEAK
                    end = data.index( b"\0", stroff + name )
                    out.append( data[stroff + name:end].decode() )
        return sorted( out )
    except Exception:
        return None


# --------------------------------------------------------------------------------------------
# Đóng gói / cài đặt
# --------------------------------------------------------------------------------------------

def tgz( out, entries ):
    """entries: list (đường dẫn trong archive, file nguồn hoặc bytes)"""
    os.makedirs( os.path.dirname( out ), exist_ok = True )
    with tarfile.open( out, "w:gz", format = tarfile.USTAR_FORMAT ) as t:
        for name, src in entries:
            if isinstance( src, bytes ):
                info = tarfile.TarInfo( name )
                info.size = len( src )
                info.mode = 0o644
                info.mtime = int( time.time() )
                t.addfile( info, io.BytesIO( src ) )
            else:
                info = t.gettarinfo( src, arcname = name )
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mode = 0o755 if name.endswith( ( ".so", ".dll" ) ) else 0o644
                with open( src, "rb" ) as f:
                    t.addfile( info, f )
    log( "-> " + out )


def package( args ):
    base = os.path.join( DIST, "Solar2DPlugins", args.publisher, PLUGIN )
    made = 0
    dll = os.path.join( BUILD, "win32", "plugin.box3d.dll" )
    if os.path.isfile( dll ):
        # Simulator giải nén vào thư mục Plugins; require "plugin.box3d" tìm <Plugins>\plugin\box3d.dll
        entries = [ ( "plugin/box3d.dll", dll ) ]
        tgz( os.path.join( base, "win32-sim", "data.tgz" ), entries )
        tgz( os.path.join( DIST, PLUGIN + "-win32.tgz" ), entries )
        made += 1
    else:
        log( "bỏ qua win32-sim (chưa build: python tools/build.py win)" )
    jni = os.path.join( BUILD, "android", "jniLibs" )
    sos = sorted( glob.glob( os.path.join( jni, "*", "libplugin.box3d.so" ) ) )
    if sos:
        entries = [ ( "jniLibs/%s/libplugin.box3d.so" % os.path.basename( os.path.dirname( so ) ), so ) for so in sos ]
        entries.append( ( "metadata.lua", METADATA_LUA.encode( "utf-8" ) ) )
        tgz( os.path.join( base, "android", "data.tgz" ), entries )
        tgz( os.path.join( DIST, PLUGIN + "-android.tgz" ), entries )
        made += 1
    else:
        log( "bỏ qua android (chưa build: python tools/build.py android)" )
    if not made:
        fail( "chưa có gì để đóng gói" )
    return base


def package_after_build( args ):
    """Sau khi build win / android: đóng gói luôn những gì đã có trong build/."""
    log( "đóng gói" )
    package( args )


def plugin_storages():
    """Solar2D (CoronaBuilderPluginCollector.lua) tìm ở os.getenv("HOME") or os.getenv("APPDATA") .. "/Solar2DPlugins".
    Trên Windows, Simulator thường không có HOME nên dùng APPDATA; nhưng Git Bash / MSYS lại đặt HOME cho script,
    nên cài vào cả hai chỗ cho chắc."""
    out = []
    if os.name == "nt" and os.environ.get( "APPDATA" ):
        out.append( os.path.join( os.environ["APPDATA"], "Solar2DPlugins" ) )
    home = os.environ.get( "HOME" )
    if home:
        out.append( os.path.join( home, "Solar2DPlugins" ) )
    if not out:
        out.append( os.path.join( os.path.expanduser( "~" ), "Solar2DPlugins" ) )
    uniq = []
    for d in out:
        if os.path.normcase( os.path.abspath( d ) ) not in [ os.path.normcase( os.path.abspath( u ) ) for u in uniq ]:
            uniq.append( d )
    return uniq


def install( args ):
    src = os.path.join( DIST, "Solar2DPlugins", args.publisher, PLUGIN )
    if not os.path.isdir( src ):
        fail( "chưa đóng gói: python tools/build.py package" )
    for storage in ( [ args.storage ] if args.storage else plugin_storages() ):
        dst = os.path.join( storage, args.publisher, PLUGIN )
        for plat in os.listdir( src ):
            os.makedirs( os.path.join( dst, plat ), exist_ok = True )
            shutil.copy2( os.path.join( src, plat, "data.tgz" ), os.path.join( dst, plat, "data.tgz" ) )
            log( "cài %s -> %s" % ( plat, os.path.join( dst, plat, "data.tgz" ) ) )
    # Simulator giữ bản đã giải nén trong thư mục Plugins của nó: xoá để lần chạy sau lấy bản mới
    if os.name == "nt" and os.environ.get( "APPDATA" ):
        cached = os.path.join( os.environ["APPDATA"], "Corona Labs", "Corona Simulator", "Plugins", "plugin", "box3d.dll" )
        if os.path.isfile( cached ):
            try:
                os.remove( cached )
                log( "đã xoá bản cũ Simulator đang giữ: " + cached )
            except OSError:
                log( "CHÚ Ý: không xoá được %s (Simulator đang mở?). Đóng Simulator rồi chạy lại install." % cached )
    log( "Thêm vào build.settings của project:" )
    print( """
settings = {
    plugins = {
        ["plugin.box3d"] = { publisherId = "%s" },
    },
}
""" % args.publisher )


def test( args ):
    """Build bản máy host (Linux/macOS: gcc/clang, Windows: zig x86_64) kèm trình thông dịch Lua 5.1 rồi chạy test."""
    if os.name == "nt":
        fail( "test trên Windows chưa hỗ trợ: chạy trong WSL / Linux, hoặc mở cảnh \"Tests\" trong Box3DDemo" )
    lua_inc = prepare( args )
    host = os.path.join( BUILD, "host" )
    cc = [ os.environ.get( "CC" ) or shutil.which( "cc" ) or shutil.which( "gcc" ) or shutil.which( "clang" ) or "" ]
    if not cc[0]:
        cc = zig_cmd() + [ "cc" ]
    objs = compile_all( cc, common_flags( lua_inc ) + [ "-fPIC" ], os.path.join( BUILD, "obj", "host" ), args.jobs )
    os.makedirs( os.path.join( host, "plugin" ), exist_ok = True )
    ext = ".dll" if os.name == "nt" else ".so"
    lua_srcs = [ p for p in glob.glob( os.path.join( lua_inc, "*.c" ) )
                 if os.path.basename( p ) not in ( "luac.c", "print.c", "noparser.c", "luaconf.c" ) ]
    exe = os.path.join( host, "lua" + ( ".exe" if os.name == "nt" else "" ) )
    run( cc + [ "-O2", "-w", "-DLUA_USE_POSIX", "-DLUA_USE_DLOPEN", "-o", exe ] + lua_srcs + [ "-lm", "-ldl", "-Wl,-E" ] )
    run( cc + [ "-shared", "-o", os.path.join( host, "plugin", "box3d" + ext ) ] + objs + [ "-lm", "-lpthread" ] )
    tests = os.path.join( host, "tests" )
    shutil.rmtree( tests, ignore_errors = True )
    shutil.copytree( os.path.join( ROOT, "tests" ), tests )
    r = subprocess.run( [ exe, os.path.join( "tests", "run_tests.lua" ) ], cwd = host )
    if r.returncode != 0:
        fail( "có test lỗi" )


def clean( args ):
    for d in ( BUILD, DIST ):
        shutil.rmtree( d, ignore_errors = True )
    log( "đã xoá build/ và dist/" )


def main():
    p = argparse.ArgumentParser( description = "Build plugin.box3d" )
    p.add_argument( "command", nargs = "?", default = "all", choices = [ "all", "win", "android", "package", "install", "test", "clean" ] )
    p.add_argument( "--box3d", help = "mã nguồn Box3D có sẵn (mặc định tải về third_party/box3d)" )
    p.add_argument( "--jobs", type = int, default = 0 )
    p.add_argument( "--lua-include" )
    p.add_argument( "--publisher", default = "com.s3d" )
    p.add_argument( "--abis", default = "arm64-v8a,armeabi-v7a,x86_64" )
    p.add_argument( "--android-tool", default = "auto", choices = [ "auto", "ndk", "zig" ] )
    p.add_argument( "--ndk" )
    p.add_argument( "--storage", help = "thư mục Solar2DPlugins (mặc định theo HOME/APPDATA)" )
    args = p.parse_args()

    if args.command == "win":
        build_win( args )
        package_after_build( args )
    elif args.command == "android":
        build_android( args )
        package_after_build( args )
    elif args.command == "package":
        package( args )
    elif args.command == "install":
        install( args )
    elif args.command == "test":
        test( args )
    elif args.command == "clean":
        clean( args )
    else:
        build_win( args )
        build_android( args )
        package( args )
        install( args )


if __name__ == "__main__":
    main()
