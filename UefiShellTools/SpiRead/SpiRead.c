/**
  SpiRead - UEFI Shell SPI flash region reader via PCH BIOS Control / SPIBAR.

  Works on Intel PCH (Sunrise Point and later) where the SPI controller is a
  PCI device at Bus 0 / Dev 31 / Fn 5 (LPC/eSPI bridge hides the SPI BAR at
  D31:F5 offset 0x10 on older PCH; on newer PCH it is at a fixed MMIO bar
  at SPIBAR = RCBA+0x3800 or D31:F5 BAR0).

  This tool:
    1. Locates the PCH SPI MMIO BAR through D31:F5 PCI config.
    2. Reads the Flash Descriptor region map (FLMAP0/1).
    3. Lists all flash regions with base/limit.
    4. Reads a chosen region (or arbitrary offset+length) using Hardware
       Sequencing (HSFC/HSFS) to drive the SPI controller directly.

  Usage:
    SpiRead info                         - show SPI controller info and regions
    SpiRead read <RegionId> [-f <File>]  - read a named region (0-7)
    SpiRead raw  <Offset> <Len> [-f <File>]  - read arbitrary flash bytes

  CAUTION: Writes to SPI flash are intentionally not implemented — flash
  corruption without a flash programmer is difficult to recover from.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/ShellLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/IoLib.h>
#include <Protocol/PciRootBridgeIo.h>
#include <IndustryStandard/Pci.h>

#define SPIREAD_VERSION   L"0.1"

/* PCH SPI Controller: Bus 0, Dev 31, Fn 5 */
#define SPI_BUS  0
#define SPI_DEV  31
#define SPI_FN   5

/* SPI MMIO register offsets (relative to SPIBAR) */
#define SPI_BFPREG   0x00   /* BIOS Flash Primary Region */
#define SPI_HSFS     0x04   /* Hardware Sequencing Flash Status */
#define SPI_HSFC     0x06   /* Hardware Sequencing Flash Control */
#define SPI_FADDR    0x08   /* Flash Address */
#define SPI_FDATA0   0x10   /* Flash Data 0 (first of 16 DWORDs) */
#define SPI_FRAP     0x50   /* Flash Region Access Permissions */
#define SPI_FREG_BASE 0x54  /* Flash Region 0..7 descriptors (8 × 4 bytes) */
#define SPI_FDOC     0xB4   /* Flash Descriptor Observability Control */
#define SPI_FDOD     0xB8   /* Flash Descriptor Observability Data */

/* HSFS bits */
#define HSFS_FDONE   BIT0
#define HSFS_FCERR   BIT1
#define HSFS_BERASE  BIT3
#define HSFS_SCIP    BIT5

/* HSFC bits */
#define HSFC_GO      BIT0
#define HSFC_FCYCLE_READ  (0 << 1)
#define HSFC_FDBC(n) (((n) & 0x3F) << 8)  /* byte count - 1 */

/* Max bytes per HW-seq read transaction */
#define SPI_MAX_READ_PER_OP  64

STATIC CONST CHAR16 *RegionNames[] = {
  L"Descriptor", L"BIOS",  L"ME",    L"GbE",
  L"Platform",   L"Reg5",  L"Reg6",  L"Reg7",
};

typedef struct {
  UINT32  Base;    /* byte offset in flash */
  UINT32  Limit;   /* inclusive byte limit */
  BOOLEAN Exists;
} SPI_REGION;

/* ---- MMIO helpers (32-bit accesses to SPI BAR) ---- */
STATIC UINT32
SpiRead32 (UINTN Bar, UINT32 Off) { return MmioRead32 (Bar + Off); }

STATIC UINT16
SpiRead16 (UINTN Bar, UINT32 Off) { return MmioRead16 (Bar + Off); }

STATIC VOID
SpiWrite32 (UINTN Bar, UINT32 Off, UINT32 Val) { MmioWrite32 (Bar + Off, Val); }

STATIC VOID
SpiWrite16 (UINTN Bar, UINT32 Off, UINT16 Val) { MmioWrite16 (Bar + Off, Val); }

