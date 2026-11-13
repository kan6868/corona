#!/usr/bin/env python3
"""
Builds plugin.s3d for Windows (Simulator) and Android, packages it and installs it into Solar2D's local plugin folder.

    python tools/build.py all            # win + android + package + install
    python tools/build.py win            # build/win32/plugin.s3d.dll
    python tools/build.py android        # build/android/jniLibs/<abi>/libplugin.s3d.so
    python tools/build.py package        # dist/plugin.s3d-win32.tgz, dist/plugin.s3d-android.tgz
                                         # + dist/Solar2DPlugins/<publisherId>/plugin.s3d/<platform>/data.tgz
                                         # (win / android / all package automatically after building)
    python tools/build.py install        # copies dist into %APPDATA%\\Solar2DPlugins (or $HOME/Solar2DPlugins)
    python tools/build.py clean

Common options:
    --lua-include DIR    folder with lua.h (Lua 5.1). Default: <Solar2D repo>/external/lua-5.1.3/src
    --publisher ID       publisherId used in build.settings (default com.s3d)
    --abis LIST          comma-separated Android ABIs (default arm64-v8a,armeabi-v7a,x86_64)
    --android-tool T     auto | ndk | zig   (auto: the NDK when found, otherwise zig)
    --ndk DIR            Android NDK path (default: ANDROID_NDK_HOME, ANDROID_NDK_ROOT, <SDK>/ndk/<newest>)

Only Python 3.8+ and git (to fetch cglm) are required. zig is installed with "pip install ziglang" when missing.
No Solar2D libraries are needed: the plugin only records liblua.so / libcorona.so in DT_NEEDED and
the real functions come from the Solar2D libraries already loaded at run time (Windows: looked up in "Corona Simulator.exe" at run time).
"""

import argparse
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

# Windows consoles (cp1252/cp437) cannot print every character: switch to UTF-8 and replace what cannot be shown
for _stream in ( sys.stdout, sys.stderr ):
    try:
        _stream.reconfigure( encoding = "utf-8", errors = "replace" )
    except Exception:
        pass

ROOT = os.path.dirname( os.path.dirname( os.path.abspath( __file__ ) ) )
SRC = os.path.join( ROOT, "src" )
BUILD = os.path.join( ROOT, "build" )
DIST = os.path.join( ROOT, "dist" )
CGLM_DIR = os.path.join( ROOT, "third_party", "cglm" )
CGLM_TAG = "v0.9.4"
PLUGIN = "plugin.s3d"

# Android ABI -> (zig target, extra zig flags, NDK clang target)
ANDROID_ABIS = {
    "arm64-v8a":   ( "aarch64-linux-musl", [], "aarch64-linux-android21" ),
    "armeabi-v7a": ( "arm-linux-musleabi", [ "-mcpu=cortex_a9", "-mfloat-abi=softfp", "-mthumb" ], "armv7a-linux-androideabi21" ),
    "x86_64":      ( "x86_64-linux-musl", [], "x86_64-linux-android21" ),
    "x86":         ( "x86-linux-musl", [], "i686-linux-android21" ),
}
# Libraries the plugin needs at run time (already loaded by Solar2D / Android)
ANDROID_NEEDED = [ "liblua.so", "libcorona.so", "libGLESv2.so", "liblog.so", "libm.so", "libdl.so", "libc.so" ]

METADATA_LUA = """local metadata =
{
    plugin =
    {
        format = "sharedLibrary",
        manifest =
        {
            -- Solar2D for Android only creates a depth buffer when this meta-data is present (plugin s3d requires it)
            applicationChildElements =
            {
                [[<meta-data android:name="wantsDepthBuffer" android:value="true" />]],
            },
        },
    },
}

return metadata
"""


def log( *a ):
    print( "[s3d]", *a, flush = True )


def fail( msg ):
    print( "[s3d] ERROR: " + msg, file = sys.stderr, flush = True )
    sys.exit( 1 )


