/** @file
  DiskInfo.efi - UEFI Disk and Partition Information Tool

  Enumerates all handles exposing:
    - EFI_BLOCK_IO_PROTOCOL   (raw block device)
    - EFI_DISK_IO_PROTOCOL    (sector-level I/O)
    - EFI_PARTITION_INFO_PROTOCOL (GPT / MBR partition table info)
    - EFI_SIMPLE_FILE_SYSTEM_PROTOCOL (mounted filesystems)
  and prints detailed information about each.

  Usage:
    DiskInfo.efi [-v] [-h]

  Options:
    -v   Verbose: show raw partition GUIDs and attributes
    -h   Print help

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
#include <Library/DevicePathLib.h>
#include <Library/DebugLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Guid/FileSystemInfo.h>

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
  Print (L"  %-32s : %s\n", Key, Value);
}

STATIC VOID
PrintKVU64 (
  IN CONST CHAR16  *Key,
  IN UINT64         Value
  )
{
  CHAR16  Buf[32];
  UnicodeSPrint (Buf, sizeof (Buf), L"%lu", Value);
  PrintKV (Key, Buf);
}

STATIC VOID
PrintGuid (
  IN CONST CHAR16  *Label,
  IN CONST EFI_GUID *Guid
  )
{
  CHAR16  Buf[64];
  UnicodeSPrint (
    Buf, sizeof (Buf),
    L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
    Guid->Data1, Guid->Data2, Guid->Data3,
    Guid->Data4[0], Guid->Data4[1],
    Guid->Data4[2], Guid->Data4[3], Guid->Data4[4],
    Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]
    );
  PrintKV (Label, Buf);
}

STATIC CONST CHAR16 *
MediaTypeToString (
  IN UINT32  MediaId,
  IN BOOLEAN RemovableMedia,
  IN BOOLEAN LogicalPartition
  )
{
  if (LogicalPartition) {
    return L"Logical Partition";
  }
  if (RemovableMedia) {
    return L"Removable Media";
  }
  return L"Fixed Disk";
}

// ---------------------------------------------------------------------------
// Block IO information
// ---------------------------------------------------------------------------

STATIC VOID
ShowBlockIoInfo (
  IN EFI_HANDLE    Handle,
  IN BOOLEAN       Verbose
  )
{
  EFI_STATUS         Status;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  EFI_BLOCK_IO_MEDIA     *Media;
  CHAR16              Buf[64];
  UINT64              SizeBytes;
  UINT64              SizeMB;

  Status = gBS->HandleProtocol (Handle, &gEfiBlockIoProtocolGuid, (VOID **)&BlockIo);
  if (EFI_ERROR (Status)) {
    return;
  }

  Media     = BlockIo->Media;
  SizeBytes = (UINT64)Media->LastBlock * Media->BlockSize + Media->BlockSize;
  SizeMB    = SizeBytes / (1024 * 1024);

  PrintKV (L"Type", MediaTypeToString (
              Media->MediaId, Media->RemovableMedia, Media->LogicalPartition));

  UnicodeSPrint (Buf, sizeof (Buf), L"0x%08X", Media->MediaId);
  PrintKV (L"MediaId", Buf);

  UnicodeSPrint (Buf, sizeof (Buf), L"%u bytes", Media->BlockSize);
  PrintKV (L"Block Size", Buf);

  UnicodeSPrint (Buf, sizeof (Buf), L"%lu", Media->LastBlock);
  PrintKV (L"Last LBA", Buf);

  UnicodeSPrint (Buf, sizeof (Buf), L"%lu MB (%lu GB)", SizeMB, SizeMB / 1024);
  PrintKV (L"Capacity", Buf);

  PrintKV (L"Read Only",      Media->ReadOnly       ? L"Yes" : L"No");
  PrintKV (L"Write Caching",  Media->WriteCaching    ? L"Yes" : L"No");
  PrintKV (L"Media Present",  Media->MediaPresent    ? L"Yes" : L"No");

  if (Verbose) {
    UnicodeSPrint (Buf, sizeof (Buf), L"%u bytes", Media->IoAlign);
    PrintKV (L"I/O Align", Buf);
  }
}

// ---------------------------------------------------------------------------
// Partition Info
// ---------------------------------------------------------------------------

STATIC VOID
ShowPartitionInfo (
  IN EFI_HANDLE  Handle,
  IN BOOLEAN     Verbose
  )
{
  EFI_STATUS                Status;
  EFI_PARTITION_INFO_PROTOCOL *PartInfo;
  CHAR16                    Buf[128];

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiPartitionInfoProtocolGuid,
                  (VOID **)&PartInfo
                  );
  if (EFI_ERROR (Status)) {
    PrintKV (L"Partition Info", L"Not available");
    return;
  }

  switch (PartInfo->Type) {
    case PARTITION_TYPE_OTHER:
      PrintKV (L"Partition Type", L"Other");
      break;

    case PARTITION_TYPE_MBR:
      PrintKV (L"Partition Type", L"MBR");
      UnicodeSPrint (Buf, sizeof (Buf), L"0x%02X",
                     PartInfo->Info.Mbr.OSIndicator);
      PrintKV (L"MBR OS Type", Buf);
      UnicodeSPrint (Buf, sizeof (Buf), L"0x%08X",
                     *(UINT32 *)PartInfo->Info.Mbr.StartingLBA);
      PrintKV (L"MBR Start LBA", Buf);
      UnicodeSPrint (Buf, sizeof (Buf), L"0x%08X",
                     *(UINT32 *)PartInfo->Info.Mbr.SizeInLBA);
      PrintKV (L"MBR Size (LBA)", Buf);
      break;

    case PARTITION_TYPE_GPT:
      PrintKV (L"Partition Type", L"GPT");
      {
        CHAR16  PartName[37];
        // Copy partition name (up to 36 wchars)
        StrnCpyS (PartName, ARRAY_SIZE (PartName),
                  (CHAR16 *)PartInfo->Info.Gpt.PartitionName,
                  ARRAY_SIZE (PartInfo->Info.Gpt.PartitionName));
        PrintKV (L"GPT Partition Name", PartName);
      }
      UnicodeSPrint (Buf, sizeof (Buf), L"%lu", PartInfo->Info.Gpt.StartingLBA);
      PrintKV (L"GPT Start LBA", Buf);
      UnicodeSPrint (Buf, sizeof (Buf), L"%lu", PartInfo->Info.Gpt.EndingLBA);
      PrintKV (L"GPT End LBA", Buf);
      if (Verbose) {
        PrintGuid (L"GPT Partition TypeGUID", &PartInfo->Info.Gpt.PartitionTypeGUID);
        PrintGuid (L"GPT Unique PartGUID",    &PartInfo->Info.Gpt.UniquePartitionGUID);
        UnicodeSPrint (Buf, sizeof (Buf), L"0x%016llX", PartInfo->Info.Gpt.Attributes);
        PrintKV (L"GPT Attributes", Buf);
      }
      break;

    default:
      UnicodeSPrint (Buf, sizeof (Buf), L"Unknown (%u)", PartInfo->Type);
      PrintKV (L"Partition Type", Buf);
      break;
  }
}

// ---------------------------------------------------------------------------
// Simple File System / Volume info
// ---------------------------------------------------------------------------

STATIC VOID
ShowFilesystemInfo (
  IN EFI_HANDLE  Handle
  )
{
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Sfs;
  EFI_FILE_PROTOCOL               *Root;
  EFI_FILE_SYSTEM_INFO            *FsInfo;
  UINTN                            FsInfoSize;
  CHAR16                           Buf[64];

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Sfs
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = Sfs->OpenVolume (Sfs, &Root);
  if (EFI_ERROR (Status)) {
    PrintKV (L"Filesystem", L"Could not open volume");
    return;
  }

  FsInfoSize = SIZE_OF_EFI_FILE_SYSTEM_INFO + 256 * sizeof (CHAR16);
  FsInfo     = AllocatePool (FsInfoSize);
  if (FsInfo == NULL) {
    Root->Close (Root);
    return;
  }

  Status = Root->GetInfo (Root, &gEfiFileSystemInfoGuid, &FsInfoSize, FsInfo);
  if (!EFI_ERROR (Status)) {
    PrintKV (L"Volume Label",    FsInfo->VolumeLabel);
    PrintKV (L"Read Only Vol",   FsInfo->ReadOnly ? L"Yes" : L"No");

    UnicodeSPrint (Buf, sizeof (Buf), L"%lu MB",
                   FsInfo->VolumeSize / (1024 * 1024));
    PrintKV (L"Volume Size", Buf);

    UnicodeSPrint (Buf, sizeof (Buf), L"%lu MB",
                   FsInfo->FreeSpace / (1024 * 1024));
    PrintKV (L"Free Space", Buf);

    UnicodeSPrint (Buf, sizeof (Buf), L"%u bytes", FsInfo->BlockSize);
    PrintKV (L"FS Block Size", Buf);
  }

  FreePool (FsInfo);
  Root->Close (Root);
}

// ---------------------------------------------------------------------------
// Device path display
// ---------------------------------------------------------------------------

STATIC VOID
ShowDevicePath (
  IN EFI_HANDLE  Handle
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Dp;
  CHAR16                    *DpText;
  EFI_STATUS                 Status;

  Status = gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&Dp);
  if (EFI_ERROR (Status)) {
    PrintKV (L"Device Path", L"N/A");
    return;
  }

  DpText = ConvertDevicePathToText (Dp, FALSE, TRUE);
  if (DpText != NULL) {
    PrintKV (L"Device Path", DpText);
    FreePool (DpText);
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
  EFI_STATUS  Status;
  BOOLEAN     Verbose;
  UINTN       i;
  EFI_HANDLE *Handles;
  UINTN       HandleCount;

  Verbose = FALSE;

  for (i = 1; i < Argc; i++) {
    if (StrnCmp (Argv[i], L"-v", 2) == 0) {
      Verbose = TRUE;
    } else if (StrnCmp (Argv[i], L"-h", 2) == 0 ||
               StrnCmp (Argv[i], L"--help", 6) == 0) {
      Print (L"Usage: DiskInfo.efi [-v] [-h]\n");
      Print (L"  -v   Verbose (show GUIDs and attributes)\n");
      Print (L"  -h   Help\n");
      return 0;
    }
  }

  PrintBanner (L"UEFI Disk Information Tool v" TOOLKIT_VERSION);

  // ---- Block I/O handles ------------------------------------------------
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );

  if (EFI_ERROR (Status)) {
    Print (L"  No BlockIo handles found (%r).\n", Status);
    return 1;
  }

  Print (L"  Found %lu Block I/O device(s).\n\n", (UINT64)HandleCount);

  for (i = 0; i < HandleCount; i++) {
    Print (L"\n  --- Block Device %lu ---\n", (UINT64)i);
    ShowDevicePath (Handles[i]);
    ShowBlockIoInfo (Handles[i], Verbose);
    ShowPartitionInfo (Handles[i], Verbose);
    ShowFilesystemInfo (Handles[i]);
  }

  FreePool (Handles);

  // ---- Summary: Simple File Systems -------------------------------------
  PrintBanner (L"Mounted Filesystems");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );

  if (EFI_ERROR (Status)) {
    Print (L"  No mounted filesystems found.\n");
  } else {
    Print (L"  %lu mounted filesystem(s):\n\n", (UINT64)HandleCount);
    for (i = 0; i < HandleCount; i++) {
      Print (L"  FS%lu:\n", (UINT64)i);
      ShowDevicePath (Handles[i]);
      ShowFilesystemInfo (Handles[i]);
      Print (L"\n");
    }
    FreePool (Handles);
  }

  Print (L"============================================================\n");
  Print (L"  Done. Press any key to exit.\n");
  Print (L"============================================================\n\n");

  ShellPromptForResponse (ShellPromptResponseTypeAnyKeyContinue, NULL, NULL);

  return 0;
}