/* Locate the SPI controller MMIO BAR via EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL. */
STATIC EFI_STATUS
GetSpiBar (
  OUT UINTN *SpiBar
  )
{
  EFI_STATUS                        Status;
  EFI_HANDLE                       *Handles;
  UINTN                             Count = 0;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *Rb = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol, &gEfiPciRootBridgeIoProtocolGuid,
                  NULL, &Count, &Handles);
  if (EFI_ERROR (Status) || Count == 0) {
    return EFI_NOT_FOUND;
  }
  gBS->HandleProtocol (Handles[0], &gEfiPciRootBridgeIoProtocolGuid,
                        (VOID **)&Rb);
  FreePool (Handles);

  UINT16 VendorId = 0xFFFF;
  Rb->Pci.Read (Rb, EfiPciWidthUint16,
                EFI_PCI_ADDRESS (SPI_BUS, SPI_DEV, SPI_FN, PCI_VENDOR_ID_OFFSET),
                1, &VendorId);
  if (VendorId == 0xFFFF) {
    Print (L"SpiRead: PCH SPI controller not found at %02x:%02x.%x\n",
           SPI_BUS, SPI_DEV, SPI_FN);
    return EFI_NOT_FOUND;
  }

  UINT32 Bar0 = 0;
  Rb->Pci.Read (Rb, EfiPciWidthUint32,
                EFI_PCI_ADDRESS (SPI_BUS, SPI_DEV, SPI_FN, PCI_BASE_ADDRESSREG_OFFSET),
                1, &Bar0);

  /* BAR0 is a 32-bit MMIO BAR; mask off flags. */
  *SpiBar = (UINTN)(Bar0 & 0xFFFFF000u);
  if (*SpiBar == 0) {
    Print (L"SpiRead: SPI BAR not assigned.\n");
    return EFI_NOT_FOUND;
  }
  return EFI_SUCCESS;
}

/* Read the 8 flash region descriptors from FREG registers. */
STATIC VOID
ReadRegions (
  IN  UINTN       Bar,
  OUT SPI_REGION *Regions,
  IN  UINTN       Count
  )
{
  for (UINTN i = 0; i < Count; i++) {
    UINT32 Freg = SpiRead32 (Bar, SPI_FREG_BASE + (UINT32)(i * 4));
    UINT32 Base  = (Freg & 0x00007FFF) << 12;
    UINT32 Limit = ((Freg >> 16) & 0x00007FFF);
    if (Limit == 0) {
      Regions[i].Exists = FALSE;
      Regions[i].Base  = 0;
      Regions[i].Limit = 0;
    } else {
      Regions[i].Exists = TRUE;
      Regions[i].Base  = Base;
      Regions[i].Limit = (Limit << 12) | 0xFFF;
    }
  }
}

