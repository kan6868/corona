-- Chạy bộ test ngoài Solar2D: lua tests/run_tests.lua (cần plugin/box3d.so|dll trong package.cpath)
package.path = "./tests/?.lua;" .. package.path
package.cpath = "./?.so;./?.dll;" .. package.cpath
local b3 = require( "plugin.box3d" )
print( "plugin.box3d " .. b3.version .. " / " .. b3.engine )
local _, failed = require( "box3d_tests" ).run( b3, print )
os.exit( failed == 0 and 0 or 1 )
