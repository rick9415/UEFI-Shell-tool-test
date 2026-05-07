@echo -off
#
# SysInfo.nsh - System Information Script
#
# Displays CPU architecture, memory map, UEFI firmware version,
# and other platform information available from the UEFI Shell.
#
# Usage: SysInfo.nsh
#

echo ============================================================
echo   System Information
echo ============================================================
echo.

# ---- Firmware / Shell version -----------------------------------
echo [Firmware]
ver
echo.

# ---- CPU Architecture -------------------------------------------
echo [CPU Architecture]
if x%PROCESSOR_ARCHITECTURE% == xAMD64  echo   Architecture : x86_64 (AMD64)
if x%PROCESSOR_ARCHITECTURE% == xIA32   echo   Architecture : x86 (IA-32)
if x%PROCESSOR_ARCHITECTURE% == xAARCH64 echo   Architecture : ARM64 (AArch64)
if x%PROCESSOR_ARCHITECTURE% == xARM   echo   Architecture : ARM (32-bit)
echo.

# ---- Memory Map -------------------------------------------------
echo [Memory Map]
memmap
echo.

# ---- Block Devices ----------------------------------------------
echo [Storage Devices]
map -r
echo.

# ---- Loaded Protocols (brief) -----------------------------------
echo [Key Protocols on Handle 0]
dh -p 01 -d
echo.

# ---- Time / Date ------------------------------------------------
echo [System Time]
time
date
echo.

echo ============================================================
echo   Done.
echo ============================================================
echo.
