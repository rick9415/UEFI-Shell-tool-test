/**
  NvramOps - UEFI Shell NVRAM (EFI Variable) batch operations tool.

  Provides list, get, set, delete, export, and import of EFI variables.
  Batch import/export uses a simple line-oriented text format:
    <GuidStr> <Name> <Attrs> <HexData>

  Uses gRT->GetNextVariableName / GetVariable / SetVariable exclusively.

  Usage:
    NvramOps list   [-g <GUID>] [-p <Prefix>]
    NvramOps get    <Name> <GUID>
    NvramOps set    <Name> <GUID> <Attrs> <HexData>
    NvramOps del    <Name> <GUID>
    NvramOps export [-g <GUID>] [-p <Prefix>] <File>
    NvramOps import <File>

  Attribute flags (OR together, decimal):
    1  = EFI_VARIABLE_NON_VOLATILE
    2  = EFI_VARIABLE_BOOTSERVICE_ACCESS
    4  = EFI_VARIABLE_RUNTIME_ACCESS

  CAUTION: Deleting or overwriting firmware variables (e.g. BootOrder, Setup)
  may render the system unbootable.  Use del/set with care.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/ShellLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define NVRAMOPS_VERSION  L"0.1"
#define MAX_VAR_NAME      512   /* chars */
#define MAX_VAR_DATA      (64 * 1024)

/* Attribute flag descriptions */
STATIC CONST struct { UINT32 Bit; CONST CHAR16 *Name; } AttrBits[] = {
  { 0x0001, L"NV"   },
  { 0x0002, L"BS"   },
  { 0x0004, L"RT"   },
  { 0x0008, L"HW"   },
  { 0x0010, L"AC"   },
  { 0x0020, L"TO"   },
  { 0x1000, L"APPEND" },
};

STATIC VOID
PrintAttrs (
  IN UINT32 Attrs
  )
{
  BOOLEAN First = TRUE;
  for (UINTN i = 0; i < ARRAY_SIZE (AttrBits); i++) {
    if (Attrs & AttrBits[i].Bit) {
      if (!First) Print (L"+");
      Print (L"%s", AttrBits[i].Name);
      First = FALSE;
    }
  }
  if (First) Print (L"0");
}

/* Convert a binary buffer to a hex string (caller provides buffer). */
STATIC VOID
BinToHex (
  IN  UINT8   *Src,
  IN  UINTN    Len,
  OUT CHAR16  *Dst,
  IN  UINTN    DstChars   /* must be >= Len*2+1 */
  )
{
  STATIC CONST CHAR16 Hex[] = L"0123456789ABCDEF";
  for (UINTN i = 0; i < Len && i * 2 + 1 < DstChars; i++) {
    Dst[i * 2]     = Hex[Src[i] >> 4];
    Dst[i * 2 + 1] = Hex[Src[i] & 0xF];
  }
  Dst[Len * 2] = L'\0';
}

/* Parse a hex string into binary. Returns byte count or 0 on error. */
STATIC UINTN
HexToBin (
  IN  CONST CHAR16 *Hex,
  OUT UINT8        *Dst,
  IN  UINTN         MaxLen
  )
{
  UINTN  Len = StrLen (Hex);
  if (Len % 2 != 0) return 0;
  UINTN  Bytes = Len / 2;
  if (Bytes > MaxLen) return 0;

  for (UINTN i = 0; i < Bytes; i++) {
    CHAR16 Hi = Hex[i * 2], Lo = Hex[i * 2 + 1];
    UINT8  HiV, LoV;
    if      (Hi >= L'0' && Hi <= L'9') HiV = (UINT8)(Hi - L'0');
    else if (Hi >= L'A' && Hi <= L'F') HiV = (UINT8)(Hi - L'A' + 10);
    else if (Hi >= L'a' && Hi <= L'f') HiV = (UINT8)(Hi - L'a' + 10);
    else return 0;
    if      (Lo >= L'0' && Lo <= L'9') LoV = (UINT8)(Lo - L'0');
    else if (Lo >= L'A' && Lo <= L'F') LoV = (UINT8)(Lo - L'A' + 10);
    else if (Lo >= L'a' && Lo <= L'f') LoV = (UINT8)(Lo - L'a' + 10);
    else return 0;
    Dst[i] = (UINT8)((HiV << 4) | LoV);
  }
  return Bytes;
}

