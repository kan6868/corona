-- animals.lua: a herd of animals (Kenney Animal Kit in assets/anim/animals) walking around the floor.
--
-- The OBJ models in this kit are already split into groups (g): body, tail, leg-front-left, leg-back-right, wing-left...
-- This module builds a motion cycle by rotating each group around its joint (legs around the hip, the tail around its base,
-- wings around the shoulder), writes N OBJ frames to system.TemporaryDirectory and loads them with s3d.loadOBJAnimation:
-- the frames share one topology, so s3d interpolates smoothly between them. No plugin changes needed.
--
--   local animals = require( "animals" )
--   local herd = animals.newHerd( s3d, scene, { center = { 0, 0, -1 } } )
--   every frame: herd:update( dt ); herd:setPlaying( false ) stops the whole herd
--
-- Source files are named "animal-xxx.obj.txt" (Solar2D for Android drops every *.obj); "animal-xxx.obj" is tried as a fallback.
local M = {}

local DIR = "assets/anim/animals/"
local TEXTURE = DIR .. "Textures/colormap.png"
local FRAMES = 8                -- frames per cycle (frames are interpolated, so 8 is smooth enough)
local VERSION = 2               -- bump when the frame generator changes so old cached files are not reused

local sin, cos, rad, abs, pi = math.sin, math.cos, math.rad, math.abs, math.pi

-- motion styles: amplitudes (degrees / meters), stride = meters walked per cycle
local STYLES = {
    walk   = { leg = 28, tail = 18, bob = 0.035, stride = 0.55 },
    hop    = { leg = 40, bob = 0.28, stride = 0.9 },
    waddle = { leg = 30, wing = 22, roll = 9, bob = 0.03, stride = 0.32 },
    fly    = { leg = 12, wing = 55, bob = 0.12, stride = 1.6, altitude = 0.9 },
    crawl  = { leg = 22, bob = 0.02, stride = 0.3 },
}

-- the animals: style, scale, speed (m/s), heading offset (degrees; the crab walks sideways)
M.CATALOG = {
    { "beaver", "walk", 0.55, 0.7 },
    { "bee", "fly", 0.45, 1.1 },
    { "bunny", "hop", 0.5, 1.1 },
    { "cat", "walk", 0.5, 1.0 },
    { "caterpillar", "crawl", 0.5, 0.35 },
    { "chick", "waddle", 0.45, 0.6 },
    { "cow", "walk", 0.75, 0.8 },
    { "crab", "walk", 0.45, 0.6, 90 },
    { "deer", "walk", 0.7, 1.2 },
    { "dog", "walk", 0.55, 1.2 },
    { "elephant", "walk", 0.95, 0.7 },
    { "fox", "walk", 0.55, 1.2 },
    { "giraffe", "walk", 1.0, 0.9 },
    { "hog", "walk", 0.6, 0.9 },
    { "koala", "walk", 0.5, 0.6 },
    { "lion", "walk", 0.75, 1.0 },
    { "monkey", "walk", 0.55, 1.0 },
    { "panda", "walk", 0.65, 0.7 },
    { "parrot", "waddle", 0.5, 0.6 },
    { "penguin", "waddle", 0.5, 0.55 },
    { "pig", "walk", 0.6, 0.8 },
    { "polar", "walk", 0.7, 0.8 },
    { "tiger", "walk", 0.75, 1.1 },
}

-- ---------------------------------------------------------------------------------------------
-- Read a source OBJ: keep vt as is, remember the group of every face
local function openSource( name )
    for _, fn in ipairs{ DIR .. "animal-" .. name .. ".obj.txt", DIR .. "animal-" .. name .. ".obj" } do
        local path = system.pathForFile( fn, system.ResourceDirectory )
        local f = path and io.open( path, "r" )
        if f then return f end
    end
    error( "animals: cannot find " .. DIR .. "animal-" .. name .. ".obj.txt (or .obj)", 3 )
end

