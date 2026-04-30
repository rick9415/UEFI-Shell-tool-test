/**
  MemDump - UEFI Shell physical memory dump utility.

  Usage: MemDump <PhysAddr> <Length> [Width] [-f <File>]
    PhysAddr  Physical address in hex (e.g. 0xFED00000)
    Length    Number of bytes in hex
    Width     Access width: 1, 2, 4, 8 (default 1)
    -f File   Save raw binary to file path

  Reads physical memory using direct pointer access (identity-mapped in UEFI).
  Maps EFI memory map to annotate regions.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/ShellLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#define MEMDUMP_VERSION L"0.1"
#define BYTES_PER_ROW   16

/* Describe an EFI memory type as a short tag. */
STATIC CONST CHAR16 *
MemTypeStr (
  IN UINT32  Type
  )
{
  STATIC CONST CHAR16 *Names[] = {
    L"Reserved",   L"LoaderCode",  L"LoaderData",  L"BSCode",
    L"BSData",     L"RTCode",      L"RTData",       L"Conv",
    L"Unusable",   L"ACPIReclaim", L"ACPIMemNVS",   L"MMIO",
    L"MMIOPort",   L"PalCode",     L"Persistent",
  };
  if (Type < ARRAY_SIZE (Names)) {
    return Names[Type];
  }
  return L"Unknown";
}

/* Read the EFI memory map; caller must FreePool the returned buffer. */
STATIC EFI_MEMORY_DESCRIPTOR *
GetMemoryMap (
  OUT UINTN  *MapSize,
  OUT UINTN  *DescSize
  )
{
  EFI_STATUS           Status;
  UINTN                MapKey;
  UINT32               DescVer;
  EFI_MEMORY_DESCRIPTOR *Map = NULL;

  *MapSize = 0;
  Status = gBS->GetMemoryMap (MapSize, NULL, &MapKey, DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return NULL;
  }
  *MapSize += 2 * (*DescSize);
  Map = AllocatePool (*MapSize);
  if (Map == NULL) {
    return NULL;
  }
  Status = gBS->GetMemoryMap (MapSize, Map, &MapKey, DescSize, &DescVer);
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return NULL;
  }
  return Map;
}

/* Return the EFI memory type covering PhysAddr, or -1 if not found. */
STATIC INT32
LookupMemType (
  IN EFI_MEMORY_DESCRIPTOR *Map,
  IN UINTN                  MapSize,
  IN UINTN                  DescSize,
  IN EFI_PHYSICAL_ADDRESS   Addr
  )
{
  EFI_MEMORY_DESCRIPTOR *Desc;
  UINTN                  Offset;

  for (Offset = 0; Offset < MapSize; Offset += DescSize) {
    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Offset);
    if (Addr >= Desc->PhysicalStart &&
        Addr < Desc->PhysicalStart + Desc->NumberOfPages * EFI_PAGE_SIZE)
    {
      return (INT32)Desc->Type;
    }
  }
  return -1;
}

/* Hex dump with ASCII sidebar, annotating row start with memory type. */
STATIC VOID
HexDump (
  IN UINT8                 *Data,
  IN UINTN                  Len,
  IN EFI_PHYSICAL_ADDRESS   Base,
  IN UINTN                  Width,
  IN EFI_MEMORY_DESCRIPTOR *Map,
  IN UINTN                  MapSize,
  IN UINTN                  DescSize
  )
{
  UINTN  Row, Col, Idx;
  INT32  Type;

  for (Row = 0; Row < Len; Row += BYTES_PER_ROW) {
    /* Address + optional memory type tag */
    Type = LookupMemType (Map, MapSize, DescSize, Base + Row);
    if (Type >= 0) {
      Print (L"%016lx  [%-12s]  ", Base + Row, MemTypeStr ((UINT32)Type));
    } else {
      Print (L"%016lx                   ", Base + Row);
    }

    /* Hex columns, grouped by access width */
    for (Col = 0; Col < BYTES_PER_ROW; Col++) {
      if (Col > 0 && (Col % Width) == 0) {
        Print (L" ");
      }
      Idx = Row + Col;
      if (Idx < Len) {
        Print (L"%02x", Data[Idx]);
      } else {
        Print (L"  ");
      }
    }

    /* ASCII sidebar */
    Print (L"  |");
    for (Col = 0; Col < BYTES_PER_ROW; Col++) {
      Idx = Row + Col;
      if (Idx < Len) {
        UINT8 C = Data[Idx];
        Print (L"%c", (C >= 0x20 && C < 0x7f) ? C : '.');
      } else {
        Print (L" ");
      }
    }
    Print (L"|\n");
  }
}

