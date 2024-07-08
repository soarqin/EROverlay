@echo off
set BTOOL=C:\Software\BinderTool\BinderTool.exe
mkdir ..\text 2>NUL
for /D %%v in (*) do (
    echo %%v
    echo   Unpacking...
    %BTOOL% "%%v\item_dlc01.msgbnd.dcx"
    %BTOOL% "%%v\item_dlc01.msgbnd"
    %BTOOL% "%%v\menu_dlc01.msgbnd.dcx"
    %BTOOL% "%%v\menu_dlc01.msgbnd"
    del /q %%v\item_dlc01.msgbnd %%v\menu_dlc01.msgbnd
    echo   Extracting text strings from fmg...
    %BTOOL% "%%v\item_dlc01\msg\%%v\NpcName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\NpcName_dlc01.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\PlaceName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\PlaceName_dlc01.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\AccessoryName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\AccessoryName_dlc01.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\GemName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\GemName_dlc01.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\GoodsName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\GoodsName_dlc01.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\ProtectorName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\ProtectorName_dlc01.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\WeaponName.fmg"
    %BTOOL% "%%v\item_dlc01\msg\%%v\WeaponName_dlc01.fmg"
    mkdir "..\text\%%v" 2>NUL
    echo   Copying...
    copy /y "%%v\item_dlc01\msg\%%v\NpcName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\NpcName_dlc01.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\PlaceName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\PlaceName_dlc01.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\AccessoryName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\AccessoryName_dlc01.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\GemName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\GemName_dlc01.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\GoodsName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\GoodsName_dlc01.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\ProtectorName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\ProtectorName_dlc01.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\WeaponName.txt" "..\text\%%v\" >NUL
    copy /y "%%v\item_dlc01\msg\%%v\WeaponName_dlc01.txt" "..\text\%%v\" >NUL
    echo   Cleaning up...
    rmdir /s /q %%v\item_dlc01 %%v\menu_dlc01
    echo  Done.
)