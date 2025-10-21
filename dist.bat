@echo off

setlocal enabledelayedexpansion

del /q build\CMakeCache.txt
cmake -B build -G "Visual Studio 17 2022" -A x64 -T v142 -DRELEASE_USE_STATIC_CRT=OFF -DRELEASE_USE_LTO=ON .
cmake --build build --config Release

IF NOT EXIST dist mkdir dist
IF NOT EXIST dist\Boss mkdir dist\Boss
IF NOT EXIST dist\Minimap mkdir dist\Minimap

pushd dist\Boss
copy /y ..\..\build\bin\EROverlay.dll .
IF NOT EXIST overlays mkdir overlays
copy /y ..\..\build\bin\overlays\Boss.dll overlays\
copy /y ..\..\src\boss\README.md .
copy /y ..\..\LICENSE .
IF NOT EXIST configs mkdir configs
copy /y ..\..\configs\boss.ini configs\
copy /y ..\..\configs\common.ini configs\
copy /y ..\..\configs\input.ini configs\
copy /y ..\..\configs\style.ini configs\
IF NOT EXIST data mkdir data
xcopy /y /e ..\..\src\boss\data\* data\
7z a -tzip -r -mx=9 ..\Boss.zip
popd

pushd dist\Minimap
copy /y ..\..\build\bin\EROverlay.dll .
IF NOT EXIST overlays mkdir overlays
copy /y ..\..\build\bin\overlays\Minimap.dll overlays\
copy /y ..\..\src\minimap\README.md .
copy /y ..\..\LICENSE .
IF NOT EXIST configs mkdir configs
copy /y ..\..\configs\minimap.ini configs\
IF NOT EXIST data mkdir data
xcopy /y /e ..\..\src\minimap\data\* data\
7z a -tzip -r -mx=9 ..\Minimap.zip
popd

endlocal
