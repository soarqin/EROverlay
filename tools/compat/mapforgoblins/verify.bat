@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
    echo Run from the Visual Studio x64 Native Tools Command Prompt.
    exit /b 1
)
if "%~2"=="" (
    echo Usage: verify.bat patched-upstream-dir actual-minimap-fixture-dir
    exit /b 1
)
set "UPSTREAM=%~f1"
set "FIXTURE=%~f2"
pushd "%~dp0..\..\.."
set "OUT=build\native-checks\goblins-api"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc tools\minimap_runtime_icon_verify.cpp src\minimap\runtimeicons.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:%OUT%\ /Fe:%OUT%\minimap_runtime_icon_verify.exe
if errorlevel 1 goto :failed
%OUT%\minimap_runtime_icon_verify.exe
if errorlevel 1 goto :failed
rem This test DLL has controlled marker state. It is NEVER an installable mod.
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MT /LD /I"%UPSTREAM%\src" "%UPSTREAM%\src\goblin_map_api.cpp" "%UPSTREAM%\src\generated_shared\goblin_map_icons.cpp" tools\compat\mapforgoblins\fixtures\provider.cpp /Fo:%OUT%\ /Fe:%OUT%\MapForGoblins.dll /link /IMPLIB:%OUT%\MapForGoblins.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc\minimap\compat tools\compat\mapforgoblins\fixtures\client.cpp /Fo:%OUT%\ /Fe:%OUT%\client.exe
if errorlevel 1 goto :failed
%OUT%\client.exe "%CD%\%OUT%\MapForGoblins.dll"
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc /Ideps\imgui tools\minimap_mod_marker_verify.cpp src\minimap\render.cpp src\minimap\settings.cpp src\minimap\data.cpp src\minimap\goblins.cpp src\util\mapstate.cpp src\minimap\resources.cpp src\minimap\runtimeicons.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:%OUT%\ /Fe:%OUT%\minimap_mod_marker_verify.exe /link build\deps\imgui\Release\imgui.lib xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
%OUT%\minimap_mod_marker_verify.exe "%FIXTURE%\vanilla-worldmap.gfx" "%FIXTURE%\common-layouts.bnd" "%FIXTURE%\common-atlases.tpf" "%FIXTURE%\vanilla-params\BonfireWarpParam.param" "%FIXTURE%\vanilla-params\WorldMapPointParam.param" --goblins-dll "%CD%\%OUT%\MapForGoblins.dll"
if errorlevel 1 goto :failed
%OUT%\minimap_mod_marker_verify.exe "%FIXTURE%\swapped-layer-worldmap.gfx" "%FIXTURE%\common-layouts.bnd" "%FIXTURE%\common-atlases.tpf" "%FIXTURE%\vanilla-params\BonfireWarpParam.param" "%FIXTURE%\vanilla-params\WorldMapPointParam.param" --goblins-dll "%CD%\%OUT%\MapForGoblins.dll"
if errorlevel 1 goto :failed
popd
exit /b 0
:failed
popd
exit /b 1
