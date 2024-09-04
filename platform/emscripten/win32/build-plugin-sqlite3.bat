path C:\emsdk\emscripten\1.37.21;C:\emsdk\python\2.7.5.3_64bit
call emcc.bat -s SIDE_MODULE=1 -o Emscripten/lsqlite3.bc -DEMSCRIPTEN -O2 -I../../../external/lua-5.1.3/src ../../../external/lsqlite3-7/lsqlite3.c
call emcc.bat -s SIDE_MODULE=1 -o Emscripten/sqlite3.bc -DEMSCRIPTEN -O2 -I../../../external/lua-5.1.3/src ../../../external/lsqlite3-7/sqlite3.c
call emcc.bat -s SIDE_MODULE=1 -O2 -shared Emscripten/sqlite3.bc Emscripten/lsqlite3.bc -o Emscripten/sqlite3.js
