# KI-2026-002: Module Manager 菜单打开时 Alt+Tab 卡死

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-002 |
| **状态** | resolved |
| **首次记录** | 2026-06 |
| **最后验证** | 2026-06 |
| **区域** | module_manager |
| **标签** | `alt-tab`, `device-lost`, `imgui`, `d3d9` |

## 症状（Symptoms）

- 未开菜单时 Alt+Tab 正常；**Insert/F10 菜单打开**后切走再切回游戏挂起
- NDJSON 可能在 `WM_ACTIVATEAPP` 后停止；device 保持 `DEVICELOST` (`0x88760808`)，无 `device_recovered`

## 根因（Root cause，须已验证）

1. 菜单打开时 ImGui 持有额外 D3D9 pool 对象；Alt+Tab 时 device 进入 `DEVICELOST`，对象未及时释放，`DEVICENOTRESET` 过晚，游戏 `Reset()` 阻塞。
2. 在已安装 hook 后从 `PeekMessage`/`GetMessage` 路径运行完整 `PumpFromMessageThread()`，焦点返回时消息线程与渲染线程死锁。

## 已验证修复（Verified fix）

`runtime/module_manager/presentation.cpp`：

1. `PumpPreHookBootstrap()` 仅在 `!g_hooksInstalled` 时使用；完整 pump 仅从 `EndScene` 且 `D3D_OK` 时调用
2. Present hook：**始终**先 `UpdateStability()`（与 mmultiplayer 一致）
3. 在 `DEVICELOST` 入口、`DEVICENOTRESET`、失焦/关菜单时 invalidate ImGui
4. EndScene lost 路径：绕过 overlay，最小副作用

**验证：** `debug-*.ndjson` 出现 `imgui_unfocus_hide` / `imgui_lost_invalidate`，随后 `device_recovered` / `imgui_device_reset`。

**文档：** [troubleshooting.md](../troubleshooting.md#alt-tab-freezes-game-module-manager-loaded)、[module-manager.md](../module-manager.md)

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-06 前 | 仅在 `DEVICENOTRESET` 时释放 ImGui 资源 | 卡死 | 过晚；Reset 已阻塞 |
| 2026-06 前 | 消息 hook 中始终跑完整 ImGui message pump | 卡死 | 焦点返回死锁 |
| 2026-06 前 | 忽略 `DEVICELOST`，EndScene 继续画 overlay | 挂起/黑屏 | pool 对象未释放 |

## 相关

- **源码：** `runtime/module_manager/presentation.cpp`

## 变更日志

| 日期 | 说明 |
|------|------|
| 2026-06 | 验证修复 |
| 2026-06-29 | 迁入 known-issues 登记册 |
