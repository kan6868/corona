# plugin.s3d: simple 3D for Solar2D

`plugin.s3d` adds real 3D rendering to [Solar2D](https://solar2d.com): meshes with depth testing,
per-pixel lighting, shadow maps, fog, post-processing and frame animation from OBJ sequences.
It is a native plugin built only on Solar2D's public native API (`CoronaGraphics.h`), so it works
with a stock Solar2D engine: no engine changes, no custom build.

![version](https://img.shields.io/badge/version-0.5.0-blue)

- **Primitives:** cube, sphere, cylinder, capsule, plane; Wavefront OBJ models.
- **Lighting:** one directional light (Lambert + ambient), per-pixel normals, textures, vertex color tint.
- **Shadows:** shadow map with PCF filtering (low / medium / high), or cheap planar shadows.
- **Fog:** linear, per pixel, by camera depth.
- **Post-processing:** FXAA, exposure, contrast, saturation, gamma, brightness, tint, vignette.
- **Animation:** vertex animation from a sequence of OBJ files, interpolated on the CPU in C, with clips and events.
- **Camera:** perspective camera, orbit helper, world-to-screen projection, screen-to-ray picking.
- **Physics-friendly:** `setPose( x, y, z, qx, qy, qz, qw )` takes a position + quaternion straight from a physics engine.
- **Mixes with 2D:** 3D objects are ordinary display objects; your 2D UI keeps working on top.

## Platforms

| Platform | Status |
| --- | --- |
| Windows Simulator (`win32-sim`) | Supported. Tested on Solar2D 2026.3736, OpenGL 4.6. |
| Android (`android`) | Supported: arm64-v8a, armeabi-v7a, x86_64. Tested on a phone (OpenGL ES 2.0+). |
| macOS Simulator, iOS | Not built yet. The C code is portable; build scripts are welcome. |
| Vulkan backend | Not supported: Solar2D's Vulkan backend has no custom commands, named uniforms or shell transforms yet. 3D objects are hidden and a warning is printed. |

## Quick start

```lua
local s3d = require( "plugin.s3d" )

-- optional: draw the scene through post-processing (FXAA, color grading)
local post = s3d.newPostProcess{ clearColor = { 0.62, 0.76, 0.9 }, fxaa = true }
local scene = post.scene

s3d.setLight{ direction = { -0.5, -1, -0.4 }, ambient = 0.35 }
s3d.setShadowMap{ size = 2048, area = 10 }            -- enable before creating shadow casters
s3d.setFog{ color = { 0.62, 0.76, 0.9 }, start = 10, ["end"] = 40 }

s3d.newPlane( scene, 20, 20, { texture = "checker.png" } )
local box = s3d.newCube( scene, 1, { texture = "crate.png", castShadow = true } )
box:moveTo( 0, 0.5, 0 ):rotateTo( 0, 45, 0 )

s3d.orbit( 30, 25, 8, { 0, 0.5, 0 } )                 -- yaw, pitch, distance, target

Runtime:addEventListener( "enterFrame", function( e )
    box:rotateTo( 0, e.time * 0.05, 0 )
end )
```

`build.settings`:

```lua
settings =
{
    plugins =
    {
        ["plugin.s3d"] = { publisherId = "com.s3d" },
    },
}
```

The full reference is in **[docs/API.md](docs/API.md)**. The `S3DDemo` project shows every feature: shadows, fog,
post-processing, OBJ models, frame animation, a herd of walking animals and touch picking.

## Installing

### From a release

Download `plugin.s3d-win32.tgz` and `plugin.s3d-android.tgz`, host them anywhere, and point `build.settings` at them:

```lua
["plugin.s3d"] =
{
    publisherId = "com.s3d",
    supportedPlatforms =
    {
        android = { url = "https://<your-server>/plugin.s3d-android.tgz" },
        ["win32-sim"] = { url = "https://<your-server>/plugin.s3d-win32.tgz" },
    },
},
```

### From source

```bat
build.bat            :: Windows: build Windows + Android, package, install into the local plugin folder
./build.sh           #  Linux / macOS / Git Bash: same commands
```

After `install`, the short `build.settings` entry from the quick start works in the Simulator and in Android builds
made from the Simulator. See [Building](#building) for details.

## Things to know

- **Do not set `x`, `y`, `rotation`, `xScale`... on 3D objects or on the groups that hold them.** Use `moveTo`,
  `rotateTo`, `scaleTo`. Create 3D objects in a group from `s3d.newScene()` or `post.scene`.
- **Name OBJ files `model.obj.txt`.** Solar2D for Android always strips `*.obj` files from the APK (the exclusion list
  is hard-coded and `build.settings` cannot change it). `s3d.loadOBJ( scene, "model.obj" )` also tries `model.obj.txt`,
  so the same code works everywhere.
- **Android needs a depth buffer.** The Android package adds `<meta-data android:name="wantsDepthBuffer" android:value="true" />`
  to the manifest for you. If you load the plugin some other way, add it in `build.settings`
  (`android.applicationChildElements`).
- **Touch and hit-testing are still 2D.** Use `s3d.screenToRay` + `s3d.rayPlaneY` (or your own ray tests) to pick in 3D.
- **Transparent objects are not depth-sorted.** Insert them last in their group, or turn off depth writes.

## How it works

Each 3D object is a `display.newMesh{ mode = "triangles", hasZ = true }` with an effect from the `filter.s3d.*` family.
The effects carry a custom data type (`s3d`) that stores the object's model matrix, normal matrix and flags in the
effect instance. When Solar2D draws the object, the data type's `before()` callback:

1. installs geometry writers for `position` (vertices multiplied by the model matrix on the CPU) and `userData`
   (transformed normals),
2. enables a custom state block for depth testing and face culling,
3. draws the mesh with `CoronaShaderRawDraw`,
4. issues a render-thread command that writes the camera, light, fog and shadow uniforms,
5. restores the default state so 2D objects drawn afterwards are unaffected.

A custom shell transform (`s3d`) turns `a_Position` into a `vec3` and computes
`gl_Position = u_S3D_ViewProj * vec4( a_Position, 1.0 )`.

Shadow maps are drawn into the plugin's own FBO (RGBA8 with 24-bit packed depth + a depth renderbuffer). For every
group with shadow casters, the plugin inserts a layer at the bottom of the group: a begin marker that switches to the
shadow FBO, depth-only copies of the casters drawn with the light's matrix, and an end marker that restores Solar2D's
FBO and binds the shadow texture to unit 7. Solar2D's snapshots have no depth buffer, so the plugin attaches one
(depth 24 + stencil 8 when available) and clears it once per frame.

## Building

Everything goes through `tools/build.py` (Python 3.8+). `build.bat` (Windows) and `build.sh` (Linux, macOS, Git Bash)
are thin wrappers.

| Command | Result |
| --- | --- |
| `build.bat` | Build Windows + Android, package, install. |
| `build.bat win` | `build/win32/plugin.s3d.dll` (32-bit, for the Windows Simulator), then package. |
| `build.bat android` | `build/android/jniLibs/<abi>/libplugin.s3d.so`, then package. |
| `build.bat package` | Package what has been built. |
| `build.bat install` | Copy the packages to `%APPDATA%\Solar2DPlugins` (and `$HOME/Solar2DPlugins` when `HOME` is set). |
| `build.bat clean` | Remove `build/` and `dist/`. |

Options: `--publisher ID` (default `com.s3d`), `--abis arm64-v8a,armeabi-v7a,x86_64`, `--android-tool auto|ndk|zig`,
`--ndk <NDK folder>`, `--lua-include <folder with lua.h>`, `--storage <Solar2DPlugins folder>`.

Packaging produces:

| File | Contents |
| --- | --- |
| `dist/plugin.s3d-win32.tgz` | `plugin/s3d.dll` (platform `win32-sim`) |
| `dist/plugin.s3d-android.tgz` | `jniLibs/<abi>/libplugin.s3d.so` + `metadata.lua` |
| `dist/Solar2DPlugins/<publisherId>/plugin.s3d/<platform>/data.tgz` | The same, laid out as Solar2D's local plugin folder |

The script prepares everything else itself:

- downloads [cglm](https://github.com/recp/cglm) v0.9.4 into `third_party/cglm` (git, or a zip without git),
- installs [zig](https://ziglang.org) with `pip install ziglang` when it is missing,
- embeds `src/s3d.lua` into the generated `src/s3d_lua.h`,
- looks for Lua 5.1's `lua.h` in `<Solar2D repo>/external/lua-5.1.3/src`; outside the Solar2D repo pass `--lua-include`.

**No Solar2D libraries are needed to build.** On Windows the plugin looks up the Lua, Corona and OpenGL functions in the
running process. On Android it only records `liblua.so`, `libcorona.so`, `libGLESv2.so`... as dependencies (by linking
against empty stub libraries with those names); the real functions come from the libraries the Solar2D app has already
loaded.

### Android: NDK or zig

- With the Android NDK, the script finds it through `ANDROID_NDK_HOME`, `ANDROID_NDK_ROOT`, an `android-ndk-*` folder
  at a drive root or in the user folder (e.g. `D:\android-ndk-r28c`), or `<Android SDK>/ndk/<newest>`, and uses its
  clang. To choose one: `build.bat android --ndk D:\android-ndk-r28c`.
- Without the NDK it uses zig with musl. This works because the plugin only calls a few libc functions whose ABI is
  identical in Bionic (`malloc`, `free`, `memcpy`, `strlen`, `strstr`, `vsnprintf`, `puts`...); math is compiled in.

Either way only `luaopen_plugin_s3d` is exported (Solar2D loads plugins with `RTLD_GLOBAL`), ARMv7 uses `softfp` + NEON
like the NDK, and libraries use 16 KB pages for Android 15+. After building, each `.so` is checked for the right
architecture, the entry point, its dependencies and stray exports.

### Without installing

The Windows Simulator can also load a `plugin.s3d.dll` placed in the project folder; see the top of `S3DDemo/main.lua`
for a loader that copies it to a temporary folder first, so the DLL can be rebuilt while the Simulator is open.

### CMake (optional)

`CMakeLists.txt` is provided for Visual Studio (`-A Win32`) and Android Studio (`externalNativeBuild`). It needs the
real `liblua.so` / `libcorona.so` from `Corona.aar` and is less tested than `tools/build.py`.

## Limitations

- OpenGL / OpenGL ES backend only (see Platforms).
- Vertices are transformed on the CPU and every object is one draw call. A few hundred objects and tens of thousands
  of triangles run fine; there is no instancing.
- No 3D parent/child hierarchy: every object has its own transform.
- One directional light. Shadows only cover the square `center ± area`; a larger area gives coarser shadows.
- No `.mtl` materials, no glTF, no skeletal animation (frame animation only).
- No MSAA; use FXAA through `newPostProcess`.
- Planar shadows use stencil to avoid double blending. Stencil is available inside `newPostProcess`; when drawing
  straight to a screen without stencil, concave objects may show darker overlaps.

## Third-party code

- [cglm](https://github.com/recp/cglm) (MIT), downloaded at build time.
- `src/s3d_corona.h` contains type declarations copied from Solar2D's `CoronaGraphics.h` (MIT).
- The FXAA shader is based on FXAA 3.x by Timothy Lottes.
- The demo's animals are from Kenney's Animal Kit (CC0, [kenney.nl](https://kenney.nl)).