def run( cmd, **kw ):
    log( " ".join( '"%s"' % c if " " in c else c for c in cmd ) )
    r = subprocess.run( cmd, **kw )
    if r.returncode != 0:
        fail( "command failed (exit code %d)" % r.returncode )


# --------------------------------------------------------------------------------------------
# Setup: cglm, the embedded Lua, zig
# --------------------------------------------------------------------------------------------

def ensure_cglm():
    if os.path.isfile( os.path.join( CGLM_DIR, "include", "cglm", "cglm.h" ) ):
        return
    os.makedirs( os.path.dirname( CGLM_DIR ), exist_ok = True )
    if shutil.which( "git" ):
        log( "fetching cglm %s with git" % CGLM_TAG )
        r = subprocess.run( [ "git", "-c", "advice.detachedHead=false", "clone", "-q", "--depth", "1", "--branch", CGLM_TAG, "https://github.com/recp/cglm.git", CGLM_DIR ] )
        if r.returncode == 0:
            return
        shutil.rmtree( CGLM_DIR, ignore_errors = True )
    log( "fetching cglm %s as a zip" % CGLM_TAG )
    url = "https://codeload.github.com/recp/cglm/zip/refs/tags/" + CGLM_TAG
    try:
        data = urllib.request.urlopen( url, timeout = 60 ).read()
    except Exception as e:
        fail( "could not download cglm (%s). Extract cglm %s into %s yourself" % ( e, CGLM_TAG, CGLM_DIR ) )
    with zipfile.ZipFile( io.BytesIO( data ) ) as z:
        top = z.namelist()[0].split( "/" )[0]
        tmp = CGLM_DIR + "_tmp"
        z.extractall( tmp )
        shutil.move( os.path.join( tmp, top ), CGLM_DIR )
        shutil.rmtree( tmp, ignore_errors = True )


def embed_lua():
    run( [ sys.executable, os.path.join( ROOT, "tools", "embed_lua.py" ), os.path.join( SRC, "s3d.lua" ), os.path.join( SRC, "s3d_lua.h" ) ] )


def find_lua_include( arg ):
    cands = [ arg ] if arg else []
    env = os.environ.get( "LUA_INCLUDE" )
    if env:
        cands.append( env )
    # the plugin lives in <repo>/test-projects/s3d-plugin -> <repo>/external/lua-5.1.3/src
    cands.append( os.path.join( ROOT, "..", "..", "external", "lua-5.1.3", "src" ) )
    cands.append( os.path.join( ROOT, "third_party", "lua-5.1.3", "src" ) )
    for c in cands:
        if c and os.path.isfile( os.path.join( c, "lua.h" ) ) and os.path.isfile( os.path.join( c, "lauxlib.h" ) ):
            return os.path.abspath( c )
    fail( "lua.h (Lua 5.1) not found. Use --lua-include <Solar2D's external/lua-5.1.3/src folder>" )


def zig_cmd():
    try:
        import ziglang  # noqa: F401
    except ImportError:
        if shutil.which( "zig" ):
            return [ "zig" ]
        log( "installing zig: pip install ziglang" )
        r = subprocess.run( [ sys.executable, "-m", "pip", "install", "--user", "ziglang" ] )
        if r.returncode != 0:
            r = subprocess.run( [ sys.executable, "-m", "pip", "install", "--break-system-packages", "ziglang" ] )
        if r.returncode != 0:
            fail( "could not install ziglang. Run: %s -m pip install ziglang" % sys.executable )
    return [ sys.executable, "-m", "ziglang" ]


def prepare( args ):
    ensure_cglm()
    embed_lua()
    return find_lua_include( args.lua_include )


def common_flags( lua_inc ):
    return [ "-O2", "-std=c99", "-I", os.path.join( CGLM_DIR, "include" ), "-I", lua_inc, "-I", SRC ]


