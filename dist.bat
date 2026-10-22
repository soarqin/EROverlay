@echo off

setlocal
pushd "%~dp0" || exit /b 1

call :dist
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

:dist
where cmake >nul || exit /b 1
where 7z >nul || exit /b 1
cmake -B build -G "Visual Studio 18 2026" -A x64 -T v142 -DRELEASE_USE_STATIC_CRT=OFF -DRELEASE_USE_LTO=ON . || exit /b 1
rem Debug and Release share build\bin, so incremental builds can leave the wrong DLLs.
cmake --build build --config Release --clean-first || exit /b 1

for %%P in (Boss Minimap) do (
    if exist "dist\%%P" rmdir /s /q "dist\%%P"
    if exist "dist\%%P" exit /b 1
    if exist "dist\%%P.zip" del /q "dist\%%P.zip"
    if exist "dist\%%P.zip" exit /b 1
)

call :package Boss || exit /b 1
call :package Minimap || exit /b 1
echo Created dist\Boss.zip and dist\Minimap.zip.
exit /b 0

:package
mkdir "dist\%~1\overlays" "dist\%~1\configs" "dist\%~1\configs_CN" || exit /b 1
copy /y "build\bin\EROverlay.dll" "dist\%~1\" || exit /b 1
copy /y "build\bin\overlays\%~1.dll" "dist\%~1\overlays\" || exit /b 1
for %%F in (README.md README_CN.md) do (
    copy /y "src\%~1\%%F" "dist\%~1\" || exit /b 1
)
copy /y LICENSE "dist\%~1\" || exit /b 1
for %%C in (common input style %~1) do (
    copy /y "configs\%%C.ini" "dist\%~1\configs\" || exit /b 1
    copy /y "configs_CN\%%C.ini" "dist\%~1\configs_CN\" || exit /b 1
)
if /i "%~1"=="Boss" (
    mkdir "dist\Boss\docs" || exit /b 1
    copy /y "docs\boss-revival.md" "dist\Boss\docs\" || exit /b 1
    copy /y "docs\boss-revival.evidence.json" "dist\Boss\docs\" || exit /b 1
    xcopy /y /e "src\boss\data\*" "dist\Boss\data\" || exit /b 1
) else (
    copy /y "deps\bcdec\LICENSE" "dist\Minimap\LICENSE_bcdec.txt" || exit /b 1
)
pushd "dist\%~1" || exit /b 1
7z a -tzip -r -mx=9 "..\%~1.zip" *
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
