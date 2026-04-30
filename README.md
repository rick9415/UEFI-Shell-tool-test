# UEFI Shell Tools

EDK2-based UEFI Shell applications for pre-OS hardware debugging.  
All tools run directly under the UEFI Shell before any OS loads.

## Tools

### MemDump — Physical Memory Dump
Reads physical memory via direct pointer access (UEFI identity-maps RAM).  
Annotates each 16-byte row with the EFI memory-map region type.

```
MemDump <PhysAddr> <Length> [Width] [-f <File>]

MemDump 0xFED00000 0x1000          # HPET registers
MemDump 0x7EF00000 0x200 4         # DWORD-aligned dump
MemDump 0xE0000    0x20000 1 -f legacy.bin
```

`Width` controls display grouping (1/2/4/8).  
`-f` saves a raw binary to a shell file.

---

### PciEdit — PCI Config Space Viewer / Editor
Uses `EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL` for correct ECAM/MMIO access
across multi-segment systems.

```
PciEdit list
PciEdit dump  <Seg> <Bus> <Dev> <Fn>
PciEdit read  <Seg> <Bus> <Dev> <Fn> <Offset> <Width(1|2|4)>
PciEdit write <Seg> <Bus> <Dev> <Fn> <Offset> <Width(1|2|4)> <Value>

PciEdit list
PciEdit dump  0 0 1f 0          # PCH LPC/eSPI
PciEdit read  0 0 02 0 10 4     # GPU BAR0
PciEdit write 0 0 1f 3 04 2 06  # set Command register
```

All addresses are hex; `Seg` selects which root bridge.

---

### SpiRead — SPI Flash Region Reader
Locates the Intel PCH SPI controller at D31:F5, maps the MMIO BAR,
reads the Flash Descriptor region table, and reads flash using
**hardware sequencing** (HSFC/HSFS registers — 64 bytes per transaction).

```
SpiRead info
SpiRead read <RegionId(0-7)> [-f <File>]
SpiRead raw  <FlashOffset> <Length> [-f <File>]

SpiRead info
SpiRead read 1 -f bios.bin      # dump BIOS region
SpiRead raw  0x0 0x1000         # first 4 KB (descriptor)
```

Region IDs: 0=Descriptor 1=BIOS 2=ME 3=GbE 4=Platform 5-7=reserved.  
**Read-only by design** — no write support.

---

### NvramOps — NVRAM Variable Batch Operations
Full CRUD and batch export/import of EFI variables using `gRT`.

```
NvramOps list   [-g <GUID>] [-p <Prefix>]
NvramOps get    <Name> <GUID>
NvramOps set    <Name> <GUID> <Attrs> <HexData>
NvramOps del    <Name> <GUID>
NvramOps export [-g <GUID>] [-p <Prefix>] <File>
NvramOps import <File>

NvramOps list -p Boot
NvramOps get BootOrder 8be4df61-93ca-11d2-aa0d-00e098032b8c
NvramOps export -p Boot boot_backup.nvr
NvramOps import boot_backup.nvr
```

**Attr flags** (add values): `1`=NV `2`=BS `4`=RT — standard combo is `7`.  
**HexData**: contiguous hex string, e.g. `01000000`.  
Export format: one variable per line — `<GUID> <Name> <Attrs> <HexData>`.

> **CAUTION**: Deleting or corrupting firmware variables (`BootOrder`, `Setup`,
> etc.) can render the system unbootable.

---

## Building

Requires an EDK2 workspace with BaseTools compiled.

```bash
EDK2_DIR=/path/to/edk2 bash build.sh
# or
bash build.sh /path/to/edk2 X64 GCC5
```

The script symlinks `UefiShellTools/` into the EDK2 tree and calls `build`.  
Output `.efi` files land in:

```
$EDK2_DIR/Build/UefiShellTools/DEBUG_GCC5/X64/
  MemDump.efi
  PciEdit.efi
  SpiRead.efi
  NvramOps.efi
```

Copy them to a FAT32 USB stick or EFI system partition and launch from the
UEFI Shell:

```
Shell> fs0:
fs0:\> MemDump.efi 0xFED00000 0x400
```

### Manual EDK2 build (no script)

```bash
cd /path/to/edk2
ln -s /path/to/this/repo/UefiShellTools .
source edksetup.sh
build -p UefiShellTools/UefiShellTools.dsc -a X64 -t GCC5 -b DEBUG
```

---

## UEFI Protocol Reference

| Tool | Key protocols / tables |
|------|------------------------|
| MemDump | `gBS->GetMemoryMap`, direct physical pointer |
| PciEdit | `EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL` (Pci.Read/Write) |
| SpiRead | PCI config via root bridge I/O + MMIO SPIBAR registers |
| NvramOps | `gRT->GetNextVariableName`, `GetVariable`, `SetVariable` |

## Directory Layout

```
UefiShellTools/
  UefiShellTools.dec      Package declaration
  UefiShellTools.dsc      Platform build description
  MemDump/
    MemDump.inf
    MemDump.c
  PciEdit/
    PciEdit.inf
    PciEdit.c
  SpiRead/
    SpiRead.inf
    SpiRead.c
  NvramOps/
    NvramOps.inf
    NvramOps.c
build.sh                  Convenience build wrapper
```