# --------------------------------------------------------------------------------------------
# Windows
# --------------------------------------------------------------------------------------------

def build_win( args ):
    lua_inc = prepare( args )
    out_dir = os.path.join( BUILD, "win32" )
    os.makedirs( out_dir, exist_ok = True )
    out = os.path.join( out_dir, "plugin.s3d.dll" )
    # the Windows Simulator is 32-bit: the DLL must be x86
    run( zig_cmd() + [ "cc", "-target", "x86-windows-gnu", "-shared", "-DS3D_DYNAMIC" ] + common_flags( lua_inc )
         + [ "-Wl,--strip-all", "-o", out, os.path.join( SRC, "s3d.c" ) ] )
    for junk in glob.glob( os.path.join( out_dir, "*.lib" ) ) + glob.glob( os.path.join( out_dir, "*.pdb" ) ):
        os.remove( junk )
    log( "-> " + out )
    return out


# --------------------------------------------------------------------------------------------
# Android
# --------------------------------------------------------------------------------------------

def find_ndk( arg ):
    cands = [ arg ] if arg else []
    for k in ( "ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK" ):
        if os.environ.get( k ):
            cands.append( os.environ[k] )
    # standalone NDK downloads are often extracted at a drive root or in the user folder: D:\android-ndk-r28c, C:\android-ndk-r27...
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
    """android-ndk-r28c -> (28, 3); SDK folder ndk/28.2.13676358 -> (28, 2, ...)"""
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
    """Empty library that only carries a name (soname): gives the plugin correct DT_NEEDED entries without the real library."""
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
            fail( "unsupported ABI: %s (available: %s)" % ( a, ", ".join( ANDROID_ABIS ) ) )

    tool = args.android_tool
    ndk = None
    if tool in ( "auto", "ndk" ):
        ndk = find_ndk( args.ndk )
        if not ndk and tool == "ndk":
            fail( "Android NDK not found. Install it with Android Studio (SDK Manager > NDK) or pass --ndk <folder>" )
    if ndk:
        clang = ndk_clang( ndk )
        log( "using NDK: " + ndk )
    else:
        zig = zig_cmd()
        log( "no NDK, using zig (musl libc, only functions whose ABI matches Bionic are used)" )

    out_root = os.path.join( BUILD, "android", "jniLibs" )
    outs = []
    for abi in abis:
        zig_target, zig_extra, ndk_target = ANDROID_ABIS[abi]
        if ndk:
            base = [ clang, "--target=" + ndk_target, "-fPIC" ]
            cflags = base + [ "-D__ANDROID__" ]
        else:
            base = zig + [ "cc", "-target", zig_target ] + zig_extra + [ "-fPIC" ]
            cflags = base + [ "-D__ANDROID__", "-DS3D_OWN_GLES2", "-fno-stack-protector", "-U_FORTIFY_SOURCE" ]

        stub_dir = os.path.join( BUILD, "android", "stubs", abi )

        def compile_so( src, out, soname, base = base ):
            cmd = base + [ "-shared", "-nostdlib", "-o", out, src, "-Wl,-soname," + soname ]
            r = subprocess.run( cmd, stdout = subprocess.DEVNULL, stderr = subprocess.PIPE, text = True )
            if r.returncode != 0:
                fail( "could not create stub %s:\n%s" % ( soname, r.stderr ) )

        make_stubs( compile_so, stub_dir, [ "liblua.so", "libcorona.so" ] if ndk else ANDROID_NEEDED )

        obj_dir = os.path.join( BUILD, "android", "obj", abi )
        os.makedirs( obj_dir, exist_ok = True )
        obj = os.path.join( obj_dir, "s3d.o" )
        run( cflags + common_flags( lua_inc ) + [ "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections", "-c", "-o", obj, os.path.join( SRC, "s3d.c" ) ] )

        out_dir = os.path.join( out_root, abi )
        os.makedirs( out_dir, exist_ok = True )
        out = os.path.join( out_dir, "libplugin.s3d.so" )
        if ndk:
            # the NDK has a real sysroot: stubs are only needed for Solar2D's libraries; libc/libm/GLESv2/log come from the NDK
            # (no -nostdlib so clang's builtins stay available: ARMv7 needs __aeabi_idiv, __aeabi_d2f...)
            libs = [ os.path.join( stub_dir, l ) for l in ( "liblua.so", "libcorona.so" ) ] + [ "-lGLESv2", "-llog", "-lm", "-ldl" ]
            nostd = []
        else:
            libs = [ os.path.join( stub_dir, l ) for l in ANDROID_NEEDED ]
            nostd = [ "-nostdlib" ]
        # export only luaopen_plugin_s3d: Solar2D loads plugins with RTLD_GLOBAL, so the runtime's memcpy, sinf... must not leak
        vscript = os.path.join( obj_dir, "exports.map" )
        with open( vscript, "w" ) as f:
            f.write( "{\n  global:\n    luaopen_plugin_s3d;\n  local:\n    *;\n};\n" )
        link = base + [ "-shared" ] + nostd + [ "-o", out, obj ] + libs + [
            "-Wl,-soname,libplugin.s3d.so",
            "-Wl,--no-undefined-version",
            "-Wl,--hash-style=both",            # Android < 6 needs DT_HASH
            "-Wl,-z,max-page-size=16384",       # Android 15+: trang 16 KB
            "-Wl,-z,noexecstack",
            "-Wl,--build-id",
            "-Wl,--version-script," + vscript,
            "-Wl,--gc-sections",
            "-Wl,--strip-all",
        ]
        if not ndk:
            link.insert( len( base ) + 1, "-Wl,--no-as-needed" )  # keep the DT_NEEDED of the empty stubs
        run( link )
        check_android_so( out, abi )
        outs.append( out )
        log( "-> " + out )
    return outs


