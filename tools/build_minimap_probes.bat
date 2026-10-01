@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
    echo Run this script from the Visual Studio x64 Native Tools Command Prompt.
    exit /b 1
)
pushd "%~dp0.."
if not exist "build\ida\probes" mkdir "build\ida\probes"
cl /nologo /std:c++latest /utf-8 /O2 /MT /LD tools\minimap_file_probe.cpp /Fe:build\ida\probes\minimap_file_probe.dll /Fo:build\ida\probes\minimap_file_probe.obj /link bcrypt.lib /OUT:build\ida\probes\minimap_file_probe.dll
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /O2 /MT /DMINIMAP_SPRITE_PROBE /LD tools\minimap_file_probe.cpp /Fe:build\ida\probes\minimap_sprite_probe.dll /Fo:build\ida\probes\minimap_sprite_probe.obj /link bcrypt.lib /OUT:build\ida\probes\minimap_sprite_probe.dll
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /O2 /MT tools\minimap_probe_loader.cpp /Fe:build\ida\probes\minimap_probe_loader.exe /Fo:build\ida\probes\minimap_probe_loader.obj
if errorlevel 1 goto :failed
cl /nologo /std:c++latest /utf-8 /O2 /MT tools\minimap_dds_probe.cpp /Fe:build\ida\probes\minimap_dds_probe.exe /Fo:build\ida\probes\minimap_dds_probe.obj /link d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 goto :failed
popd
exit /b 0
:failed
popd
exit /b 1