/* Parse a GUID string "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" */
STATIC EFI_STATUS
ParseGuid (
  IN  CONST CHAR16 *Str,
  OUT EFI_GUID     *Guid
  )
{
  /* Skip optional braces */
  if (Str[0] == L'{') Str++;
  CHAR8 AsciiGuid[37];
  for (UINTN i = 0; i < 36 && Str[i]; i++) {
    AsciiGuid[i] = (CHAR8)Str[i];
  }
  AsciiGuid[36] = '\0';

  /* Simple sscanf-style parse */
  UINT32 D1;
  UINT16 D2, D3;
  UINT8  D4[8];
  CHAR8 *P = AsciiGuid;

  /* This is a lightweight parse; AsciiStrHexToUintn is available in BaseLib */
  CHAR16 Tmp[37];
  for (UINTN i = 0; i < 36; i++) Tmp[i] = (CHAR16)AsciiGuid[i];
  Tmp[36] = L'\0';

  /* Split on '-' */
  CHAR16 Parts[5][13];
  UINTN  PartIdx = 0, CharIdx = 0;
  ZeroMem (Parts, sizeof Parts);
  for (UINTN i = 0; i <= 36 && PartIdx < 5; i++) {
    if (Tmp[i] == L'-' || Tmp[i] == L'\0' || Tmp[i] == L'}') {
      Parts[PartIdx][CharIdx] = L'\0';
      PartIdx++;
      CharIdx = 0;
    } else {
      if (CharIdx < 12) Parts[PartIdx][CharIdx++] = Tmp[i];
    }
  }
  if (PartIdx < 5) return EFI_INVALID_PARAMETER;
  (VOID)P;

  D1 = (UINT32)StrHexToUint64 (Parts[0]);
  D2 = (UINT16)StrHexToUint64 (Parts[1]);
  D3 = (UINT16)StrHexToUint64 (Parts[2]);

  /* D4 from parts[3] (2 bytes) and parts[4] (6 bytes) */
  UINT8 D4Tmp[8];
  UINTN D4Len = HexToBin (Parts[3], D4Tmp, 2);
  if (D4Len != 2) return EFI_INVALID_PARAMETER;
  D4[0] = D4Tmp[0]; D4[1] = D4Tmp[1];

  D4Len = HexToBin (Parts[4], D4Tmp, 6);
  if (D4Len != 6) return EFI_INVALID_PARAMETER;
  for (UINTN i = 0; i < 6; i++) D4[2 + i] = D4Tmp[i];

  Guid->Data1 = D1;
  Guid->Data2 = D2;
  Guid->Data3 = D3;
  CopyMem (Guid->Data4, D4, 8);
  return EFI_SUCCESS;
}

/* Print a GUID as the canonical "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" form. */
STATIC VOID
PrintGuid (
  IN CONST EFI_GUID *G
  )
{
  Print (L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
         G->Data1, G->Data2, G->Data3,
         G->Data4[0], G->Data4[1],
         G->Data4[2], G->Data4[3], G->Data4[4],
         G->Data4[5], G->Data4[6], G->Data4[7]);
}

/* Fetch variable data; caller must FreePool. Returns NULL on error. */
STATIC UINT8 *
GetVar (
  IN  CONST CHAR16  *Name,
  IN  CONST EFI_GUID *Guid,
  OUT UINTN          *DataSize,
  OUT UINT32         *Attributes
  )
{
  EFI_STATUS  Status;
  UINT8      *Buf = NULL;
  *DataSize = 0;

  Status = gRT->GetVariable ((CHAR16 *)Name, (EFI_GUID *)Guid,
                              Attributes, DataSize, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return NULL;
  }
  Buf = AllocatePool (*DataSize);
  if (Buf == NULL) return NULL;

  Status = gRT->GetVariable ((CHAR16 *)Name, (EFI_GUID *)Guid,
                              Attributes, DataSize, Buf);
  if (EFI_ERROR (Status)) {
    FreePool (Buf);
    return NULL;
  }
  return Buf;
}

