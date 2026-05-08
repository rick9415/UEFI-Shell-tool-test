@echo -off
#
# BootMgr.nsh - UEFI Boot Option Manager
#
# Lists current UEFI boot variables (BootXXXX), shows the BootOrder,
# and provides helpers to add, delete, or temporarily override boot
# entries using the UEFI Shell built-in 'bcfg' command.
#
# Usage: BootMgr.nsh [list|add|del|moveup|movedn|help]
#

set CMD %1

if "%CMD%" == ""       goto ShowMenu
if /i "%CMD%" == "list"   goto ListBoot
if /i "%CMD%" == "add"    goto AddBoot
if /i "%CMD%" == "del"    goto DelBoot
if /i "%CMD%" == "moveup" goto MoveUp
if /i "%CMD%" == "movedn" goto MoveDown
if /i "%CMD%" == "help"   goto ShowHelp
goto ShowMenu

:ShowMenu
echo ============================================================
echo   UEFI Boot Manager
echo ============================================================
echo.
echo   list         - List all boot entries
echo   add          - Add a new boot entry (interactive)
echo   del  <N>     - Delete boot entry number N
echo   moveup <N>   - Move entry N one position up in BootOrder
echo   movedn <N>   - Move entry N one position down in BootOrder
echo   help         - Show this help
echo.
echo ============================================================
echo.
goto ListBoot

:ListBoot
echo ============================================================
echo   Current Boot Options
echo ============================================================
echo.
echo [BootOrder variable]
bcfg boot dump -v
echo.

echo [EFI Global Variables: Boot####]
dmpstore -guid 8BE4DF61-93CA-11D2-AA0D-00E098032B8C -b
echo.
goto Done

:AddBoot
echo.
echo -- Add Boot Entry --
echo.
echo Available filesystems:
map -r
echo.
set /p ADD_DESC=Description (label):
set /p ADD_FS=Filesystem (e.g. FS0):
set /p ADD_PATH=EFI image path (e.g. \EFI\BOOT\BOOTx64.EFI):
set /p ADD_OPT=Optional load options (leave blank for none):

echo.
echo Adding boot entry:
echo   Label : %ADD_DESC%
echo   File  : %ADD_FS%:%ADD_PATH%
bcfg boot add 0 %ADD_FS%:%ADD_PATH% "%ADD_DESC%"
echo.
echo Entry added at position 0.  Run 'BootMgr.nsh list' to verify.
goto Done

:DelBoot
set ENTRY_NUM %2
if "%ENTRY_NUM%" == "" goto DelPrompt
goto DoDel

:DelPrompt
echo.
bcfg boot dump
echo.
set /p ENTRY_NUM=Enter boot entry number to delete:

:DoDel
echo.
echo Deleting boot entry %ENTRY_NUM% ...
bcfg boot rm %ENTRY_NUM%
echo Done.
goto Done

:MoveUp
set ENTRY_NUM %2
if "%ENTRY_NUM%" == "" goto MoveUpPrompt
goto DoMoveUp

:MoveUpPrompt
echo.
bcfg boot dump
echo.
set /p ENTRY_NUM=Enter boot entry number to move up:

:DoMoveUp
set /a NEW_NUM=%ENTRY_NUM%-1
if %NEW_NUM% lss 0 set NEW_NUM=0
echo Moving entry %ENTRY_NUM% to position %NEW_NUM% ...
bcfg boot mv %ENTRY_NUM% %NEW_NUM%
echo Done.
goto Done

:MoveDown
set ENTRY_NUM %2
if "%ENTRY_NUM%" == "" goto MoveDnPrompt
goto DoMoveDn

:MoveDnPrompt
echo.
bcfg boot dump
echo.
set /p ENTRY_NUM=Enter boot entry number to move down:

:DoMoveDn
set /a NEW_NUM=%ENTRY_NUM%+1
echo Moving entry %ENTRY_NUM% to position %NEW_NUM% ...
bcfg boot mv %ENTRY_NUM% %NEW_NUM%
echo Done.
goto Done

:ShowHelp
echo.
echo BootMgr.nsh - UEFI Boot Option Manager
echo.
echo   BootMgr.nsh list
echo       Display all BootXXXX variables and the BootOrder.
echo.
echo   BootMgr.nsh add
echo       Interactively add a new EFI boot entry.
echo.
echo   BootMgr.nsh del [N]
echo       Delete boot entry number N (prompts if N is omitted).
echo.
echo   BootMgr.nsh moveup [N]
echo       Move boot entry N one step toward the front of BootOrder.
echo.
echo   BootMgr.nsh movedn [N]
echo       Move boot entry N one step toward the back of BootOrder.
echo.

:Done
echo ============================================================
echo   Boot Manager done.
echo ============================================================
echo.
