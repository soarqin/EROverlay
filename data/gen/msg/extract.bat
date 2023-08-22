@echo off
set BTOOL=C:\Software\BinderTool\BinderTool.exe
mkdir ..\text 2>NUL
for /D %%v in (*) do (
    echo %%v
    echo   Unpacking...
    %BTOOL% "%%v\item.msgbnd.dcx"
    %BTOOL% "%%v\item.msgbnd"
    echo   Extracting text strings from fmg...
    %BTOOL% "%%v\item\msg\%%v\NpcName.fmg"
    %BTOOL% "%%v\item\msg\%%v\PlaceName.fmg"
    mkdir "..\text\%%v" 2>NUL
    echo   Copying...
    copy /y "%%v\item\msg\%%v\NpcName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item\msg\%%v\PlaceName.txt" "..\text\%%v\" >NUL
    echo   Cleaning up...
    rmdir /s /q %%v\item
    del /q %%v\item.msgbnd
    echo  Done.
)