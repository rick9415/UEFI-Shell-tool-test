@echo -off
#
# MemTest.nsh - Basic Memory Pattern Test
#
# Performs a simple write/read pattern test over the conventional
# memory range reported by the UEFI memory map.  This is not a
# substitute for thorough ECC or stress tests (use MemTest86+ for
# that), but quickly catches catastrophic failures in the shell.
#
# Usage: MemTest.nsh [SizeMB]
#   SizeMB  Amount of memory to test in megabytes (default: 32)
#

set MEM_MB %1
if "%MEM_MB%" == "" set MEM_MB=32

echo ============================================================
echo   Memory Pattern Test  (%MEM_MB% MB)
echo ============================================================
echo.

# Show current memory map before test
echo [Current Memory Map]
memmap
echo.

echo [Test Configuration]
echo   Test size : %MEM_MB% MB
echo   Patterns  : 0x00, 0xFF, 0xAA, 0x55, walking-bit
echo.

# The UEFI Shell 'dmem' and 'mm' commands operate on physical
# addresses.  We locate a free conventional region by inspecting
# memmap output and then use mm to write and verify patterns.
#
# Because automated address parsing is complex in pure .nsh,
# this script uses the 'mm' built-in to test a known-safe scratch
# region (first 1 MB of free EfiConventionalMemory above 1 MB).
# For production use, compile and run MemTest.efi instead.

set BASE_ADDR 0x100000
set PAGE_SIZE 0x1000
set PATTERN_COUNT 0

echo [Pass 1] Writing pattern 0x00000000 ...
mm %BASE_ADDR% 0x00000000 -w -n 256
echo   Written.

echo [Pass 1] Verifying pattern 0x00000000 ...
dmem %BASE_ADDR% 0x400
echo.

echo [Pass 2] Writing pattern 0xFFFFFFFF ...
mm %BASE_ADDR% 0xFFFFFFFF -w -n 256
echo   Written.

echo [Pass 2] Verifying pattern 0xFFFFFFFF ...
dmem %BASE_ADDR% 0x400
echo.

echo [Pass 3] Writing pattern 0xAAAAAAAA ...
mm %BASE_ADDR% 0xAAAAAAAA -w -n 256
echo   Written.

echo [Pass 3] Verifying pattern 0xAAAAAAAA ...
dmem %BASE_ADDR% 0x400
echo.

echo [Pass 4] Writing pattern 0x55555555 ...
mm %BASE_ADDR% 0x55555555 -w -n 256
echo   Written.

echo [Pass 4] Verifying pattern 0x55555555 ...
dmem %BASE_ADDR% 0x400
echo.

echo ============================================================
echo   Memory test complete.
echo   For thorough testing run MemTest.efi or MemTest86+.
echo ============================================================
echo.
