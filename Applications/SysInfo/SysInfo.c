/** @file
  SysInfo.efi - UEFI System Information Tool

  Displays system information collected from:
    - SMBIOS tables (type 0 BIOS, type 1 System, type 4 CPU, type 17 Memory)
    - ACPI RSDP / FADT
    - UEFI firmware revision
    - Memory map

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/ShellCEntryLib.h>
#include <Library/ShellLib.h>
#include <Library/DebugLib.h>

#include <Protocol/Smbios.h>
#include <IndustryStandard/SmBios.h>

#define TOOLKIT_VERSION  L"1.0.0"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

STATIC VOID
PrintBanner (
  IN CONST CHAR16  *Title
  )
{
  Print (L"\n============================================================\n");
  Print (L"  %s\n", Title);
  Print (L"============================================================\n");
}

STATIC VOID
PrintKV (
  IN CONST CHAR16  *Key,
  IN CONST CHAR16  *Value
  )
{
  Print (L"  %-28s : %s\n", Key, Value);
}

STATIC VOID
PrintKVU (
  IN CONST CHAR16  *Key,
  IN UINTN          Value
  )
{
  CHAR16  Buf[32];

  UnicodeSPrint (Buf, sizeof (Buf), L"%lu", (UINT64)Value);
  PrintKV (Key, Buf);
}

// ---------------------------------------------------------------------------
// SMBIOS helpers
// ---------------------------------------------------------------------------

/**
  Return the string at index StringNumber from an SMBIOS record.
  SMBIOS strings are packed after the formatted area, separated by NUL bytes,
  terminated by a double-NUL.
**/
STATIC CONST CHAR8 *
SmbiosGetString (
  IN SMBIOS_STRUCTURE  *Record,
  IN UINT8              StringNumber
  )
{
  CONST CHAR8  *Str;
  UINT8         Index;

  if (StringNumber == 0) {
    return "N/A";
  }

  Str   = (CONST CHAR8 *)Record + Record->Length;
  Index = 1;

  while (*Str != '\0') {
    if (Index == StringNumber) {
      return Str;
    }
    Str += AsciiStrLen (Str) + 1;
    Index++;
  }

  return "N/A";
}

STATIC VOID
PrintSmbiosString (
  IN CONST CHAR16      *Key,
  IN SMBIOS_STRUCTURE  *Record,
  IN UINT8              StringNumber
  )
{
  CONST CHAR8  *Ascii;
  CHAR16        Wide[256];

  Ascii = SmbiosGetString (Record, StringNumber);
  AsciiStrToUnicodeStrS (Ascii, Wide, ARRAY_SIZE (Wide));
  PrintKV (Key, Wide);
}

// ---------------------------------------------------------------------------
// SMBIOS Type 0 - BIOS Information
// ---------------------------------------------------------------------------

STATIC VOID
ShowBiosInfo (
  IN EFI_SMBIOS_PROTOCOL  *Smbios
  )
{
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE0       *T0;
  EFI_STATUS               Status;
  CHAR16                   Buf[64];

  Type   = SMBIOS_TYPE_BIOS_INFORMATION;
  Handle = SMBIOS_HANDLE_PI_RESERVED;

  Status = Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL);
  if (EFI_ERROR (Status)) {
    Print (L"  SMBIOS Type 0 not found.\n");
    return;
  }

  T0 = (SMBIOS_TABLE_TYPE0 *)Record;

  PrintSmbiosString (L"BIOS Vendor",        &T0->Hdr, T0->Vendor);
  PrintSmbiosString (L"BIOS Version",       &T0->Hdr, T0->BiosVersion);
  PrintSmbiosString (L"BIOS Release Date",  &T0->Hdr, T0->BiosReleaseDate);

  UnicodeSPrint (Buf, sizeof (Buf), L"%u.%u",
                 T0->SystemBiosMajorRelease,
                 T0->SystemBiosMinorRelease);
  PrintKV (L"BIOS Release", Buf);

  UnicodeSPrint (Buf, sizeof (Buf), L"%u.%u",
                 T0->EmbeddedControllerFirmwareMajorRelease,
                 T0->EmbeddedControllerFirmwareMinorRelease);
  PrintKV (L"EC Firmware Release", Buf);
}

