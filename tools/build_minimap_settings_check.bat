@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 (
    echo Run from the Visual Studio x64 Native Tools Command Prompt.
    exit /b 1
)
pushd "%~dp0.."
if not exist build\native-checks mkdir build\native-checks
cl /nologo /std:c++latest /utf-8 /EHsc /O2 /MD /Isrc tools\minimap_settings_verify.cpp src\minimap\settings.cpp src\config.cpp /Fo:build\native-checks\ /Fe:build\native-checks\minimap_settings_verify.exe
if errorlevel 1 goto :failed
build\native-checks\minimap_settings_verify.exe
if errorlevel 1 goto :failed
popd
exit /b 0
:failed
popd
exit /b 1
