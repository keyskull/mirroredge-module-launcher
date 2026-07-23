# SDK Verification Tools

用纯指令扫描、运行时反射和可选 Ghidra 参考确保 SDK 代码准确性。

## 工具

| 工具 | 用途 | 依赖 |
|------|------|------|
| `setup-ghidra.ps1` | 下载安装 Ghidra 11.3 (~425MB) | JDK 17+ |
| `extract-sdk-data.ps1` | Ghidra headless 分析 MirrorsEdge.exe，导出深度参考数据 | Ghidra + 游戏二进制 |
| `ghidra-export.py` | Ghidra Python 脚本：提取类布局、模式地址、FNV | Ghidra headless |
| `verify-sdk.ps1` | 纯指令/二进制扫描（唯一 pattern + section/RVA + FNV + lite reference） | 游戏二进制（无需 Ghidra） |
| `compute-code-probe.ps1` | 计算 FNV 哈希并更新 game_signature.h | 游戏二进制 |
| `generate-static-asserts.ps1` | 生成 sdk_verify_generated.h 布局参考 | 无 |

当前策略：`sdk_verify_generated.h` 是布局参考和 CI 一致性检查输入，默认不从
`me_sdk.h` 全局 include。不要重新引入旧的“注入 generated headers”方案；它会受
`#pragma pack` 上下文影响。

## 快速开始

```powershell
# 验证二进制（无需 Ghidra — 推荐日常使用）
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly

# 刷新纯指令扫描参考数据
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly -WriteLiteReference

# 对比当前二进制和已提交参考数据
.\tools\sdk-verify\verify-sdk.ps1 -BinaryOnly -CheckLiteReference

# 完整 Ghidra 分析（15 分钟，一次性）
.\tools\sdk-verify\setup-ghidra.ps1
.\tools\sdk-verify\extract-sdk-data.ps1

# 启用代码探针门控
.\tools\sdk-verify\compute-code-probe.ps1
```

## 输出

- `reference/sdk-reference-lite.json` — 纯指令扫描导出的轻量参考数据（无 timestamp；可提交和 CI 对比）
- `reference/sdk-reference.json` — Ghidra 导出的参考数据
- `shared/me_sdk/sdk_verify_generated.h` — 布局参考 static_assert（默认不从 `me_sdk.h` 全局 include；CI 检查其是否最新）
