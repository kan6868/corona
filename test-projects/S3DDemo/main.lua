-- plugin.s3d demo: basic 3D + post-processing (FXAA, color grading, vignette), fog, shadow map, OBJ animation
display.setStatusBar( display.HiddenStatusBar )

-- Load plugin.s3d.dll from the project folder. On Windows the DLL is copied to a uniquely named temp folder
-- and loaded from there: the file in the project is never locked, so it can be rebuilt and overwritten while the Simulator is open.
do
    local main = system.pathForFile( "main.lua", system.ResourceDirectory )
    local dir = main and main:match( "^(.*)[/\\]main%.lua$" )
    if dir then
        local loaded = false
        local okLfs, lfs = pcall( require, "lfs" )
        local src = io.open( dir .. "\\plugin.s3d.dll", "rb" )
        if okLfs and src then
            local data = src:read( "*a" )
            src:close()
            local tmp = system.pathForFile( "", system.TemporaryDirectory )
            local d = tmp .. "\\s3d_" .. os.time() .. "_" .. math.random( 100000 )
            lfs.mkdir( d ); lfs.mkdir( d .. "\\plugin" )
            local out = io.open( d .. "\\plugin\\s3d.dll", "wb" )
            if out then
                out:write( data ); out:close()
                package.cpath = d .. "\\?.dll;" .. package.cpath
                loaded = true
            end
        end
        if not loaded then package.cpath = dir .. "\\?.dll;" .. dir .. "/?.so;" .. package.cpath end
    end
end

local s3d = require( "plugin.s3d" )
print( "s3d version", s3d.version, "backend", s3d.backend )

local SKY = { 0.62, 0.76, 0.9 }

-- The scene is drawn into a snapshot so post-processing can be applied. post.scene is used like s3d.newScene().
local post = s3d.newPostProcess{
    clearColor = SKY,
    fxaa = true,
    exposure = 1.05, contrast = 1.1, saturation = 1.15, gamma = 1.0,
    tint = { 1.0, 0.98, 0.94 }, vignette = 0.35, vignetteSoftness = 0.6,
}
local scene = post.scene

-- Shadow map: enable it BEFORE creating objects so castShadow = true uses the shadow map
s3d.setLight{ direction = { -0.5, -1, -0.4 }, ambient = 0.35, color = { 1, 0.97, 0.9 } }
-- mobile: 1024 map + 2x2 filter ("medium"); desktop: 2048 + 3x3 ("high")
s3d.setShadowMap{ size = s3d.isMobile and 1024 or 2048, quality = s3d.isMobile and "medium" or "high",
    area = 12, center = { 0, 0, -3 }, distance = 30, bias = 0.0015, darkness = 0.7 }

-- repeating floor texture: set wrap = repeat BEFORE loading the image (the image should have power-of-two sides)
display.setDefault( "textureWrapX", "repeat" )
display.setDefault( "textureWrapY", "repeat" )
local floor = s3d.newPlane( scene, 40, 40, { texture = "assets/checker.png", uvRepeat = 10 } )
display.setDefault( "textureWrapX", "clampToEdge" )
display.setDefault( "textureWrapY", "clampToEdge" )

local crate = s3d.newCube( scene, 1.2, { texture = "assets/crate.png", castShadow = true } )
crate:moveTo( -1.6, 0.6, 0 )

local ball = s3d.newSphere( scene, 0.6, 32, 20, { texture = "assets/white.png", color = { 0.95, 0.35, 0.3 }, castShadow = true } )
ball:moveTo( 1.5, 0.6, 0.5 )

local pillar = s3d.newCylinder( scene, 0.35, 1.6, 24, { texture = "assets/white.png", color = { 0.4, 0.75, 0.45 }, castShadow = true } )
pillar:moveTo( 0.2, 0.8, -2 )

-- models are named .obj.txt: Solar2D for Android drops every *.obj file from the build
local torus = s3d.loadOBJ( scene, "assets/torus.obj.txt", { texture = "assets/white.png", color = { 0.95, 0.8, 0.3 }, castShadow = true } )
torus:moveTo( 0, 1.8, 1.2 )

