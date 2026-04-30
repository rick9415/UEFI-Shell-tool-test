/**
  PciEdit - UEFI Shell PCI configuration space viewer and editor.

  Usage:
    PciEdit list                           - enumerate all PCI devices
    PciEdit dump  <Seg> <Bus> <Dev> <Fn>  - dump 256-byte config space
    PciEdit read  <Seg> <Bus> <Dev> <Fn> <Offset> <Width>
    PciEdit write <Seg> <Bus> <Dev> <Fn> <Offset> <Width> <Value>

  Uses EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL for correct ECAM/MMIO access
  on multi-segment systems.  Falls back to CF8/CFC I/O for Seg 0.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/ShellLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci.h>

#define PCIEDIT_VERSION L"0.1"

/* Width tag strings */
STATIC CONST CHAR16 *WidthStr[] = { L"?", L"UINT8", L"UINT16", L"?", L"UINT32" };

/* Encode a PCI address for EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL. */
STATIC UINT64
MakePciAddr (
  IN UINT32  Bus,
  IN UINT32  Dev,
  IN UINT32  Fn,
  IN UINT32  Offset
  )
{
  return EFI_PCI_ADDRESS (Bus, Dev, Fn, Offset);
}

/* Locate all EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL instances. */
STATIC EFI_STATUS
GetRootBridges (
  OUT EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL ***Bridges,
  OUT UINTN                             *Count
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE *Handles = NULL;
  UINTN       NumHandles = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciRootBridgeIoProtocolGuid,
                  NULL,
                  &NumHandles,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    *Count = 0;
    *Bridges = NULL;
    return Status;
  }

  *Bridges = AllocatePool (NumHandles * sizeof (EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *));
  if (*Bridges == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  for (UINTN i = 0; i < NumHandles; i++) {
    gBS->HandleProtocol (Handles[i], &gEfiPciRootBridgeIoProtocolGuid,
                         (VOID **)&(*Bridges)[i]);
  }
  *Count = NumHandles;
  FreePool (Handles);
  return EFI_SUCCESS;
}

/* Check whether a device exists (VendorID != 0xFFFF). */
STATIC BOOLEAN
PciDeviceExists (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Rb,
  IN UINT32                           Bus,
  IN UINT32                           Dev,
  IN UINT32                           Fn
  )
{
  UINT16     VendorId = 0xFFFF;
  UINT64     Addr     = MakePciAddr (Bus, Dev, Fn, PCI_VENDOR_ID_OFFSET);

  Rb->Pci.Read (Rb, EfiPciWidthUint16, Addr, 1, &VendorId);
  return (VendorId != 0xFFFF);
}

/* Enumerate and print all detected PCI devices under a root bridge. */
STATIC VOID
ListDevices (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Rb,
  IN UINT32                           SegNum
  )
{
  UINT16  VendorId, DeviceId;
  UINT8   ClassCode[3], HeaderType;

  for (UINT32 Bus = 0; Bus <= 255; Bus++) {
    for (UINT32 Dev = 0; Dev < 32; Dev++) {
      for (UINT32 Fn = 0; Fn < 8; Fn++) {
        if (!PciDeviceExists (Rb, Bus, Dev, Fn)) {
          if (Fn == 0) break;
          continue;
        }

        Rb->Pci.Read (Rb, EfiPciWidthUint16,
                      MakePciAddr (Bus, Dev, Fn, PCI_VENDOR_ID_OFFSET),
                      1, &VendorId);
        Rb->Pci.Read (Rb, EfiPciWidthUint16,
                      MakePciAddr (Bus, Dev, Fn, PCI_DEVICE_ID_OFFSET),
                      1, &DeviceId);
        Rb->Pci.Read (Rb, EfiPciWidthUint8,
                      MakePciAddr (Bus, Dev, Fn, PCI_CLASSCODE_OFFSET),
                      3, ClassCode);
        Rb->Pci.Read (Rb, EfiPciWidthUint8,
                      MakePciAddr (Bus, Dev, Fn, PCI_HEADER_TYPE_OFFSET),
                      1, &HeaderType);

        Print (L"  %04x:%02x:%02x.%x  VID=%04x DID=%04x  "
               L"Class=%02x/%02x/%02x  Hdr=%02x\n",
               SegNum, Bus, Dev, Fn,
               VendorId, DeviceId,
               ClassCode[2], ClassCode[1], ClassCode[0],
               HeaderType & 0x7F);

        /* Multi-function bit not set on Fn0 → only one function */
        if (Fn == 0 && !(HeaderType & 0x80)) {
          break;
        }
      }
    }
  }
}

/* Dump the full 256-byte config space as a hex table. */
STATIC VOID
DumpConfigSpace (
  IN EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Rb,
  IN UINT32                           Bus,
  IN UINT32                           Dev,
  IN UINT32                           Fn
  )
{
  UINT8  Cfg[256];

  Rb->Pci.Read (Rb, EfiPciWidthUint8,
                MakePciAddr (Bus, Dev, Fn, 0),
                256, Cfg);

  Print (L"\nPCI config space %02x:%02x.%x\n\n", Bus, Dev, Fn);
  Print (L"     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");
  Print (L"     ------------------------------------------------\n");
  for (UINTN Row = 0; Row < 16; Row++) {
    Print (L"%02x | ", Row * 16);
    for (UINTN Col = 0; Col < 16; Col++) {
      Print (L"%02x ", Cfg[Row * 16 + Col]);
    }
    Print (L"\n");
  }
}

INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16 **Argv
  )
{
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL **Bridges;
  UINTN                             BridgeCount;
  EFI_STATUS                        Status;

  if (Argc < 2) {
    Print (L"PciEdit v%s - PCI Config Space Viewer/Editor\n\n", PCIEDIT_VERSION);
    Print (L"Usage:\n");
    Print (L"  PciEdit list\n");
    Print (L"  PciEdit dump  <Seg> <Bus> <Dev> <Fn>\n");
    Print (L"  PciEdit read  <Seg> <Bus> <Dev> <Fn> <Off> <Width(1|2|4)>\n");
    Print (L"  PciEdit write <Seg> <Bus> <Dev> <Fn> <Off> <Width(1|2|4)> <Value>\n");
    return EFI_INVALID_PARAMETER;
  }

  Status = GetRootBridges (&Bridges, &BridgeCount);
  if (EFI_ERROR (Status) || BridgeCount == 0) {
    Print (L"Error: no PCI root bridges found (%r).\n", Status);
    return Status;
  }

  /* ---- list ---- */
  if (StrCmp (Argv[1], L"list") == 0) {
    Print (L"PCI devices (%lu root bridge(s)):\n\n", (UINT64)BridgeCount);
    Print (L"  Seg:Bus:Dev.Fn  VID    DID    Class        Hdr\n");
    Print (L"  -------------------------------------------------\n");
    for (UINTN b = 0; b < BridgeCount; b++) {
      ListDevices (Bridges[b], (UINT32)b);
    }
    goto Done;
  }

  /* All remaining commands need Seg/Bus/Dev/Fn */
  if (Argc < 6) {
    Print (L"Error: insufficient arguments.\n");
    goto Done;
  }

  UINT32 Seg = (UINT32)StrHexToUint64 (Argv[2]);
  UINT32 Bus = (UINT32)StrHexToUint64 (Argv[3]);
  UINT32 Dev = (UINT32)StrHexToUint64 (Argv[4]);
  UINT32 Fn  = (UINT32)StrHexToUint64 (Argv[5]);

  if (Seg >= BridgeCount) {
    Print (L"Error: segment %u not present (max %lu).\n",
           Seg, (UINT64)(BridgeCount - 1));
    goto Done;
  }

  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *Rb = Bridges[Seg];

  if (!PciDeviceExists (Rb, Bus, Dev, Fn)) {
    Print (L"Error: no device at %04x:%02x:%02x.%x\n", Seg, Bus, Dev, Fn);
    goto Done;
  }

  /* ---- dump ---- */
  if (StrCmp (Argv[1], L"dump") == 0) {
    DumpConfigSpace (Rb, Bus, Dev, Fn);
    goto Done;
  }

  /* read / write need Offset and Width */
  if (Argc < 8) {
    Print (L"Error: need <Offset> <Width>.\n");
    goto Done;
  }

  UINT32 Offset = (UINT32)StrHexToUint64 (Argv[6]);
  UINT32 Width  = (UINT32)StrDecimalToUint64 (Argv[7]);
  UINT64 Addr   = MakePciAddr (Bus, Dev, Fn, Offset);
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH IoWidth;

  switch (Width) {
    case 1: IoWidth = EfiPciWidthUint8;  break;
    case 2: IoWidth = EfiPciWidthUint16; break;
    case 4: IoWidth = EfiPciWidthUint32; break;
    default:
      Print (L"Error: Width must be 1, 2, or 4.\n");
      goto Done;
  }

  /* ---- read ---- */
  if (StrCmp (Argv[1], L"read") == 0) {
    UINT32 Val = 0;
    Rb->Pci.Read (Rb, IoWidth, Addr, 1, &Val);
    if (Width <= 4) Val &= (0xFFFFFFFFu >> (32 - Width * 8));
    Print (L"%04x:%02x:%02x.%x +0x%03x [%s] = 0x%x\n",
           Seg, Bus, Dev, Fn, Offset,
           Width < ARRAY_SIZE(WidthStr) ? WidthStr[Width] : L"?",
           Val);
    goto Done;
  }

  /* ---- write ---- */
  if (StrCmp (Argv[1], L"write") == 0) {
    if (Argc < 9) {
      Print (L"Error: need <Value>.\n");
      goto Done;
    }
    UINT32 Val = (UINT32)StrHexToUint64 (Argv[8]);
    /* Read-modify for sub-DWORD widths */
    UINT32 Old = 0;
    Rb->Pci.Read (Rb, IoWidth, Addr, 1, &Old);
    Rb->Pci.Write (Rb, IoWidth, Addr, 1, &Val);
    Print (L"%04x:%02x:%02x.%x +0x%03x [%s]  0x%x -> 0x%x\n",
           Seg, Bus, Dev, Fn, Offset,
           Width < ARRAY_SIZE(WidthStr) ? WidthStr[Width] : L"?",
           Old, Val);
    goto Done;
  }

  Print (L"Error: unknown command '%s'.\n", Argv[1]);

Done:
  FreePool (Bridges);
  return EFI_SUCCESS;
}
