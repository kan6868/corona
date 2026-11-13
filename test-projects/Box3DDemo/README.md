# Box3DDemo

Demo `plugin.box3d` (vật lý) + `plugin.s3d` (vẽ) với 15 cảnh theo các sample phổ biến của Box3D.

| # | Cảnh | Sample Box3D tương ứng | Nút riêng |
| --- | --- | --- | --- |
| 1 | Box Stack | Stacking / Box Stack | |
| 2 | Pyramid | Stacking / Large Pyramid (12 tầng, 78 hộp) | |
| 3 | Jenga | Stacking / Jenga Stack | |
| 4 | Dominoes | Stacking / Dominoes | Đẩy |
| 5 | Bridge | Joints / Bridge (revolute) | Thả vật |
| 6 | Ball and Chain | Joints / Ball and Chain (spherical) | |
| 7 | Ragdoll Pile | Ragdoll / Pile (11 phần, giới hạn khớp) | |
| 8 | Restitution | Restitution / Sphere Restitution | |
| 9 | Inclined Plane | Shapes / Inclined Plane (ma sát) | |
| 10 | Convex Pile | Benchmark / Convex Pile (thu nhỏ) | |
| 11 | Explosion | Benchmark / Explosion | Nổ |
| 12 | Bullet vs Stack | Continuous / Bullet vs Stack | Bắn |
| 13 | Spinner | Benchmark / Spinner (motor revolute) | Motor |
| 14 | Elevator | Joints / Prismatic + Bodies / Kinematic | Lên/xuống |
| 15 | Tests | 22 bài test tự động (`box3d_tests.lua`) | |

Điều khiển: kéo để xoay camera · D-pad góc dưới trái, phím mũi tên hoặc WASD để đi · con lăn chuột, nút + − để zoom ·
chạm vào cảnh để ném bóng · nút trên cùng: chuyển cảnh, làm lại, dừng, chạy từng bước. Body đã ngủ được tô sẫm.

Chạy trong Simulator Windows: đặt `plugin.s3d.dll` và `plugin.box3d.dll` vào thư mục project (đã có sẵn), hoặc cài plugin
bằng `build.bat install` trong `s3d-plugin` và `box3d-plugin`. Android: cài cả hai plugin rồi build từ Simulator;
`build.settings` đã khai báo `publisherId = "com.s3d"`.

File: `main.lua` (UI, vòng lặp), `scenes.lua` (các cảnh), `phys3d.lua` (nối s3d với box3d qua move event),
`camera3d.lua` (camera), `box3d_tests.lua` (bản sao của `box3d-plugin/tests`).
