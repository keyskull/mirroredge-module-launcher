# KI-2026-009: Engine tab crashes from unsafe engine lookup

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-009 |
| **状态** | `resolved` |
| **首次记录** | 2026-07-04 |
| **最后验证** | 2026-07-04 |
| **区域** | core / module_manager / sdk |
| **标签** | `engine-tab`, `ui`, `thread-safety`, `seh` |

## 症状（Symptoms）

打开 Module Manager 后切到 **Engine** 标签，游戏可能直接崩溃。Harness 表现为 Engine tab 点击后进程退出或无法继续采集 `mm/multiplayer/engine-*` UI target。

## 根因（Root cause，须已验证）

`runtime/core/menu/engine_tab.cpp` 在 UI 渲染路径中直接调用 `Engine::GetEngine()`。该路径会裸读 / 扫描 UE3 `GObjects`，没有使用 `safe_gui` 的 SEH 保护和对象合理性检查；当对象数组正在变化或返回对象不可安全读取时，Engine tab 渲染会触发访问冲突。

## 已验证修复（Verified fix）

- `runtime/core/menu/engine_tab.cpp` 改为通过 `MeSdk::Safe::Gui::TryFindTdGameEngine(true)` 获取 engine，再交给 `TryReadEngineMenuState()` 读取。
- 验证：`build.ps1 -NoDeploy` 通过；`tools/debug-harness/scenarios/mp-core-functional.ps1 -SkipBuild` 通过，`mod-tab: Engine OK` 和 `mp-core-functional: PASS`。
- 相关快速排查见 [troubleshooting.md](../troubleshooting.md#module-manager--core-inject-split-mode)。

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-07-04 | 仅依赖 Module Manager tab 回调外层 SEH | 不足 | 外层可避免整进程退出，但 Engine tab 内部仍会反复 fault；应消除裸 UE3 查找路径 |

## 相关

- **源码：** `runtime/core/menu/engine_tab.cpp`
- **源码：** `shared/me_sdk/runtime/safe_gui.h`
- **文档：** [troubleshooting.md](../troubleshooting.md#module-manager--core-inject-split-mode)
- **Harness：** `mp-core-functional`

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| 2026-07-04 | GPT-5.5 | 创建记录；记录 Engine tab 改用 safe_gui engine lookup |