// ---------------------------------------------------------------------------
// SMBIOS Type 1 - System Information
// ---------------------------------------------------------------------------

STATIC VOID
ShowSystemInfo (
  IN EFI_SMBIOS_PROTOCOL  *Smbios
  )
{
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE1       *T1;
  EFI_STATUS               Status;
  CHAR16                   UuidBuf[64];
  EFI_GUID                 *Uuid;

  Type   = SMBIOS_TYPE_SYSTEM_INFORMATION;
  Handle = SMBIOS_HANDLE_PI_RESERVED;

  Status = Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL);
  if (EFI_ERROR (Status)) {
    Print (L"  SMBIOS Type 1 not found.\n");
    return;
  }

  T1   = (SMBIOS_TABLE_TYPE1 *)Record;
  Uuid = &T1->Uuid;

  PrintSmbiosString (L"Manufacturer", &T1->Hdr, T1->Manufacturer);
  PrintSmbiosString (L"Product Name", &T1->Hdr, T1->ProductName);
  PrintSmbiosString (L"Version",      &T1->Hdr, T1->Version);
  PrintSmbiosString (L"Serial Number",&T1->Hdr, T1->SerialNumber);
  PrintSmbiosString (L"SKU Number",   &T1->Hdr, T1->SKUNumber);
  PrintSmbiosString (L"Family",       &T1->Hdr, T1->Family);

  UnicodeSPrint (
    UuidBuf, sizeof (UuidBuf),
    L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
    Uuid->Data1, Uuid->Data2, Uuid->Data3,
    Uuid->Data4[0], Uuid->Data4[1],
    Uuid->Data4[2], Uuid->Data4[3], Uuid->Data4[4],
    Uuid->Data4[5], Uuid->Data4[6], Uuid->Data4[7]
    );
  PrintKV (L"UUID", UuidBuf);
}

// ---------------------------------------------------------------------------
// SMBIOS Type 4 - Processor Information
// ---------------------------------------------------------------------------

STATIC VOID
ShowCpuInfo (
  IN EFI_SMBIOS_PROTOCOL  *Smbios
  )
{
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE4       *T4;
  EFI_STATUS               Status;
  CHAR16                   Buf[64];
  UINTN                    CpuIdx;

  Type   = SMBIOS_TYPE_PROCESSOR_INFORMATION;
  Handle = SMBIOS_HANDLE_PI_RESERVED;
  CpuIdx = 0;

  do {
    Status = Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL);
    if (EFI_ERROR (Status)) {
      break;
    }

    T4 = (SMBIOS_TABLE_TYPE4 *)Record;
    CpuIdx++;

    UnicodeSPrint (Buf, sizeof (Buf), L"CPU %lu Socket", (UINT64)CpuIdx);
    PrintSmbiosString (Buf, &T4->Hdr, T4->Socket);

    UnicodeSPrint (Buf, sizeof (Buf), L"CPU %lu Manufacturer", (UINT64)CpuIdx);
    PrintSmbiosString (Buf, &T4->Hdr, T4->ProcessorManufacturer);

    UnicodeSPrint (Buf, sizeof (Buf), L"CPU %lu Version", (UINT64)CpuIdx);
    PrintSmbiosString (Buf, &T4->Hdr, T4->ProcessorVersion);

    UnicodeSPrint (Buf, sizeof (Buf), L"CPU %lu Max Speed", (UINT64)CpuIdx);
    {
      CHAR16  SpeedBuf[32];
      UnicodeSPrint (SpeedBuf, sizeof (SpeedBuf), L"%u MHz", T4->MaxSpeed);
      PrintKV (Buf, SpeedBuf);
    }

    UnicodeSPrint (Buf, sizeof (Buf), L"CPU %lu Current Speed", (UINT64)CpuIdx);
    {
      CHAR16  SpeedBuf[32];
      UnicodeSPrint (SpeedBuf, sizeof (SpeedBuf), L"%u MHz", T4->CurrentSpeed);
      PrintKV (Buf, SpeedBuf);
    }

    UnicodeSPrint (Buf, sizeof (Buf), L"CPU %lu Core Count", (UINT64)CpuIdx);
    {
      CHAR16  CoreBuf[32];
      UINT32  Cores = (T4->CoreCount2 != 0) ? T4->CoreCount2 : T4->CoreCount;
      UINT32  Threads = (T4->ThreadCount2 != 0) ? T4->ThreadCount2 : T4->ThreadCount;
      UnicodeSPrint (CoreBuf, sizeof (CoreBuf), L"%u cores / %u threads", Cores, Threads);
      PrintKV (Buf, CoreBuf);
    }

  } while (!EFI_ERROR (Status));
}

