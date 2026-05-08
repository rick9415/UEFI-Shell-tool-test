@echo -off
#
# DiskUtil.nsh - Disk and Partition Utility
#
# Enumerates mapped filesystems, shows block device handles,
# displays partition info via 'diskinfo', and provides helpers
# for basic file operations (backup EFI, wipe free space).
#
# Usage: DiskUtil.nsh [list|info <FS>|backup <FS> <dst>|help]
#

set CMD %1

if "%CMD%" == ""       goto ShowMenu
if /i "%CMD%" == "list"   goto ListDisks
if /i "%CMD%" == "info"   goto DiskInfo
if /i "%CMD%" == "backup" goto BackupEfi
if /i "%CMD%" == "help"   goto ShowHelp
goto ShowMenu

:ShowMenu
echo ============================================================
echo   Disk and Partition Utility
echo ============================================================
echo.
echo   list              - List all mapped filesystems
echo   info  <FS>        - Show partition info for a filesystem
echo   backup <FS> <dst> - Backup EFI partition to destination path
echo   help              - Show this help
echo.
echo ============================================================
echo.
goto ListDisks

:ListDisks
echo ============================================================
echo   Mapped Filesystems
echo ============================================================
echo.
map -r -b
echo.

echo [Block I/O Protocol Handles]
dh -p BlockIo -d
echo.

echo [Partition Info Protocol Handles]
dh -p PartitionInfo -d
echo.
goto Done

:DiskInfo
set FS_NAME %2
if "%FS_NAME%" == "" goto DiskInfoPrompt
goto DoDiskInfo

:DiskInfoPrompt
map -r
echo.
set /p FS_NAME=Enter filesystem name (e.g. FS0):

:DoDiskInfo
echo.
echo ============================================================
echo   Disk Info: %FS_NAME%
echo ============================================================
echo.
echo [Directory Listing]
ls %FS_NAME%:\ -b
echo.
echo [Volume Label]
vol %FS_NAME%:
echo.
goto Done

:BackupEfi
set SRC_FS  %2
set DST_PATH %3

if "%SRC_FS%"  == "" goto BackupPrompt
if "%DST_PATH%" == "" goto BackupPrompt
goto DoBackup

:BackupPrompt
map -r
echo.
set /p SRC_FS=Source filesystem (e.g. FS0):
set /p DST_PATH=Destination directory (e.g. FS1:\Backup):

:DoBackup
echo.
echo ============================================================
echo   Backing up EFI partition: %SRC_FS% -> %DST_PATH%
echo ============================================================
echo.

if not exist %DST_PATH% mkdir %DST_PATH%

echo Copying %SRC_FS%:\EFI\ ...
cp -r %SRC_FS%:\EFI %DST_PATH%\EFI

if %lasterror% == 0 goto BackupOk
echo ERROR: Copy failed (error %lasterror%). Check source and destination paths.
goto Done

:BackupOk
echo.
echo Backup complete.
echo   Source : %SRC_FS%:\EFI
echo   Dest   : %DST_PATH%\EFI
echo.
goto Done

:ShowHelp
echo.
echo DiskUtil.nsh - Disk and Partition Utility
echo.
echo   DiskUtil.nsh list
echo       List all filesystems and block device handles.
echo.
echo   DiskUtil.nsh info [FS]
echo       Show directory listing and volume label for a filesystem.
echo.
echo   DiskUtil.nsh backup [SrcFS] [DstPath]
echo       Recursively copy \EFI from SrcFS to DstPath (backup use).
echo.

:Done
echo ============================================================
echo   Disk Utility done.
echo ============================================================
echo.
