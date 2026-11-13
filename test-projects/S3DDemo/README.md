# S3DDemo

Demo project for [plugin.s3d](../s3d-plugin/README.md). It shows:

- primitives (cube, sphere, cylinder), an OBJ model (torus) and a textured floor,
- a shadow map with switchable quality, fog, and post-processing (FXAA, color grading, vignette),
- frame animation from OBJ sequences (a waving flag and a swimming fish),
- a herd of animals walking around the scene (`animals.lua`, see below),
- a light that orbits the scene, with a small "sun" marking where it is,
- camera controls and touch picking.

## Controls

| Input | Action |
| --- | --- |
| Drag | Orbit the camera |
| D-pad (bottom left), arrow keys, WASD | Move the camera target over the floor |
| Mouse wheel, `+` / `−` buttons, `+` / `-` keys | Zoom |
| Tap the floor | Move the red ball there |
| Top buttons | Toggle FXAA, fog, shadows, color grading, sun orbit, animation; cycle shadow quality |

## Running

- **Windows Simulator:** install the plugin (`build.bat install` in `s3d-plugin`), or put `plugin.s3d.dll` in this
  folder; `main.lua` loads a DLL found here first.
- **Android:** install the plugin, then build from the Simulator. `build.settings` already declares
  `publisherId = "com.s3d"` and the depth buffer setting.

## Files

| File | Contents |
| --- | --- |
| `main.lua` | Scene setup, UI, camera, main loop. |
| `animals.lua` | Walking animals: builds walk / hop / waddle / fly cycles by rotating each OBJ group (legs, tail, wings) around its joint, writes the frames to `system.TemporaryDirectory` and plays them with `s3d.loadOBJAnimation`. |
| `assets/anim/*.obj.txt` | Flag (30 frames) and fish (24 frames) animations. |
| `assets/anim/animals/` | [Kenney Animal Kit](https://kenney.nl) (CC0). The `.obj.txt` copies are what the demo loads, because Android builds drop `*.obj` files. |
| `config.lua`, `build.settings` | Landscape, 1280 x 720 content area. |
