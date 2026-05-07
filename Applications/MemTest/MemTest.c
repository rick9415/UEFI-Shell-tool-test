/** @file
  MemTest.efi - UEFI Memory Test Tool

  Performs the following passes over EfiConventionalMemory regions:
    Pass 0 : Fill with 0x00
    Pass 1 : Fill with 0xFF
    Pass 2 : Fill with 0xAA / 0x55 alternating (checkerboard)
    Pass 3 : Walking-bit (each bit position toggled)
    Pass 4 : Address-in-address (each cell = its own address low 32 bits)

  The amount of memory tested is limited by the -s option (default 64 MB)
  so that boot-time testing completes quickly.  Use -a to test ALL available
  EfiConventionalMemory.

  Usage:
    MemTest.efi [-s <SizeMB>] [-p <Passes>] [-a] [-v]

  Options:
    -s <SizeMB>   Size to test per region in MB (default: 64)
    -p <Passes>   Bitmask of passes to run 0-4 (default: 31 = all)
    -a            Test all EfiConventionalMemory (overrides -s)
    -v            Verbose: print address ranges being tested

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

#define TOOLKIT_VERSION  L"1.0.0"
#define DEFAULT_TEST_MB  64
#define ALL_PASSES       0x1F   // bits 0-4

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
PrintProgress (
  IN UINTN  Done,
  IN UINTN  Total
  )
{
  UINTN  Pct;
  UINTN  Bars;
  UINTN  i;

  Pct  = (Total > 0) ? ((Done * 100) / Total) : 100;
  Bars = Pct / 5;

  Print (L"\r  [");
  for (i = 0; i < 20; i++) {
    Print ((i < Bars) ? L"#" : L".");
  }
  Print (L"] %3lu%%", (UINT64)Pct);
}

// ---------------------------------------------------------------------------
// Pattern passes
// ---------------------------------------------------------------------------

/**
  Pass 0/1: Fill entire buffer with a repeating 8-bit value and verify.
  Returns number of errors found.
**/
STATIC UINTN
PassSolidPattern (
  IN UINT8   *Base,
  IN UINTN    Bytes,
  IN UINT8    Pattern,
  IN BOOLEAN  Verbose
  )
{
  UINTN  Errors;
  UINTN  i;

  if (Verbose) {
    Print (L"\n  Writing 0x%02x to %lu bytes ... ", Pattern, (UINT64)Bytes);
  }

  SetMem (Base, Bytes, Pattern);

  if (Verbose) {
    Print (L"Verifying ... ");
  }

  Errors = 0;
  for (i = 0; i < Bytes; i++) {
    if (Base[i] != Pattern) {
      Errors++;
      if (Verbose && Errors <= 8) {
        Print (L"\n  ERROR at 0x%lx: expected 0x%02x got 0x%02x",
               (UINT64)(UINTN)(Base + i), Pattern, Base[i]);
      }
    }
  }
  return Errors;
}

/**
  Pass 2: Checkerboard - alternating 0xAA and 0x55 per DWORD.
**/
STATIC UINTN
PassCheckerboard (
  IN UINT32  *Base32,
  IN UINTN    Count32,
  IN BOOLEAN  Verbose
  )
{
  UINTN  Errors;
  UINTN  i;
  UINT32 Expected;

  if (Verbose) {
    Print (L"\n  Writing checkerboard (0xAAAAAAAA/0x55555555) ... ");
  }

  for (i = 0; i < Count32; i++) {
    Base32[i] = (i & 1) ? 0x55555555U : 0xAAAAAAAAU;
  }

  if (Verbose) {
    Print (L"Verifying ... ");
  }

  Errors = 0;
  for (i = 0; i < Count32; i++) {
    Expected = (i & 1) ? 0x55555555U : 0xAAAAAAAAU;
    if (Base32[i] != Expected) {
      Errors++;
      if (Verbose && Errors <= 8) {
        Print (L"\n  ERROR at 0x%lx: expected 0x%08x got 0x%08x",
               (UINT64)(UINTN)(Base32 + i), Expected, Base32[i]);
      }
    }
  }
  return Errors;
}

/**
  Pass 3: Walking-bit test.  Each bit position from 0..31 is written as
  (1 << bit) to every cell, then verified.
**/
STATIC UINTN
PassWalkingBit (
  IN UINT32  *Base32,
  IN UINTN    Count32,
  IN BOOLEAN  Verbose
  )
{
  UINTN  Errors;
  UINTN  Bit;
  UINTN  i;
  UINT32 Pattern;

  Errors = 0;

  for (Bit = 0; Bit < 32; Bit++) {
    Pattern = (1U << Bit);

    if (Verbose) {
      Print (L"\n  Walking bit %lu (0x%08x) ... ", (UINT64)Bit, Pattern);
    } else {
      PrintProgress (Bit, 32);
    }

    for (i = 0; i < Count32; i++) {
      Base32[i] = Pattern;
    }

    for (i = 0; i < Count32; i++) {
      if (Base32[i] != Pattern) {
        Errors++;
        if (Verbose && Errors <= 8) {
          Print (L"\n  ERROR at 0x%lx bit %lu: expected 0x%08x got 0x%08x",
                 (UINT64)(UINTN)(Base32 + i), (UINT64)Bit, Pattern, Base32[i]);
        }
      }
    }
  }

  return Errors;
}

