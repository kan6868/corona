-- Box3DDemo: plugin.box3d (vật lý 3D, Box3D của Erin Catto) + plugin.s3d (vẽ 3D) trên Solar2D.
-- Các cảnh theo những sample phổ biến của Box3D; cảnh cuối chạy 22 bài test tự động.
display.setStatusBar( display.HiddenStatusBar )

-- Trong Simulator Windows: nạp plugin.s3d.dll / plugin.box3d.dll đặt ngay trong thư mục project (chép sang thư mục
-- tạm để file trong project không bị khoá). Không có file thì dùng plugin cài qua build.settings (Android cũng vậy).
do
    local main = system.pathForFile( "main.lua", system.ResourceDirectory )
    local dir = main and main:match( "^(.*)[/\\]main%.lua$" )
    local okLfs, lfs = pcall( require, "lfs" )
    if dir and okLfs then
        local tmp = system.pathForFile( "", system.TemporaryDirectory ) .. "\\plug_" .. os.time() .. "_" .. math.random( 100000 )
        local made = false
        for _, name in ipairs{ "s3d", "box3d" } do
            local src = io.open( dir .. "\\plugin." .. name .. ".dll", "rb" )
            if src then
                local data = src:read( "*a" ); src:close()
                if not made then lfs.mkdir( tmp ); lfs.mkdir( tmp .. "\\plugin" ); made = true end
                local out = io.open( tmp .. "\\plugin\\" .. name .. ".dll", "wb" )
                if out then out:write( data ); out:close() end
            end
        end
        if made then package.cpath = tmp .. "\\?.dll;" .. package.cpath end
    end
end

local s3d = require( "plugin.s3d" )
local box3d = require( "plugin.box3d" )
local phys3d = require( "phys3d" )
local camera3d = require( "camera3d" )
local scenes = require( "scenes" )
print( "s3d " .. tostring( s3d.version ) .. ", box3d " .. tostring( box3d.version ) )

local SKY = { 0.62, 0.76, 0.9 }
local post = s3d.newPostProcess{ clearColor = SKY, fxaa = true, exposure = 1.05, contrast = 1.08, saturation = 1.1, vignette = 0.3 }
s3d.setLight{ direction = { -0.5, -1, -0.35 }, ambient = 0.38, color = { 1, 0.97, 0.9 } }
s3d.setShadowMap{ size = s3d.isMobile and 1024 or 2048, quality = s3d.isMobile and "medium" or "high",
    area = 16, distance = 40, bias = 0.0015, darkness = 0.65 }
s3d.setFog{ color = SKY, start = 25, ["end"] = 70 }

-- UI 2D (ngoài hậu kỳ)
local ui = display.newGroup()
local L, T = display.screenOriginX, display.screenOriginY
local W = display.actualContentWidth
local title = display.newText{ parent = ui, text = "", x = L + 16, y = T + 22, font = native.systemFontBold, fontSize = 22 }
title.anchorX = 0
local info = display.newText{ parent = ui, text = "", x = L + 16, y = T + 50, width = W * 0.62, font = native.systemFont, fontSize = 15 }
info.anchorX, info.anchorY = 0, 0
local stats = display.newText{ parent = ui, text = "", x = L + 16, y = T + 100, font = native.systemFont, fontSize = 15 }
stats.anchorX, stats.anchorY = 0, 0

local function makeButton( parent, x, y, w, label, fn, color )
    local g = display.newGroup(); parent:insert( g )
    local bg = display.newRoundedRect( g, x, y, w, 44, 10 )
    local c = color or { 0.15, 0.45, 0.3, 0.85 }
    bg:setFillColor( unpack( c ) )
    local txt = display.newText{ parent = g, text = label, x = x, y = y, font = native.systemFontBold, fontSize = 17 }
    bg:addEventListener( "touch", function( e )
        if e.phase == "began" then bg:setFillColor( c[1] + 0.15, c[2] + 0.15, c[3] + 0.15, c[4] )
        elseif e.phase == "ended" or e.phase == "cancelled" then
            bg:setFillColor( unpack( c ) )
            if e.phase == "ended" then fn() end
        end
        return true
    end )
    g.setLabel = function( _, s ) txt.text = s end
    return g
end

