@echo -off
#
# UefiToolkit.nsh - Main interactive menu for UEFI Shell Toolkit
#
# Usage: UefiToolkit.nsh
#

set TOOLKIT_VER 1.0.0
set SCRIPT_DIR  %cwd%

:MainMenu
cls
echo ============================================================
echo   UEFI Shell Toolkit v%TOOLKIT_VER%
echo ============================================================
echo.
echo   [1]  System Information
echo   [2]  Memory Test
echo   [3]  Device / Protocol Enumeration
echo   [4]  Boot Manager
echo   [5]  Disk Utility
echo   [Q]  Quit
echo.
echo ============================================================
echo.

set /p CHOICE=Enter choice:

if "%CHOICE%" == "1" goto RunSysInfo
if "%CHOICE%" == "2" goto RunMemTest
if "%CHOICE%" == "3" goto RunDevEnum
if "%CHOICE%" == "4" goto RunBootMgr
if "%CHOICE%" == "5" goto RunDiskUtil
if /i "%CHOICE%" == "q" goto Quit
if /i "%CHOICE%" == "Q" goto Quit

echo Invalid choice. Press any key to continue.
pause
goto MainMenu

:RunSysInfo
%SCRIPT_DIR%SysInfo.nsh
echo.
echo Press any key to return to menu.
pause
goto MainMenu

:RunMemTest
echo.
set /p MEM_MB=Enter memory size to test in MB (default 32):
if "%MEM_MB%" == "" set MEM_MB=32
%SCRIPT_DIR%MemTest.nsh %MEM_MB%
echo.
echo Press any key to return to menu.
pause
goto MainMenu

:RunDevEnum
%SCRIPT_DIR%DevEnum.nsh
echo.
echo Press any key to return to menu.
pause
goto MainMenu

:RunBootMgr
%SCRIPT_DIR%BootMgr.nsh
echo.
echo Press any key to return to menu.
pause
goto MainMenu

:RunDiskUtil
%SCRIPT_DIR%DiskUtil.nsh
echo.
echo Press any key to return to menu.
pause
goto MainMenu

:Quit
echo.
echo Exiting UEFI Shell Toolkit. Goodbye.
echo.