/**
  Pass 4: Address-in-address.  Each DWORD is written with the low 32 bits
  of its own physical address, then verified.
**/
STATIC UINTN
PassAddressInAddress (
  IN UINT32  *Base32,
  IN UINTN    Count32,
  IN BOOLEAN  Verbose
  )
{
  UINTN  Errors;
  UINTN  i;
  UINT32 Expected;

  if (Verbose) {
    Print (L"\n  Writing address-in-address pattern ... ");
  }

  for (i = 0; i < Count32; i++) {
    Base32[i] = (UINT32)(UINTN)(Base32 + i);
  }

  if (Verbose) {
    Print (L"Verifying ... ");
  }

  Errors = 0;
  for (i = 0; i < Count32; i++) {
    Expected = (UINT32)(UINTN)(Base32 + i);
    if (Base32[i] != Expected) {
      Errors++;
      if (Verbose && Errors <= 8) {
        Print (L"\n  ERROR at 0x%lx: expected 0x%08x got 0x%08x",
               (UINT64)(UINTN)(Base32 + i), Expected, Base32[i]);
      }
    }
  }
  return Errors;
}

// ---------------------------------------------------------------------------
// Memory map enumeration
// ---------------------------------------------------------------------------

typedef struct {
  EFI_PHYSICAL_ADDRESS  Start;
  UINTN                 Pages;
} CONVENTIONAL_REGION;

STATIC EFI_STATUS
GetConventionalRegions (
  OUT CONVENTIONAL_REGION  **RegionsOut,
  OUT UINTN                 *RegionCountOut
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
  UINTN                 RegionCount;
  CONVENTIONAL_REGION  *Regions;

  MapSize = 0;
  Map     = NULL;

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    MapSize += DescSize * 8;
    Map = AllocatePool (MapSize);
    if (Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
  }
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return Status;
  }

  EntryCount  = MapSize / DescSize;
  RegionCount = 0;

  // Count conventional regions first
  for (i = 0; i < EntryCount; i++) {
    EFI_MEMORY_DESCRIPTOR *E = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + i * DescSize);
    if (E->Type == EfiConventionalMemory && E->PhysicalStart >= 0x100000) {
      RegionCount++;
    }
  }

  Regions = AllocatePool (RegionCount * sizeof (CONVENTIONAL_REGION));
  if (Regions == NULL) {
    FreePool (Map);
    return EFI_OUT_OF_RESOURCES;
  }

  RegionCount = 0;
  for (i = 0; i < EntryCount; i++) {
    EFI_MEMORY_DESCRIPTOR *E = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + i * DescSize);
    if (E->Type == EfiConventionalMemory && E->PhysicalStart >= 0x100000) {
      Regions[RegionCount].Start = E->PhysicalStart;
      Regions[RegionCount].Pages = (UINTN)E->NumberOfPages;
      RegionCount++;
    }
  }

  FreePool (Map);

  *RegionsOut     = Regions;
  *RegionCountOut = RegionCount;
  return EFI_SUCCESS;
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