def check_android_so( path, abi ):
    """Quick ELF check: right architecture, exports luaopen_plugin_s3d, DT_NEEDED has liblua/libcorona."""
    with open( path, "rb" ) as f:
        data = f.read()
    if data[:4] != b"\x7fELF":
        fail( path + " is not an ELF file" )
    machine = int.from_bytes( data[18:20], "little" )
    want = { "arm64-v8a": 183, "armeabi-v7a": 40, "x86_64": 62, "x86": 3 }[abi]
    if machine != want:
        fail( "%s: e_machine %d, expected %d" % ( path, machine, want ) )
    for needle in ( b"luaopen_plugin_s3d", b"liblua.so", b"libcorona.so", b"libGLESv2.so" ):
        if needle not in data:
            fail( "%s is missing %s" % ( path, needle.decode() ) )
    exported = elf_exports( data )
    if exported is not None and exported != [ "luaopen_plugin_s3d" ]:
        fail( "%s exports extra symbols: %s" % ( path, ", ".join( exported[:10] ) ) )


def elf_exports( data ):
    """Global symbols defined in .dynsym (None if they cannot be read)."""
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
                if shndx != 0 and ( info >> 4 ) in ( 1, 2 ):  # defined, GLOBAL/WEAK
                    end = data.index( b"\0", stroff + name )
                    out.append( data[stroff + name:end].decode() )
        return sorted( out )
    except Exception:
        return None


# --------------------------------------------------------------------------------------------
# Packaging / install
# --------------------------------------------------------------------------------------------

