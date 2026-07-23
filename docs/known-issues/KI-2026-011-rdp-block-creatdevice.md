# KI-2026-011: RDP session blocks CreateDevice — game shows "Message" dialog

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-011 |
| **状态** | `resolved` |
| **首次记录** | 2026-07-13 |
| **最后验证** | 2026-07-13 |
| **区域** | d3d9 / injection / harness |
| **标签** | `rdp`, `remote-desktop`, `create-device`, `proxy`, `iat`, `inline-hook` |

## 症状（Symptoms）

- 通过 Remote Desktop (RDP) 连接机器时，Mirror's Edge 启动后显示 `#32770` 类 "Message" 对话框，内容为 **"This game cannot run with 3D graphics over a Remote Desktop connection."**
- 即使 d3d9 proxy 已部署，`create_device_enter` / `create_device_return` 日志从不出现
- 使用原版游戏（无 proxy、无 d3d9.dll）启动也出现相同对话框
- `module_manager` PumpThread 只显示 heartbeat，`wait_proxy_device` 永不结束
- `core` IPC pipe 永远不可达 (PING timeout)
- 所有 bot spawn 测试全部 FAIL，游戏窗口标题为 "Message" 而非 "Mirror's Edge"
- **此问题与 proxy、module_manager、engine.dll 均无关** — 纯游戏自身的 RDP 检测拒绝初始化 D3D

## 根因（Root cause，已验证）

游戏启动时调用 `GetSystemMetrics(SM_REMOTESESSION)` (0x1000) 检测到 RDP 会话，弹出错误对话框并拒绝调用 `IDirect3D9::CreateDevice`。此检测发生在 vanilla 游戏主程序内，与 proxy DLL 无关。

验证方式：
1. 移除 `Binaries\d3d9.dll`（禁用 proxy），启动原版 `MirrorsEdge.exe` → 相同 "Message" 对话框
2. 移除根目录 `d3d9.dll`（禁用 DXVK wrapper）→ 相同 "Message" 对话框
3. 枚举游戏窗口：`Class=#32770, Title=Message`, Child `Static` Text= "This game cannot run with 3D graphics over a Remote Desktop connection."

## 已验证修复（Verified fix）

**不使用 Remote Desktop 连接游戏机器。** 使用以下替代方案：

- **Parsec**（免费，为游戏流媒体设计，不触发 RDP 检测）
- **Moonlight + Sunshine**（开源，基于 NVIDIA GameStream）
- **Steam Remote Play**（如果游戏通过 Steam 运行）
- **物理控制台**（本地登录）

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

**所有 RDP bypass 尝试均不可靠，因为这些 hook 都只在游戏调用目标 API 时生效，但 DLL 加载时 IAT 尚未解析，且游戏可通过 `GetProcAddress` 动态获取函数指针绕过 IAT patch。**

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-07-13 | IAT patch `GetSystemMetrics` only (main EXE) | 部分生效 (~30%) | 游戏有两个 IAT descriptor (SecuROM)，只覆盖了一个；游戏通过 GetProcAddress 绕过 |
| 2026-07-13 | IAT patch `GetSystemMetrics` + `SystemParametersInfoA` (main EXE) | 不生效 | 游戏导入的是 `SystemParametersInfoW` (Unicode)，不是 A 版本 |
| 2026-07-13 | IAT patch `GetSystemMetrics` + `SystemParametersInfoW` (main EXE only) | 部分生效 (~30%) | 同样只覆盖了第一个 IAT descriptor |
| 2026-07-13 | IAT patch across ALL modules via `EnumProcessModules` (15+12 entries) | 不生效 (0/5) | 15 个 `GetSystemMetrics` entry + 12 个 `SystemParametersInfoW` entry 全部 patch 后，游戏仍检测到 RDP — 必然使用了 `GetProcAddress` 或 ordinals 动态获取函数指针，绕过所有 IAT |
| 2026-07-13 | Inline hook `GetSystemMetrics` — `VirtualAlloc` trampoline + JMP rel32 | **Crash (游戏死亡)** | 系统 `GetSystemMetrics` prologue 为 `6A 10 68 xx xx...` (thunk, 非标准 prologue)，复制 5 字节到 trampoline 后 JMP 回原函数+5 时执行到 `PUSH 10h; PUSH addr; JMP` 但 addr 未重定位，崩溃 |
| 2026-07-13 | Inline hook `GetSystemMetrics` + `SystemParametersInfoW` 双函数 | **Crash (游戏死亡)** | `SystemParametersInfoW` prologue 同样非 position-independent；同时 hook 两个函数导致游戏启动即崩溃 |
| 2026-07-13 | Inline hook `GetSystemMetrics` **only**，验证 prologue `8B FF 55 8B EC` 匹配后才 hook | **不生效 (5/5 RDP-ERR)** | Prologue 验证失败 (`6A106828D9` ≠ `8BFF558BEC`)，inline hook 正确跳过，但 IAT-only 版本仍被 GetProcAddress 绕过 |
| 2026-07-13 | `ScheduleManagerPreload()` 在 `Direct3DCreate9` 内异步预加载 `module_manager.dll` | 非根本原因 | Subagent 分析确认 hook 代码正确，preload 线程未阻塞 — 根因是游戏根本未调用 CreateDevice |
| 2026-07-13 | Minimal proxy (纯 forward `Direct3DCreate9`，无 hook、无 module 加载) | 相同 "Message" 对话框 | 进一步确认问题与 proxy 代码无关 |
| 2026-07-13 | 移除 `Binaries\d3d9.dll` (完全不用 proxy) | 相同 "Message" 对话框 | 最终确认：这是原版游戏的问题 |

## 影响范围

此 RDP 检测导致所有下游组件无法启动：

1. `CreateDevice` 不被调用 → `proxy_device` 通知不触发
2. `module_manager` `wait_proxy_device` 永久等待
3. `core` 无法初始化 (IPC pipe 不创建)
4. `engine.dll` 功能不可用 (SpawnCharacter, LevelLoadHook 等)
5. **所有 bot spawn / multiplayer 测试全部 FAIL**

## 相关

- **源码：** `launcher/proxy/d3d9/d3d9.cpp` (RDP bypass 代码已于 2026-07-14 移除；仅保留注释说明)
- **文档：** [troubleshooting.md](../troubleshooting.md#rdp-remote-desktop-blocks-creatdevice)
- **Harness：** `mp-playthrough-bots` (all attempts FAIL due to RDP block)

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| 2026-07-13 | Cursor AI | 创建记录；通过窗口枚举确认 "Message" 对话框内容为 "This game cannot run with 3D graphics over a Remote Desktop connection"；确认 vanilla 游戏也有同样问题 |
| 2026-07-13 | Cursor AI | 记录所有 RDP bypass 失败方案 (IAT patching × 4 次迭代, inline hook × 3 次迭代) |
| 2026-07-13 | Cursor AI | 确认根因与 proxy 无关；`resolved` — 修复方案为不使用 RDP |
| 2026-07-14 | Cursor AI | 从 `d3d9.cpp` 删除 ~196 行 RDP bypass 死代码，保留引用 KI-2026-011 的注释 |