local function readSource( name )
    local f = openSource( name )
    local V, VT, VN, faces = {}, {}, {}, {}
    local group = "body"
    for line in f:lines() do
        local tag, rest = line:match( "^%s*(%S+)%s*(.*)$" )
        if tag == "v" then
            local x, y, z = rest:match( "(%S+)%s+(%S+)%s+(%S+)" )
            V[#V + 1] = { tonumber( x ), tonumber( y ), tonumber( z ) }
        elseif tag == "vt" then
            VT[#VT + 1] = rest
        elseif tag == "vn" then
            local x, y, z = rest:match( "(%S+)%s+(%S+)%s+(%S+)" )
            VN[#VN + 1] = { tonumber( x ), tonumber( y ), tonumber( z ) }
        elseif tag == "g" or tag == "o" then
            group = rest
        elseif tag == "f" then
            local face = { group = group }
            for tok in rest:gmatch( "%S+" ) do
                local vi, ti, ni = tok:match( "^([^/]*)/?([^/]*)/?([^/]*)$" )
                face[#face + 1] = { tonumber( vi ), tonumber( ti ), tonumber( ni ) }
            end
            faces[#faces + 1] = face
        end
    end
    f:close()
    return { V = V, VT = VT, VN = VN, faces = faces }
end

-- role of a group: body / tail / leg (fl, fr, bl, br) / wing (left, right)
local function roleOf( g )
    if g:match( "^leg%-" ) then return "leg", g:match( "^leg%-(%a+)%-(%a+)" ) end
    if g:match( "^wing%-" ) then return "wing", g:match( "^wing%-(%a+)" ) end
    if g == "tail" then return "tail" end
    return "body"
end

-- ---------------------------------------------------------------------------------------------
-- Rotations around an axis through point p (for positions) and through the origin (for normals)
local function rotX( x, y, z, a, py, pz )
    local c, s = cos( a ), sin( a ); local dy, dz = y - py, z - pz
    return x, py + dy * c - dz * s, pz + dy * s + dz * c
end
local function rotY( x, y, z, a, px, pz )
    local c, s = cos( a ), sin( a ); local dx, dz = x - px, z - pz
    return px + dx * c + dz * s, y, pz - dx * s + dz * c
end
local function rotZ( x, y, z, a, px, py )
    local c, s = cos( a ), sin( a ); local dx, dy = x - px, y - py
    return px + dx * c - dy * s, py + dx * s + dy * c, z
end

-- Pose of each group at phase p (0..2*pi): returns a transform (x, y, z, isNormal) -> x, y, z
local function poseFor( style, role, side1, side2, piv, p )
    local S = STYLES[style]
    local s, c = sin( p ), cos( p )
    local legA, wingA, tailA = 0, 0, 0
    if role == "leg" then
        local front, left = side1 == "front", side2 == "left"
        if style == "hop" then
            legA = rad( S.leg ) * ( front and -c or c )        -- front / back legs stretch opposite ways in a hop
        elseif style == "waddle" or style == "fly" then
            legA = rad( S.leg ) * ( left and s or -s )
        else
            local diag = ( front and left ) or ( not front and not left ) -- front-left + back-right move together
            legA = rad( S.leg ) * ( diag and s or -s )
        end
    elseif role == "wing" then
        local sign = side1 == "left" and 1 or -1
        if piv.upright then
            -- upright wings on the back (bee): only open outwards and close again, never cross each other
            wingA = -rad( S.wing or 0 ) * ( 0.5 + 0.5 * cos( 4 * p ) ) * sign
        else
            local f = ( style == "fly" ) and cos( 4 * p ) or cos( 2 * p )
            wingA = rad( S.wing or 0 ) * f * sign
        end
    elseif role == "tail" then
        tailA = rad( S.tail or 10 ) * s
    end

    -- whole body: bounce, side-to-side roll (waddle)
    local bob
    if style == "hop" then bob = S.bob * math.max( 0, s )
    elseif style == "fly" then bob = S.bob * s
    else bob = S.bob * abs( c ) end
    local roll = rad( S.roll or 0 ) * s

    return function( x, y, z, normal )
        if role == "leg" and legA ~= 0 then
            if normal then x, y, z = rotX( x, y, z, legA, 0, 0 ) else x, y, z = rotX( x, y, z, legA, piv[2], piv[3] ) end
        elseif role == "wing" and wingA ~= 0 then
            if normal then x, y, z = rotZ( x, y, z, wingA, 0, 0 ) else x, y, z = rotZ( x, y, z, wingA, piv[1], piv[2] ) end
        elseif role == "tail" and tailA ~= 0 then
            if normal then x, y, z = rotY( x, y, z, tailA, 0, 0 ) else x, y, z = rotY( x, y, z, tailA, piv[1], piv[3] ) end
        end
        if roll ~= 0 then x, y, z = rotZ( x, y, z, roll, 0, 0 ) end
        if not normal then y = y + bob end
        return x, y, z
    end
end

-- joint of a group: legs = top (hip), wings = inner edge (shoulder), tail = end nearest the body
local function pivots( src )
    local box = {}
    for _, face in ipairs( src.faces ) do
        local b = box[face.group]
        if not b then b = { 1e9, 1e9, 1e9, -1e9, -1e9, -1e9 }; box[face.group] = b end
        for _, cn in ipairs( face ) do
            local v = src.V[cn[1]]
            for k = 1, 3 do
                if v[k] < b[k] then b[k] = v[k] end
                if v[k] > b[k + 3] then b[k + 3] = v[k] end
            end
        end
    end
    local piv = {}
    for g, b in pairs( box ) do
        local role, side = roleOf( g )
        local cx, cy, cz = ( b[1] + b[4] ) * 0.5, ( b[2] + b[5] ) * 0.5, ( b[3] + b[6] ) * 0.5
        if role == "leg" then piv[g] = { cx, b[5], cz }
        elseif role == "wing" then
            local inner = side == "left" and b[1] or b[4]
            if b[5] - b[2] > b[4] - b[1] then piv[g] = { inner, b[2], cz, upright = true } -- upright wing: joint at its base
            else piv[g] = { inner, cy, cz } end
        elseif role == "tail" then piv[g] = { cx, cy, b[6] } -- animals face +Z: the tail base is on the larger Z side
        else piv[g] = { cx, cy, cz } end
    end
    return piv
end

-- Write N frames; returns the list of file names (in TemporaryDirectory)
local function writeFrames( name, style )
    local files = {}
    local prefix = string.format( "s3danimal_v%d_%s_", VERSION, name )
    for k = 1, FRAMES do files[k] = string.format( "%s%02d.obj", prefix, k ) end
    -- already generated by an earlier run?
    local lastPath = system.pathForFile( files[FRAMES], system.TemporaryDirectory )
    local probe = lastPath and io.open( lastPath, "r" )
    if probe then probe:close(); return files end

    local src = readSource( name )
    local piv = pivots( src )
    -- a vertex (or normal) can belong to several groups: re-index by (index, group) pairs
    local vList, vIndex, nList, nIndex = {}, {}, {}, {}
    local faceLines = {}
    for _, face in ipairs( src.faces ) do
        local g = face.group
        local parts = {}
        for _, cn in ipairs( face ) do
            local vk = cn[1] .. "|" .. g
            local vi = vIndex[vk]
            if not vi then vList[#vList + 1] = { cn[1], g }; vi = #vList; vIndex[vk] = vi end
            local ni = ""
            if cn[3] then
                local nk = cn[3] .. "|" .. g
                ni = nIndex[nk]
                if not ni then nList[#nList + 1] = { cn[3], g }; ni = #nList; nIndex[nk] = ni end
            end
            parts[#parts + 1] = vi .. "/" .. ( cn[2] or "" ) .. "/" .. ni
        end
        faceLines[#faceLines + 1] = "f " .. table.concat( parts, " " )
    end
    local vtText = #src.VT > 0 and ( "vt " .. table.concat( src.VT, "\nvt " ) .. "\n" ) or ""
    local fText = table.concat( faceLines, "\n" ) .. "\n"

    for k = 1, FRAMES do
        local p = 2 * pi * ( k - 1 ) / FRAMES
        local pose = {}
        for g, pv in pairs( piv ) do
            local role, a, b = roleOf( g )
            pose[g] = poseFor( style, role, a, b, pv, p )
        end
        local out = { "# s3d animals: " .. name .. " frame " .. k }
        for i, e in ipairs( vList ) do
            local v = src.V[e[1]]
            local x, y, z = pose[e[2]]( v[1], v[2], v[3], false )
            out[#out + 1] = string.format( "v %.4f %.4f %.4f", x, y, z )
        end
        for i, e in ipairs( nList ) do
            local n = src.VN[e[1]]
            local x, y, z = pose[e[2]]( n[1], n[2], n[3], true )
            out[#out + 1] = string.format( "vn %.4f %.4f %.4f", x, y, z )
        end
        local path = system.pathForFile( files[k], system.TemporaryDirectory )
        local f = assert( io.open( path, "w" ), "animals: cannot write " .. tostring( path ) )
        f:write( table.concat( out, "\n" ), "\n", vtText, fText )
        f:close()
    end
    return files
end
M._writeFrames = writeFrames -- exposed for tests

-- ---------------------------------------------------------------------------------------------
-- Create one animated animal (not positioned yet)
function M.newAnimal( s3d, parent, name, style, opts )
    opts = opts or {}
    local S = STYLES[style] or STYLES.walk
    local files = writeFrames( name, style )
    local speed = opts.speed or 1
    local scale = opts.scale or 0.6
    -- step rate matches the walking speed: (m/s) / (m per cycle) = cycles per second
    local cyclesPerSec = speed / ( S.stride * scale / 0.6 )
    local obj = s3d.loadOBJAnimation( parent, files, {
        baseDir = system.TemporaryDirectory, scale = scale, fps = FRAMES * cyclesPerSec,
        castShadow = opts.castShadow ~= false, autoPlay = false,
    } )
    -- the texture lives in ResourceDirectory but the frames in TemporaryDirectory: set the texture afterwards and rebuild the effect
    obj.fill = { type = "image", filename = TEXTURE }
    obj:setEffect( s3d.EFFECT_LIT )
    obj:play()
    obj.altitude = S.altitude or 0
    return obj
end

-- ---------------------------------------------------------------------------------------------
-- A herd walking on circles around center. opts:
--   center = { x, 0, z }, rings = { { radius = 5.2, dir = 1 }, { radius = 7, dir = -1 } },
--   names = { "cat", "dog", ... } (default: the whole kit; 10 animals on phones), castShadow = true,
--   perFrame = 1 (animals created per frame: loading gradually avoids a freeze at startup)
function M.newHerd( s3d, parent, opts )
    opts = opts or {}
    local center = opts.center or { 0, 0, 0 }
    local rings = opts.rings or { { radius = 5.2, dir = 1 }, { radius = 7, dir = -1 } }
    local list = {}
    local want = {}
    if opts.names then for _, n in ipairs( opts.names ) do want[n] = true end end
    for _, e in ipairs( M.CATALOG ) do
        if not opts.names or want[e[1]] then list[#list + 1] = e end
    end
    local maxCount = opts.max or ( s3d.isMobile and 10 or #list )
    while #list > maxCount do table.remove( list, ( #list % 2 == 0 ) and #list or 1 ) end

    local herd = { members = {}, playing = true }
    -- spread the animals evenly over each ring
    local perRing = {}
    for i = 1, #list do
        local r = ( ( i - 1 ) % #rings ) + 1
        perRing[r] = ( perRing[r] or 0 ) + 1
    end
    local slot = {}
    local queue = {}
    for i, e in ipairs( list ) do
        local r = ( ( i - 1 ) % #rings ) + 1
        slot[r] = ( slot[r] or 0 ) + 1
        queue[#queue + 1] = { entry = e, ring = rings[r], angle = 2 * pi * ( slot[r] - 1 ) / perRing[r] + r * 0.4 }
    end

    local function place( m )
        local R = m.ring.radius
        local x, z = center[1] + R * cos( m.angle ), center[3] + R * sin( m.angle )
        m.obj:moveTo( x, m.obj.altitude, z )
        -- animals face local +Z; turn along the tangent (rotate 180 degrees on clockwise rings)
        local heading = -math.deg( m.angle ) + ( m.ring.dir < 0 and 180 or 0 ) + ( m.yawOffset or 0 )
        m.obj:rotateTo( 0, heading, 0 )
    end

    local function spawn( q )
        local e = q.entry
        local ok, obj = pcall( M.newAnimal, s3d, parent, e[1], e[2], { scale = e[3], speed = e[4], castShadow = opts.castShadow } )
        if not ok then print( "animals: " .. tostring( obj ) ); return end
        local m = { obj = obj, name = e[1], ring = q.ring, angle = q.angle, speed = e[4], yawOffset = e[5] }
        if not herd.playing then obj:pause() end
        herd.members[#herd.members + 1] = m
        place( m )
    end

    local nextIndex = 1
    local perFrame = opts.perFrame or 1
    local function onFrame()
        for _ = 1, perFrame do
            local q = queue[nextIndex]
            if not q then Runtime:removeEventListener( "enterFrame", onFrame ); return end
            nextIndex = nextIndex + 1
            spawn( q )
        end
    end
    Runtime:addEventListener( "enterFrame", onFrame )

    function herd:update( dt )
        if not self.playing then return end
        for _, m in ipairs( self.members ) do
            m.angle = m.angle + m.ring.dir * m.speed / m.ring.radius * dt
            place( m )
        end
    end
    function herd:setPlaying( on )
        self.playing = on and true or false
        for _, m in ipairs( self.members ) do if on then m.obj:resume() else m.obj:pause() end end
    end
    function herd:count() return #self.members, #queue end
    return herd
end

return M