def tgz( out, entries ):
    """entries: list of (path inside the archive, source file or bytes)"""
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
    dll = os.path.join( BUILD, "win32", "plugin.s3d.dll" )
    if os.path.isfile( dll ):
        # the Simulator extracts into its Plugins folder; require "plugin.s3d" looks for <Plugins>\plugin\s3d.dll
        entries = [ ( "plugin/s3d.dll", dll ) ]
        tgz( os.path.join( base, "win32-sim", "data.tgz" ), entries )
        tgz( os.path.join( DIST, PLUGIN + "-win32.tgz" ), entries )
        made += 1
    else:
        log( "skipping win32-sim (not built yet: python tools/build.py win)" )
    jni = os.path.join( BUILD, "android", "jniLibs" )
    sos = sorted( glob.glob( os.path.join( jni, "*", "libplugin.s3d.so" ) ) )
    if sos:
        entries = [ ( "jniLibs/%s/libplugin.s3d.so" % os.path.basename( os.path.dirname( so ) ), so ) for so in sos ]
        entries.append( ( "metadata.lua", METADATA_LUA.encode( "utf-8" ) ) )
        tgz( os.path.join( base, "android", "data.tgz" ), entries )
        tgz( os.path.join( DIST, PLUGIN + "-android.tgz" ), entries )
        made += 1
    else:
        log( "skipping android (not built yet: python tools/build.py android)" )
    if not made:
        fail( "nothing to package" )
    return base


def package_after_build( args ):
    """After building win / android: package whatever is in build/."""
    log( "packaging" )
    package( args )


def plugin_storages():
    """Solar2D (CoronaBuilderPluginCollector.lua) looks in os.getenv("HOME") or os.getenv("APPDATA") .. "/Solar2DPlugins".
    On Windows the Simulator usually has no HOME and uses APPDATA, but Git Bash / MSYS set HOME for scripts,
    so install into both places to be safe."""
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
        fail( "not packaged yet: python tools/build.py package" )
    for storage in ( [ args.storage ] if args.storage else plugin_storages() ):
        dst = os.path.join( storage, args.publisher, PLUGIN )
        for plat in os.listdir( src ):
            os.makedirs( os.path.join( dst, plat ), exist_ok = True )
            shutil.copy2( os.path.join( src, plat, "data.tgz" ), os.path.join( dst, plat, "data.tgz" ) )
            log( "installing %s -> %s" % ( plat, os.path.join( dst, plat, "data.tgz" ) ) )
    # the Simulator keeps an extracted copy in its Plugins folder: delete it so the next run picks up the new build
    if os.name == "nt" and os.environ.get( "APPDATA" ):
        cached = os.path.join( os.environ["APPDATA"], "Corona Labs", "Corona Simulator", "Plugins", "plugin", "s3d.dll" )
        if os.path.isfile( cached ):
            try:
                os.remove( cached )
                log( "removed the old copy the Simulator was holding: " + cached )
            except OSError:
                log( "NOTE: could not delete %s (is the Simulator open?). Close the Simulator and run install again." % cached )
    log( "Add to your project's build.settings:" )
    print( """
settings = {
    plugins = {
        ["plugin.s3d"] = { publisherId = "%s" },
    },
}
""" % args.publisher )


def clean( args ):
    for d in ( BUILD, DIST ):
        shutil.rmtree( d, ignore_errors = True )
    log( "removed build/ and dist/" )


def main():
    p = argparse.ArgumentParser( description = "Build plugin.s3d" )
    p.add_argument( "command", nargs = "?", default = "all", choices = [ "all", "win", "android", "package", "install", "clean" ] )
    p.add_argument( "--lua-include" )
    p.add_argument( "--publisher", default = "com.s3d" )
    p.add_argument( "--abis", default = "arm64-v8a,armeabi-v7a,x86_64" )
    p.add_argument( "--android-tool", default = "auto", choices = [ "auto", "ndk", "zig" ] )
    p.add_argument( "--ndk" )
    p.add_argument( "--storage", help = "Solar2DPlugins folder (default from HOME/APPDATA)" )
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
    elif args.command == "clean":
        clean( args )
    else:
        build_win( args )
        build_android( args )
        package( args )
        install( args )


if __name__ == "__main__":
    main()
