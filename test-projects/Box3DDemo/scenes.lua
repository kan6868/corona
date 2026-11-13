-- Các cảnh demo, theo những sample phổ biến của Box3D (samples/sample_*.cpp).
-- Mỗi cảnh: build( env, ctx ) -> { camera = { yaw, pitch, dist, target }, update = function( dt ) end }
--   env: phys3d (env.world là world của plugin.box3d)
--   ctx.button( label, fn ): thêm nút riêng của cảnh · ctx.info( text ): dòng mô tả · ctx.box3d: plugin
local S = {}

local CRATE = { texture = "assets/crate.png", color = { 1, 1, 1 } }
local function col( r, g, b ) return { color = { r, g, b } } end
local PALETTE = { { 0.9, 0.35, 0.3 }, { 0.3, 0.65, 0.9 }, { 0.95, 0.8, 0.3 }, { 0.45, 0.8, 0.45 }, { 0.75, 0.5, 0.9 }, { 0.95, 0.6, 0.3 } }
local function pal( i ) return col( unpack( PALETTE[( i - 1 ) % #PALETTE + 1] ) ) end

local function add( name, info, build ) S[#S + 1] = { name = name, info = info, build = build } end

---------------------------------------------------------------------------------------------
add( "Box Stack", "Stacking / Box Stack: 15 hộp chồng thẳng. Bấm vào cảnh để ném bóng.", function( env )
    env:ground( 20 )
    for i = 1, 15 do
        env:box( 0.5, 0.5, 0.5, { position = { 0, 0.5 + ( i - 1 ) * 1.0, 0 } }, { rollingResistance = 0.1 }, CRATE )
    end
    return { camera = { 35, 15, 22, { 0, 6, 0 } } }
end )

add( "Pyramid", "Stacking / Large Pyramid (thu nhỏ): kim tự tháp 12 tầng, 78 hộp.", function( env )
    env:ground( 20 )
    local base = 12
    for row = 0, base - 1 do
        local n = base - row
        for i = 0, n - 1 do
            env:box( 0.5, 0.5, 0.5, { position = { ( i - ( n - 1 ) / 2 ) * 1.05, 0.5 + row, 0 } }, nil, CRATE )
        end
    end
    return { camera = { 25, 18, 24, { 0, 5, 0 } } }
end )

add( "Jenga", "Stacking / Jenga Stack: 14 tầng, mỗi tầng 3 thanh xoay 90°. Ném bóng để rút thanh.", function( env )
    env:ground( 20 )
    local hx, hy, hz, gap = 1.5, 0.3, 0.5, 0.02
    for layer = 0, 13 do
        local y = hy + layer * ( 2 * hy + 0.005 )
        for k = -1, 1 do
            local off = k * ( 2 * hz + gap )
            if layer % 2 == 0 then
                env:box( hx, hy, hz, { position = { 0, y, off } }, { friction = 0.6 }, CRATE )
            else
                env:box( hz, hy, hx, { position = { off, y, 0 } }, { friction = 0.6 }, CRATE )
            end
        end
    end
    return { camera = { 40, 20, 18, { 0, 4, 0 } } }
end )

add( "Dominoes", "Stacking / Dominoes: 40 quân bài xếp vòng cung, tự đẩy quân đầu sau 1 giây.", function( env, ctx )
    env:ground( 25 )
    local first
    local n, radius = 40, 9
    for i = 0, n - 1 do
        local a = math.pi * 1.4 * i / ( n - 1 ) - math.pi * 0.2
        local x, z = radius * math.cos( a ), radius * math.sin( a ) - 4
        -- trục mỏng (X cục bộ) nằm theo tiếp tuyến của cung: xoay quanh Y một góc 90° - a
        local _, b = env:box( 0.08, 0.6, 0.3, { position = { x, 0.6, z }, angles = { 0, 90 - math.deg( a ), 0 } },
            { friction = 0.6 }, pal( i + 1 ) )
        first = first or b
    end
    local t = 0
    local pushed = false
    local function push()
        -- X cục bộ = (sin a, 0, -cos a), ngược chiều quân kế tiếp: đẩy theo -X, vào gần đỉnh quân bài
        local qx, qy, qz, qw = first:getRotation()
        local nx, _, nz = ctx.box3d.rotate( qx, qy, qz, qw, 1, 0, 0 )
        local x, y, z = first:getPosition()
        first:applyImpulse( -nx * 60, 0, -nz * 60, x, y + 0.45, z )
    end
    ctx.button( "Đẩy", push )
    return {
        camera = { 0, 40, 26, { 0, 0, 0 } },
        update = function( dt )
            t = t + dt
            if not pushed and t > 1 then pushed = true; push() end
        end,
    }
end )

add( "Bridge", "Joints / Bridge: 24 tấm ván nối bằng revolute joint giữa hai trụ.", function( env, ctx )
    env:ground( 25 )
    local n, w, y = 24, 0.5, 4
    local x0 = -n * w / 2
    local _, left = env:box( 0.3, y / 2, 1.2, { type = "static", position = { x0 - 0.3, y / 2, 0 } }, nil, col( 0.5, 0.5, 0.55 ) )
    local _, right = env:box( 0.3, y / 2, 1.2, { type = "static", position = { -x0 + 0.3, y / 2, 0 } }, nil, col( 0.5, 0.5, 0.55 ) )
    local prev = left
    for i = 0, n - 1 do
        local _, plank = env:box( w / 2 - 0.01, 0.08, 1, { position = { x0 + w * ( i + 0.5 ), y - 0.08, 0 } }, { density = 500 }, CRATE )
        env.world:newRevoluteJoint{ bodyA = prev, bodyB = plank, anchor = { x0 + w * i, y - 0.08, 0 }, axis = { 0, 0, 1 } }
        prev = plank
    end
    env.world:newRevoluteJoint{ bodyA = prev, bodyB = right, anchor = { -x0, y - 0.08, 0 }, axis = { 0, 0, 1 } }
    local function drop()
        for k = 1, 6 do
            local x = ( math.random() - 0.5 ) * n * w * 0.7
            if k % 2 == 0 then
                env:sphere( 0.35, { position = { x, y + 3 + k, ( math.random() - 0.5 ) } }, nil, pal( k ) )
            else
                env:box( 0.35, 0.35, 0.35, { position = { x, y + 3 + k, ( math.random() - 0.5 ) } }, nil, pal( k ) )
            end
        end
    end
    drop()
    ctx.button( "Thả vật", drop )
    return { camera = { 20, 20, 18, { 0, 3, 0 } } }
end )

add( "Ball and Chain", "Joints / Ball and Chain: xích 20 mắt capsule (spherical joint) kéo một quả cầu nặng.", function( env )
    env:ground( 25 )
    local top, n, seg = 12, 20, 0.5
    local _, anchor = env:box( 0.3, 0.3, 0.3, { type = "static", position = { 0, top, 0 } }, nil, col( 0.5, 0.5, 0.55 ) )
    local prev = anchor
    for i = 1, n do
        local x = ( i - 0.5 ) * seg
        local _, link = env:capsule( 0.08, seg / 2 - 0.1, { position = { x, top, 0 }, angles = { 0, 0, 90 } },
            { density = 2000 }, col( 0.75, 0.75, 0.8 ) )
        env.world:newSphericalJoint{ bodyA = prev, bodyB = link, anchor = { ( i - 1 ) * seg, top, 0 } }
        prev = link
    end
    local r = 1.0
    local _, ball = env:sphere( r, { position = { n * seg + r, top, 0 } }, { density = 3000 }, col( 0.3, 0.3, 0.35 ) )
    env.world:newSphericalJoint{ bodyA = prev, bodyB = ball, anchor = { n * seg, top, 0 } }
    -- vài hộp để quả cầu quét qua
    for i = 1, 8 do env:box( 0.5, 0.5, 0.5, { position = { -3 + i * 0.9, 0.5, ( i % 2 ) * 1.2 - 0.6 } }, nil, CRATE ) end
    return { camera = { 15, 15, 26, { 2, 6, 0 } } }
end )

-- ragdoll đơn giản: 11 phần, spherical joint có giới hạn nón ở cổ/vai/hông, revolute ở khuỷu/gối
local ragdollGroup = 0
local function ragdoll( env, x, y, z, tint )
    ragdollGroup = ragdollGroup - 1
    local f = { groupIndex = ragdollGroup, friction = 0.6 } -- các phần cùng một ragdoll không va nhau
    local c = { color = tint }
    local function cap( r, h, px, py, pz ) local _, b = env:capsule( r, h, { position = { x + px, y + py, z + pz } }, f, c ); return b end
    local function bx( hx, hy, hz, px, py, pz ) local _, b = env:box( hx, hy, hz, { position = { x + px, y + py, z + pz } }, f, c ); return b end
    local pelvis = bx( 0.16, 0.1, 0.1, 0, 1.0, 0 )
    local torso = bx( 0.18, 0.22, 0.11, 0, 1.36, 0 )
    local _, head = env:sphere( 0.12, { position = { x, y + 1.72, z } }, f, col( 0.95, 0.8, 0.65 ) )
    local thighL, thighR = cap( 0.065, 0.16, -0.09, 0.7, 0 ), cap( 0.065, 0.16, 0.09, 0.7, 0 )
    local shinL, shinR = cap( 0.055, 0.16, -0.09, 0.26, 0 ), cap( 0.055, 0.16, 0.09, 0.26, 0 )
    local armL, armR = cap( 0.05, 0.13, -0.26, 1.4, 0 ), cap( 0.05, 0.13, 0.26, 1.4, 0 )
    local foreL, foreR = cap( 0.045, 0.12, -0.26, 1.06, 0 ), cap( 0.045, 0.12, 0.26, 1.06, 0 )
    local w = env.world
    local function ball( a, b, px, py, axis, cone )
        w:newSphericalJoint{ bodyA = a, bodyB = b, anchor = { x + px, y + py, z }, axis = axis,
            enableConeLimit = true, coneAngle = cone, enableTwistLimit = true, lowerTwistAngle = -25, upperTwistAngle = 25 }
    end
    local function hinge( a, b, px, py, lo, hi )
        w:newRevoluteJoint{ bodyA = a, bodyB = b, anchor = { x + px, y + py, z }, axis = { 1, 0, 0 },
            enableLimit = true, lowerAngle = lo, upperAngle = hi }
    end
    ball( pelvis, torso, 0, 1.12, { 0, 1, 0 }, 25 )
    ball( torso, head, 0, 1.6, { 0, 1, 0 }, 35 )
    ball( pelvis, thighL, -0.09, 0.92, { 0, -1, 0 }, 50 )
    ball( pelvis, thighR, 0.09, 0.92, { 0, -1, 0 }, 50 )
    hinge( thighL, shinL, -0.09, 0.48, 0, 140 )
    hinge( thighR, shinR, 0.09, 0.48, 0, 140 )
    ball( torso, armL, -0.26, 1.56, { 0, -1, 0 }, 85 )
    ball( torso, armR, 0.26, 1.56, { 0, -1, 0 }, 85 )
    hinge( armL, foreL, -0.26, 1.24, -140, 0 )
    hinge( armR, foreR, 0.26, 1.24, -140, 0 )
    return pelvis
end

add( "Ragdoll Pile", "Ragdoll / Pile: 8 ragdoll (11 phần, giới hạn khớp) rơi xuống bậc thang.", function( env )
    env:ground( 25 )
    for i = 0, 5 do
        env:box( 3, 0.25 * ( i + 1 ), 0.6, { type = "static", position = { 0, 0.25 * ( i + 1 ), -i * 1.2 } }, nil, col( 0.6, 0.6, 0.65 ) )
    end
    for i = 1, 8 do
        ragdoll( env, ( ( i - 1 ) % 4 - 1.5 ) * 1.2, 2.5 + math.floor( ( i - 1 ) / 4 ) * 2.2, -5 + ( i % 2 ), PALETTE[( i - 1 ) % #PALETTE + 1] )
    end
    return { camera = { 60, 18, 14, { 0, 1.5, -3 } } }
end )

add( "Restitution", "Restitution: 6 quả cầu hệ số nảy 0, 0.2 … 1.0 rơi cùng độ cao.", function( env )
    env:ground( 20, { restitution = 0 } )
    for i = 0, 5 do
        env:sphere( 0.4, { position = { ( i - 2.5 ) * 1.4, 6, 0 } }, { restitution = i / 5 }, pal( i + 1 ) )
    end
    return { camera = { 0, 12, 16, { 0, 3, 0 } } }
end )

add( "Inclined Plane", "Shapes / Inclined Plane: dốc 25°, 5 hộp ma sát 0 … 0.8 (tan 25° ≈ 0.47).", function( env )
    env:ground( 25 )
    local b3 = env.b3
    local qx, qy, qz, qw = b3.quatFromEuler( 0, 0, -25 )
    local _, ramp = env:box( 8, 0.25, 4, { type = "static", position = { 0, 3.5, 0 }, rotation = { qx, qy, qz, qw } }, { friction = 1 }, col( 0.55, 0.55, 0.6 ) )
    local nx, ny = b3.rotate( qx, qy, qz, qw, 0, 1, 0 )
    local tx, ty = b3.rotate( qx, qy, qz, qw, 1, 0, 0 )
    for i = 0, 4 do
        local z = ( i - 2 ) * 1.5
        local px, py = -5 * tx + 0.8 * nx, 3.5 - 5 * ty + 0.8 * ny
        env:box( 0.5, 0.5, 0.5, { position = { px, py, z }, rotation = { qx, qy, qz, qw } }, { friction = i * 0.2 }, pal( i + 1 ) )
    end
    return { camera = { 20, 15, 22, { 0, 2, 0 } } }
end )

add( "Convex Pile", "Benchmark / Convex Pile (thu nhỏ): 80 hộp, cầu, trụ, capsule rơi vào thùng.", function( env )
    env:ground( 25 )
    local wall = col( 0.55, 0.55, 0.6 )
    env:box( 4, 1.5, 0.2, { type = "static", position = { 0, 1.5, 4 } }, nil, wall )
    env:box( 4, 1.5, 0.2, { type = "static", position = { 0, 1.5, -4 } }, nil, wall )
    env:box( 0.2, 1.5, 4, { type = "static", position = { 4, 1.5, 0 } }, nil, wall )
    env:box( 0.2, 1.5, 4, { type = "static", position = { -4, 1.5, 0 } }, nil, wall )
    local k = 0
    for layer = 0, 4 do
        for i = 0, 3 do
            for j = 0, 3 do
                k = k + 1
                local p = { ( i - 1.5 ) * 1.6 + ( math.random() - 0.5 ) * 0.3, 4 + layer * 1.6, ( j - 1.5 ) * 1.6 + ( math.random() - 0.5 ) * 0.3 }
                local o = { position = p, angles = { math.random( 360 ), math.random( 360 ), math.random( 360 ) } }
                local kind = k % 4
                if kind == 0 then env:box( 0.35, 0.25, 0.45, o, nil, pal( k ) )
                elseif kind == 1 then env:sphere( 0.4, o, nil, pal( k ) )
                elseif kind == 2 then env:cylinder( 0.6, 0.35, o, nil, pal( k ) )
                else env:capsule( 0.25, 0.3, o, nil, pal( k ) ) end
            end
        end
    end
    return { camera = { 30, 35, 20, { 0, 1, 0 } } }
end )

add( "Explosion", "Benchmark / Explosion: vòng 48 hộp, bấm \"Nổ\" để đẩy ra từ tâm.", function( env, ctx )
    env:ground( 25 )
    for ring = 1, 3 do
        local n = 8 * ring
        for i = 0, n - 1 do
            local a = 2 * math.pi * i / n
            env:box( 0.4, 0.4, 0.4, { position = { 2 * ring * math.cos( a ), 0.4, 2 * ring * math.sin( a ) }, angles = { 0, -math.deg( a ), 0 } }, nil, CRATE )
        end
    end
    local function boom() env.world:explode{ position = { 0, 0, 0 }, radius = 8, falloff = 4, impulsePerArea = 3000 } end
    ctx.button( "Nổ", boom )
    return { camera = { 30, 40, 22, { 0, 0, 0 } } }
end )

add( "Bullet vs Stack", "Continuous / Bullet vs Stack: bắn viên đạn 120 m/s vào chồng hộp (va chạm liên tục).", function( env, ctx )
    env:ground( 25 )
    for row = 0, 7 do
        for i = 0, 3 do
            env:box( 0.4, 0.4, 0.4, { position = { 3, 0.4 + row * 0.8, ( i - 1.5 ) * 0.82 } }, nil, CRATE )
        end
    end
    local function fire()
        env:sphere( 0.15, { position = { -12, 2.5 + math.random() * 2, ( math.random() - 0.5 ) * 2 }, linearVelocity = { 120, 0, 0 }, bullet = true },
            { density = 8000 }, col( 0.25, 0.25, 0.3 ) )
    end
    ctx.button( "Bắn", fire )
    return { camera = { 70, 12, 18, { 0, 2.5, 0 } } }
end )

add( "Spinner", "Benchmark / Spinner (thu nhỏ): cánh quạt có motor (revolute) khuấy 120 quả cầu trong thùng.", function( env, ctx )
    env:ground( 25 )
    local wall = col( 0.55, 0.55, 0.6 )
    env:box( 5, 2, 0.2, { type = "static", position = { 0, 2, 5 } }, nil, wall )
    env:box( 5, 2, 0.2, { type = "static", position = { 0, 2, -5 } }, nil, wall )
    env:box( 0.2, 2, 5, { type = "static", position = { 5, 2, 0 } }, nil, wall )
    env:box( 0.2, 2, 5, { type = "static", position = { -5, 2, 0 } }, nil, wall )
    local _, paddle = env:box( 4.6, 0.6, 0.15, { position = { 0, 0.7, 0 } }, { density = 2000 }, col( 0.9, 0.35, 0.3 ) )
    local joint = env.world:newRevoluteJoint{ bodyA = env.groundBody, bodyB = paddle, anchor = { 0, 0.7, 0 }, axis = { 0, 1, 0 },
        enableMotor = true, motorSpeed = 120, maxMotorTorque = 1e6 }
    for i = 1, 120 do
        env:sphere( 0.3, { position = { ( math.random() - 0.5 ) * 8, 2 + math.floor( ( i - 1 ) / 30 ) * 0.8, ( math.random() - 0.5 ) * 8 } }, nil, pal( i ) )
    end
    local on = true
    ctx.button( "Motor", function() on = not on; joint:enableMotor( on, 120 ) end )
    return { camera = { 25, 45, 20, { 0, 0, 0 } } }
end )

add( "Elevator", "Joints / Prismatic + Bodies / Kinematic: thang máy có motor và băng chuyền kinematic.", function( env, ctx )
    env:ground( 25 )
    local _, lift = env:box( 1.5, 0.15, 1.5, { position = { -4, 0.3, 0 } }, { density = 500 }, col( 0.3, 0.65, 0.9 ) )
    local j = env.world:newPrismaticJoint{ bodyA = env.groundBody, bodyB = lift, anchor = { -4, 0.3, 0 }, axis = { 0, 1, 0 },
        enableLimit = true, lowerTranslation = 0, upperTranslation = 5, enableMotor = true, motorSpeed = 1.5, maxMotorForce = 1e6 }
    for i = 0, 2 do env:box( 0.4, 0.4, 0.4, { position = { -4 + ( i - 1 ) * 0.9, 0.9, 0 } }, nil, CRATE ) end
    local _, platform = env:box( 1.5, 0.15, 1.5, { type = "kinematic", position = { 3, 2, 0 } }, nil, col( 0.95, 0.8, 0.3 ) )
    env:box( 0.4, 0.4, 0.4, { position = { 3, 2.6, 0 } }, nil, CRATE )
    local t, up = 0, true
    ctx.button( "Lên/xuống", function() up = not up; j:enableMotor( true, up and 1.5 or -1.5 ) end )
    return {
        camera = { 20, 15, 18, { 0, 2, 0 } },
        update = function( dt )
            t = t + dt
            -- body kinematic: đặt vận tốc, Box3D tự đẩy vật đứng trên nó
            platform:setLinearVelocity( 2.5 * math.cos( t * 0.8 ), 0, 0 )
            local tr = j:getTranslation()
            if up and tr > 4.95 then up = false; j:enableMotor( true, -1.5 )
            elseif not up and tr < 0.05 then up = true; j:enableMotor( true, 1.5 ) end
        end,
    }
end )

add( "Tests", "22 bài test tự động của plugin.box3d (box3d_tests.lua): rơi tự do, joint, raycast, sự kiện...", function( env, ctx )
    local results = {}
    local ok, failed = 0, 0
    local tests = require( "box3d_tests" )
    ok, failed = tests.run( ctx.box3d, function( line ) results[#results + 1] = line; print( line ) end )
    ctx.report( results, failed == 0 )
    return { camera = { 30, 22, 14, { 0, 1, 0 } }, noPhysics = true }
end )

return S
