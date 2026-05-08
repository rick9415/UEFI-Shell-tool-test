# UEFI Shell Toolkit

A comprehensive UEFI Shell toolkit providing system diagnostic and management utilities. The toolkit includes both UEFI Shell scripts (`.nsh`) for quick use and full C UEFI applications built with EDK2.

## Structure

```
.
├── Scripts/                  # UEFI Shell scripts (.nsh)
│   ├── UefiToolkit.nsh       # Main interactive menu
│   ├── SysInfo.nsh           # System information
│   ├── MemTest.nsh           # Memory pattern test
│   ├── DevEnum.nsh           # Device/protocol enumeration
│   ├── BootMgr.nsh           # Boot option manager
│   └── DiskUtil.nsh          # Disk and partition utilities
├── Applications/             # C UEFI applications (EDK2)
│   ├── SysInfo/              # Full system info (SMBIOS, ACPI, CPU)
│   ├── MemTest/              # Memory walk & pattern test
│   ├── DiskInfo/             # Block device & partition info
│   └── PciInfo/              # PCI device enumeration
├── Include/                  # Shared headers
├── UefiToolkit.dsc           # EDK2 package build descriptor
└── UefiToolkit.dec           # EDK2 package declaration
```

## Shell Scripts

Copy the `Scripts/` directory to your EFI partition (e.g. `FS0:\Tools\`) and run from the UEFI Shell:

```
FS0:\> Tools\UefiToolkit.nsh
```

Or run individual scripts:

```
FS0:\> Tools\SysInfo.nsh
FS0:\> Tools\MemTest.nsh  64
FS0:\> Tools\DevEnum.nsh
FS0:\> Tools\BootMgr.nsh
FS0:\> Tools\DiskUtil.nsh
```

## C Applications (EDK2 Build)

### Prerequisites

- [EDK2](https://github.com/tianocore/edk2) source tree
- NASM ≥ 2.15
- Python ≥ 3.6
- GCC or Visual Studio toolchain

### Build

```bash
# Place this repo inside your EDK2 workspace as UefiToolkitPkg/
cd <edk2-workspace>
git clone <this-repo> UefiToolkitPkg

source edksetup.sh
build -p UefiToolkitPkg/UefiToolkit.dsc -a X64 -t GCC5

# Windows
cmdedksetup.bat
build -p UefiToolkitPkg/UefiToolkit.dsc -a X64 -t VS2019
```

Compiled `.efi` files appear under `Build/UefiToolkitPkg/`.

Copy them to your EFI partition and run from the UEFI Shell:

```
FS0:\> SysInfo.efi
FS0:\> MemTest.efi  -s 64  -p 3
FS0:\> DiskInfo.efi
FS0:\> PciInfo.efi  -v
```

## Tools Reference

| Tool | Script | Application | Description |
|---|---|---|---|
| System Info | `SysInfo.nsh` | `SysInfo.efi` | CPU, memory, BIOS, SMBIOS data |
| Memory Test | `MemTest.nsh` | `MemTest.efi` | Walking-bit and pattern memory test |
| Device Enum | `DevEnum.nsh` | — | All handle/protocol enumeration |
| Boot Manager | `BootMgr.nsh` | — | View/modify UEFI boot options |
| Disk Utility | `DiskUtil.nsh` | `DiskInfo.efi` | Block devices and partition tables |
| PCI Info | — | `PciInfo.efi` | PCI/PCIe device tree |

## License

BSD 2-Clause License. See individual source files for details.
