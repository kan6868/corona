-- Bộ test cho plugin.box3d: các tình huống phổ biến trong sample / unit test của Box3D.
-- Thuần Lua, không dùng display: chạy được trong Solar2D (Box3DDemo, cảnh "Tests") và ngoài Solar2D (tests/run_tests.lua).
--   local tests = require( "box3d_tests" )
--   local results = tests.run( require( "plugin.box3d" ), print )

local M = {}

local DT = 1 / 60
local abs, sqrt, pi = math.abs, math.sqrt, math.pi

local function steps( world, seconds, fn )
    local n = math.floor( seconds / DT + 0.5 )
    for i = 1, n do
        world:step( DT )
        if fn then fn( i * DT ) end
    end
end

local function near( a, b, tol, what )
    if abs( a - b ) > tol then
        error( string.format( "%s: %.4f, mong đợi %.4f ± %.4f", what or "giá trị", a, b, tol ), 0 )
    end
end

local function check( cond, msg ) if not cond then error( msg, 0 ) end end

-- sàn: hộp tĩnh 40 x 2 x 40, mặt trên ở y = 0
local function ground( b3, world, opts )
    local g = world:newBody{ type = "static", position = { 0, -1, 0 } }
    g:addBox( 20, 1, 20, opts )
    return g
end

