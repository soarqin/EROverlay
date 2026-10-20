@echo off
setlocal
where cl >nul 2>nul
if not errorlevel 1 goto :build
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "delims=" %%I in ('vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "UI_SCENE_VS=%%I"
if not defined UI_SCENE_VS (
    echo Run from a Visual Studio x64 Native Tools Command Prompt with the v142 toolset installed.
    exit /b 1
)
call "%UI_SCENE_VS%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.29 >nul || exit /b 1
:build
pushd "%~dp0.."
if not exist build\ui-scene mkdir build\ui-scene
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W4 /Isrc /Ideps\pattern16 tools\signature_verify.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\signature_verify.exe
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W4 /Isrc /Ideps\pattern16 tools\ui_scene_scan_verify.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\ui_scene_scan_verify.exe
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W4 /Isrc /Ideps\pattern16 /Ideps\nlohmann_json\include tools\minimap_scan_verify.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\minimap_scan_verify.exe
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W3 /Isrc /Ideps\fmt\include tools\plugin_thread_verify.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\plugin_thread_verify.exe /link build\deps\fmt\Release\fmt.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W3 /Isrc tools\minimap_map_verify.cpp src\minimap\data.cpp src\minimap\goblins.cpp src\util\mapstate.cpp src\minimap\resources.cpp src\minimap\runtimeicons.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\minimap_map_verify.exe /link xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W3 /wd4819 /Isrc /Ideps\imgui tools\minimap_visibility_verify.cpp src\minimap\render.cpp src\minimap\settings.cpp src\minimap\data.cpp src\minimap\goblins.cpp src\util\mapstate.cpp src\minimap\resources.cpp src\minimap\runtimeicons.cpp src\minimap\gfx.cpp src\util\assets.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\minimap_visibility_verify.exe /link build\deps\imgui\Release\imgui.lib xmllite.lib shlwapi.lib
if errorlevel 1 goto :failed
set OBJ=build\src\EROverlayDLL.dir\Release
set OBJS=%OBJ%\api.obj %OBJ%\assets.obj %OBJ%\config.obj %OBJ%\d3drenderer.obj %OBJ%\dllmain.obj %OBJ%\embeddedfont.obj %OBJ%\file.obj %OBJ%\gamefiles.obj %OBJ%\gameflags.obj %OBJ%\global.obj %OBJ%\hooking.obj %OBJ%\input.obj %OBJ%\mapstate.obj %OBJ%\param.obj %OBJ%\pointers.obj %OBJ%\steam.obj %OBJ%\sysfont.obj %OBJ%\textureupload.obj %OBJ%\winhttp.obj %OBJ%\wstring.obj
set UI_SCENE_LINK_LIBS=build\deps\fmt\Release\fmt.lib build\deps\imgui\Release\imgui.lib build\deps\imgui\Release\imgui_dx12_backend.lib build\deps\minhook\Release\minhook.lib deps\steamworks\lib\steam_api64.lib /LTCG /DELAYLOAD:steam_api64.dll delayimp.lib d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib dwmapi.lib shlwapi.lib version.lib advapi32.lib user32.lib gdi32.lib
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W3 /wd4819 /DEROVERLAY_EXPORTS /D_CRT_SECURE_NO_WARNINGS /Isrc /Ideps\pattern16 /Ideps\imgui /Ideps\fmt\include /Ideps\minhook tools\ui_scene_render_verify.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\ui_scene_render_verify.exe /link %OBJS% %UI_SCENE_LINK_LIBS%
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /W3 /wd4819 /DEROVERLAY_EXPORTS /Isrc /Ideps\imgui tools\minimap_texture_verify.cpp /Fo:build\ui-scene\ /Fe:build\ui-scene\minimap_texture_verify.exe /link %OBJS% %OBJ%\plugin.obj %OBJ%\uiscene.obj %UI_SCENE_LINK_LIBS%
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
:failed
popd
exit /b 1
