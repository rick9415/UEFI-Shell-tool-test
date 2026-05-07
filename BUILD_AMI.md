# 在 AMI Aptio V 環境下建置

## 前置條件

| 項目 | 說明 |
|---|---|
| AMI Aptio V SDK | 需向 AMI 取得授權（NDA） |
| Python 3.6+ | `python --version` |
| NASM 2.15+ | `nasm -v` |
| 編譯器 (Windows) | Visual Studio 2017 / 2019 / 2022 |
| 編譯器 (Linux) | GCC 9+ 或 LLVM/Clang |

---

## 一、設定工作目錄

### 方法 A：放在 Aptio 樹根（推薦）

```
AptioV/
├── edk2/
├── AmiModulePkg/
├── AmiCompatibilityPkg/
└── UefiToolkitPkg/      ← git clone 到這裡
```

```bash
cd AptioV
git clone <this-repo> UefiToolkitPkg
```

### 方法 B：Aptio 以 MdePkg 在根目錄（較舊版本）

如果你的 Aptio 樹根直接有 `MdePkg/`（沒有 `edk2/` 子目錄），
則把 `UefiToolkitAmi.dsc` 裡所有 `edk2/` 前綴去掉：

```bash
sed -i 's|edk2/||g' UefiToolkitPkg/UefiToolkitAmi.dsc
```

---

## 二、初始化建置環境

### Windows (CMD / Developer Command Prompt)

```cmd
:: 切到 AptioV 根目錄
cd C:\AptioV

:: 初始化 EDK2 建置工具（AMI 通常在根目錄提供這個 bat）
edksetup.bat Rebuild

:: 或者如果 AMI 有自己的初始化腳本
AmiSetup.bat
```

### Linux (bash)

```bash
cd ~/AptioV

# 初始化
source edksetup.sh --reconfig
# 或 AMI 版本
source AmiSetup.sh
```

---

## 三、確認 Conf/target.txt

編輯 `Conf/target.txt`，確認以下設定：

```ini
# 目標架構（依主機板選擇）
TARGET_ARCH = X64

# 建置工具鏈標籤
# Windows VS2019：
TOOL_CHAIN_TAG = VS2019
# Windows VS2017：
# TOOL_CHAIN_TAG = VS2017
# Linux GCC：
# TOOL_CHAIN_TAG = GCC5

# 建置類型
TARGET = DEBUG
# 或
# TARGET = RELEASE
```

---

## 四、執行建置

### 使用 UefiToolkitAmi.dsc（AMI 路徑版）

```bash
# X64 Release
build -p UefiToolkitPkg/UefiToolkitAmi.dsc \
      -a X64 \
      -t GCC5 \
      -b RELEASE

# X64 Debug
build -p UefiToolkitPkg/UefiToolkitAmi.dsc \
      -a X64 \
      -t GCC5 \
      -b DEBUG

# IA32
build -p UefiToolkitPkg/UefiToolkitAmi.dsc \
      -a IA32 \
      -t GCC5 \
      -b RELEASE
```

### Windows + VS2019

```cmd
build -p UefiToolkitPkg\UefiToolkitAmi.dsc ^
      -a X64 ^
      -t VS2019 ^
      -b RELEASE
```

### 只建置單一工具

```bash
# 只建 SysInfo.efi
build -p UefiToolkitPkg/UefiToolkitAmi.dsc \
      -a X64 -t GCC5 -b RELEASE \
      -m UefiToolkitPkg/Applications/SysInfo/SysInfo.inf
```

---

## 五、輸出檔案位置

```
Build/UefiToolkitPkg/RELEASE_GCC5/X64/
├── SysInfo.efi
├── MemTest.efi
├── DiskInfo.efi
└── PciInfo.efi
```

---

## 六、整合進 AMI ROM（可選）

如果要把工具嵌入韌體映像而不是放在 EFI 分割區，
需在主平台的 FDF 檔案中加入：

```ini
# 在主平台 .fdf 的 [FV.SHELL] 或 [FV.DXE] section 加入：
INF UefiToolkitPkg/Applications/SysInfo/SysInfo.inf
INF UefiToolkitPkg/Applications/MemTest/MemTest.inf
INF UefiToolkitPkg/Applications/DiskInfo/DiskInfo.inf
INF UefiToolkitPkg/Applications/PciInfo/PciInfo.inf
```

並在主平台 DSC 的 `[Components]` 區段加入相同的四行。
建置後工具會被打包進 ROM 映像，
UEFI Shell 啟動後可在 `FS0:\EFI\Tools\` 找到。

---

## 七、常見錯誤排除

### `Could not find 'RegisterFilterLib'`
你的 Aptio EDK2 分支比較舊，尚未加入此 Library。
在 `UefiToolkitAmi.dsc` 中把 `RegisterFilterLib` 那行注解掉即可。

### `undefined reference to ShellPromptForResponse`
確認 `ShellLib` 路徑正確，並且 `ShellPkg` 已在工作目錄中。

### `error: 'gEfiPartitionInfoProtocolGuid' undeclared`
較舊的 MdePkg 可能沒有 `PartitionInfo.h`。
在 `DiskInfo.c` 加入：
```c
// 若 EDK2 版本不含 PartitionInfo protocol，直接跳過
#ifndef EFI_PARTITION_INFO_PROTOCOL_GUID
#define gEfiPartitionInfoProtocolGuid \
  { 0x8cf2f62c, 0xbc9b, 0x4821, \
    { 0x80, 0x8d, 0xec, 0x9e, 0xc4, 0x21, 0xa1, 0xa0 } }
#endif
```

### `build error: no such file edk2/MdePkg/...`
表示你的 Aptio MdePkg 在根目錄，不在 `edk2/` 底下。
參考上方「方法 B」移除 `edk2/` 前綴。
