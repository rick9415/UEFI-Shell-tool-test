## @file
#  UefiToolkit.dsc - Platform Description for UEFI Shell Toolkit
#
#  Builds all four UEFI Shell applications:
#    SysInfo.efi   - System information (SMBIOS, memory map, firmware)
#    MemTest.efi   - Multi-pass memory pattern test
#    DiskInfo.efi  - Block device and partition enumeration
#    PciInfo.efi   - PCI/PCIe device tree
#
#  Build command (from EDK2 workspace root):
#    build -p UefiToolkitPkg/UefiToolkit.dsc -a X64 -t GCC5
#    build -p UefiToolkitPkg/UefiToolkit.dsc -a IA32 -t GCC5
#    build -p UefiToolkitPkg/UefiToolkit.dsc -a AARCH64 -t GCC5
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = UefiToolkitPkg
  PLATFORM_GUID                  = A1B2C3D4-E5F6-7890-ABCD-EF1234567890
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/UefiToolkitPkg
  SUPPORTED_ARCHITECTURES        = IA32|X64|AARCH64|ARM
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

# ---------------------------------------------------------------------------
# Library Classes
# ---------------------------------------------------------------------------

[LibraryClasses]
  # Entry point
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf

  # Shell
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf

  # Base
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf

  # UEFI
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf

  # Debug
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf

  # Miscellaneous
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf

# ---------------------------------------------------------------------------
# PCDs
# ---------------------------------------------------------------------------

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x1F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000040
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0x80000040

  gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE

# ---------------------------------------------------------------------------
# Components (Applications)
# ---------------------------------------------------------------------------

[Components]
  UefiToolkitPkg/Applications/SysInfo/SysInfo.inf
  UefiToolkitPkg/Applications/MemTest/MemTest.inf
  UefiToolkitPkg/Applications/DiskInfo/DiskInfo.inf
  UefiToolkitPkg/Applications/PciInfo/PciInfo.inf

# ---------------------------------------------------------------------------
# Build Options
# ---------------------------------------------------------------------------

[BuildOptions]
  GCC:RELEASE_*_*_CC_FLAGS    = -DNDEBUG
  GCC:DEBUG_*_*_CC_FLAGS      = -g
  MSFT:RELEASE_*_*_CC_FLAGS   = /DNDEBUG
  MSFT:DEBUG_*_*_CC_FLAGS     = /Zi /Od
