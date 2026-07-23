# SDK Verification

SDK 验证系统：布局参考/CI、运行时反射、纯指令扫描；Ghidra 反编译作为可选深度参考。

## 架构

```
tools/sdk-verify/                     ← SDK 验证工具
├── setup-ghidra.ps1                  ← 下载安装 Ghidra
├── ghidra-export.py                  ← Ghidra headless: 导出类布局
├── extract-sdk-data.ps1              ← 运行 Ghidra 分析
├── compute-code-probe.ps1            ← 计算 FNV 并更新 game_signature.h
├── verify-sdk.ps1                    ← 纯指令/二进制扫描（无需 Ghidra）
├── generate-static-asserts.ps1       ← 生成 static_assert 验证头
└── reference/
    ├── sdk-reference-lite.json       ← 纯指令扫描参考数据
    └── sdk-reference.json            ← Ghidra 导出的深度参考数据

shared/me_sdk/
├── sdk_verify.h                      ← static_assert 宏定义
├── sdk_verify_generated.h            ← 自动生成的布局参考（40 个类 + 3 个成员）
├── runtime/
│   ├── sdk_errors.h                  ← 错误码（含 ClassLayoutMismatch/ClassNotFound）
│   ├── init.h                        ← ValidateClassLayouts() 声明
│   ├── init.cpp                      ← 运行时 IsA 链验证（22 个关键类）
│   └── game_signature.h              ← FNV 代码探针门控

CI:
├── .github/workflows/sdk-verify.yml  ← PR/push 自动验证
└── build.ps1                         ← -VerifySdk 参数
```

## 验证层

### 1. 布局参考 / CI

`sdk_verify_generated.h` 包含 40 个类大小和 3 个关键成员偏移的 `static_assert` 参考：
```cpp
MMOD_SDK_VERIFY_CLASS_SIZE(UObject, 0x003C);
MMOD_SDK_VERIFY_CLASS_SIZE(AActor, 0x01C0);
...
```

注意：generated SDK headers 使用不同 `#pragma pack` 上下文，不能把该文件从 `me_sdk.h` 末尾全局 include，否则会用错误 packing 计算部分类大小。当前 CI 只检查该参考头是否由脚本生成且保持最新；如要启用真正编译时检查，必须在对应 package header 的正确 pack 上下文内添加。

### 2. 运行时（每次游戏启动）

`ValidateClassLayouts()` 在 `ValidateRuntime()` 中被调用，验证：
- 22 个关键 UE3 类可通过 `FindClass` 找到
- IsA 继承链正确（如 `ATdPlayerPawn -> ATdPawn -> APawn -> AActor -> UObject`）

错误的偏移/布局会在游戏内触发 `ClassLayoutMismatch` 或 `ClassNotFound` 错误，可通过 harness 的 `GET_STATUS` 检测。

### 3. 纯指令扫描（默认日常路径）

`verify-sdk.ps1` 直接读取 `MirrorsEdge.exe`，不需要 Ghidra：
- PE image size gate
- `0x1000..0x1FFF` 代码探针 FNV-1a
- GNames/GObjects opcode pattern 唯一性检查（0 次或多次都失败）
- opcode 所在 file offset / RVA / VA / PE section 解析
- opcode 内嵌全局指针 VA 提取，并验证 target VA 落在 PE section 内
- 可选输出或检查 `tools/sdk-verify/reference/sdk-reference-lite.json`

日常记录基线：
```powershell
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly -WriteLiteReference
```

日常回归检查：
```powershell
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly -CheckLiteReference
```

### 4. Ghidra 反编译参考（可选深度路径）

Ghidra headless 分析 `MirrorsEdge.exe`，可额外导出：
- 代码探针 FNV（用于代码探针门控）
- GNames/GObjects 模式地址
- 类布局参考（仅在 Ghidra 项目中已有/导入可识别数据类型时可覆盖手动记录；否则至少保留二进制和 pattern 证据）

## 工作流

### 日常纯指令验证
```powershell
# 只走纯二进制/opcode 扫描，不依赖 Ghidra
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly

# 刷新可提交/可对比的轻量参考数据
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly -WriteLiteReference

# 对比当前游戏二进制和已提交的轻量参考数据
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly -CheckLiteReference
```