STATIC BOOLEAN
ArgIs (
  IN CONST CHAR16  *Arg,
  IN CONST CHAR16  *Flag
  )
{
  return (StrnCmp (Arg, Flag, StrLen (Flag)) == 0);
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
  EFI_STATUS           Status;
  UINTN                TestMB;
  UINT32               PassMask;
  BOOLEAN              TestAll;
  BOOLEAN              Verbose;
  UINTN                i;
  CONVENTIONAL_REGION *Regions;
  UINTN                RegionCount;
  UINT64               TotalErrors;
  UINT64               TotalTested;

  // Defaults
  TestMB   = DEFAULT_TEST_MB;
  PassMask = ALL_PASSES;
  TestAll  = FALSE;
  Verbose  = FALSE;

  // Parse arguments
  for (i = 1; i < Argc; i++) {
    if (ArgIs (Argv[i], L"-s") && (i + 1) < Argc) {
      TestMB = (UINTN)StrDecimalToUintn (Argv[++i]);
    } else if (ArgIs (Argv[i], L"-p") && (i + 1) < Argc) {
      PassMask = (UINT32)StrDecimalToUintn (Argv[++i]);
    } else if (ArgIs (Argv[i], L"-a")) {
      TestAll = TRUE;
    } else if (ArgIs (Argv[i], L"-v")) {
      Verbose = TRUE;
    } else if (ArgIs (Argv[i], L"-h") || ArgIs (Argv[i], L"--help")) {
      Print (L"Usage: MemTest.efi [-s SizeMB] [-p PassMask] [-a] [-v]\n");
      Print (L"  -s <MB>    Megabytes to test per region (default: %lu)\n",
             (UINT64)DEFAULT_TEST_MB);
      Print (L"  -p <mask>  Pass bitmask 0-31 (default: 31 = all passes)\n");
      Print (L"             Bit 0=Zeros, 1=Ones, 2=Checkerboard,\n");
      Print (L"             Bit 3=WalkingBit, 4=AddressInAddress\n");
      Print (L"  -a         Test ALL EfiConventionalMemory\n");
      Print (L"  -v         Verbose output\n");
      return 0;
    }
  }

  PrintBanner (L"UEFI Memory Test Tool v" TOOLKIT_VERSION);

  Print (L"  Test size per region : %s\n",
         TestAll ? L"ALL EfiConventionalMemory" : L"");
  if (!TestAll) {
    Print (L"  Test size            : %lu MB\n", (UINT64)TestMB);
  }
  Print (L"  Pass mask            : 0x%02x\n", (UINT64)PassMask);
  Print (L"  Verbose              : %s\n", Verbose ? L"Yes" : L"No");
  Print (L"\n");

  // Get conventional memory regions
  Status = GetConventionalRegions (&Regions, &RegionCount);
  if (EFI_ERROR (Status)) {
    Print (L"  ERROR: Could not read memory map (%r)\n", Status);
    return 1;
  }

  Print (L"  Found %lu EfiConventionalMemory region(s).\n\n", (UINT64)RegionCount);

  TotalErrors = 0;
  TotalTested = 0;

  for (i = 0; i < RegionCount; i++) {
    UINT8  *Base;
    UINTN   TestBytes;
    UINTN   MaxBytes;
    UINTN   Errors;

    MaxBytes  = Regions[i].Pages * EFI_PAGE_SIZE;
    TestBytes = TestAll ? MaxBytes : MIN (TestMB * 1024 * 1024, MaxBytes);

    // Align to DWORD boundary
    TestBytes &= ~(UINTN)3;

    if (TestBytes == 0) {
      continue;
    }

    Base = (UINT8 *)(UINTN)Regions[i].Start;

    Print (L"  Region %lu: 0x%016lx - 0x%016lx  (%lu MB)\n",
           (UINT64)i,
           (UINT64)(UINTN)Base,
           (UINT64)(UINTN)(Base + TestBytes - 1),
           (UINT64)(TestBytes / (1024 * 1024)));

    // Pass 0 - Zeros
    if (PassMask & BIT0) {
      Print (L"    [Pass 0] Solid 0x00 ... ");
      Errors = PassSolidPattern (Base, TestBytes, 0x00, Verbose);
      Print (L" %s (%lu errors)\n", Errors ? L"FAIL" : L"PASS", (UINT64)Errors);
      TotalErrors += Errors;
    }

    // Pass 1 - Ones
    if (PassMask & BIT1) {
      Print (L"    [Pass 1] Solid 0xFF ... ");
      Errors = PassSolidPattern (Base, TestBytes, 0xFF, Verbose);
      Print (L" %s (%lu errors)\n", Errors ? L"FAIL" : L"PASS", (UINT64)Errors);
      TotalErrors += Errors;
    }

    // Pass 2 - Checkerboard
    if (PassMask & BIT2) {
      Print (L"    [Pass 2] Checkerboard ... ");
      Errors = PassCheckerboard ((UINT32 *)Base, TestBytes / 4, Verbose);
      Print (L" %s (%lu errors)\n", Errors ? L"FAIL" : L"PASS", (UINT64)Errors);
      TotalErrors += Errors;
    }

    // Pass 3 - Walking bit
    if (PassMask & BIT3) {
      Print (L"    [Pass 3] Walking bit  ... ");
      Errors = PassWalkingBit ((UINT32 *)Base, TestBytes / 4, Verbose);
      if (!Verbose) {
        Print (L"\r");
      }
      Print (L"    [Pass 3] Walking bit   %s (%lu errors)\n",
             Errors ? L"FAIL" : L"PASS", (UINT64)Errors);
      TotalErrors += Errors;
    }

    // Pass 4 - Address in address
    if (PassMask & BIT4) {
      Print (L"    [Pass 4] Address-in-address ... ");
      Errors = PassAddressInAddress ((UINT32 *)Base, TestBytes / 4, Verbose);
      Print (L" %s (%lu errors)\n", Errors ? L"FAIL" : L"PASS", (UINT64)Errors);
      TotalErrors += Errors;
    }

    TotalTested += TestBytes;
    Print (L"\n");
  }

  FreePool (Regions);

  Print (L"============================================================\n");
  Print (L"  Total tested : %lu MB\n", TotalTested / (1024 * 1024));
  Print (L"  Total errors : %lu\n", TotalErrors);
  Print (L"  Result       : %s\n", (TotalErrors == 0) ? L"PASS" : L"FAIL");
  Print (L"============================================================\n\n");

  ShellPromptForResponse (ShellPromptResponseTypeAnyKeyContinue, NULL, NULL);

  return (TotalErrors == 0) ? 0 : 1;
}