local list = {}
local function test( name, fn ) list[#list + 1] = { name = name, fn = fn } end

---------------------------------------------------------------------------------------------
test( "Rơi tự do: y = y0 - g t² / 2", function( b3 )
    local w = b3.newWorld{ gravity = { 0, -10, 0 } }
    local b = w:newBody{ position = { 0, 10, 0 } }
    b:addSphere( 0.25 )
    steps( w, 1 )
    local _, y = b:getPosition()
    local _, vy = b:getLinearVelocity()
    near( y, 5, 0.1, "độ cao sau 1 s" )
    near( vy, -10, 0.1, "vận tốc sau 1 s" )
    w:destroy()
end )

test( "Một hộp nằm yên trên sàn rồi ngủ", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local b = w:newBody{ position = { 0, 2, 0 } }
    b:addBox( 0.5, 0.5, 0.5 )
    steps( w, 4 )
    local x, y, z = b:getPosition()
    near( y, 0.5, 0.02, "độ cao tâm hộp" )
    near( x, 0, 0.01, "x" ); near( z, 0, 0.01, "z" )
    check( not b:isAwake(), "hộp vẫn thức sau 4 s" )
    check( w:getAwakeBodyCount() == 0, "còn body thức" )
    w:destroy()
end )

test( "Chồng 10 hộp không đổ", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local boxes = {}
    for i = 1, 10 do
        local b = w:newBody{ position = { 0, 0.5 + ( i - 1 ) * 1.0, 0 } }
        b:addBox( 0.5, 0.5, 0.5 )
        boxes[i] = b
    end
    steps( w, 5 )
    for i, b in ipairs( boxes ) do
        local x, y, z = b:getPosition()
        near( x, 0, 0.05, "x hộp " .. i ); near( z, 0, 0.05, "z hộp " .. i )
        near( y, 0.5 + ( i - 1 ), 0.05, "y hộp " .. i )
    end
    w:destroy()
end )

test( "Kim tự tháp 8 tầng đứng vững", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local base, top = 8
    local start = {}
    for row = 0, base - 1 do
        local n = base - row
        for i = 0, n - 1 do
            local x = ( i - ( n - 1 ) / 2 ) * 1.05
            local b = w:newBody{ position = { x, 0.5 + row, 0 } }
            b:addBox( 0.5, 0.5, 0.5 )
            start[#start + 1] = { b, x, 0.5 + row }
            top = b
        end
    end
    steps( w, 6 )
    for _, s in ipairs( start ) do
        local x, y = s[1]:getPosition()
        near( x, s[2], 0.1, "x" ); near( y, s[3], 0.1, "y" )
    end
    w:destroy()
end )

test( "Nảy: hệ số nảy 0.8 cho độ cao ~0.64 lần", function( b3 )
    local w = b3.newWorld{ gravity = { 0, -10, 0 } }
    ground( b3, w, { restitution = 0 } )
    local r, h0 = 0.25, 4
    local b = w:newBody{ position = { 0, h0 + r, 0 } }
    b:addSphere( r, { restitution = 0.8 } )
    local bounced, peak = false, 0
    steps( w, 3, function()
        local _, y = b:getPosition()
        local _, vy = b:getLinearVelocity()
        if not bounced and vy > 0 then bounced = true end
        if bounced and y - r > peak then peak = y - r end
    end )
    check( bounced, "quả cầu không nảy lên" )
    near( peak / h0, 0.64, 0.1, "tỉ lệ độ cao nảy" )
    w:destroy()
end )

test( "Ma sát: dốc 20°, μ = 0.6 đứng yên, μ = 0.05 trượt", function( b3 )
    local w = b3.newWorld()
    local qx, qy, qz, qw = b3.quatFromEuler( 0, 0, 20 )
    local slope = w:newBody{ type = "static", rotation = { qx, qy, qz, qw } }
    slope:addBox( 10, 0.25, 3, { friction = 1 } )
    local function onSlope( mu, z )
        local nx, ny = b3.rotate( qx, qy, qz, qw, 0, 1, 0 )
        local b = w:newBody{ position = { nx * 0.8, ny * 0.8, z }, rotation = { qx, qy, qz, qw } }
        b:addBox( 0.5, 0.5, 0.5, { friction = mu } )
        return b
    end
    local sticky, slippy = onSlope( 0.6, -1.5 ), onSlope( 0.05, 1.5 )
    w:step( DT ); w:step( DT ) -- hai hộp chạm dốc
    for _ = 1, 10 do w:step( DT ) end
    local sx, sy = sticky:getPosition()
    local px, py = slippy:getPosition()
    steps( w, 2 )
    local sx2, sy2 = sticky:getPosition()
    local px2, py2 = slippy:getPosition()
    check( sqrt( ( sx2 - sx ) ^ 2 + ( sy2 - sy ) ^ 2 ) < 0.05, "hộp μ = 0.6 bị trượt" )
    check( sqrt( ( px2 - px ) ^ 2 + ( py2 - py ) ^ 2 ) > 1.0, "hộp μ = 0.05 không trượt" )
    w:destroy()
end )

test( "Con lắc (revolute): giữ khoảng cách, chu kỳ ≈ 2π√(L/g)", function( b3 )
    local w = b3.newWorld{ gravity = { 0, -10, 0 } }
    local pivot = w:newBody{ type = "static", position = { 0, 5, 0 } }
    local L = 2
    local bob = w:newBody{ position = { L, 5, 0 }, angularDamping = 0, linearDamping = 0 }
    bob:addSphere( 0.05, { density = 1000 } )
    local j = w:newRevoluteJoint{ bodyA = pivot, bodyB = bob, anchor = { 0, 5, 0 }, axis = { 0, 0, 1 } }
    check( j:getType() == "revolute", "sai loại joint" )
    local maxErr, crossings, firstT, lastT, prevX = 0, 0, nil, nil, nil
    steps( w, 6, function( t )
        local x, y = bob:getPosition()
        local d = sqrt( x * x + ( y - 5 ) ^ 2 )
        maxErr = math.max( maxErr, abs( d - L ) )
        if prevX and prevX > 0 and x <= 0 then
            crossings = crossings + 1
            firstT = firstT or t
            lastT = t
        end
        prevX = x
    end )
    check( maxErr < 0.02, string.format( "khoảng cách lệch %.3f m", maxErr ) )
    check( crossings >= 2, "con lắc không dao động" )
    -- biên độ 90° nên chu kỳ dài hơn công thức góc nhỏ khoảng 18%
    local period = ( lastT - firstT ) / ( crossings - 1 )
    near( period, 2 * pi * sqrt( L / 10 ) * 1.18, 0.2, "chu kỳ (s)" )
    w:destroy()
end )

test( "Motor revolute: 90°/s trong 2 s quay ~180°", function( b3 )
    local w = b3.newWorld{ gravity = { 0, 0, 0 } }
    local base = w:newBody{ type = "static" }
    local wheel = w:newBody{ position = { 0, 0, 0 } }
    wheel:addCylinder( 0.2, 1 )
    local j = w:newRevoluteJoint{ bodyA = base, bodyB = wheel, anchor = { 0, 0, 0 }, axis = { 0, 1, 0 },
        enableMotor = true, motorSpeed = 90, maxMotorTorque = 1e6 } -- bánh 628 kg: torque lớn để lên tốc ngay
    local total, prev = 0, j:getAngle()
    steps( w, 2, function()
        local a = j:getAngle()
        local d = a - prev
        if d > 180 then d = d - 360 elseif d < -180 then d = d + 360 end
        total, prev = total + d, a
    end )
    near( abs( total ), 180, 6, "góc quay (độ)" )
    w:destroy()
end )

test( "Prismatic: rơi theo trục và dừng ở giới hạn dưới", function( b3 )
    local w = b3.newWorld{ gravity = { 0, -10, 0 } }
    local base = w:newBody{ type = "static", position = { 0, 3, 0 } }
    local slider = w:newBody{ position = { 0, 3, 0 } }
    slider:addBox( 0.2, 0.2, 0.2 )
    local j = w:newPrismaticJoint{ bodyA = base, bodyB = slider, anchor = { 0, 3, 0 }, axis = { 0, 1, 0 },
        enableLimit = true, lowerTranslation = -1, upperTranslation = 0 }
    steps( w, 2 )
    local x, y, z = slider:getPosition()
    near( j:getTranslation(), -1, 0.02, "độ dời" )
    near( y, 2, 0.02, "y" ); near( x, 0, 0.01, "x" ); near( z, 0, 0.01, "z" )
    w:destroy()
end )

test( "Distance joint giữ độ dài", function( b3 )
    local w = b3.newWorld()
    local a = w:newBody{ type = "static", position = { 0, 6, 0 } }
    local b = w:newBody{ position = { 1.5, 6, 0.5 } }
    b:addSphere( 0.2 )
    w:newDistanceJoint{ bodyA = a, bodyB = b }
    local len = sqrt( 1.5 ^ 2 + 0.5 ^ 2 )
    local maxErr = 0
    steps( w, 3, function()
        local x, y, z = b:getPosition()
        maxErr = math.max( maxErr, abs( sqrt( x * x + ( y - 6 ) ^ 2 + z * z ) - len ) )
    end )
    check( maxErr < 0.02, string.format( "độ dài lệch %.3f m", maxErr ) )
    w:destroy()
end )

test( "Weld joint: hai hộp hàn rơi như một khối", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local a = w:newBody{ position = { 0, 3, 0 } }
    a:addBox( 0.5, 0.5, 0.5 )
    local b = w:newBody{ position = { 1, 3, 0 } }
    b:addBox( 0.5, 0.5, 0.5 )
    w:newWeldJoint{ bodyA = a, bodyB = b, anchor = { 0.5, 3, 0 } }
    a:applyAngularImpulse( 0, 0, 2 )
    local maxErr = 0
    steps( w, 3, function()
        local ax, ay, az = a:getPosition()
        local bx, by, bz = b:getPosition()
        maxErr = math.max( maxErr, abs( sqrt( ( bx - ax ) ^ 2 + ( by - ay ) ^ 2 + ( bz - az ) ^ 2 ) - 1 ) )
    end )
    check( maxErr < 0.02, string.format( "khoảng cách hai hộp lệch %.3f m", maxErr ) )
    w:destroy()
end )

test( "Xích 10 mắt (spherical) không đứt", function( b3 )
    local w = b3.newWorld()
    local anchor = w:newBody{ type = "static", position = { 0, 10, 0 } }
    local prev, n, seg = anchor, 10, 0.5
    local links = {}
    for i = 1, n do
        local b = w:newBody{ position = { i * seg - seg / 2, 10, 0 } }
        b:addCapsule( -seg / 2 + 0.05, 0, 0, seg / 2 - 0.05, 0, 0, 0.05 )
        w:newSphericalJoint{ bodyA = prev, bodyB = b, anchor = { ( i - 1 ) * seg, 10, 0 } }
        links[i], prev = b, b
    end
    local maxD, minY = 0, 10
    steps( w, 4, function()
        local x, y, z = links[n]:getWorldPoint( seg / 2, 0, 0 )
        maxD = math.max( maxD, sqrt( x * x + ( y - 10 ) ^ 2 + z * z ) )
        minY = math.min( minY, y )
    end )
    check( maxD <= n * seg * 1.02, string.format( "đầu xích cách điểm neo %.2f m > %.2f m", maxD, n * seg ) )
    check( minY < 10 - n * seg * 0.8, string.format( "xích không rủ xuống (thấp nhất y = %.2f)", minY ) )
    w:destroy()
end )

test( "Raycast trúng mặt trên của hộp", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local box = w:newBody{ type = "static", position = { 2, 0.5, 0 } }
    box:addBox( 0.5, 0.5, 0.5 )
    box.tag = "target"
    local hit = w:rayCast( 2, 5, 0, 0, -10, 0 )
    check( hit, "không trúng gì" )
    check( hit.body == box and hit.body.tag == "target", "trúng sai body" )
    near( hit.y, 1, 0.001, "điểm trúng y" ); near( hit.ny, 1, 0.001, "pháp tuyến y" )
    near( hit.fraction, 0.4, 0.001, "fraction" )
    check( w:rayCast( 30, 5, 0, 0, -10, 0 ) == nil, "tia ngoài sàn vẫn trúng" )
    w:destroy()
end )

test( "Sự kiện va chạm: began khi hộp chạm sàn", function( b3 )
    local w = b3.newWorld()
    local g = ground( b3, w )
    local b = w:newBody{ position = { 0, 1, 0 } }
    b:addBox( 0.25, 0.25, 0.25, { hitEvents = true } )
    local began, hit = 0, 0
    steps( w, 1.5, function()
        local ev = w:getContactEvents()
        for _, e in ipairs( ev.began ) do
            if ( e.bodyA == b and e.bodyB == g ) or ( e.bodyA == g and e.bodyB == b ) then began = began + 1 end
        end
        hit = hit + #ev.hit
    end )
    check( began >= 1, "không có sự kiện began" )
    check( hit >= 1, "không có sự kiện hit" )
    w:destroy()
end )

test( "Move events: body rơi báo vị trí, rồi báo ngủ", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local b = w:newBody{ position = { 0, 1, 0 } }
    b:addSphere( 0.5 )
    local moves, slept = 0, false
    steps( w, 5, function()
        for _, e in ipairs( w:getMoveEvents() ) do
            if e.body == b then
                moves = moves + 1
                if e.sleep then slept = true end
            end
        end
    end )
    check( moves > 10, "ít move event" )
    check( slept, "không có sự kiện ngủ" )
    w:destroy()
end )

test( "Nổ đẩy các hộp ra xa tâm", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local boxes = {}
    for i = 0, 3 do
        local a = i * pi / 2
        local b = w:newBody{ position = { 1.5 * math.cos( a ), 0.5, 1.5 * math.sin( a ) } }
        b:addBox( 0.5, 0.5, 0.5 )
        boxes[#boxes + 1] = b
    end
    steps( w, 0.5 )
    -- hộp 1 m³ nặng 1000 kg; mặt hướng về tâm 1 m²: 5000 N·s/m² cho Δv ≈ 5 m/s
    w:explode{ position = { 0, 0.5, 0 }, radius = 5, falloff = 2, impulsePerArea = 5000 }
    steps( w, 0.5 )
    for i, b in ipairs( boxes ) do
        local x, _, z = b:getPosition()
        check( sqrt( x * x + z * z ) > 2.0, "hộp " .. i .. " không bị đẩy ra" )
    end
    w:destroy()
end )

test( "Va chạm liên tục: viên đạn không xuyên tường mỏng", function( b3 )
    local w = b3.newWorld{ gravity = { 0, 0, 0 } }
    local wall = w:newBody{ type = "static", position = { 5, 0, 0 } }
    wall:addBox( 0.05, 3, 3 )
    local bullet = w:newBody{ position = { 0, 0, 0 }, linearVelocity = { 400, 0, 0 }, bullet = true }
    bullet:addSphere( 0.1 )
    steps( w, 0.5 )
    local x = bullet:getPosition()
    check( x < 5, string.format( "đạn xuyên tường: x = %.2f", x ) )
    w:destroy()
end )

test( "Body kinematic đi đúng vận tốc và đẩy hộp", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local pusher = w:newBody{ type = "kinematic", position = { -3, 0.5, 0 }, linearVelocity = { 2, 0, 0 } }
    pusher:addBox( 0.5, 0.5, 0.5 )
    local box = w:newBody{ position = { 0, 0.5, 0 } }
    box:addBox( 0.5, 0.5, 0.5 )
    steps( w, 3 )
    local px = pusher:getPosition()
    local bx = box:getPosition()
    near( px, 3, 0.05, "x của body kinematic" )
    check( bx > 3.5, string.format( "hộp không bị đẩy: x = %.2f", bx ) )
    w:destroy()
end )

test( "Mesh tam giác: quả cầu nằm trên lưới", function( b3 )
    local w = b3.newWorld()
    local verts, idx = {}, {}
    local n, size = 8, 2
    for j = 0, n do for i = 0, n do
        verts[#verts + 1] = ( i - n / 2 ) * size; verts[#verts + 1] = 0; verts[#verts + 1] = ( j - n / 2 ) * size
    end end
    for j = 0, n - 1 do for i = 0, n - 1 do
        local a = j * ( n + 1 ) + i + 1
        local b, c, d = a + 1, a + n + 1, a + n + 2
        idx[#idx + 1] = a; idx[#idx + 1] = c; idx[#idx + 1] = b
        idx[#idx + 1] = b; idx[#idx + 1] = c; idx[#idx + 1] = d
    end end
    local g = w:newBody{ type = "static" }
    g:addMesh( verts, idx )
    local ball = w:newBody{ position = { 0.3, 3, 0.7 } }
    ball:addSphere( 0.5 )
    steps( w, 3 )
    local _, y = ball:getPosition()
    near( y, 0.5, 0.02, "tâm quả cầu" )
    w:destroy()
end )

test( "Hull: tứ diện, trụ, capsule nằm yên", function( b3 )
    local w = b3.newWorld()
    ground( b3, w )
    local tet = w:newBody{ position = { -2, 2, 0 } }
    tet:addHull{ { 0, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } }
    local cyl = w:newBody{ position = { 0, 2, 0 } }
    cyl:addCylinder( 1, 0.4 )
    local cap = w:newBody{ position = { 2, 2, 0 } }
    cap:addCapsule( -0.5, 0, 0, 0.5, 0, 0, 0.3 )
    steps( w, 5 )
    local _, ty = tet:getPosition()
    local _, cy = cyl:getPosition()
    local _, py = cap:getPosition()
    check( ty > -0.01 and ty < 0.5, "tứ diện rơi sai: y = " .. ty )
    near( cy, 0.5, 0.03, "tâm trụ đứng" )
    near( py, 0.3, 0.03, "tâm capsule nằm" )
    check( w:getAwakeBodyCount() == 0, "còn vật chưa ngủ" )
    w:destroy()
end )

test( "Tất định: hai world giống nhau cho kết quả giống hệt", function( b3 )
    local function run()
        local w = b3.newWorld()
        ground( b3, w )
        local last
        for i = 1, 20 do
            local b = w:newBody{ position = { ( i % 3 ) * 0.3, 1 + i * 1.1, ( i % 2 ) * 0.2 }, angles = { i * 7, i * 11, i * 13 } }
            if i % 2 == 0 then b:addBox( 0.4, 0.3, 0.5 ) else b:addSphere( 0.4 ) end
            last = b
        end
        steps( w, 3 )
        local r = { last:getTransform() }
        w:destroy()
        return r
    end
    local a, b = run(), run()
    for i = 1, 7 do check( a[i] == b[i], "khác nhau ở thành phần " .. i ) end
end )

test( "Field riêng, huỷ body, isValid", function( b3 )
    local w = b3.newWorld()
    local b = w:newBody{ position = { 0, 1, 0 } }
    b:addSphere( 0.5 )
    b.name = "ball"
    check( b.name == "ball", "không giữ field riêng" )
    check( b:isValid(), "body mới không hợp lệ" )
    check( #w:getBodies() == 1, "getBodies sai" )
    b:destroy()
    check( not b:isValid(), "body đã huỷ vẫn hợp lệ" )
    check( #w:getBodies() == 0, "getBodies còn body đã huỷ" )
    check( not pcall( b.getPosition, b ), "gọi hàm trên body đã huỷ không báo lỗi" )
    w:destroy()
    check( not pcall( w.step, w, DT ), "step world đã huỷ không báo lỗi" )
end )

---------------------------------------------------------------------------------------------

M.list = list

-- chạy hết: trả về số đạt, số lỗi, danh sách kết quả
function M.run( b3, log )
    log = log or print
    local passed, failed, results = 0, 0, {}
    for i, t in ipairs( list ) do
        local t0 = os.clock()
        local ok, err = pcall( t.fn, b3 )
        local ms = ( os.clock() - t0 ) * 1000
        results[i] = { name = t.name, ok = ok, err = err, ms = ms }
        if ok then passed = passed + 1 else failed = failed + 1 end
        log( string.format( "%s %2d. %s (%.0f ms)%s", ok and "ĐẠT" or "LỖI", i, t.name, ms, ok and "" or ( "\n        " .. tostring( err ) ) ) )
    end
    log( string.format( "Kết quả: %d đạt, %d lỗi / %d test", passed, failed, #list ) )
    return passed, failed, results
end

return M
