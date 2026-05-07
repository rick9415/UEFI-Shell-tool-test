## @file
#  UefiToolkitAmi.dsc - AMI Aptio V Build Descriptor
#
#  Use this DSC when building inside an AMI Aptio V workspace.
#  Key differences from UefiToolkit.dsc:
#    - Library paths reference edk2/ subdirectory (AMI convention)
#    - RegisterFilterLib removed (not in older Aptio EDK2 forks)
#    - AmiCompatibilityPkg DebugLib used when available
#    - Toolchain tag uses VS2019 / GCC5 matching AMI's tools_def.txt
#
#  Build commands (run from AptioV workspace root after sourcing edksetup):
#    Windows + VS2019:
#      build -p UefiToolkitPkg\UefiToolkitAmi.dsc -a X64 -t VS2019
#
#    Linux + GCC5:
#      build -p UefiToolkitPkg/UefiToolkitAmi.dsc -a X64 -t GCC5
#
#    32-bit:
#      build -p UefiToolkitPkg/UefiToolkitAmi.dsc -a IA32 -t GCC5
#
#  Output: Build/UefiToolkitPkg/X64/  (or IA32/)
#            SysInfo.efi
#            MemTest.efi
#            DiskInfo.efi
#            PciInfo.efi
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = UefiToolkitPkg
  PLATFORM_GUID                  = A1B2C3D4-E5F6-7890-ABCD-EF1234567891
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/UefiToolkitPkg
  SUPPORTED_ARCHITECTURES        = IA32|X64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

# ---------------------------------------------------------------------------
# Library Classes
# AMI Aptio V nests upstream EDK2 under the "edk2/" subdirectory.
# Adjust the prefix below to match your workspace layout.
# If your Aptio tree has MdePkg at the root level, remove the "edk2/" prefix.
# ---------------------------------------------------------------------------

[LibraryClasses]
  # --- Entry point ----------------------------------------------------------
  UefiApplicationEntryPoint|edk2/MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  ShellCEntryLib|edk2/ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf

  # --- Shell ----------------------------------------------------------------
  ShellLib|edk2/ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  FileHandleLib|edk2/MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  SortLib|edk2/MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf

  # --- Base -----------------------------------------------------------------
  BaseLib|edk2/MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|edk2/MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  PrintLib|edk2/MdePkg/Library/BasePrintLib/BasePrintLib.inf
  MemoryAllocationLib|edk2/MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DevicePathLib|edk2/MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf

  # --- UEFI -----------------------------------------------------------------
  UefiLib|edk2/MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|edk2/MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|edk2/MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiHiiServicesLib|edk2/MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  HiiLib|edk2/MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf

  # --- Debug ----------------------------------------------------------------
  # Option A: print debug messages to ConOut (safe default)
  DebugLib|edk2/MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf

  # Option B: use AMI's debug library if available in your Aptio tree:
  # DebugLib|AmiModulePkg/Library/AmiDebugLib/AmiDebugLib.inf

  DebugPrintErrorLevelLib|edk2/MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf

  # --- Miscellaneous --------------------------------------------------------
  PcdLib|edk2/MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  # RegisterFilterLib was added in EDK2 after ~202105; omit for older AMI forks.
  # Uncomment if your Aptio EDK2 submodule is recent enough:
  # RegisterFilterLib|edk2/MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf

# ---------------------------------------------------------------------------
# PCDs
# ---------------------------------------------------------------------------

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x1F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000040
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0x80000040

  gEfiShellPkgTokenSpaceGuid.PcdShellLibAutoInitialize|FALSE

# ---------------------------------------------------------------------------
# Components
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
  # GCC (Linux)
  GCC:RELEASE_*_*_CC_FLAGS    = -DNDEBUG
  GCC:DEBUG_*_*_CC_FLAGS      = -g -O0

  # MSFT / VS2019 (Windows) - AMI typically ships VS2015/VS2019 configs
  MSFT:RELEASE_*_*_CC_FLAGS   = /DNDEBUG /O2
  MSFT:DEBUG_*_*_CC_FLAGS     = /Zi /Od
