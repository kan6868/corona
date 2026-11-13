-- camera3d: camera quay quanh điểm đích cho plugin.s3d.
--   kéo: xoay · D-pad góc dưới trái / phím mũi tên, WASD: dời điểm đích · con lăn, nút + −, phím + −: zoom
--   chạm (không kéo): gọi opts.onTap( x, y )
local M = {}
M.__index = M

function M.new( opts )
    local self = setmetatable( {}, M )
    self.s3d = opts.s3d
    self.yaw, self.pitch, self.dist = opts.yaw or 30, opts.pitch or 22, opts.dist or 14
    self.target = opts.target or { 0, 1, 0 }
    self.minDist, self.maxDist = opts.minDist or 2.5, opts.maxDist or 60
    self.onTap = opts.onTap
    self.onChange = opts.onChange
    self.held = {}
    self:apply()
    self:_touch()
    self:_pad( opts.ui )
    self:_wheel()
    self:_keys()
    return self
end

function M:set( yaw, pitch, dist, target )
    self.yaw, self.pitch, self.dist = yaw or self.yaw, pitch or self.pitch, dist or self.dist
    if target then self.target = { target[1], target[2], target[3] } end
    self:apply()
end

function M:apply()
    self.s3d.orbit( self.yaw, self.pitch, self.dist, self.target )
    if self.onChange then self.onChange( self ) end
end

function M:zoom( f )
    self.dist = math.max( self.minDist, math.min( self.maxDist, self.dist * f ) )
    self:apply()
end

-- gọi mỗi frame
function M:update( dt )
    local mx, mz = 0, 0
    for _, v in pairs( self.held ) do mx, mz = mx + v[1], mz + v[2] end
    if self.zooming then self:zoom( 1 + ( self.zooming - 1 ) * 0.25 ) end
    if mx == 0 and mz == 0 then return end
    local y = math.rad( self.yaw )
    local fx, fz = -math.sin( y ), -math.cos( y )
    local rx, rz = math.cos( y ), -math.sin( y )
    local sp = self.dist * 0.6 * dt
    self.target[1] = self.target[1] + ( fx * mz + rx * mx ) * sp
    self.target[3] = self.target[3] + ( fz * mz + rz * mx ) * sp
    self:apply()
end

function M:_touch()
    local drag
    Runtime:addEventListener( "touch", function( e )
        if e.phase == "began" then
            drag = { x = e.x, y = e.y, yaw = self.yaw, pitch = self.pitch, moved = false }
        elseif e.phase == "moved" and drag then
            if math.abs( e.x - drag.x ) + math.abs( e.y - drag.y ) > 6 then drag.moved = true end
            self.yaw = drag.yaw - ( e.x - drag.x ) * 0.3
            self.pitch = math.max( 3, math.min( 85, drag.pitch + ( e.y - drag.y ) * 0.3 ) )
            self:apply()
        elseif ( e.phase == "ended" or e.phase == "cancelled" ) and drag then
            if not drag.moved and e.phase == "ended" and self.onTap then self.onTap( e.x, e.y ) end
            drag = nil
        end
        return true
    end )
end

local function button( parent, x, y, size, label, fontSize )
    local bg = display.newRoundedRect( parent, x, y, size, size, 12 )
    bg:setFillColor( 0, 0, 0, 0.45 )
    display.newText{ parent = parent, text = label, x = x, y = y, font = native.systemFontBold, fontSize = fontSize or 28 }
    return bg
end

function M:_pad( ui )
    local L = display.screenOriginX
    local B = display.screenOriginY + display.actualContentHeight
    local cx, cy, step, size = L + 110, B - 110, 74, 66
    for _, d in ipairs{ { "up", "▲", 0, -1, 0, 1 }, { "down", "▼", 0, 1, 0, -1 }, { "left", "◀", -1, 0, -1, 0 }, { "right", "▶", 1, 0, 1, 0 } } do
        local id, ox, oy, mx, mz = d[1], d[3], d[4], d[5], d[6]
        local bg = button( ui, cx + ox * step, cy + oy * step, size, d[2] )
        bg:addEventListener( "touch", function( e )
            if e.phase == "began" then
                display.getCurrentStage():setFocus( bg, e.id )
                self.held[id] = { mx, mz }
                bg:setFillColor( 0.15, 0.45, 0.3, 0.85 )
            elseif e.phase == "ended" or e.phase == "cancelled" then
                display.getCurrentStage():setFocus( bg, nil )
                self.held[id] = nil
                bg:setFillColor( 0, 0, 0, 0.45 )
            end
            return true
        end )
    end
    for i, z in ipairs{ { "+", 1 / 1.15 }, { "−", 1.15 } } do
        local bg = button( ui, cx + 3 * step - 10, cy + ( i == 1 and -step or step ) * 0.5, size, z[1], 34 )
        bg:addEventListener( "touch", function( e )
            if e.phase == "began" then
                display.getCurrentStage():setFocus( bg, e.id ); self.zooming = z[2]
            elseif e.phase == "ended" or e.phase == "cancelled" then
                display.getCurrentStage():setFocus( bg, nil ); self.zooming = nil
            end
            return true
        end )
    end
end

function M:_wheel()
    Runtime:addEventListener( "mouse", function( e )
        if e.type == "scroll" and e.scrollY and e.scrollY ~= 0 then self:zoom( e.scrollY > 0 and 1.15 or 1 / 1.15 ) end
    end )
end

function M:_keys()
    local KEYS = { up = "up", w = "up", down = "down", s = "down", left = "left", a = "left", right = "right", d = "right" }
    local DIR = { up = { 0, 1 }, down = { 0, -1 }, left = { -1, 0 }, right = { 1, 0 } }
    Runtime:addEventListener( "key", function( e )
        local d = KEYS[e.keyName]
        if d then
            self.held["key_" .. d] = ( e.phase == "down" ) and DIR[d] or nil
            return true
        end
        if e.phase == "down" then
            if e.keyName == "=" or e.keyName == "+" or e.keyName == "numPad+" then self:zoom( 1 / 1.15 ); return true end
            if e.keyName == "-" or e.keyName == "numPad-" then self:zoom( 1.15 ); return true end
        end
        return false
    end )
end

return M
