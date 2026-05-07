/** @file
  Common helpers shared across UEFI Toolkit applications.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef UEFI_TOOLKIT_COMMON_H_
#define UEFI_TOOLKIT_COMMON_H_

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/ShellLib.h>
#include <Library/ShellCEntryLib.h>
#include <Library/DebugLib.h>

#define TOOLKIT_VERSION  L"1.0.0"

/** Print a section header banner. */
VOID
PrintBanner (
  IN CONST CHAR16  *Title
  );

/** Print a key/value pair aligned to a fixed column. */
VOID
PrintKV (
  IN CONST CHAR16  *Key,
  IN CONST CHAR16  *Fmt,
  ...
  );

/** Convert EFI_STATUS to a human-readable string. */
CONST CHAR16 *
StatusToString (
  IN EFI_STATUS  Status
  );

/** Pause and wait for a keypress. */
VOID
WaitForKey (
  VOID
  );

#endif // UEFI_TOOLKIT_COMMON_H_