-- frame animation from a sequence of OBJ files (assets/anim/*.obj.txt: flag 30 frames, fish 24 frames, same topology)
local pole = s3d.newCylinder( scene, 0.04, 2.7, 12, { texture = "assets/white.png", color = { 0.85, 0.85, 0.9 }, castShadow = true } )
pole:moveTo( 3.4, 1.35, -2.6 )
local flag = s3d.loadOBJAnimation( scene, "assets/anim/flag_%04d.obj", { first = 1, last = 30, fps = 30,
    texture = "assets/white.png", color = { 0.85, 0.2, 0.2 }, cull = false, castShadow = true } )
flag:moveTo( 3.44, 1.6, -2.6 )
local fish = s3d.loadOBJAnimation( scene, "assets/anim/fish_%04d.obj", { first = 1, last = 24, fps = 24,
    texture = "assets/white.png", color = { 0.95, 0.55, 0.25 }, castShadow = true } )
local anims = { flag, fish }

-- a herd of animals (Kenney Animal Kit, assets/anim/animals) walking around the floor: the inner ring counter-clockwise,
-- the outer ring clockwise. Legs, tails and wings move by OBJ group (see animals.lua). One animal is loaded per frame.
local animals = require( "animals" )
local herd = animals.newHerd( s3d, scene, { center = { 0, 0, -1 }, rings = { { radius = 5.2, dir = 1 }, { radius = 7, dir = -1 } } } )

-- a row of crates going into the distance to show the fog
for i = 1, 8 do
    local c = s3d.newCube( scene, 0.8, { texture = "assets/crate.png", castShadow = true } )
    c:moveTo( -6 + i * 1.6, 0.4, -6 - i * 2.2 )
end

s3d.setFog{ color = SKY, start = 8, ["end"] = 30 }

-- "sun": an unlit sphere placed towards the light so you can see where the light is (casts no shadow)
local SUN_CENTER, SUN_DIST = { 0, 0, -1 }, 7
local sun = s3d.newSphere( scene, 0.35, 16, 10, { texture = "assets/white.png", color = { 1, 0.9, 0.4 }, unlit = true } )
local sunAz, sunEl = 220, 50 -- azimuth, elevation (degrees)
local function updateSun()
    local az, el = math.rad( sunAz ), math.rad( sunEl )
    local tx, ty, tz = math.cos( el ) * math.cos( az ), math.sin( el ), math.cos( el ) * math.sin( az ) -- direction to the light
    s3d.setLight{ direction = { -tx, -ty, -tz } } -- direction = where the light travels
    sun:moveTo( SUN_CENTER[1] + tx * SUN_DIST, SUN_CENTER[2] + ty * SUN_DIST, SUN_CENTER[3] + tz * SUN_DIST )
end
updateSun()

-- 2D UI, drawn afterwards and not post-processed
local ui = display.newGroup()
local L, T = display.screenOriginX, display.screenOriginY
local title = display.newText{ parent = ui, text = "plugin.s3d " .. tostring( s3d.version ) .. " - drag: orbit, arrows/pad: move, wheel: zoom, tap floor: place ball",
    x = L + 16, y = T + 24, font = native.systemFontBold, fontSize = 22 }
title.anchorX = 0
local info = display.newText{ parent = ui, text = "", x = L + 16, y = T + 54, font = native.systemFont, fontSize = 18 }
info.anchorX = 0

local state = { fxaa = true, fog = true, shadow = true, grade = true, sunOrbit = true, anim = true }
local objects = { crate, ball, pillar, torus }

local function applyState()
    post:set{ fxaa = state.fxaa }
    if state.grade then
        post:set{ exposure = 1.05, contrast = 1.1, saturation = 1.15, tint = { 1.0, 0.98, 0.94 }, vignette = 0.35 }
    else
        post:set{ exposure = 1, contrast = 1, saturation = 1, tint = { 1, 1, 1 }, vignette = 0 }
    end
    if state.fog then s3d.setFog{ color = SKY, start = 8, ["end"] = 30 } else s3d.setFog( false ) end
    s3d.setShadowMap{ enabled = state.shadow }
    for _, a in ipairs( anims ) do if state.anim then a:resume() else a:pause() end end
    herd:setPlaying( state.anim )
end

local buttons = { { "fxaa", "FXAA" }, { "fog", "Fog" }, { "shadow", "Shadow" }, { "grade", "Grade" }, { "sunOrbit", "Sun orbit" }, { "anim", "Anim" } }
for i, b in ipairs( buttons ) do
    local key, label = b[1], b[2]
    local x = L + display.actualContentWidth - 70 - ( #buttons - i ) * 130
    local bg = display.newRoundedRect( ui, x, T + 40, 120, 44, 10 )
    local txt = display.newText{ parent = ui, text = "", x = x, y = T + 40, font = native.systemFontBold, fontSize = 13 }
    local function refresh()
        txt.text = label .. ( state[key] and ": on" or ": off" )
        if state[key] then bg:setFillColor( 0.15, 0.45, 0.3, 0.85 ) else bg:setFillColor( 0, 0, 0, 0.5 ) end
    end
    refresh()
    bg:addEventListener( "touch", function( e )
        if e.phase == "ended" then
            state[key] = not state[key]
            refresh()
            applyState()
        end
        return true
    end )
end

-- shadow quality button (compare FPS on phones): low = 1 tap, medium = 2x2, high = 3x3
do
    local levels = { "low", "medium", "high" }
    local names = { low = "low", medium = "medium", high = "high" }
    local cur = s3d.isMobile and 2 or 3
    local x = L + display.actualContentWidth - 70 - #buttons * 130
    local bg = display.newRoundedRect( ui, x, T + 40, 120, 44, 10 )
    bg:setFillColor( 0.2, 0.3, 0.55, 0.85 )
    local txt = display.newText{ parent = ui, text = "", x = x, y = T + 40, font = native.systemFontBold, fontSize = 13 }
    local function refresh() txt.text = "Shadow: " .. names[levels[cur]] end
    refresh()
    bg:addEventListener( "touch", function( e )
        if e.phase == "ended" then
            cur = cur % #levels + 1
            s3d.setShadowMap{ quality = levels[cur] }
            refresh()
        end
        return true
    end )
end

-- camera orbiting a target; 4 buttons / arrow keys move the target over the floor, mouse wheel zooms
local yaw, pitch, dist = 30, 24, 14
local target = { 0, 0.5, 0 }
local MIN_DIST, MAX_DIST = 2.5, 40
local function updateCamera()
    s3d.orbit( yaw, pitch, dist, target )
    -- the shadow map area follows the target so objects around the camera always have shadows
    s3d.setShadowMap{ center = { target[1], 0, target[3] - 2 } }
end
updateCamera()

local function zoom( factor )
    dist = math.max( MIN_DIST, math.min( MAX_DIST, dist * factor ) )
    updateCamera()
end

-- held directions: x = right(+)/left(-), z = forward(+)/back(-)
local held = {}
local function moveStep( dt )
    local mx, mz = 0, 0
    for _, v in pairs( held ) do mx, mz = mx + v[1], mz + v[2] end
    if mx == 0 and mz == 0 then return end
    local y = math.rad( yaw )
    local fx, fz = -math.sin( y ), -math.cos( y ) -- forward: from the camera towards the target, on the floor
    local rx, rz = math.cos( y ), -math.sin( y )  -- right
    local speed = dist * 0.6 * dt                 -- move faster when zoomed out
    target[1] = math.max( -20, math.min( 20, target[1] + ( fx * mz + rx * mx ) * speed ) )
    target[3] = math.max( -30, math.min( 20, target[3] + ( fz * mz + rz * mx ) * speed ) )
    updateCamera()
end

-- D-pad in the bottom-left corner: hold to keep moving
do
    local B = display.screenOriginY + display.actualContentHeight
    local cx, cy, step, size = L + 110, B - 110, 74, 66
    local pad = { { "up", "▲", 0, -1, 0, 1 }, { "down", "▼", 0, 1, 0, -1 }, { "left", "◀", -1, 0, -1, 0 }, { "right", "▶", 1, 0, 1, 0 } }
    for _, d in ipairs( pad ) do
        local id, label, ox, oy, mx, mz = d[1], d[2], d[3], d[4], d[5], d[6]
        local bg = display.newRoundedRect( ui, cx + ox * step, cy + oy * step, size, size, 12 )
        bg:setFillColor( 0, 0, 0, 0.45 )
        display.newText{ parent = ui, text = label, x = bg.x, y = bg.y, font = native.systemFontBold, fontSize = 28 }
        bg:addEventListener( "touch", function( e )
            if e.phase == "began" then
                display.getCurrentStage():setFocus( bg, e.id )
                held[id] = { mx, mz }
                bg:setFillColor( 0.15, 0.45, 0.3, 0.85 )
            elseif e.phase == "ended" or e.phase == "cancelled" then
                display.getCurrentStage():setFocus( bg, nil )
                held[id] = nil
                bg:setFillColor( 0, 0, 0, 0.45 )
            end
            return true
        end )
    end
    -- zoom buttons for phones (no mouse wheel)
    for i, z in ipairs( { { "+", 1 / 1.15 }, { "−", 1.15 } } ) do
        local bg = display.newRoundedRect( ui, cx + 3 * step - 10, cy + ( i == 1 and -step or step ) * 0.5, size, size, 12 )
        bg:setFillColor( 0, 0, 0, 0.45 )
        display.newText{ parent = ui, text = z[1], x = bg.x, y = bg.y, font = native.systemFontBold, fontSize = 34 }
        bg:addEventListener( "touch", function( e )
            if e.phase == "began" then
                display.getCurrentStage():setFocus( bg, e.id )
                bg.zooming = z[2]
            elseif e.phase == "ended" or e.phase == "cancelled" then
                display.getCurrentStage():setFocus( bg, nil )
                bg.zooming = nil
            end
            return true
        end )
        Runtime:addEventListener( "enterFrame", function() if bg.zooming then zoom( 1 + ( bg.zooming - 1 ) * 0.25 ) end end )
    end
end

-- mouse wheel (Simulator / desktop): ~15% per notch
Runtime:addEventListener( "mouse", function( e )
    if e.type == "scroll" and e.scrollY and e.scrollY ~= 0 then
        zoom( e.scrollY > 0 and 1.15 or 1 / 1.15 )
    end
end )

-- keyboard: arrows / WASD move, +/- zoom
local KEYS = { up = "up", w = "up", down = "down", s = "down", left = "left", a = "left", right = "right", d = "right" }
local KEYDIR = { up = { 0, 1 }, down = { 0, -1 }, left = { -1, 0 }, right = { 1, 0 } }
Runtime:addEventListener( "key", function( e )
    local dir = KEYS[e.keyName]
    if dir then
        held["key_" .. dir] = ( e.phase == "down" ) and KEYDIR[dir] or nil
        return true
    end
    if e.phase == "down" then
        if e.keyName == "=" or e.keyName == "+" or e.keyName == "numPad+" then zoom( 1 / 1.15 ); return true end
        if e.keyName == "-" or e.keyName == "numPad-" then zoom( 1.15 ); return true end
    end
    return false
end )

local dragStart
Runtime:addEventListener( "touch", function( e )
    if e.phase == "began" then
        dragStart = { x = e.x, y = e.y, yaw = yaw, pitch = pitch, moved = false }
    elseif e.phase == "moved" and dragStart then
        if math.abs( e.x - dragStart.x ) + math.abs( e.y - dragStart.y ) > 6 then dragStart.moved = true end
        yaw = dragStart.yaw - ( e.x - dragStart.x ) * 0.3
        pitch = math.max( 5, math.min( 85, dragStart.pitch + ( e.y - dragStart.y ) * 0.3 ) )
        updateCamera()
    elseif e.phase == "ended" and dragStart then
        if not dragStart.moved then
            local o, d = s3d.screenToRay( e.x, e.y )
            local x, y, z = s3d.rayPlaneY( o, d, 0 )
            if x and math.abs( x ) < 20 and math.abs( z ) < 20 then ball:moveTo( x, 0.6, z ) end
        end
        dragStart = nil
    end
    return true
end )

local frames, last = 0, system.getTimer()
local prevTime
Runtime:addEventListener( "enterFrame", function( e )
    local dt = prevTime and math.min( 0.1, ( e.time - prevTime ) / 1000 ) or 0
    prevTime = e.time
    moveStep( dt )
    local t = e.time / 1000
    crate:rotateTo( 0, t * 40, 0 )
    torus:rotateTo( t * 50, t * 30, 0 )
    -- the fish swims a 2.2 m circle around (0, 2.4, -1); its nose is local +X, turned along the tangent
    if state.anim then
        herd:update( dt )
        local a = t * 0.5
        fish:moveTo( 2.2 * math.cos( a ), 2.4 + 0.15 * math.sin( t * 1.3 ), -1 + 2.2 * math.sin( a ) )
        fish:rotateTo( 0, -math.deg( a ) - 90, 0 )
    end
    if state.sunOrbit then
        sunAz = ( sunAz + 0.6 ) % 360             -- ~10 seconds per turn at 60 fps
        sunEl = 45 + 25 * math.sin( t * 0.4 )     -- up and down between 20 and 70 degrees
        updateSun()
    end
    frames = frames + 1
    if e.time - last > 500 then
        info.text = string.format( "fps %.0f", frames * 1000 / ( e.time - last ) )
        frames, last = 0, e.time
    end
end )