-- trạng thái cảnh
local current, index, env, scene, sceneGroup, sceneButtons, report = nil, 1, nil, nil, nil, nil, nil
local cam

local function clearScene()
    if sceneButtons then sceneButtons:removeSelf(); sceneButtons = nil end
    if report then report:removeSelf(); report = nil end
    if env then env:destroy(); env = nil end
    if sceneGroup then sceneGroup:removeSelf(); sceneGroup = nil end
end

local function load( i )
    clearScene()
    index = ( i - 1 ) % #scenes + 1
    current = scenes[index]
    sceneGroup = display.newGroup()
    post.scene:insert( sceneGroup )
    env = phys3d.new{ s3d = s3d, box3d = box3d, scene = sceneGroup }
    sceneButtons = display.newGroup(); ui:insert( sceneButtons )
    local bx = L + W - 80
    local ctx = {
        box3d = box3d,
        button = function( label, fn )
            makeButton( sceneButtons, bx, T + 96, 140, label, fn, { 0.55, 0.3, 0.15, 0.9 } )
            bx = bx - 150
        end,
        report = function( lines, allOk )
            report = display.newGroup(); ui:insert( report )
            local bg = display.newRoundedRect( report, display.contentCenterX, display.contentCenterY + 30, W * 0.8, 560, 14 )
            bg:setFillColor( 0, 0, 0, 0.75 )
            local t = display.newText{ parent = report, text = table.concat( lines, "\n" ), x = display.contentCenterX,
                y = display.contentCenterY + 30, width = W * 0.76, font = native.systemFont, fontSize = 15 }
            t:setFillColor( allOk and 0.75 or 1, 1, allOk and 0.8 or 0.6 )
        end,
    }
    math.randomseed( 1234 )
    scene = current.build( env, ctx ) or {}
    title.text = string.format( "%d/%d  %s", index, #scenes, current.name )
    info.text = current.info .. "\nKéo: xoay · nút/phím mũi tên: đi · cuộn/±: zoom · chạm: ném bóng"
    if scene.camera then cam:set( unpack( scene.camera ) ) end
end

cam = camera3d.new{
    s3d = s3d, ui = ui, yaw = 30, pitch = 20, dist = 20, target = { 0, 2, 0 },
    onTap = function( x, y ) if env and not ( scene and scene.noPhysics ) then env:shoot( x, y, 25, 0.3 ) end end,
    onChange = function( c ) s3d.setShadowMap{ center = { c.target[1], 0, c.target[3] } } end,
}

-- nút chung
do
    local y = T + 40
    makeButton( ui, L + W - 70, y, 120, "Cảnh ▶", function() load( index + 1 ) end )
    makeButton( ui, L + W - 200, y, 120, "◀ Cảnh", function() load( index - 1 ) end )
    makeButton( ui, L + W - 330, y, 120, "Làm lại", function() load( index ) end )
    local pauseBtn
    pauseBtn = makeButton( ui, L + W - 460, y, 120, "Dừng", function()
        if not env then return end
        env.paused = not env.paused
        pauseBtn:setLabel( env.paused and "Chạy" or "Dừng" )
    end )
    makeButton( ui, L + W - 590, y, 120, "1 bước", function() if env then env.paused = true; pauseBtn:setLabel( "Chạy" ); env:stepOnce() end end )
end

local frames, lastFps, fps, prev = 0, system.getTimer(), 0, nil
local stepMs = 0
Runtime:addEventListener( "enterFrame", function( e )
    local dt = prev and math.min( 0.1, ( e.time - prev ) / 1000 ) or 0
    prev = e.time
    cam:update( dt )
    if env and not ( scene and scene.noPhysics ) then
        local t0 = system.getTimer()
        local n = env:update( e.time )
        if n > 0 then stepMs = stepMs * 0.9 + ( system.getTimer() - t0 ) / n * 0.1 end
        if scene.update and not env.paused then scene.update( dt ) end
    end
    frames = frames + 1
    if e.time - lastFps > 500 then
        fps = frames * 1000 / ( e.time - lastFps )
        frames, lastFps = 0, e.time
        if env then
            stats.text = string.format( "fps %.0f · body %d · đang thức %d · bước %.2f ms", fps, #env.world:getBodies(),
                env.world:getAwakeBodyCount(), stepMs )
        end
    end
end )

load( 1 )