/* Print variable: header + hex dump + ASCII. */
STATIC VOID
PrintVariable (
  IN CONST CHAR16   *Name,
  IN CONST EFI_GUID *Guid,
  IN UINT32          Attrs,
  IN UINT8          *Data,
  IN UINTN           DataSize
  )
{
  Print (L"\nName  : %s\n", Name);
  Print (L"GUID  : ");
  PrintGuid (Guid);
  Print (L"\nAttrs : 0x%08x (", Attrs);
  PrintAttrs (Attrs);
  Print (L")\nSize  : %lu bytes\nData  :\n", (UINT64)DataSize);

  for (UINTN Row = 0; Row < DataSize; Row += 16) {
    Print (L"  %04x  ", Row);
    for (UINTN Col = 0; Col < 16; Col++) {
      if (Row + Col < DataSize) Print (L"%02x ", Data[Row + Col]);
      else                       Print (L"   ");
    }
    Print (L" |");
    for (UINTN Col = 0; Col < 16 && Row + Col < DataSize; Col++) {
      UINT8 C = Data[Row + Col];
      Print (L"%c", (C >= 0x20 && C < 0x7f) ? C : '.');
    }
    Print (L"|\n");
  }
}

/* ---- Commands ---- */

STATIC EFI_STATUS
CmdList (
  IN CONST CHAR16 *GuidFilter,   /* may be NULL */
  IN CONST CHAR16 *PrefixFilter  /* may be NULL */
  )
{
  EFI_STATUS  Status;
  CHAR16     *Name;
  EFI_GUID    Guid;
  UINTN       NameSize;
  UINTN       Count = 0;
  EFI_GUID    FilterGuid;
  BOOLEAN     UseGuidFilter = FALSE;

  if (GuidFilter != NULL) {
    if (!EFI_ERROR (ParseGuid (GuidFilter, &FilterGuid))) {
      UseGuidFilter = TRUE;
    }
  }

  Name = AllocatePool (MAX_VAR_NAME * sizeof (CHAR16));
  if (Name == NULL) return EFI_OUT_OF_RESOURCES;

  Print (L"\n%-50s  %-38s  %s\n",
         L"Name", L"GUID", L"Attrs");
  Print (L"%-50s  %-38s  %s\n",
         L"----", L"----", L"-----");

  Name[0] = L'\0';
  ZeroMem (&Guid, sizeof Guid);

  for (;;) {
    NameSize = MAX_VAR_NAME * sizeof (CHAR16);
    Status = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    if (Status == EFI_NOT_FOUND) break;
    if (EFI_ERROR (Status)) {
      Print (L"GetNextVariableName error: %r\n", Status);
      break;
    }

    /* Apply filters */
    if (UseGuidFilter && !CompareGuid (&Guid, &FilterGuid)) continue;
    if (PrefixFilter != NULL && StrnCmp (Name, PrefixFilter, StrLen (PrefixFilter)) != 0) continue;

    UINT32 Attrs = 0;
    UINTN  Sz = 0;
    gRT->GetVariable (Name, &Guid, &Attrs, &Sz, NULL);

    Print (L"%-50s  ", Name);
    PrintGuid (&Guid);
    Print (L"  ");
    PrintAttrs (Attrs);
    Print (L"  %lu B\n", (UINT64)Sz);
    Count++;
  }

  Print (L"\n%lu variable(s) found.\n", (UINT64)Count);
  FreePool (Name);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CmdGet (
  IN CONST CHAR16 *Name,
  IN CONST CHAR16 *GuidStr
  )
{
  EFI_GUID  Guid;
  UINTN     DataSize;
  UINT32    Attrs;
  UINT8    *Data;

  if (EFI_ERROR (ParseGuid (GuidStr, &Guid))) {
    Print (L"Error: invalid GUID '%s'.\n", GuidStr);
    return EFI_INVALID_PARAMETER;
  }
  Data = GetVar (Name, &Guid, &DataSize, &Attrs);
  if (Data == NULL) {
    Print (L"Error: variable '%s' not found.\n", Name);
    return EFI_NOT_FOUND;
  }
  PrintVariable (Name, &Guid, Attrs, Data, DataSize);
  FreePool (Data);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CmdSet (
  IN CONST CHAR16 *Name,
  IN CONST CHAR16 *GuidStr,
  IN CONST CHAR16 *AttrsStr,
  IN CONST CHAR16 *HexData
  )
{
  EFI_GUID  Guid;
  UINT32    Attrs;
  UINT8     Buf[MAX_VAR_DATA];
  UINTN     DataLen;

  if (EFI_ERROR (ParseGuid (GuidStr, &Guid))) {
    Print (L"Error: invalid GUID '%s'.\n", GuidStr);
    return EFI_INVALID_PARAMETER;
  }
  Attrs = (UINT32)StrDecimalToUint64 (AttrsStr);
  DataLen = HexToBin (HexData, Buf, sizeof Buf);
  if (DataLen == 0 && StrLen (HexData) > 0) {
    Print (L"Error: invalid hex data.\n");
    return EFI_INVALID_PARAMETER;
  }

  EFI_STATUS Status = gRT->SetVariable (
                              (CHAR16 *)Name, &Guid, Attrs, DataLen, Buf);
  if (EFI_ERROR (Status)) {
    Print (L"Error: SetVariable failed: %r\n", Status);
    return Status;
  }
  Print (L"Variable '%s' set (%lu bytes).\n", Name, (UINT64)DataLen);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CmdDelete (
  IN CONST CHAR16 *Name,
  IN CONST CHAR16 *GuidStr
  )
{
  EFI_GUID  Guid;

  if (EFI_ERROR (ParseGuid (GuidStr, &Guid))) {
    Print (L"Error: invalid GUID '%s'.\n", GuidStr);
    return EFI_INVALID_PARAMETER;
  }
  /* SetVariable with DataSize=0 deletes the variable */
  UINT32     Attrs = 0;
  UINTN      Sz = 0;
  gRT->GetVariable ((CHAR16 *)Name, &Guid, &Attrs, &Sz, NULL);

  EFI_STATUS Status = gRT->SetVariable ((CHAR16 *)Name, &Guid, Attrs, 0, NULL);
  if (EFI_ERROR (Status)) {
    Print (L"Error: delete failed: %r\n", Status);
    return Status;
  }
  Print (L"Variable '%s' deleted.\n", Name);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
CmdExport (
  IN CONST CHAR16 *GuidFilter,
  IN CONST CHAR16 *PrefixFilter,
  IN CONST CHAR16 *FilePath
  )
{
  EFI_STATUS         Status;
  CHAR16            *Name;
  EFI_GUID           Guid;
  UINTN              NameSize;
  SHELL_FILE_HANDLE  Fh;
  UINTN              Count = 0;
  EFI_GUID           FilterGuid;
  BOOLEAN            UseGuidFilter = FALSE;

  if (GuidFilter != NULL && !EFI_ERROR (ParseGuid (GuidFilter, &FilterGuid))) {
    UseGuidFilter = TRUE;
  }

  Status = ShellOpenFileByName (FilePath, &Fh,
             EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Print (L"Error: cannot open '%s': %r\n", FilePath, Status);
    return Status;
  }

  Name = AllocatePool (MAX_VAR_NAME * sizeof (CHAR16));
  if (Name == NULL) { ShellCloseFile (&Fh); return EFI_OUT_OF_RESOURCES; }

  Name[0] = L'\0';
  ZeroMem (&Guid, sizeof Guid);

  CHAR16 *HexBuf = AllocatePool ((MAX_VAR_DATA * 2 + 2) * sizeof (CHAR16));
  if (HexBuf == NULL) {
    FreePool (Name);
    ShellCloseFile (&Fh);
    return EFI_OUT_OF_RESOURCES;
  }

  for (;;) {
    NameSize = MAX_VAR_NAME * sizeof (CHAR16);
    Status = gRT->GetNextVariableName (&NameSize, Name, &Guid);
    if (Status == EFI_NOT_FOUND) break;
    if (EFI_ERROR (Status)) break;

    if (UseGuidFilter && !CompareGuid (&Guid, &FilterGuid)) continue;
    if (PrefixFilter != NULL && StrnCmp (Name, PrefixFilter, StrLen (PrefixFilter)) != 0) continue;

    UINT32 Attrs = 0;
    UINTN  DataSize = 0;
    UINT8 *Data = GetVar (Name, &Guid, &DataSize, &Attrs);
    if (Data == NULL) continue;

    BinToHex (Data, DataSize, HexBuf, MAX_VAR_DATA * 2 + 2);
    FreePool (Data);

    /* Write line: <GUID> <Name> <Attrs> <HexData>\n */
    CHAR16 Line[MAX_VAR_NAME + 512];
    UINTN  LineLen = UnicodeSPrint (Line, sizeof Line,
                       L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x %s %u %s\n",
                       Guid.Data1, Guid.Data2, Guid.Data3,
                       Guid.Data4[0], Guid.Data4[1],
                       Guid.Data4[2], Guid.Data4[3], Guid.Data4[4],
                       Guid.Data4[5], Guid.Data4[6], Guid.Data4[7],
                       Name, Attrs, HexBuf);
    ShellWriteFile (Fh, &LineLen, Line);
    Count++;
  }

  FreePool (HexBuf);
  FreePool (Name);
  ShellCloseFile (&Fh);
  Print (L"Exported %lu variable(s) to '%s'.\n", (UINT64)Count, FilePath);
  return EFI_SUCCESS;
}

/* Very simple line parser — reads the exported text file and restores vars. */
STATIC EFI_STATUS
CmdImport (
  IN CONST CHAR16 *FilePath
  )
{
  SHELL_FILE_HANDLE  Fh;
  EFI_STATUS         Status;
  UINTN              Count = 0, Errors = 0;

  Status = ShellOpenFileByName (FilePath, &Fh, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    Print (L"Error: cannot open '%s': %r\n", FilePath, Status);
    return Status;
  }

  CHAR16 *Line;
  while (!ShellFileHandleEof (Fh)) {
    Line = ShellFileHandleReturnLine (Fh, NULL);
    if (Line == NULL) break;
    if (StrLen (Line) == 0 || Line[0] == L'#') {
      FreePool (Line);
      continue;
    }

    /* Tokenise by space: GuidStr Name Attrs HexData */
    CHAR16 *Tokens[4];
    UINTN   TokIdx = 0;
    CHAR16 *Cur = Line;

    ZeroMem (Tokens, sizeof Tokens);
    while (TokIdx < 4 && *Cur) {
      while (*Cur == L' ') Cur++;
      if (*Cur == L'\0') break;
      Tokens[TokIdx++] = Cur;
      /* Find end of token */
      while (*Cur && *Cur != L' ') Cur++;
      if (*Cur == L' ') *Cur++ = L'\0';
    }

    if (TokIdx < 4) {
      FreePool (Line);
      continue;
    }

    /* Restore */
    EFI_GUID Guid;
    if (EFI_ERROR (ParseGuid (Tokens[0], &Guid))) {
      Errors++;
      FreePool (Line);
      continue;
    }
    UINT32 Attrs   = (UINT32)StrDecimalToUint64 (Tokens[2]);
    UINT8  Buf[MAX_VAR_DATA];
    UINTN  DataLen = HexToBin (Tokens[3], Buf, sizeof Buf);

    Status = gRT->SetVariable (Tokens[1], &Guid, Attrs, DataLen, Buf);
    if (EFI_ERROR (Status)) {
      Print (L"  WARN: failed to set '%s': %r\n", Tokens[1], Status);
      Errors++;
    } else {
      Count++;
    }
    FreePool (Line);
  }

  ShellCloseFile (&Fh);
  Print (L"Import done: %lu set, %lu error(s).\n", (UINT64)Count, (UINT64)Errors);
  return EFI_SUCCESS;
}

/* ---- Entry point ---- */

INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16 **Argv
  )
{
  if (Argc < 2) {
    Print (L"NvramOps v%s - UEFI NVRAM Variable Batch Operations\n\n",
           NVRAMOPS_VERSION);
    Print (L"Usage:\n");
    Print (L"  NvramOps list   [-g <GUID>] [-p <Prefix>]\n");
    Print (L"  NvramOps get    <Name> <GUID>\n");
    Print (L"  NvramOps set    <Name> <GUID> <Attrs> <HexData>\n");
    Print (L"  NvramOps del    <Name> <GUID>\n");
    Print (L"  NvramOps export [-g <GUID>] [-p <Prefix>] <File>\n");
    Print (L"  NvramOps import <File>\n\n");
    Print (L"Attrs (sum): 1=NV  2=BS  4=RT\n");
    Print (L"HexData: contiguous hex bytes, e.g. 01000000\n\n");
    Print (L"CAUTION: Modifying firmware variables can brick the system.\n");
    return EFI_INVALID_PARAMETER;
  }

  CONST CHAR16 *Cmd = Argv[1];

  /* ---- list ---- */
  if (StrCmp (Cmd, L"list") == 0) {
    CONST CHAR16 *GuidF = NULL, *PrefF = NULL;
    for (UINTN i = 2; i < Argc; i++) {
      if (StrCmp (Argv[i], L"-g") == 0 && i + 1 < Argc) GuidF = Argv[++i];
      if (StrCmp (Argv[i], L"-p") == 0 && i + 1 < Argc) PrefF = Argv[++i];
    }
    return CmdList (GuidF, PrefF);
  }

  /* ---- get ---- */
  if (StrCmp (Cmd, L"get") == 0) {
    if (Argc < 4) { Print (L"Usage: NvramOps get <Name> <GUID>\n"); return EFI_INVALID_PARAMETER; }
    return CmdGet (Argv[2], Argv[3]);
  }

  /* ---- set ---- */
  if (StrCmp (Cmd, L"set") == 0) {
    if (Argc < 6) { Print (L"Usage: NvramOps set <Name> <GUID> <Attrs> <HexData>\n"); return EFI_INVALID_PARAMETER; }
    return CmdSet (Argv[2], Argv[3], Argv[4], Argv[5]);
  }

  /* ---- del ---- */
  if (StrCmp (Cmd, L"del") == 0) {
    if (Argc < 4) { Print (L"Usage: NvramOps del <Name> <GUID>\n"); return EFI_INVALID_PARAMETER; }
    return CmdDelete (Argv[2], Argv[3]);
  }

  /* ---- export ---- */
  if (StrCmp (Cmd, L"export") == 0) {
    CONST CHAR16 *GuidF = NULL, *PrefF = NULL, *File = NULL;
    for (UINTN i = 2; i < Argc; i++) {
      if (StrCmp (Argv[i], L"-g") == 0 && i + 1 < Argc) GuidF = Argv[++i];
      else if (StrCmp (Argv[i], L"-p") == 0 && i + 1 < Argc) PrefF = Argv[++i];
      else File = Argv[i];
    }
    if (File == NULL) { Print (L"Usage: NvramOps export [-g G] [-p P] <File>\n"); return EFI_INVALID_PARAMETER; }
    return CmdExport (GuidF, PrefF, File);
  }

  /* ---- import ---- */
  if (StrCmp (Cmd, L"import") == 0) {
    if (Argc < 3) { Print (L"Usage: NvramOps import <File>\n"); return EFI_INVALID_PARAMETER; }
    return CmdImport (Argv[2]);
  }

  Print (L"Error: unknown command '%s'.\n", Cmd);
  return EFI_INVALID_PARAMETER;
}