// ---------------------------------------------------------------------------
// SMBIOS Type 17 - Memory Device
// ---------------------------------------------------------------------------

STATIC VOID
ShowMemoryInfo (
  IN EFI_SMBIOS_PROTOCOL  *Smbios
  )
{
  EFI_SMBIOS_TYPE          Type;
  EFI_SMBIOS_HANDLE        Handle;
  EFI_SMBIOS_TABLE_HEADER  *Record;
  SMBIOS_TABLE_TYPE17      *T17;
  EFI_STATUS               Status;
  CHAR16                   Buf[64];
  UINT64                   TotalMB;
  UINTN                    SlotIdx;

  static CONST CHAR16 *MemTypeStr[] = {
    L"Unknown",  L"Other",  L"DRAM",    L"EDRAM",  L"VRAM",
    L"SRAM",     L"RAM",    L"ROM",     L"Flash",  L"EEPROM",
    L"FEPROM",   L"EPROM",  L"CDRAM",   L"3DRAM",  L"SDRAM",
    L"SGRAM",    L"RDRAM",  L"DDR",     L"DDR2",   L"DDR2 FB-DIMM",
    L"Reserved", L"Reserved", L"Reserved", L"DDR3", L"FBD2",
    L"DDR4",     L"LPDDR",  L"LPDDR2", L"LPDDR3", L"LPDDR4",
    L"Logical non-volatile", L"HBM",   L"HBM2",   L"DDR5", L"LPDDR5"
  };

  Type    = SMBIOS_TYPE_MEMORY_DEVICE;
  Handle  = SMBIOS_HANDLE_PI_RESERVED;
  TotalMB = 0;
  SlotIdx = 0;

  do {
    Status = Smbios->GetNext (Smbios, &Handle, &Type, &Record, NULL);
    if (EFI_ERROR (Status)) {
      break;
    }

    T17 = (SMBIOS_TABLE_TYPE17 *)Record;
    SlotIdx++;

    // Skip empty slots
    if (T17->Size == 0) {
      continue;
    }

    UnicodeSPrint (Buf, sizeof (Buf), L"Slot %lu Locator", (UINT64)SlotIdx);
    PrintSmbiosString (Buf, &T17->Hdr, T17->DeviceLocator);

    UnicodeSPrint (Buf, sizeof (Buf), L"Slot %lu Bank", (UINT64)SlotIdx);
    PrintSmbiosString (Buf, &T17->Hdr, T17->BankLocator);

    UnicodeSPrint (Buf, sizeof (Buf), L"Slot %lu Manufacturer", (UINT64)SlotIdx);
    PrintSmbiosString (Buf, &T17->Hdr, T17->Manufacturer);

    // Size
    {
      UINT64  SizeMB;
      CHAR16  SizeBuf[32];

      if (T17->Size == 0x7FFF) {
        SizeMB = T17->ExtendedSize & 0x7FFFFFFF;
      } else if (T17->Size & 0x8000) {
        SizeMB = T17->Size & 0x7FFF; // unit is KB
        SizeMB /= 1024;
      } else {
        SizeMB = T17->Size & 0x7FFF;
      }

      TotalMB += SizeMB;
      UnicodeSPrint (Buf, sizeof (Buf), L"Slot %lu Size", (UINT64)SlotIdx);
      UnicodeSPrint (SizeBuf, sizeof (SizeBuf), L"%lu MB", SizeMB);
      PrintKV (Buf, SizeBuf);
    }

    // Type
    {
      UINT8    MemTypeIdx = T17->MemoryType;
      CHAR16   TypeBuf[64];

      if (MemTypeIdx < ARRAY_SIZE (MemTypeStr)) {
        UnicodeSPrint (TypeBuf, sizeof (TypeBuf), L"%s @ %u MT/s",
                       MemTypeStr[MemTypeIdx], T17->Speed);
      } else {
        UnicodeSPrint (TypeBuf, sizeof (TypeBuf), L"Type 0x%02x @ %u MT/s",
                       MemTypeIdx, T17->Speed);
      }
      UnicodeSPrint (Buf, sizeof (Buf), L"Slot %lu Type", (UINT64)SlotIdx);
      PrintKV (Buf, TypeBuf);
    }

  } while (!EFI_ERROR (Status));

  {
    CHAR16  TotBuf[32];
    UnicodeSPrint (TotBuf, sizeof (TotBuf), L"%lu MB (%lu GB)",
                   TotalMB, TotalMB / 1024);
    PrintKV (L"Total Installed RAM", TotBuf);
  }
}

