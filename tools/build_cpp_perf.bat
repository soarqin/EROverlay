@echo off
setlocal
if "%~1"=="" (
    echo Usage: tools\build_cpp_perf.bat ^<exported-C++-reference-root^>
    exit /b 1
)
set "ER_CPP_REF=%~f1"
set "ER_CPP_FIXTURE=%~dp0cpp_render_perf.cpp"
if not exist "%ER_CPP_REF%\reference.json" exit /b 1
for %%I in (cmake.exe) do set "ER_CPP_CMAKE=%%~$PATH:I"
if not defined ER_CPP_CMAKE exit /b 1
set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%ProgramFiles%\CMake\bin"
for /f "delims=" %%I in ('vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "ER_RUST_VS=%%I"
if not defined ER_RUST_VS exit /b 1
call "%ER_RUST_VS%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.29 >nul || exit /b 1
pushd "%ER_CPP_REF%" || exit /b 1
"%ER_CPP_CMAKE%" -B build -G "Visual Studio 17 2022" -A x64 -T v142 -DRELEASE_USE_STATIC_CRT=OFF -DRELEASE_USE_LTO=ON . || goto :failed
"%ER_CPP_CMAKE%" --build build --config Release || goto :failed
if not exist build\rust-comparison mkdir build\rust-comparison
set OBJ=build\src\EROverlayDLL.dir\Release
set OBJS=%OBJ%\api.obj %OBJ%\assets.obj %OBJ%\config.obj %OBJ%\d3drenderer.obj %OBJ%\dllmain.obj %OBJ%\embeddedfont.obj %OBJ%\file.obj %OBJ%\gamefiles.obj %OBJ%\gameflags.obj %OBJ%\global.obj %OBJ%\hooking.obj %OBJ%\input.obj %OBJ%\mapstate.obj %OBJ%\param.obj %OBJ%\pointers.obj %OBJ%\steam.obj %OBJ%\sysfont.obj %OBJ%\textureupload.obj %OBJ%\uiscene.obj %OBJ%\winhttp.obj %OBJ%\wstring.obj
cl /nologo /std:c++latest /utf-8 /EHsc /Zi /Fd:build\rust-comparison\compiler.pdb /O2 /MD /W3 /wd4819 /DEROVERLAY_EXPORTS /Isrc /Ideps\pattern16 /Ideps\imgui /Ideps\fmt\include "%ER_CPP_FIXTURE%" /Fo:build\rust-comparison\ /Fe:build\rust-comparison\cpp_render_perf.exe /link /DEBUG %OBJS% build\deps\fmt\Release\fmt.lib build\deps\imgui\Release\imgui.lib build\deps\imgui\Release\imgui_dx12_backend.lib build\deps\minhook\Release\minhook.lib deps\steamworks\lib\steam_api64.lib /LTCG /DELAYLOAD:steam_api64.dll delayimp.lib d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib dwmapi.lib shlwapi.lib version.lib advapi32.lib user32.lib gdi32.lib dbghelp.lib
set RESULT=%ERRORLEVEL%
popd
exit /b %RESULT%
:failed
popd
exit /b 1
