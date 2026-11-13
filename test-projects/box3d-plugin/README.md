# plugin.box3d

Vật lý 3D cho Solar2D bằng [Box3D](https://github.com/erincatto/box3d) (Erin Catto, tác giả Box2D, giấy phép MIT).
Plugin native viết bằng C, không sửa engine Solar2D. Dùng kèm `plugin.s3d` để vẽ (xem `test-projects/Box3DDemo`).

Phiên bản 0.1.0, Box3D commit `a8ac09f` (2026-11-11). Nền tảng: Windows Simulator (Win32 x86), Android
(arm64-v8a, armeabi-v7a, x86_64).

## Dùng nhanh

```lua
local box3d = require( "plugin.box3d" )
local world = box3d.newWorld{ gravity = { 0, -10, 0 } }

local ground = world:newBody{ type = "static", position = { 0, -1, 0 } }
ground:addBox( 20, 1, 20 )                    -- nửa cạnh: sàn 40 x 2 x 40, mặt trên ở y = 0

local box = world:newBody{ position = { 0, 3, 0 }, angles = { 0, 30, 0 } }
box:addBox( 0.5, 0.5, 0.5, { friction = 0.6, restitution = 0.1 } )

Runtime:addEventListener( "enterFrame", function()
    world:step( 1 / 60 )
    local x, y, z, qx, qy, qz, qw = box:getTransform()
    -- với plugin.s3d: mesh:setPose( box:getTransform() )
end )
```

Đơn vị: mét, kilogram, giây. Hệ tay phải, Y hướng lên (khớp `plugin.s3d`). **Góc tính bằng độ**, vận tốc góc bằng
độ/giây, giống Solar2D. Quaternion là 4 số `x, y, z, w`. Mật độ mặc định 1000 kg/m³ (nước): hộp 1 m³ nặng 1 tấn.

## World

| Hàm | Mô tả |
| --- | --- |
| `box3d.newWorld{ gravity, subSteps = 4, workers = 1, enableSleep = true, enableContinuous = true }` | Tạo world. `workers > 1` bật đa luồng của Box3D. |
| `world:step( dt [, subSteps] )` | Chạy một bước. Nên gọi với bước cố định 1/60 s. |
| `world:newBody( opts )` | Tạo body (xem dưới). |
| `world:rayCast( ox, oy, oz, tx, ty, tz )` | Tia từ `o`, dài bằng vector `t`. Trả về `{ body, shape, x, y, z, nx, ny, nz, fraction }` gần nhất, hoặc `nil`. |
| `world:explode{ position, radius, falloff, impulsePerArea = 1000 }` | Đẩy body ra khỏi tâm nổ. |
| `world:getContactEvents()` | `{ began = {...}, ended = {...}, hit = {...} }` của bước trước; mỗi phần tử có `bodyA, bodyB, shapeA, shapeB`, phần tử `hit` thêm `x, y, z, nx, ny, nz, speed`. |
| `world:getMoveEvents()` | Body đã di chuyển ở bước trước: `{ body, x, y, z, qx, qy, qz, qw, sleep }`. Rẻ hơn hỏi từng body khi đồng bộ hình. |
| `world:getBodies()`, `world:getAwakeBodyCount()` | Danh sách body còn sống, số body đang thức. |
| `world:setGravity( x, y, z )`, `world:getGravity()`, `world:enableSleeping( bool )` | |
| `world:destroy()` | Huỷ world (cũng tự huỷ khi bị thu gom rác). |

## Body

`world:newBody{ type = "dynamic" | "static" | "kinematic", position = { x, y, z }, rotation = { qx, qy, qz, qw }`
hoặc `angles = { rx, ry, rz }` (độ, thứ tự Y·X·Z như s3d), `linearVelocity, angularVelocity` (độ/s),
`linearDamping, angularDamping, gravityScale, bullet, enableSleep, awake, lockRotation }`.

| Phương thức | Mô tả |
| --- | --- |
| `addBox( hx, hy, hz [, opts] )` | Hộp theo nửa cạnh. |
| `addSphere( r [, opts] )` | Cầu; `opts.offset` = tâm. |
| `addCapsule( x1, y1, z1, x2, y2, z2, r [, opts] )` | Capsule giữa hai tâm bán cầu. |
| `addCylinder( height, r [, sides = 16, opts] )` | Trụ đứng theo Y (convex hull). |
| `addHull( points [, opts] )` | Bao lồi; `points` phẳng `{ x1, y1, z1, ... }` hoặc `{ { x, y, z }, ... }`. |
| `addMesh( vertices, indices [, opts] )` | Lưới tam giác (nên cho body static); chỉ số đánh từ 1. `opts.scale`. |
| `getPosition()`, `getRotation()`, `getTransform()` | `x, y, z` · `qx, qy, qz, qw` · cả 7 số. |
| `setTransform( x, y, z [, qx, qy, qz, qw] )` | Dịch chuyển tức thời. |
| `get/setLinearVelocity`, `get/setAngularVelocity` | Vận tốc (m/s, độ/s). |
| `applyForce( fx, fy, fz [, px, py, pz] )`, `applyImpulse( ... )` | Không có điểm thì đặt vào khối tâm. |
| `applyTorque( x, y, z )`, `applyAngularImpulse( x, y, z )` | |
| `getMass()`, `getType()`, `setType( t )`, `isAwake()`, `setAwake( b )` | |
| `setBullet( b )`, `setGravityScale( s )`, `setDamping( linear [, angular] )` | |
| `getWorldPoint( lx, ly, lz )`, `getLocalPoint( x, y, z )`, `getShapes()` | |
| `isValid()`, `destroy()` | |

Body nhận field tuỳ ý (`body.obj = mesh`, `body.tag = "enemy"`); event và raycast trả về đúng object body đó.

`opts` của shape: `density, friction, restitution, rollingResistance, sensor, contactEvents = true, hitEvents,
categoryBits, maskBits, groupIndex` (cùng `groupIndex` âm thì không va nhau, như Box2D), `offset, rotation | angles`
(vị trí và hướng của shape trong body). Shape có `setFriction, setRestitution, setDensity, getFriction,
getRestitution, isSensor, isValid, destroy`.

## Joint

Mọi joint nhận `bodyA, bodyB, anchor = { x, y, z }` (điểm neo trong toạ độ thế giới; mặc định là vị trí bodyB) và
`collideConnected`. Plugin tự tính local frame cho hai body từ `anchor` và `axis`.

| Hàm | Tham số riêng |
| --- | --- |
| `world:newRevoluteJoint{ ... }` | `axis` (trục bản lề), `enableLimit, lowerAngle, upperAngle` (độ), `enableMotor, motorSpeed` (độ/s), `maxMotorTorque`, `enableSpring, hertz, dampingRatio` |
| `world:newSphericalJoint{ ... }` | `axis` (tâm nón giới hạn), `enableConeLimit, coneAngle`, `enableTwistLimit, lowerTwistAngle, upperTwistAngle`, `enableSpring, hertz, dampingRatio` |
| `world:newPrismaticJoint{ ... }` | `axis` (hướng trượt), `enableLimit, lowerTranslation, upperTranslation`, `enableMotor, motorSpeed` (m/s), `maxMotorForce`, spring |
| `world:newWeldJoint{ ... }` | `linearHertz, angularHertz, linearDampingRatio, angularDampingRatio` (0 = cứng) |
| `world:newDistanceJoint{ bodyA, bodyB, anchorA, anchorB, length, enableSpring, hertz, dampingRatio, enableLimit, minLength, maxLength }` | |

Joint có `destroy, isValid, getType, getAngle` (revolute, độ), `getTranslation` (prismatic),
`enableMotor( on [, speed [, maxTorqueOrForce]] ), setLimits( lo, hi ), setLength( len ), getConstraintForce()`.

Tiện ích: `box3d.quatFromEuler( rx, ry, rz )`, `box3d.quatFromAxisAngle( ax, ay, az, deg )`,
`box3d.quatMul( q1, q2 )` (8 số), `box3d.rotate( qx, qy, qz, qw, vx, vy, vz )`.

## Test

`tests/box3d_tests.lua` có 22 bài test theo các sample và unit test phổ biến của Box3D:

| # | Test | Kiểm tra |
| --- | --- | --- |
| 1 | Rơi tự do | y = y0 − g t²/2, v = −g t sau 1 s (sai ≤ 0,1) |
| 2 | Một hộp | nằm đúng y = 0,5 và ngủ sau 4 s |
| 3 | Chồng 10 hộp | không lệch quá 5 cm sau 5 s |
| 4 | Kim tự tháp 8 tầng | không hộp nào lệch quá 10 cm |
| 5 | Nảy | hệ số 0,8 cho độ cao nảy ≈ 0,64 lần |
| 6 | Ma sát trên dốc 20° | μ = 0,6 đứng yên, μ = 0,05 trượt |
| 7 | Con lắc revolute | khoảng cách giữ trong 2 cm, chu kỳ đúng (biên độ 90°) |
| 8 | Motor revolute | 90°/s trong 2 s quay ≈ 180° |
| 9 | Prismatic | dừng đúng ở giới hạn dưới |
| 10 | Distance joint | độ dài giữ trong 2 cm |
| 11 | Weld joint | hai hộp hàn giữ khoảng cách |
| 12 | Xích 10 mắt spherical | không đứt, có rủ xuống |
| 13 | Raycast | điểm trúng, pháp tuyến, fraction đúng; trượt khi ngoài sàn |
| 14 | Contact event | có `began` và `hit` khi hộp chạm sàn |
| 15 | Move event | báo vị trí và báo ngủ |
| 16 | Nổ | 4 hộp bị đẩy ra xa tâm |
| 17 | Va chạm liên tục | đạn 400 m/s không xuyên tường 10 cm |
| 18 | Body kinematic | đi đúng vận tốc và đẩy hộp |
| 19 | Mesh tam giác | quả cầu nằm trên lưới |
| 20 | Hull | tứ diện, trụ, capsule nằm yên và ngủ |
| 21 | Tất định | hai world giống nhau cho kết quả giống hệt từng bit |
| 22 | Vòng đời | field riêng, destroy, isValid, lỗi khi dùng body / world đã huỷ |

Chạy: `./build.sh test` (Linux, macOS hoặc WSL; build bản máy host kèm Lua 5.1 rồi chạy). Trong Solar2D: cảnh
**Tests** của Box3DDemo chạy đúng bộ test này trên thiết bị và hiện kết quả.

## Build

Giống `plugin.s3d`: `build.bat` (Windows) hoặc `./build.sh` gọi `tools/build.py`.

```bat
build.bat              :: win + android + đóng gói + cài vào %APPDATA%\Solar2DPlugins
build.bat win          :: build\win32\plugin.box3d.dll
build.bat android      :: build\android\jniLibs\<abi>\libplugin.box3d.so (NDK nếu có, không thì zig)
build.bat package      :: dist\plugin.box3d-win32.tgz, dist\plugin.box3d-android.tgz
build.bat install | test | clean
```

- Script tự tải Box3D đúng commit đã kiểm thử vào `third_party/box3d` (hoặc `--box3d <thư mục>`).
- Windows: DLL x86 link với `lua.dll` của Simulator qua import library tạo bằng `zig dlltool`; chỉ export
  `luaopen_plugin_box3d`.
- Android: chỉ cần `liblua.so` lúc chạy (link với stub mang tên). Build bằng zig thì bản ARMv7 có thêm
  `src/android_compat_arm32.c`: musl 32-bit gọi `__clock_gettime64` mà Bionic không có. Script kiểm tra symbol này.
- Box3D chạy 1 luồng mặc định (`workers = 1`): không tạo thread nên an toàn khi build bằng zig. Muốn đa luồng
  trên Android, nên build bằng NDK.

```lua
-- build.settings
plugins = {
    ["plugin.box3d"] = { publisherId = "com.s3d" },
    ["plugin.s3d"] = { publisherId = "com.s3d" },
},
```

## Đã kiểm tra

- 22/22 test trên bản máy host (gcc, glibc) và trên chính bản Android x86_64 build bằng zig nạp qua glibc.
- 14 cảnh của Box3DDemo chạy 600 bước không lỗi, không NaN, không vật xuyên sàn (Lua 5.1 thật, s3d giả lập).
- Bảng import/export của DLL Windows và ELF Android (kiến trúc, DT_NEEDED, symbol export).

**Chưa chạy trên Simulator Windows và điện thoại thật** tại thời điểm viết.
