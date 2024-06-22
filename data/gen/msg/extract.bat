@echo off
set BTOOL=C:\Software\BinderTool\BinderTool.exe
mkdir ..\text 2>NUL
for /D %%v in (*) do (
    echo %%v
    echo   Unpacking...
    %BTOOL% "%%v\item_dlc01.msgbnd.dcx"
    %BTOOL% "%%v\item_dlc01.msgbnd"
    echo   Extracting text strings from fmg...
    %BTOOL% "%%v\item_dlc01\msg\%%v\NpcName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\NpcName_dlc01.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\PlaceName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\PlaceName_dlc01.fmg"
    mkdir "..\text\%%v" 2>NUL
    echo   Copying...
    copy /y "%%v\item_dlc01\msg\%%v\NpcName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\NpcName_dlc01.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\PlaceName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\PlaceName_dlc01.txt" "..\text\%%v\" >NUL
    echo   Cleaning up...
    rmdir /s /q %%v\item_dlc01
    del /q %%v\item_dlc01.msgbnd
    echo  Done.
)