/* Save raw bytes to a shell file path. */
STATIC EFI_STATUS
SaveToFile (
  IN CONST CHAR16 *Path,
  IN UINT8        *Data,
  IN UINTN         Len
  )
{
  SHELL_FILE_HANDLE  Handle;
  EFI_STATUS         Status;
  UINTN              Written = Len;

  Status = ShellOpenFileByName (Path, &Handle,
             EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ,
             0);
  if (EFI_ERROR (Status)) {
    Print (L"Error: cannot open '%s' for write: %r\n", Path, Status);
    return Status;
  }
  Status = ShellWriteFile (Handle, &Written, Data);
  ShellCloseFile (&Handle);
  if (EFI_ERROR (Status)) {
    Print (L"Error: write failed: %r\n", Status);
  } else {
    Print (L"Saved %lu bytes to %s\n", (UINT64)Written, Path);
  }
  return Status;
}

INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16 **Argv
  )
{
  EFI_PHYSICAL_ADDRESS   PhysAddr;
  UINTN                  Length, Width = 1;
  CONST CHAR16          *OutFile = NULL;
  UINT8                 *Data;
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN                  MapSize, DescSize;

  if (Argc < 3) {
    Print (L"MemDump v%s - Physical Memory Dump Utility\n", MEMDUMP_VERSION);
    Print (L"\nUsage: MemDump <PhysAddr> <Length> [Width] [-f <File>]\n");
    Print (L"  PhysAddr  Physical address (hex, e.g. 0xFED00000)\n");
    Print (L"  Length    Byte count (hex)\n");
    Print (L"  Width     Access granularity: 1, 2, 4, or 8 (default 1)\n");
    Print (L"  -f File   Save raw binary to file\n\n");
    Print (L"Examples:\n");
    Print (L"  MemDump 0xFED00000 0x1000          # HPET registers\n");
    Print (L"  MemDump 0x7EF00000 0x200 4         # DWORD-aligned dump\n");
    Print (L"  MemDump 0xE0000    0x20000 1 -f mm.bin  # Legacy BIOS area\n");
    return EFI_INVALID_PARAMETER;
  }

  PhysAddr = StrHexToUint64 (Argv[1]);
  Length   = (UINTN)StrHexToUint64 (Argv[2]);

  /* Parse optional Width and -f flag */
  for (UINTN i = 3; i < Argc; i++) {
    if (StrCmp (Argv[i], L"-f") == 0) {
      if (i + 1 < Argc) {
        OutFile = Argv[++i];
      }
    } else {
      UINTN W = (UINTN)StrDecimalToUint64 (Argv[i]);
      if (W == 1 || W == 2 || W == 4 || W == 8) {
        Width = W;
      }
    }
  }

  if (Length == 0 || Length > 256 * 1024 * 1024) {
    Print (L"Error: Length must be 1..256M bytes.\n");
    return EFI_INVALID_PARAMETER;
  }

  /* Allocate read buffer and copy from physical address */
  Data = AllocatePool (Length);
  if (Data == NULL) {
    Print (L"Error: out of memory.\n");
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem (Data, (VOID *)(UINTN)PhysAddr, Length);

  /* Get memory map for region annotation */
  Map = GetMemoryMap (&MapSize, &DescSize);

  Print (L"\nPhysical memory dump: 0x%016lx + 0x%lx bytes (width=%lu)\n\n",
         PhysAddr, (UINT64)Length, (UINT64)Width);

  HexDump (Data, Length, PhysAddr, Width,
           Map, Map ? MapSize : 0, Map ? DescSize : 0);

  if (OutFile != NULL) {
    SaveToFile (OutFile, Data, Length);
  }

  if (Map != NULL) {
    FreePool (Map);
  }
  FreePool (Data);
  return EFI_SUCCESS;
}