// ---------------------------------------------------------------------------
// UEFI Firmware Information
// ---------------------------------------------------------------------------

STATIC VOID
ShowFirmwareInfo (
  VOID
  )
{
  CHAR16  Buf[64];

  UnicodeSPrint (Buf, sizeof (Buf), L"%u.%u",
                 (gST->Hdr.Revision >> 16) & 0xFFFF,
                 gST->Hdr.Revision & 0xFFFF);
  PrintKV (L"UEFI Spec Revision", Buf);

  UnicodeSPrint (Buf, sizeof (Buf), L"0x%08X", gST->Hdr.Revision);
  PrintKV (L"FirmwareRevision", Buf);

  PrintKV (L"Firmware Vendor", gST->FirmwareVendor ? gST->FirmwareVendor : L"N/A");
}

// ---------------------------------------------------------------------------
// Memory Map Summary
// ---------------------------------------------------------------------------

STATIC VOID
ShowMemoryMap (
  VOID
  )
{
  EFI_STATUS            Status;
  EFI_MEMORY_DESCRIPTOR *Map;
  UINTN                 MapSize;
  UINTN                 MapKey;
  UINTN                 DescSize;
  UINT32                DescVer;
  UINTN                 EntryCount;
  UINTN                 i;
  UINT64                TotalConventional;
  CHAR16                Buf[64];

  static CONST CHAR16 *MemTypeNames[] = {
    L"EfiReservedMemoryType",
    L"EfiLoaderCode",
    L"EfiLoaderData",
    L"EfiBootServicesCode",
    L"EfiBootServicesData",
    L"EfiRuntimeServicesCode",
    L"EfiRuntimeServicesData",
    L"EfiConventionalMemory",
    L"EfiUnusableMemory",
    L"EfiACPIReclaimMemory",
    L"EfiACPIMemoryNVS",
    L"EfiMemoryMappedIO",
    L"EfiMemoryMappedIOPortSpace",
    L"EfiPalCode",
    L"EfiPersistentMemory",
  };

  MapSize = 0;
  Map     = NULL;

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    MapSize += DescSize * 8; // extra room for allocator entries
    Map = AllocatePool (MapSize);
    if (Map == NULL) {
      Print (L"  Out of memory.\n");
      return;
    }
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
  }

  if (EFI_ERROR (Status)) {
    Print (L"  GetMemoryMap failed: %r\n", Status);
    if (Map != NULL) {
      FreePool (Map);
    }
    return;
  }

  EntryCount        = MapSize / DescSize;
  TotalConventional = 0;

  Print (L"\n  %-4s  %-42s  %-18s  %-10s\n",
         L"#", L"Type", L"Physical Start", L"Pages");
  Print (L"  %-4s  %-42s  %-18s  %-10s\n",
         L"----", L"------------------------------------------",
         L"------------------", L"----------");

  for (i = 0; i < EntryCount; i++) {
    EFI_MEMORY_DESCRIPTOR *Entry;
    CONST CHAR16          *TypeName;

    Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + i * DescSize);

    if (Entry->Type < ARRAY_SIZE (MemTypeNames)) {
      TypeName = MemTypeNames[Entry->Type];
    } else {
      TypeName = L"Unknown";
    }

    if (Entry->Type == EfiConventionalMemory) {
      TotalConventional += Entry->NumberOfPages;
    }

    Print (L"  %-4lu  %-42s  0x%016llX  %-10lu\n",
           (UINT64)i,
           TypeName,
           Entry->PhysicalStart,
           (UINT64)Entry->NumberOfPages);
  }

  UnicodeSPrint (Buf, sizeof (Buf), L"%lu MB (%lu GB)",
                 (TotalConventional * EFI_PAGE_SIZE) / (1024 * 1024),
                 (TotalConventional * EFI_PAGE_SIZE) / (1024 * 1024 * 1024));
  Print (L"\n");
  PrintKV (L"Free Conventional Memory", Buf);

  FreePool (Map);
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
  EFI_STATUS          Status;
  EFI_SMBIOS_PROTOCOL *Smbios;

  // Locate SMBIOS protocol
  Status = gBS->LocateProtocol (
                  &gEfiSmbiosProtocolGuid,
                  NULL,
                  (VOID **)&Smbios
                  );

  PrintBanner (L"UEFI System Information Tool v" TOOLKIT_VERSION);

  // ---- Firmware -------------------------------------------------------
  PrintBanner (L"UEFI Firmware");
  ShowFirmwareInfo ();

  // ---- BIOS -----------------------------------------------------------
  PrintBanner (L"BIOS Information (SMBIOS Type 0)");
  if (!EFI_ERROR (Status)) {
    ShowBiosInfo (Smbios);
  } else {
    Print (L"  SMBIOS protocol not available (%r).\n", Status);
  }

  // ---- System ---------------------------------------------------------
  PrintBanner (L"System Information (SMBIOS Type 1)");
  if (!EFI_ERROR (Status)) {
    ShowSystemInfo (Smbios);
  }

  // ---- CPU ------------------------------------------------------------
  PrintBanner (L"Processor Information (SMBIOS Type 4)");
  if (!EFI_ERROR (Status)) {
    ShowCpuInfo (Smbios);
  }

  // ---- Memory (SMBIOS) -----------------------------------------------
  PrintBanner (L"Memory Devices (SMBIOS Type 17)");
  if (!EFI_ERROR (Status)) {
    ShowMemoryInfo (Smbios);
  }

  // ---- Memory Map -----------------------------------------------------
  PrintBanner (L"UEFI Memory Map");
  ShowMemoryMap ();

  Print (L"\n============================================================\n");
  Print (L"  Done. Press any key to exit.\n");
  Print (L"============================================================\n\n");

  {
    UINTN        Index;
    EFI_INPUT_KEY Key;
    gST->ConIn->Reset (gST->ConIn, FALSE);
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
  }

  return EFI_SUCCESS;
}