/* Poll until a HW sequencing operation completes (FDONE or FCERR). */
STATIC EFI_STATUS
SpiWaitDone (
  IN UINTN Bar
  )
{
  UINT16  Hsfs;
  UINT32  Retry = 1000000;

  do {
    Hsfs = SpiRead16 (Bar, SPI_HSFS);
    if (Hsfs & (HSFS_FDONE | HSFS_FCERR)) {
      /* Clear status bits */
      SpiWrite16 (Bar, SPI_HSFS, Hsfs & (HSFS_FDONE | HSFS_FCERR));
      return (Hsfs & HSFS_FCERR) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
  } while (--Retry);

  return EFI_TIMEOUT;
}

/**
  Read up to SPI_MAX_READ_PER_OP bytes using hardware sequencing.
  FlashOffset is the absolute offset within the SPI flash device.
**/
STATIC EFI_STATUS
SpiHwRead (
  IN  UINTN   Bar,
  IN  UINT32  FlashOffset,
  IN  UINT32  ByteCount,       /* 1..64 */
  OUT UINT8  *Buffer
  )
{
  EFI_STATUS Status;

  if (ByteCount == 0 || ByteCount > SPI_MAX_READ_PER_OP) {
    return EFI_INVALID_PARAMETER;
  }

  /* Abort any in-progress cycle */
  UINT16 Hsfs = SpiRead16 (Bar, SPI_HSFS);
  if (Hsfs & HSFS_SCIP) {
    return EFI_NOT_READY;
  }

  /* Set flash address */
  SpiWrite32 (Bar, SPI_FADDR, FlashOffset & 0x07FFFFFF);

  /* Trigger read: FDBC = ByteCount-1, FCYCLE = 0 (read), GO */
  UINT16 Hsfc = (UINT16)(HSFC_FDBC (ByteCount - 1) | HSFC_FCYCLE_READ | HSFC_GO);
  SpiWrite16 (Bar, SPI_HSFC, Hsfc);

  Status = SpiWaitDone (Bar);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  /* Copy result from FDATA registers (little-endian DWORDs) */
  UINT32 DWords = (ByteCount + 3) / 4;
  for (UINT32 d = 0; d < DWords; d++) {
    UINT32 Word = SpiRead32 (Bar, SPI_FDATA0 + d * 4);
    for (UINT32 b = 0; b < 4 && (d * 4 + b) < ByteCount; b++) {
      Buffer[d * 4 + b] = (UINT8)(Word >> (b * 8));
    }
  }
  return EFI_SUCCESS;
}

/* Read an arbitrary span of flash, iterating over 64-byte windows. */
STATIC EFI_STATUS
SpiReadSpan (
  IN  UINTN         Bar,
  IN  UINT32        FlashOffset,
  IN  UINT32        Length,
  OUT UINT8        *Buffer
  )
{
  EFI_STATUS Status;
  UINT32     Done = 0;

  while (Done < Length) {
    UINT32 Chunk = Length - Done;
    if (Chunk > SPI_MAX_READ_PER_OP) {
      Chunk = SPI_MAX_READ_PER_OP;
    }
    Status = SpiHwRead (Bar, FlashOffset + Done, Chunk, Buffer + Done);
    if (EFI_ERROR (Status)) {
      Print (L"SpiRead: HW read error at offset 0x%x: %r\n",
             FlashOffset + Done, Status);
      return Status;
    }
    Done += Chunk;

    /* Progress indicator every 64 KB */
    if ((Done & 0xFFFF) == 0) {
      Print (L"\r  Reading... %lu KB", (UINT64)(Done / 1024));
    }
  }
  Print (L"\r                          \r");
  return EFI_SUCCESS;
}

STATIC VOID
PrintInfo (
  IN UINTN       Bar,
  IN SPI_REGION *Regions
  )
{
  UINT32 Bfpreg = SpiRead32 (Bar, SPI_BFPREG);
  UINT32 Hsfs16 = SpiRead16 (Bar, SPI_HSFS);

  Print (L"\nSPI Controller MMIO BAR: 0x%lx\n", (UINT64)Bar);
  Print (L"BFPREG  = 0x%08x  (BIOS Flash Primary Region)\n", Bfpreg);
  Print (L"HSFS    = 0x%04x\n\n", Hsfs16 & 0xFFFF);

  Print (L"Flash Region Map:\n");
  Print (L"  ID  Name           Base         Limit        Size\n");
  Print (L"  --  -----------    ----------   ----------   ----------\n");
  for (UINTN i = 0; i < 8; i++) {
    if (Regions[i].Exists) {
      UINT32 Size = Regions[i].Limit - Regions[i].Base + 1;
      Print (L"  %lu   %-12s   0x%08x   0x%08x   %lu KB\n",
             (UINT64)i,
             i < ARRAY_SIZE(RegionNames) ? RegionNames[i] : L"?",
             Regions[i].Base, Regions[i].Limit,
             (UINT64)(Size / 1024));
    } else {
      Print (L"  %lu   %-12s   (not present)\n",
             (UINT64)i,
             i < ARRAY_SIZE(RegionNames) ? RegionNames[i] : L"?");
    }
  }
}

INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16 **Argv
  )
{
  EFI_STATUS  Status;
  UINTN       Bar;
  SPI_REGION  Regions[8];

  if (Argc < 2) {
    Print (L"SpiRead v%s - SPI Flash Region Reader\n\n", SPIREAD_VERSION);
    Print (L"Usage:\n");
    Print (L"  SpiRead info\n");
    Print (L"  SpiRead read <RegionId(0-7)> [-f <File>]\n");
    Print (L"  SpiRead raw  <FlashOffset> <Length> [-f <File>]\n\n");
    Print (L"Reads SPI flash via Intel PCH hardware sequencing.\n");
    Print (L"Read-only — no write support by design.\n");
    return EFI_INVALID_PARAMETER;
  }

  Status = GetSpiBar (&Bar);
  if (EFI_ERROR (Status)) {
    Print (L"Error: cannot locate SPI MMIO BAR (%r).\n", Status);
    return Status;
  }

  ReadRegions (Bar, Regions, 8);

  /* ---- info ---- */
  if (StrCmp (Argv[1], L"info") == 0) {
    PrintInfo (Bar, Regions);
    return EFI_SUCCESS;
  }

  UINT32        FlashOffset = 0, Length = 0;
  CONST CHAR16 *OutFile = NULL;

  /* ---- read <RegionId> ---- */
  if (StrCmp (Argv[1], L"read") == 0) {
    if (Argc < 3) {
      Print (L"Error: need <RegionId>.\n");
      return EFI_INVALID_PARAMETER;
    }
    UINT32 RId = (UINT32)StrDecimalToUint64 (Argv[2]);
    if (RId > 7 || !Regions[RId].Exists) {
      Print (L"Error: region %u not present.\n", RId);
      return EFI_NOT_FOUND;
    }
    FlashOffset = Regions[RId].Base;
    Length      = Regions[RId].Limit - Regions[RId].Base + 1;
    if (Argc >= 5 && StrCmp (Argv[3], L"-f") == 0) {
      OutFile = Argv[4];
    }
    Print (L"Reading region %u (%s): 0x%08x..0x%08x (%lu KB)\n",
           RId, RegionNames[RId], FlashOffset,
           Regions[RId].Limit, (UINT64)(Length / 1024));
  }

  /* ---- raw <Offset> <Len> ---- */
  else if (StrCmp (Argv[1], L"raw") == 0) {
    if (Argc < 4) {
      Print (L"Error: need <FlashOffset> <Length>.\n");
      return EFI_INVALID_PARAMETER;
    }
    FlashOffset = (UINT32)StrHexToUint64 (Argv[2]);
    Length      = (UINT32)StrHexToUint64 (Argv[3]);
    if (Argc >= 6 && StrCmp (Argv[4], L"-f") == 0) {
      OutFile = Argv[5];
    }
    Print (L"Reading flash 0x%08x + 0x%x bytes\n", FlashOffset, Length);
  } else {
    Print (L"Error: unknown command '%s'.\n", Argv[1]);
    return EFI_INVALID_PARAMETER;
  }

  if (Length == 0 || Length > 16 * 1024 * 1024) {
    Print (L"Error: Length must be 1..16M.\n");
    return EFI_INVALID_PARAMETER;
  }

  UINT8 *Buf = AllocatePool (Length);
  if (Buf == NULL) {
    Print (L"Error: out of memory.\n");
    return EFI_OUT_OF_RESOURCES;
  }

  Status = SpiReadSpan (Bar, FlashOffset, Length, Buf);
  if (EFI_ERROR (Status)) {
    FreePool (Buf);
    return Status;
  }

  /* Print first 256 bytes as a quick preview */
  UINT32 Preview = Length < 256 ? Length : 256;
  Print (L"\nFirst %u bytes:\n", Preview);
  for (UINT32 Row = 0; Row < Preview; Row += 16) {
    Print (L"  %08x  ", FlashOffset + Row);
    for (UINT32 Col = 0; Col < 16 && Row + Col < Preview; Col++) {
      Print (L"%02x ", Buf[Row + Col]);
    }
    Print (L"\n");
  }

  if (OutFile != NULL) {
    SHELL_FILE_HANDLE Fh;
    UINTN Written = Length;
    Status = ShellOpenFileByName (OutFile, &Fh,
               EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR (Status)) {
      ShellWriteFile (Fh, &Written, Buf);
      ShellCloseFile (&Fh);
      Print (L"Saved %lu bytes to %s\n", (UINT64)Written, OutFile);
    } else {
      Print (L"Error: cannot write to '%s': %r\n", OutFile, Status);
    }
  }

  FreePool (Buf);
  return EFI_SUCCESS;
}
