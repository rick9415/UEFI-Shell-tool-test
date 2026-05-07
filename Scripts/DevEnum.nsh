@echo -off
#
# DevEnum.nsh - Device and Protocol Enumeration
#
# Lists all UEFI handles and the protocols installed on each,
# then queries common protocol GUIDs individually.
#
# Usage: DevEnum.nsh [-v]
#   -v   Verbose: dump full detail on each handle
#

set VERBOSE %1

echo ============================================================
echo   UEFI Device and Protocol Enumeration
echo ============================================================
echo.

# ---- All handles -----------------------------------------------
echo [All Handles]
dh -b
echo.

# ---- Handles by protocol class --------------------------------
echo [Block I/O Devices]
dh -p BlockIo -d
echo.

echo [Disk I/O Devices]
dh -p DiskIo -d
echo.

echo [Simple File System Devices]
dh -p SimpleFileSystem -d
echo.

echo [Network Interface (SimpleNetwork)]
dh -p SimpleNetwork -d
echo.

echo [GOP (Graphics Output Protocol)]
dh -p GraphicsOutput -d
echo.

echo [USB Host Controllers]
dh -p UsbHc -d
echo.

echo [USB2 Host Controllers]
dh -p Usb2Hc -d
echo.

echo [PCIe Root Bridges]
dh -p PciRootBridgeIo -d
echo.

echo [PCI I/O Devices]
dh -p PciIo -d
echo.

echo [Serial I/O Devices]
dh -p SerialIo -d
echo.

# ---- Driver model binding table --------------------------------
echo [Driver Binding Handles]
drivers
echo.

# ---- Device tree -----------------------------------------------
echo [Device Tree]
devtree
echo.

# ---- Verbose detail per handle --------------------------------
if /i "%VERBOSE%" == "-v" goto VerboseDetail
goto Done

:VerboseDetail
echo [Verbose Handle Detail]
dh -v
echo.

:Done
echo ============================================================
echo   Enumeration complete.
echo ============================================================
echo.
