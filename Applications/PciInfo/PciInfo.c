/** @file
  PciInfo.efi - UEFI PCI/PCIe Device Enumeration Tool

  Walks all PCI Root Bridge I/O handles and enumerates every PCI
  function reachable from them.  For each device it prints:
    - Bus/Device/Function address
    - Vendor ID, Device ID, Revision
    - Class Code (base class, sub-class, programming interface)
    - Subsystem Vendor/Device IDs (for header type 0)
    - BAR summary
    - Human-readable class description

  Usage:
    PciInfo.efi [-v] [-bif] [-b <Bus>] [-h]

  Options:
    -v         Verbose: show all BARs and full config space dump
    -bif       PCIe bifurcation analysis: list all Root Ports and Downstream
               Ports with Max Link Width vs Negotiated Link Width.  When
               NegWidth < MaxWidth the port is marked as a bifurcation
               candidate (could also be a device with a narrower interface).
    -b <Bus>   Limit scan to this PCI bus number
    -h         Help

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/ShellCEntryLib.h>
#include <Library/ShellLib.h>
#include <Library/DebugLib.h>

#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci.h>

#define TOOLKIT_VERSION  L"1.0.0"

// Standard PCI config space offsets (header type 0)
#define PCI_VENDOR_ID_OFFSET         0x00
#define PCI_DEVICE_ID_OFFSET         0x02
#define PCI_COMMAND_OFFSET           0x04
#define PCI_STATUS_OFFSET            0x06
#define PCI_REVISION_ID_OFFSET       0x08
#define PCI_PROG_IF_OFFSET           0x09
#define PCI_SUBCLASS_OFFSET          0x0A
#define PCI_BASECLASS_OFFSET         0x0B
#define PCI_HEADER_TYPE_OFFSET       0x0E
#define PCI_BAR0_OFFSET              0x10
#define PCI_SUBSYSTEM_VENDOR_OFFSET  0x2C
#define PCI_SUBSYSTEM_ID_OFFSET      0x2E
#define PCI_CAPABILITIES_PTR_OFFSET  0x34

// PCI express capability ID
#define PCI_CAPABILITY_ID_PCIEXP     0x10

// PCIe capability structure offsets (relative to capability base)
#define PCIE_CAP_REG_OFFSET          0x02  // PCIe Capabilities Register
#define PCIE_CAP_LINK_CAP_OFFSET     0x0C  // Link Capabilities Register (32-bit)
#define PCIE_CAP_LINK_STA_OFFSET     0x12  // Link Status Register (16-bit)

// PCIe port type values (bits 7:4 of PCIe Capabilities Register)
#define PCIE_PORT_TYPE_ROOT_PORT     0x4
#define PCIE_PORT_TYPE_SW_DOWNSTREAM 0x6

// ---------------------------------------------------------------------------
// Class code table
// ---------------------------------------------------------------------------

typedef struct {
  UINT8         BaseClass;
  UINT8         SubClass;
  CONST CHAR16 *Name;
} PCI_CLASS_ENTRY;

STATIC CONST PCI_CLASS_ENTRY gPciClassTable[] = {
  { 0x00, 0x00, L"Non-VGA Unclassified Device" },
  { 0x00, 0x01, L"VGA-Compatible Unclassified Device" },
  { 0x01, 0x00, L"SCSI Bus Controller" },
  { 0x01, 0x01, L"IDE Controller" },
  { 0x01, 0x02, L"Floppy Disk Controller" },
  { 0x01, 0x05, L"ATA Controller" },
  { 0x01, 0x06, L"SATA Controller" },
  { 0x01, 0x07, L"SAS Controller" },
  { 0x01, 0x08, L"NVM Express Controller" },
  { 0x01, 0x80, L"Mass Storage Controller (Other)" },
  { 0x02, 0x00, L"Ethernet Controller" },
  { 0x02, 0x80, L"Network Controller (Other)" },
  { 0x03, 0x00, L"VGA-Compatible Display Controller" },
  { 0x03, 0x01, L"XGA Display Controller" },
  { 0x03, 0x02, L"3D Controller (non-VGA)" },
  { 0x03, 0x80, L"Display Controller (Other)" },
  { 0x04, 0x00, L"Video Device" },
  { 0x04, 0x01, L"Audio Device" },
  { 0x04, 0x03, L"HD Audio Controller" },
  { 0x06, 0x00, L"Host Bridge" },
  { 0x06, 0x01, L"ISA Bridge" },
  { 0x06, 0x04, L"PCI-to-PCI Bridge" },
  { 0x06, 0x09, L"PCI-to-PCI Bridge (Semi-transparent)" },
  { 0x06, 0x0A, L"InfiniBand-to-PCI Host Bridge" },
  { 0x06, 0x80, L"Bridge (Other)" },
  { 0x07, 0x00, L"Serial Controller" },
  { 0x07, 0x01, L"Parallel Controller" },
  { 0x07, 0x80, L"Communication Controller (Other)" },
  { 0x08, 0x00, L"PIC" },
  { 0x08, 0x01, L"DMA Controller" },
  { 0x08, 0x02, L"Timer" },
  { 0x08, 0x03, L"RTC Controller" },
  { 0x08, 0x05, L"SD Host Controller" },
  { 0x08, 0x06, L"IOMMU" },
  { 0x08, 0x80, L"System Peripheral (Other)" },
  { 0x0C, 0x00, L"FireWire (IEEE 1394)" },
  { 0x0C, 0x03, L"USB Controller" },
  { 0x0C, 0x04, L"Fibre Channel" },
  { 0x0C, 0x05, L"SMBus Controller" },
  { 0x0C, 0x07, L"IPMI SMIC Interface" },
  { 0x0C, 0x80, L"Serial Bus Controller (Other)" },
  { 0x0D, 0x00, L"iRDA Controller" },
  { 0x0D, 0x11, L"Bluetooth Controller" },
  { 0x0D, 0x12, L"Broadband Controller" },
  { 0x0D, 0x20, L"802.11a Wireless Controller" },
  { 0x0D, 0x21, L"802.11b Wireless Controller" },
  { 0x0D, 0x80, L"Wireless Controller (Other)" },
  { 0x0E, 0x00, L"I2O Controller" },
  { 0x0F, 0x01, L"TV Controller" },
  { 0x0F, 0x02, L"Audio Controller" },
  { 0x0F, 0x03, L"Voice Communications Controller" },
  { 0x0F, 0x04, L"Data Communications Controller" },
  { 0x11, 0x00, L"DPIO Modules" },
  { 0x11, 0x01, L"Performance Counters" },
  { 0x12, 0x00, L"Processing Accelerator" },
  { 0x13, 0x00, L"Non-Essential Instrumentation" },
  { 0xFF, 0xFF, NULL } // sentinel
};

STATIC SHELL_FILE_HANDLE gOutputFile = NULL;

STATIC VOID
FPrint (
  IN CONST CHAR16  *Fmt,
  ...
  )
{
  VA_LIST  Args;
  CHAR16   Buf[1024];
  CHAR8    AsciiBuf[1024];
  UINTN    Len;

  VA_START (Args, Fmt);
  UnicodeVSPrint (Buf, sizeof (Buf), Fmt, Args);
  VA_END (Args);

  Print (L"%s", Buf);

  if (gOutputFile != NULL) {
    UnicodeStrToAsciiStrS (Buf, AsciiBuf, sizeof (AsciiBuf));
    Len = AsciiStrLen (AsciiBuf);
    ShellWriteFile (gOutputFile, &Len, AsciiBuf);
  }
}

STATIC CONST CHAR16 *
PciClassToString (
  IN UINT8  BaseClass,
  IN UINT8  SubClass
  )
{
  UINTN i;

  for (i = 0; gPciClassTable[i].Name != NULL; i++) {
    if (gPciClassTable[i].BaseClass == BaseClass &&
        gPciClassTable[i].SubClass  == SubClass) {
      return gPciClassTable[i].Name;
    }
  }
  // Try base class only
  for (i = 0; gPciClassTable[i].Name != NULL; i++) {
    if (gPciClassTable[i].BaseClass == BaseClass) {
      return gPciClassTable[i].Name;
    }
  }
  return L"Unknown Device";
}

// ---------------------------------------------------------------------------
// PCI config read helpers
// ---------------------------------------------------------------------------

STATIC EFI_STATUS
PciRead8 (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN  UINTN                             Bus,
  IN  UINTN                             Dev,
  IN  UINTN                             Func,
  IN  UINTN                             Reg,
  OUT UINT8                            *Value
  )
{
  UINT64 Address;
  Address = EFI_PCI_ADDRESS (Bus, Dev, Func, Reg);
  return RbIo->Pci.Read (RbIo, EfiPciWidthUint8, Address, 1, Value);
}

STATIC EFI_STATUS
PciRead16 (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN  UINTN                             Bus,
  IN  UINTN                             Dev,
  IN  UINTN                             Func,
  IN  UINTN                             Reg,
  OUT UINT16                           *Value
  )
{
  UINT64 Address;
  Address = EFI_PCI_ADDRESS (Bus, Dev, Func, Reg);
  return RbIo->Pci.Read (RbIo, EfiPciWidthUint16, Address, 1, Value);
}

STATIC EFI_STATUS
PciRead32 (
  IN  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN  UINTN                             Bus,
  IN  UINTN                             Dev,
  IN  UINTN                             Func,
  IN  UINTN                             Reg,
  OUT UINT32                           *Value
  )
{
  UINT64 Address;
  Address = EFI_PCI_ADDRESS (Bus, Dev, Func, Reg);
  return RbIo->Pci.Read (RbIo, EfiPciWidthUint32, Address, 1, Value);
}

// ---------------------------------------------------------------------------
// PCIe capability helpers
// ---------------------------------------------------------------------------

STATIC UINT8
FindPcieCapOffset (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN UINTN                             Bus,
  IN UINTN                             Dev,
  IN UINTN                             Func
  )
{
  UINT8  CapPtr;
  UINT8  CapId;
  UINT8  Next;
  UINTN  Guard;

  PciRead8 (RbIo, Bus, Dev, Func, PCI_CAPABILITIES_PTR_OFFSET, &CapPtr);
  CapPtr &= 0xFC;
  Guard = 0;
  while (CapPtr != 0 && Guard++ < 48) {
    PciRead8 (RbIo, Bus, Dev, Func, CapPtr, &CapId);
    if (CapId == PCI_CAPABILITY_ID_PCIEXP) {
      return CapPtr;
    }
    PciRead8 (RbIo, Bus, Dev, Func, CapPtr + 1, &Next);
    CapPtr = Next & 0xFC;
  }
  return 0;
}

STATIC CONST CHAR16 *
PcieLinkWidthStr (
  IN UINT8  Width
  )
{
  switch (Width) {
    case  1: return L"x1";
    case  2: return L"x2";
    case  4: return L"x4";
    case  8: return L"x8";
    case 16: return L"x16";
    case 32: return L"x32";
    default: return L"x?";
  }
}

// ---------------------------------------------------------------------------
// BAR display
// ---------------------------------------------------------------------------

STATIC VOID
ShowBars (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN UINTN                             Bus,
  IN UINTN                             Dev,
  IN UINTN                             Func,
  IN UINT8                             HeaderType
  )
{
  UINT8  BarCount;
  UINTN  i;
  UINT32 Bar;
  CHAR16 Buf[64];

  BarCount = (HeaderType == 0) ? 6 : 2;

  for (i = 0; i < BarCount; i++) {
    PciRead32 (RbIo, Bus, Dev, Func, PCI_BAR0_OFFSET + i * 4, &Bar);
    if (Bar == 0) {
      continue;
    }

    if (Bar & BIT0) {
      // I/O BAR
      UnicodeSPrint (Buf, sizeof (Buf), L"BAR%lu: I/O  0x%04X", (UINT64)i, Bar & 0xFFFFFFFC);
    } else if ((Bar >> 1 & 0x3) == 2) {
      // 64-bit MMIO BAR - read the upper half too
      UINT32 BarHi = 0;
      if (i + 1 < BarCount) {
        PciRead32 (RbIo, Bus, Dev, Func, PCI_BAR0_OFFSET + (i + 1) * 4, &BarHi);
      }
      UnicodeSPrint (Buf, sizeof (Buf), L"BAR%lu: MMIO 0x%08X%08X (64-bit)%s",
                     (UINT64)i, BarHi, Bar & 0xFFFFFFF0,
                     (Bar & BIT3) ? L" [prefetchable]" : L"");
      i++; // skip upper DWORD
    } else {
      // 32-bit MMIO BAR
      UnicodeSPrint (Buf, sizeof (Buf), L"BAR%lu: MMIO 0x%08X (32-bit)%s",
                     (UINT64)i, Bar & 0xFFFFFFF0,
                     (Bar & BIT3) ? L" [prefetchable]" : L"");
    }
    FPrint (L"    %s\n", Buf);
  }
}

// ---------------------------------------------------------------------------
// Scan a single PCI function
// ---------------------------------------------------------------------------

STATIC VOID
ScanPciFunction (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN UINTN                             Bus,
  IN UINTN                             Dev,
  IN UINTN                             Func,
  IN BOOLEAN                           Verbose
  )
{
  EFI_STATUS  Status;
  UINT16      VendorId;
  UINT16      DeviceId;
  UINT8       RevisionId;
  UINT8       BaseClass;
  UINT8       SubClass;
  UINT8       ProgIf;
  UINT8       HeaderType;
  UINT16      SubsysVendorId;
  UINT16      SubsysId;
  CHAR16      Buf[128];

  Status = PciRead16 (RbIo, Bus, Dev, Func, PCI_VENDOR_ID_OFFSET, &VendorId);
  if (EFI_ERROR (Status) || VendorId == 0xFFFF || VendorId == 0x0000) {
    return;
  }

  PciRead16 (RbIo, Bus, Dev, Func, PCI_DEVICE_ID_OFFSET,   &DeviceId);
  PciRead8  (RbIo, Bus, Dev, Func, PCI_REVISION_ID_OFFSET,  &RevisionId);
  PciRead8  (RbIo, Bus, Dev, Func, PCI_BASECLASS_OFFSET,    &BaseClass);
  PciRead8  (RbIo, Bus, Dev, Func, PCI_SUBCLASS_OFFSET,     &SubClass);
  PciRead8  (RbIo, Bus, Dev, Func, PCI_PROG_IF_OFFSET,      &ProgIf);
  PciRead8  (RbIo, Bus, Dev, Func, PCI_HEADER_TYPE_OFFSET,  &HeaderType);
  HeaderType &= 0x7F; // mask multi-function bit

  UnicodeSPrint (Buf, sizeof (Buf),
                 L"%04X:%04X  Rev %02X  Class %02X:%02X:%02X  [%s]",
                 VendorId, DeviceId, RevisionId,
                 BaseClass, SubClass, ProgIf,
                 PciClassToString (BaseClass, SubClass));
  FPrint (L"  %02X:%02X.%X  %s\n", Bus, Dev, Func, Buf);

  if (Verbose) {
    if (HeaderType == 0) {
      PciRead16 (RbIo, Bus, Dev, Func, PCI_SUBSYSTEM_VENDOR_OFFSET, &SubsysVendorId);
      PciRead16 (RbIo, Bus, Dev, Func, PCI_SUBSYSTEM_ID_OFFSET,     &SubsysId);
      FPrint (L"            Subsystem: %04X:%04X\n", SubsysVendorId, SubsysId);
    }
    ShowBars (RbIo, Bus, Dev, Func, HeaderType);
  }
}

// ---------------------------------------------------------------------------
// Scan a PCI bus
// ---------------------------------------------------------------------------

STATIC VOID
ScanPciBus (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN UINTN                             Bus,
  IN BOOLEAN                           Verbose
  )
{
  UINTN  Dev;
  UINTN  Func;
  UINT8  HeaderType;
  UINT16 VendorId;

  for (Dev = 0; Dev < 32; Dev++) {
    // Check device exists
    PciRead16 (RbIo, Bus, Dev, 0, PCI_VENDOR_ID_OFFSET, &VendorId);
    if (VendorId == 0xFFFF || VendorId == 0x0000) {
      continue;
    }

    PciRead8 (RbIo, Bus, Dev, 0, PCI_HEADER_TYPE_OFFSET, &HeaderType);

    ScanPciFunction (RbIo, Bus, Dev, 0, Verbose);

    // Multi-function device
    if (HeaderType & BIT7) {
      for (Func = 1; Func < 8; Func++) {
        ScanPciFunction (RbIo, Bus, Dev, Func, Verbose);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// PCIe bifurcation analysis
// ---------------------------------------------------------------------------

STATIC VOID
PrintBifurcationAnalysis (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo,
  IN UINTN                             BridgeIndex
  )
{
  UINTN          Bus;
  UINTN          Dev;
  UINTN          Func;
  UINTN          FuncMax;
  UINT16         VendorId;
  UINT8          BaseClass;
  UINT8          HeaderType;
  UINT8          CapOff;
  UINT16         PcieCap;
  UINT32         LinkCap;
  UINT16         LinkSta;
  UINT8          PortType;
  UINT8          MaxW;
  UINT8          NegW;
  BOOLEAN        HeaderPrinted;
  CONST CHAR16  *Hint;

  HeaderPrinted = FALSE;

  for (Bus = 0; Bus < 256; Bus++) {
    for (Dev = 0; Dev < 32; Dev++) {
      PciRead16 (RbIo, Bus, Dev, 0, PCI_VENDOR_ID_OFFSET, &VendorId);
      if (VendorId == 0xFFFF || VendorId == 0x0000) {
        continue;
      }

      PciRead8 (RbIo, Bus, Dev, 0, PCI_HEADER_TYPE_OFFSET, &HeaderType);
      FuncMax = (HeaderType & BIT7) ? 8 : 1;

      for (Func = 0; Func < FuncMax; Func++) {
        if (Func > 0) {
          PciRead16 (RbIo, Bus, Dev, Func, PCI_VENDOR_ID_OFFSET, &VendorId);
          if (VendorId == 0xFFFF || VendorId == 0x0000) {
            continue;
          }
        }

        // Root and downstream ports always have bridge class code
        PciRead8 (RbIo, Bus, Dev, Func, PCI_BASECLASS_OFFSET, &BaseClass);
        if (BaseClass != 0x06) {
          continue;
        }

        CapOff = FindPcieCapOffset (RbIo, Bus, Dev, Func);
        if (CapOff == 0) {
          continue;
        }

        PciRead16 (RbIo, Bus, Dev, Func, CapOff + PCIE_CAP_REG_OFFSET, &PcieCap);
        PortType = (UINT8)((PcieCap >> 4) & 0xF);
        if (PortType != PCIE_PORT_TYPE_ROOT_PORT &&
            PortType != PCIE_PORT_TYPE_SW_DOWNSTREAM) {
          continue;
        }

        PciRead32 (RbIo, Bus, Dev, Func, CapOff + PCIE_CAP_LINK_CAP_OFFSET, &LinkCap);
        PciRead16 (RbIo, Bus, Dev, Func, CapOff + PCIE_CAP_LINK_STA_OFFSET, &LinkSta);
        MaxW = (UINT8)((LinkCap >> 4) & 0x3F);
        NegW = (UINT8)((LinkSta >> 4) & 0x3F);

        if (!HeaderPrinted) {
          FPrint (L"\n  [Root Bridge %lu - PCIe Bifurcation Analysis]\n\n", (UINT64)BridgeIndex);
          FPrint (L"  %-10s  %-14s  %-8s  %-8s  %s\n",
                  L"BDF", L"Port Type", L"MaxWidth", L"NegWidth", L"Note");
          FPrint (L"  %-10s  %-14s  %-8s  %-8s  %s\n",
                  L"----------", L"--------------", L"--------", L"--------", L"----");
          HeaderPrinted = TRUE;
        }

        if (NegW == 0) {
          Hint = L"No link / empty";
        } else if (NegW < MaxW) {
          Hint = L"[*] Width reduced - bifurcated or narrow device";
        } else {
          Hint = L"Full width";
        }

        FPrint (L"  %02X:%02X.%X  %-14s  %-8s  %-8s  %s\n",
                Bus, Dev, Func,
                (PortType == PCIE_PORT_TYPE_ROOT_PORT) ? L"Root Port" : L"SW Downstream",
                PcieLinkWidthStr (MaxW),
                PcieLinkWidthStr (NegW),
                Hint);
      }
    }
  }

  if (!HeaderPrinted) {
    FPrint (L"\n  [Root Bridge %lu] No PCIe Root/Downstream ports detected.\n",
            (UINT64)BridgeIndex);
  }
}

// ---------------------------------------------------------------------------
// Entry Point
// ---------------------------------------------------------------------------

INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16  **Argv
  )
{
  EFI_STATUS                        Status;
  BOOLEAN                           Verbose;
  BOOLEAN                           Bifurcation;
  INTN                              FilterBus;
  UINTN                             i;
  EFI_HANDLE                       *Handles;
  UINTN                             HandleCount;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RbIo;
  UINTN                             Bus;

  Verbose      = FALSE;
  Bifurcation  = FALSE;
  FilterBus    = -1;

  for (i = 1; i < Argc; i++) {
    if (StrnCmp (Argv[i], L"-v", 2) == 0) {
      Verbose = TRUE;
    } else if (StrnCmp (Argv[i], L"-bif", 4) == 0) {
      Bifurcation = TRUE;
    } else if (StrnCmp (Argv[i], L"-b", 2) == 0 && (i + 1) < Argc) {
      FilterBus = (INTN)StrDecimalToUintn (Argv[++i]);
    } else if (StrnCmp (Argv[i], L"-h", 2) == 0 ||
               StrnCmp (Argv[i], L"--help", 6) == 0) {
      Print (L"Usage: PciInfo.efi [-v] [-bif] [-b Bus] [-h]\n");
      Print (L"  -v        Verbose (BARs, subsystem IDs)\n");
      Print (L"  -bif      PCIe bifurcation analysis (link width per root port)\n");
      Print (L"  -b <bus>  Scan only this bus number\n");
      Print (L"  -h        Help\n");
      return 0;
    }
  }

  // Automatically save output to PciInfo.txt
  {
    EFI_STATUS  FileStatus;
    FileStatus = ShellOpenFileByName (
                   L"PciInfo.txt",
                   &gOutputFile,
                   EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ,
                   0
                   );
    if (EFI_ERROR (FileStatus)) {
      Print (L"Warning: Cannot create PciInfo.txt (%r)\n", FileStatus);
      gOutputFile = NULL;
    }
  }

  FPrint (L"\n============================================================\n");
  FPrint (L"  UEFI PCI/PCIe Device Information Tool v%s\n", TOOLKIT_VERSION);
  FPrint (L"============================================================\n\n");

  // Print column header
  FPrint (L"  %-10s  %-62s\n", L"BDF", L"VendID:DevID  Rev  Class  [Description]");
  FPrint (L"  %-10s  %-62s\n", L"----------", L"--------------------------------------------------------------");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciRootBridgeIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    FPrint (L"  No PCI Root Bridges found (%r).\n", Status);
    if (gOutputFile != NULL) {
      ShellCloseFile (&gOutputFile);
      gOutputFile = NULL;
    }
    return 1;
  }

  for (i = 0; i < HandleCount; i++) {
    Status = gBS->HandleProtocol (
                    Handles[i],
                    &gEfiPciRootBridgeIoProtocolGuid,
                    (VOID **)&RbIo
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    FPrint (L"\n  [Root Bridge %lu]\n\n", (UINT64)i);

    if (FilterBus >= 0) {
      ScanPciBus (RbIo, (UINTN)FilterBus, Verbose);
    } else {
      for (Bus = 0; Bus < 256; Bus++) {
        ScanPciBus (RbIo, Bus, Verbose);
      }
    }

    if (Bifurcation) {
      PrintBifurcationAnalysis (RbIo, i);
    }
  }

  FreePool (Handles);

  FPrint (L"\n============================================================\n");
  FPrint (L"  Done. Press any key to exit.\n");
  FPrint (L"============================================================\n\n");

  if (gOutputFile != NULL) {
    ShellCloseFile (&gOutputFile);
    gOutputFile = NULL;
    Print (L"  Output saved to: PciInfo.txt\n\n");
  }

  {
    UINTN        Index;
    EFI_INPUT_KEY Key;
    gST->ConIn->Reset (gST->ConIn, FALSE);
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  }

  return 0;
}
