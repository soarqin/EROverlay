@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this script from the Visual Studio x64 Native Tools Command Prompt.
    exit /b 1
)
pushd "%~dp0.."
if not exist build\native-checks mkdir build\native-checks
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc /Ideps\nlohmann_json\include tools\minimap_native_verify.cpp src\minimap\gfx.cpp src\minimap\resources.cpp src\util\assets.cpp /Fo:build\native-checks\ /Fe:build\native-checks\minimap_native_verify.exe /link xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc tools\minimap_map_verify.cpp src\minimap\data.cpp src\util\mapstate.cpp src\minimap\resources.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:build\native-checks\ /Fe:build\native-checks\minimap_map_verify.exe /link xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc /Ideps\imgui tools\minimap_death_render_verify.cpp src\minimap\render.cpp src\minimap\data.cpp src\util\mapstate.cpp src\minimap\resources.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:build\native-checks\ /Fe:build\native-checks\minimap_death_render_verify.exe /link build\native\deps\imgui\Release\imgui.lib xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc /Ideps\imgui tools\minimap_marker_render_verify.cpp src\minimap\render.cpp src\minimap\data.cpp src\util\mapstate.cpp src\minimap\resources.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:build\native-checks\ /Fe:build\native-checks\minimap_marker_render_verify.exe /link build\native\deps\imgui\Release\imgui.lib xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
rem Do not rebuild a DLL already loaded in a verification game process.
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc tools\minimap_progress_verify.cpp src\util\gameflags.cpp src\minimap\resources.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:build\native-checks\ /Fe:build\native-checks\minimap_progress_verify.exe /link xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /LD /Isrc tools\minimap_bridge_verify.cpp src\gamefiles.cpp src\util\assets.cpp src\util\gameflags.cpp src\util\mapstate.cpp /Fo:build\native-checks\ /Fe:build\native-checks\bridge_verify.dll /link bcrypt.lib
if errorlevel 1 goto :failed
if exist build\native\src\EROverlayDLL.dir\Release\textureupload.obj (
    cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /DEROVERLAY_EXPORTS /Isrc /Ideps\imgui tools\minimap_texture_verify.cpp /Fo:build\native-checks\ /Fe:build\native-checks\minimap_texture_verify.exe /link build\native\src\EROverlayDLL.dir\Release\*.obj build\native\deps\fmt\Release\fmt.lib build\native\deps\imgui\Release\imgui.lib build\native\deps\imgui\Release\imgui_dx12_backend.lib build\native\deps\minhook\Release\minhook.lib deps\steamworks\lib\steam_api64.lib d3d12.lib dxgi.lib d3dcompiler.lib dwmapi.lib shlwapi.lib version.lib bcrypt.lib advapi32.lib
    if errorlevel 1 goto :failed
)
popd
exit /b 0
:failed
popd
exit /b 1
