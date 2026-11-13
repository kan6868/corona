# plugin.s3d API reference

Version 0.5.0.

```lua
local s3d = require( "plugin.s3d" )
```

- [Conventions](#conventions)
- [Module](#module): `version`, `backend`, `supported`, `isSupported`, `isMobile`, `init`
- [Scenes](#scenes): `newScene`, `newPostProcess`
- [Creating objects](#creating-objects): `newCube`, `newSphere`, `newCylinder`, `newCapsule`, `newPlane`, `loadOBJ`
- [Object methods](#object-methods): transform, render state, shadows, effects
- [Frame animation](#frame-animation): `loadOBJAnimation` and its methods
- [Camera](#camera): `setCamera`, `getCamera`, `orbit`
- [Light](#light): `setLight`
- [Fog](#fog): `setFog`
- [Shadows](#shadows): `setShadowMap`, `setShadow`, `getShadowMode`
- [Picking and projection](#picking-and-projection): `worldToScreen`, `screenToRay`, `rayPlaneY`
- [Effects](#effects): built-in effects, writing your own

## Conventions

- **Right-handed, Y up.** Units are arbitrary; the demos use meters.
- **Angles are in degrees.**
- **Euler rotation order:** `rotateTo( rx, ry, rz )` builds `R = Ry(yaw) * Rx(pitch) * Rz(roll)`; the model matrix is
  `T * R * S`.
- **Vectors** are passed as `{ x, y, z }` (`{ x = .., y = .., z = .. }` also works for options).
- **Colors** are `{ r, g, b [, a] }` in 0..1.
- Methods that change an object return the object, so calls can be chained: `obj:moveTo( 0, 1, 0 ):rotateTo( 0, 45, 0 )`.
- 3D objects are ordinary Solar2D display objects (meshes). **Do not set `x`, `y`, `rotation`, `xScale`, `yScale`
  on them or on the groups that contain them**; use the 3D methods instead. `setFillColor`, `alpha`, `isVisible`,
  `removeSelf`, `toFront` and event listeners work as usual.
- The first call to any constructor runs `s3d.init()` automatically.

## Module

| Field / function | Description |
| --- | --- |
| `s3d.version` | Plugin version string, e.g. `"0.5.0"`. |
| `s3d.backend` | Solar2D graphics backend (`system.getInfo( "graphicsBackend" )`), e.g. `"glBackend"`. |
| `s3d.supported` | `false` on the Vulkan backend. 3D objects are then created hidden and a warning is printed. |
| `s3d.isSupported()` | Returns `s3d.supported`. |
| `s3d.isMobile` | `true` on Android, iOS and tvOS. Handy for picking shadow quality. |
| `s3d.init()` | Registers the effects and render hooks. Called automatically; calling it again does nothing. Enables `display.setDefault( "enableDepth", true )`. |
| `s3d.EFFECT_LIT`, `EFFECT_UNLIT`, `EFFECT_NORMALS`, `EFFECT_SHADOW`, `EFFECT_DEPTH`, `EFFECT_POST` | Effect names (`"filter.s3d.lit"`, ...). See [Effects](#effects). |

## Scenes

### `s3d.newScene( [parent] )`

Returns a display group for 3D objects, inserted into `parent` when given. It is a plain group at (0, 0); leave its
position, rotation and scale alone. Shadow helper layers are added to it automatically.

### `s3d.newPostProcess( [opts] )`

Creates a full-screen snapshot that the scene is drawn into, filtered by `filter.s3d.post`. Draw your 3D objects in
`post.scene`; keep 2D UI outside it so it is not post-processed. The snapshot is invalidated every frame.

| Option | Default | Description |
| --- | --- | --- |
| `clearColor` | `{ 0, 0, 0, 1 }` | Background (sky) color. |
| `fxaa` | `true` | Fast approximate anti-aliasing. |
| `exposure` | `1` | Brightness multiplier. |
| `contrast` | `1` | Contrast around mid-gray. |
| `saturation` | `1` | 0 = grayscale. |
| `gamma` | `1` | Gamma correction. |
| `brightness` | `0` | Added to the color. |
| `tint` | `{ 1, 1, 1 }` | Color multiplier. |
| `vignette` | `0` | Corner darkening, 0..1. |
| `vignetteSoftness` | `0.5` | Width of the vignette falloff. |
| `width`, `height` | content size | Snapshot size. |
| `x`, `y` | content center | Snapshot position. |
| `parent` | stage | Group to insert the snapshot into. |

Returns a table:

| Field / method | Description |
| --- | --- |
| `post.scene` | Group to put 3D objects in (like `s3d.newScene()`). |
| `post.snapshot` | The `display.newSnapshot` object. |
| `post.settings` | Current settings. |
| `post:set( opts )` | Change any of the color options above at run time. |
| `post:setEnabled( on )` | Turn the filter off (the scene is still drawn) or back on. |
| `post:removeSelf()` | Remove the snapshot and its frame listener. |

```lua
local post = s3d.newPostProcess{ clearColor = { 0.62, 0.76, 0.9 }, exposure = 1.05, vignette = 0.3 }
post:set{ saturation = 0.5 }
```

## Creating objects

All constructors take a `parent` group and an optional `opts` table and return a 3D object.

| Function | Shape |
| --- | --- |
| `s3d.newCube( parent, size, [opts] )` | Cube with side `size` (default 1), centered at the origin. |
| `s3d.newSphere( parent, radius, segments, rings, [opts] )` | UV sphere. Defaults: radius 0.5, 24 segments, 16 rings. |
| `s3d.newCylinder( parent, radius, height, segments, [opts] )` | Capped cylinder along Y, centered. Defaults: 0.5, 1, 24. |
| `s3d.newCapsule( parent, radius, height, segments, rings, [opts] )` | Capsule along Y. `height` is the distance between the hemisphere centers (total height = `height + 2 * radius`). Defaults: 0.25, 1, 20, 6. |
| `s3d.newPlane( parent, width, depth, [opts] )` | Plane in XZ facing +Y, centered. `opts.uvRepeat` repeats the texture (set `display.setDefault( "textureWrapX"/"textureWrapY", "repeat" )` before loading it). |
| `s3d.loadOBJ( parent, filename, [opts] )` | Wavefront OBJ model. |

### Common options

| Option | Description |
| --- | --- |
| `texture` | Image file for the object's fill. For plain colored objects the demo uses a small white image plus `color`. |
| `baseDir` | Base directory for `texture` (and for the OBJ file in `loadOBJ`). Default `system.ResourceDirectory`. |
| `color` | `{ r, g, b [, a] }` passed to `setFillColor`. |
| `unlit` | `true` uses `filter.s3d.unlit` (no lighting). |
| `effect` | Any effect name, e.g. `"filter.s3d.normals"` or your own. |
| `cull` | `false` draws both sides (for open meshes like flags). |
| `castShadow` | `true` casts a shadow (shadow map or planar, see [Shadows](#shadows)). |
| `x`, `y`, `z` | Initial position. |

### OBJ files

`loadOBJ` reads `v`, `vt`, `vn` and `f` lines. Polygons are split into triangle fans; faces without `vn` get flat
normals; materials (`mtllib`, `usemtl`) and groups are ignored. Extra option: `scale` multiplies every position.

Solar2D for Android always drops `*.obj` files from the APK. Name your models `model.obj.txt`; when a name ends in
`.obj`, `loadOBJ` also tries the same name with `.txt` appended, so `s3d.loadOBJ( scene, "model.obj" )` works on every
platform.

## Object methods

### Transform

| Method | Description |
| --- | --- |
| `obj:moveTo( x, y, z )` | Set the position. |
| `obj:moveBy( dx, dy, dz )` | Move by an offset. |
| `obj:rotateTo( rx, ry, rz )` | Set the Euler rotation in degrees (pitch X, yaw Y, roll Z; see Conventions). |
| `obj:rotateBy( dx, dy, dz )` | Add to the Euler rotation. |
| `obj:scaleTo( sx, [sy, sz] )` | Set the scale; `sy` and `sz` default to `sx`. |
| `obj:setPose( x, y, z, [qx, qy, qz, qw] )` | Set position and quaternion rotation in one call. Made for physics: `obj:setPose( body:getTransform() )`. |
| `obj:setRotationQuat( qx, qy, qz, qw )` | Set only the quaternion rotation. |

After `setPose` / `setRotationQuat`, the object uses the quaternion until the next `rotateTo` / `rotateBy`.

Readable fields: `obj.position3d`, `obj.rotation3d`, `obj.scale3d` (tables `{ x, y, z }`, do not modify them directly),
`obj.quat3d` (or `nil`), `obj.vertexCount3d`.

### Render state

| Method | Description |
| --- | --- |
| `obj:setCulling( on )` | Back-face culling (default on). |
| `obj:setDepthTest( on )` | Depth testing (default on). |
| `obj:setDepthWrite( on )` | Depth writing (default on). Turn off for transparent objects. |
| `obj:setEffect( name, [uses] )` | Switch the effect. See [Custom effects](#custom-effects) for `uses`. |
| `obj:castShadow( on )` | Start or stop casting a shadow. The object must already be in a group. |

## Frame animation

### `s3d.loadOBJAnimation( parent, source, [opts] )`

Loads a sequence of OBJ files as one animated object. Every frame must have the same topology (the same faces in the
same order), which is what Blender produces with **File > Export > Wavefront (.obj)** and **Animation** checked. Positions
and normals of two neighbouring frames are interpolated in C, so playback costs a handful of Lua operations per frame.

`source` is either a `string.format` pattern such as `"anim/run_%04d.obj"` (with `opts.first` and `opts.last`) or a list
of file names.

| Option | Default | Description |
| --- | --- | --- |
| `first`, `last` | `1`, required | Frame numbers substituted into the pattern. |
| `frames` | | List of file names, instead of a pattern. |
| `fps` | `24` | Playback rate. |
| `loop` | `true` | Loop the whole sequence (clips can override). |
| `interpolate` | `true` | Blend between frames; `false` steps from frame to frame. |
| `autoPlay` | `true` | Start playing right away. |
| `clip` | | Clip to start with. |
| `clips` | `{}` | Named ranges: `{ run = { 1, 16 }, jump = { 17, 24, loop = false, fps = 30 } }`. Indices count loaded frames from 1. |
| `onComplete` | | `function( obj, clipName )`, called when a non-looping clip ends. |
| `scale` | `1` | Multiplies every position. |

All [common options](#common-options) apply too (`texture`, `castShadow`, ...).

| Method / field | Description |
| --- | --- |
| `obj:play( [clip] )` | Play a clip, or the whole sequence when `clip` is `nil`, from its start. |
| `obj:pause()`, `obj:resume()` | Pause / resume. |
| `obj:stop()` | Stop and rewind to the first frame of the clip. |
| `obj:gotoFrame( frame )` | Jump to a frame of the current clip, counted from 1; fractions blend (`2.5`). |
| `obj:setSpeed( s )` | Playback speed multiplier (default 1). |
| `obj:setFPS( fps )` | Change the frame rate. |
| `obj:setInterpolation( on )` | Turn blending on or off. |
| `obj:isPlaying()` | `true` while playing. |
| `obj.frameCount` | Number of loaded frames. |
| `obj.currentFrame` | Current (fractional) frame index. |
| `obj.clip` | Current clip name or `nil`. |

When a non-looping clip ends, the object also dispatches `{ name = "animation", phase = "ended", clip = clipName }`.

```lua
local hero = s3d.loadOBJAnimation( scene, "assets/hero/hero_%04d.obj", {
    first = 1, last = 40, fps = 24, texture = "assets/hero.png", castShadow = true,
    clips = { idle = { 1, 20 }, wave = { 21, 40, loop = false } },
    clip = "idle",
} )
hero:addEventListener( "animation", function( e ) if e.clip == "wave" then hero:play( "idle" ) end end )
hero:play( "wave" )
```

Memory: each frame keeps one position and one normal buffer (24 bytes per vertex), so a 3,000-vertex model with 60
frames uses about 4 MB.

## Camera

### `s3d.setCamera( opts )`

| Option | Default | Description |
| --- | --- | --- |
| `position` | `{ 0, 2, 6 }` | Camera position. |
| `target` | `{ 0, 0, 0 }` | Point to look at. |
| `up` | `{ 0, 1, 0 }` | Up vector. |
| `fov` | `60` | Vertical field of view in degrees. |
| `near`, `far` | `0.1`, `100` | Clip planes. |
| `aspect` | screen | Aspect ratio; defaults to `display.actualContentWidth / display.actualContentHeight`. |

Only the given options change.

### `s3d.getCamera()`

Returns a copy: `{ position, target, up, fov, near, far }`.

### `s3d.orbit( yaw, pitch, distance, [target] )`

Places the camera on a sphere around `target` (default: the current target). `yaw = 0` puts the camera on the +Z
side; positive `pitch` looks down from above.

## Light

### `s3d.setLight( opts )`

One directional light.

| Option | Default | Description |
| --- | --- | --- |
| `direction` | `{ -0.4, -1, -0.6 }` | Direction the light travels (from the light towards the scene). |
| `ambient` | `0.3` | Light that reaches every surface. |
| `color` | `{ 1, 1, 1 }` | Light color. |

Changing the light also updates the shadow map's light matrix.

## Fog

### `s3d.setFog( opts | false )`

Linear fog by camera depth, computed per pixel. Applies to the `lit`, `unlit` and planar `shadow` effects.

| Option | Default | Description |
| --- | --- | --- |
| `color` | `{ 0.7, 0.8, 0.9 }` | Fog color; usually the same as the post-process `clearColor`. |
| `start` | `10` | Distance where fog starts. |
| `["end"]` (or `finish`) | `40` | Distance where fog is complete. |

`s3d.setFog( false )` turns fog off.

## Shadows

There are two kinds. **Shadow maps** cast onto every lit object, including the caster itself. **Planar shadows**
flatten each caster onto one horizontal plane: cheap, but they only fall on the floor. `obj:castShadow( true )` (or
`castShadow = true` when creating) uses whichever mode is active **when the object starts casting**, so enable the shadow
map before creating casters.

### `s3d.setShadowMap( opts )`

| Option | Default | Description |
| --- | --- | --- |
| `enabled` | see below | `false` turns shadow maps off. |
| `size` | `1024` | Shadow texture size, 64..4096. |
| `area` | `10` | Half the side of the square that receives shadows, around `center`. |
| `center` | `{ 0, 0, 0 }` | Center of that square. Move it with the camera to keep shadows around the player. |
| `distance` | `30` | Distance from the light to `center` (orthographic projection). |
| `bias` | `0.0015` | Fights "shadow acne"; grazing surfaces get up to 4x more automatically. |
| `darkness` | `0.6` | 0 = no visible shadow, 1 = the directional light is fully blocked (ambient remains). |
| `quality` | mobile `"medium"`, desktop `"high"` | `"low"` (1 sample), `"medium"` (2x2 PCF), `"high"` (3x3 PCF). |

Only the given keys change. When `enabled` is omitted, the first call turns shadow maps on and later calls keep the
current state, so you can move `center` every frame without re-enabling shadows that were turned off.

Every object using `filter.s3d.lit` receives shadows, whether or not it casts one.

**Performance:** the main cost on phones is texture reads on every shadow-receiving pixel (a floor covers the whole
screen). `size = 1024` with `quality = "medium"` is a good balance on mobile; `"low"` is the cheapest but has jagged edges.

```lua
s3d.setShadowMap{ size = s3d.isMobile and 1024 or 2048, quality = s3d.isMobile and "medium" or "high",
    area = 12, center = { 0, 0, -3 }, distance = 30, bias = 0.0015, darkness = 0.7 }
```

### `s3d.setShadow( opts )`

Settings for planar shadows.

| Option | Default | Description |
| --- | --- | --- |
| `planeY` | `0` | Height of the plane that receives shadows. |
| `color` | `{ 0, 0, 0, 0.35 }` | Shadow color; alpha is the strength. |

### `s3d.getShadowMode()`

Returns `"map"` while shadow maps are enabled, otherwise `"planar"`.

## Picking and projection

All screen coordinates are Solar2D content coordinates (the same as touch events).

| Function | Returns |
| --- | --- |
| `s3d.worldToScreen( x, y, z )` | `sx, sy, depth` (depth in NDC, -1..1), or `nil` when the point cannot be projected. |
| `s3d.screenToRay( sx, sy )` | `origin, direction` (two `{ x, y, z }` tables) of the ray from the camera through the point. |
| `s3d.rayPlaneY( origin, direction, [h] )` | `x, y, z` where the ray hits the plane `y = h` (default 0), or `nil` if it never does. |

```lua
Runtime:addEventListener( "tap", function( e )
    local o, d = s3d.screenToRay( e.x, e.y )
    local x, y, z = s3d.rayPlaneY( o, d, 0 )
    if x then ball:moveTo( x, 0.5, z ) end
end )
```

## Effects

### Built-in effects

| Effect | Use |
| --- | --- |
| `filter.s3d.lit` | Default. Texture × (ambient + light × shadow), plus fog. |
| `filter.s3d.unlit` | Texture and fill color only, plus fog. Good for skies, glowing objects, the demo's sun. |
| `filter.s3d.normals` | Shows normals as colors, for debugging. |
| `filter.s3d.shadow` | Used internally for planar shadows. |
| `filter.s3d.depth` | Used internally for the shadow map pass. |
| `filter.s3d.post` | Used by `newPostProcess`. |

### Custom effects

Any effect defined with `shellTransform = "s3d"` and `dataType = "s3d"` gets the 3D transform:

```lua
graphics.defineEffect{
    category = "filter", group = "game", name = "toon",
    shellTransform = "s3d", dataType = "s3d",
    fragment = [[
uniform P_DEFAULT vec4 u_S3D_LightDir;   // xyz: direction towards the light, w: ambient
uniform P_DEFAULT vec4 u_S3D_LightColor;
P_COLOR vec4 FragmentKernel( P_UV vec2 uv )
{
    P_DEFAULT vec3 n = normalize( CoronaVertexUserData.xyz );   // world-space normal
    P_DEFAULT float d = step( 0.4, dot( n, u_S3D_LightDir.xyz ) );
    return CoronaColorScale( texture2D( CoronaSampler0, uv ) * ( 0.5 + 0.5 * d ) );
}
]]
}
obj:setEffect( "filter.game.toon", { light = true, fog = false, shadow = false } )
```

The second argument of `setEffect` tells the plugin which uniforms the effect declares, so it never writes a uniform
that does not exist (Solar2D would print a warning every frame). Defaults: `light = true`, `fog = true`, `shadow = false`.

What the plugin provides:

| Name | Kind | Meaning |
| --- | --- | --- |
| `CoronaVertexUserData.xyz` | vertex data | World-space normal (not normalized after interpolation). |
| `u_S3D_LightDir` | uniform `vec4` | xyz: direction towards the light (normalized), w: ambient. Needs `light = true`. |
| `u_S3D_LightColor` | uniform `vec4` | rgb: light color. Needs `light = true`. |
| `u_S3D_Fog` | uniform `vec4` | rgb: fog color, a: 1 when fog is on. Needs `fog = true`. |
| `u_S3D_FogRange` | uniform `vec4` | x: start, y: end. Needs `fog = true`. |
| `v_S3D_Depth` | varying `float` | Camera depth of the pixel (for fog). |
| `u_S3D_ShadowParams` | uniform `vec4` | x: 0 off / 1, 2, 3 quality; y: bias; z: 1 / size; w: darkness. Needs `shadow = true`. |
| `u_S3D_ShadowMap` | `sampler2D` | Shadow map (RGBA8, depth packed in RGB). Needs `shadow = true`. |
| `v_S3D_ShadowPos` | varying `vec4` | Light-space clip position of the pixel. |

The varyings are declared by the shell transform; do not declare them again. The fog and shadow helpers used by the
built-in effects (`S3D_ApplyFog`, `S3D_ShadowLit`, `S3D_ShadowVisibility`) are in `src/s3d.lua` and can be copied into
your own shaders.
