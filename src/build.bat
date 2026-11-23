@echo off
setlocal
set PATH=D:\herramientas\dev\winlibs-tmp\mingw64\bin;%PATH%
set SDL_INC=D:\herramientas\dev\SDL2-2.30.6\x86_64-w64-mingw32\include
set SDL_LIB=D:\herramientas\dev\SDL2-2.30.6\x86_64-w64-mingw32\lib
set SRC=%~dp0
set SRC=%SRC:~0,-1%
set ROOT=%SRC%\..

pushd "%SRC%"
windres ilands.rc -O coff -o ilands_res.o || exit /b 1
popd

g++ -std=c++17 -O2 -s -Wall -Wextra -Wno-unused-parameter -I"%SDL_INC%" -I"%SRC%" "%SRC%\main.cpp" "%SRC%\world.cpp" "%SRC%\assets.cpp" "%SRC%\renderer.cpp" "%SRC%\network.cpp" "%SRC%\game.cpp" "%SRC%\ilands_res.o" -L"%SDL_LIB%" -static-libgcc -static-libstdc++ -Wl,-Bstatic -lmingw32 -lSDL2main -lSDL2 -lstdc++ -lwinpthread -lpthread -Wl,-Bdynamic -mwindows -lws2_32 -lopengl32 -lgdiplus -lgdi32 -lole32 -loleaut32 -limm32 -lwinmm -lversion -lsetupapi -lcfgmgr32 -luser32 -ladvapi32 -lshell32 -o "%ROOT%\iLands.exe"