### 可选 Ghidra 深度分析
```powershell
# 1. 安装 Ghidra（仅一次，~425MB）
.\tools\sdk-verify\setup-ghidra.ps1

# 2. 运行分析（5-15 分钟）
.\tools\sdk-verify\extract-sdk-data.ps1

# 3. 启用代码探针门控
.\tools\sdk-verify\compute-code-probe.ps1

# 4. 重新生成 static_assert
.\tools\sdk-verify\generate-static-asserts.ps1
```

`setup-ghidra.ps1` 默认下载 Ghidra `11.3` 的官方 release asset
`ghidra_11.3_PUBLIC_20250205.zip`。如升级 Ghidra 版本，需要同时更新
`-GhidraVersion` 和 `-GhidraAssetDate`。

### 日常构建
```powershell
# CI 会检查 sdk_verify_generated.h 是否由脚本生成且保持最新

# 可选：验证二进制（纯指令 + lite reference check；non-fatal）
.\build.ps1 -VerifySdk
```

### 生成的头文件更新后
```powershell
# 重新生成 static_assert 验证头
.\tools\sdk-verify\generate-static-asserts.ps1
```

### CI 自动检查
- PR 修改 `shared/me_sdk/` → 自动验证 `sdk_verify_generated.h` 是否过期
- PR 修改 `game_signature.h` → 提示是否启用了代码探针门控

## 添加新类验证

1. 在 `generate-static-asserts.ps1` 的 `$knownClasses` 列表添加条目
2. 运行脚本重新生成 `sdk_verify_generated.h`
3. 如需运行时验证，在 `init.cpp` 的 `kCriticalClasses` 数组添加 IsA 链

## 添加新成员偏移验证

在 `generate-static-asserts.ps1` 的成员验证区域添加：
```powershell
[void]$sb.AppendLine('MMOD_SDK_VERIFY_MEMBER_OFFSET(ClassName, MemberName, 0x00XX);')
```

## 开发记录

### 2026-07-04

- 清理旧方案：删除 `tools/sdk-verify/inject-static-asserts.ps1`。该脚本会把 `MMOD_SDK_VERIFY_CLASS_SIZE` 直接注入 generated headers；前次验证已确认这条路会被 `#pragma pack` 上下文影响，容易制造错误结论。
- 修正 Ghidra setup：`setup-ghidra.ps1` 不再硬编码错误 asset 日期，默认使用已确认存在的 `ghidra_11.3_PUBLIC_20250205.zip`。
- 增加纯指令基线产物：`verify-sdk.ps1 -BinaryOnly -WriteLiteReference` 输出 `tools/sdk-verify/reference/sdk-reference-lite.json`，记录 image size、SHA-256、代码探针 FNV、GNames/GObjects opcode file offset/RVA/VA/section 和 target VA/section。
- 增加 lite reference 回归门：`verify-sdk.ps1 -BinaryOnly -CheckLiteReference` 对比当前扫描和已提交基线；`build.ps1 -VerifySdk` 使用该纯指令对比路径。
- 增强 pattern 可靠性：GNames/GObjects 现在要求唯一匹配，并验证 target VA 位于 PE section 内；提交基线不再包含 timestamp，避免无意义 diff。
- 集成 pure-instruction 扫描到 harness：`Invoke-DebugBuild` 结束时自动调用 `Assert-SdkBinaryInstructionScan`，覆盖**所有游戏场景**（smoke-split、inject-mp、mp-functional、mod-full、visual-test 等）。失败为 non-fatal warning，以便在代码改动后提醒更新 baseline。
- 本机日常验证：
  - `verify-sdk.ps1`：通过 image size、代码探针 FNV、GNames/GObjects pattern 扫描。当前 FNV 为 `0xD04855C8`，GNames file offset `0x47164B`，GObjects file offset `0x6C982`。
  - `generate-static-asserts.ps1` 一致性检查：`sdk_verify_generated.h` 已是最新。
- 当前策略：Ghidra 不再阻塞日常验证；本机未安装 Ghidra/analyzeHeadless 时，只缺 `tools/sdk-verify/reference/sdk-reference.json` 深度参考。默认使用 `sdk-reference-lite.json` + 运行时 `ValidateClassLayouts()`。
