@echo off

setlocal enabledelayedexpansion

del /q build\CMakeCache.txt
cmake -B build -G "Visual Studio 18 2026" -A x64 -T v142 -DRELEASE_USE_STATIC_CRT=OFF -DRELEASE_USE_LTO=ON .
cmake --build build --config Release

IF NOT EXIST dist mkdir dist
IF NOT EXIST dist\Boss mkdir dist\Boss
IF NOT EXIST dist\Minimap mkdir dist\Minimap

pushd dist\Boss
copy /y ..\..\build\bin\EROverlay.dll .
IF NOT EXIST overlays mkdir overlays
copy /y ..\..\build\bin\overlays\Boss.dll overlays\
copy /y ..\..\src\boss\README.md .
copy /y ..\..\src\boss\README_CN.md .
copy /y ..\..\LICENSE .
IF NOT EXIST configs mkdir configs
copy /y ..\..\configs\boss.ini configs\
copy /y ..\..\configs\common.ini configs\
copy /y ..\..\configs\input.ini configs\
copy /y ..\..\configs\style.ini configs\
IF NOT EXIST configs_CN mkdir configs_CN
copy /y ..\..\configs_CN\boss.ini configs_CN\
copy /y ..\..\configs_CN\common.ini configs_CN\
copy /y ..\..\configs_CN\input.ini configs_CN\
copy /y ..\..\configs_CN\style.ini configs_CN\
IF NOT EXIST data mkdir data
xcopy /y /e ..\..\src\boss\data\* data\
7z a -tzip -r -mx=9 ..\Boss.zip
popd

pushd dist\Minimap
copy /y ..\..\build\bin\EROverlay.dll .
IF NOT EXIST overlays mkdir overlays
copy /y ..\..\build\bin\overlays\Minimap.dll overlays\
copy /y ..\..\src\minimap\README.md .
copy /y ..\..\src\minimap\README_CN.md .
copy /y ..\..\LICENSE .
IF NOT EXIST configs mkdir configs
copy /y ..\..\configs\minimap.ini configs\
copy /y ..\..\configs\common.ini configs\
copy /y ..\..\configs\input.ini configs\
copy /y ..\..\configs\style.ini configs\
IF NOT EXIST configs_CN mkdir configs_CN
copy /y ..\..\configs_CN\minimap.ini configs_CN\
copy /y ..\..\configs_CN\common.ini configs_CN\
copy /y ..\..\configs_CN\input.ini configs_CN\
copy /y ..\..\configs_CN\style.ini configs_CN\
IF EXIST ..\Minimap.zip 7z d -r ..\Minimap.zip data
7z a -tzip -r -mx=9 ..\Minimap.zip EROverlay.dll overlays\Minimap.dll configs configs_CN README.md README_CN.md LICENSE
popd

endlocal
