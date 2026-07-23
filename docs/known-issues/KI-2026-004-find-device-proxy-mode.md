# KI-2026-004: 代理模式下 FindDeviceSafe 导致游戏退出

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-004 |
| **状态** | resolved |
| **首次记录** | 2026-06 |
| **最后验证** | 2026-06 |
| **区域** | module_manager / d3d9 |
| **标签** | `proxy`, `find-device`, `split-mode` |

## 症状（Symptoms）

- Split 模式（d3d9 proxy + module_manager）注入后游戏短时间自动退出
- 无 overlay；可能无清晰 launcher 错误

## 根因（Root cause，须已验证）

`IsGameProxyD3D9Active()` 为真时仍调用 `FindDeviceSafe` / 模式扫描 live device。proxy 已激活但尚未收到 device 通知时，`GetCreationParameters` 等调用触发 **R6025**（SEH 无法捕获）。

## 已验证修复（Verified fix）

1. 确保 proxy → `MmOnD3D9DeviceCreated` 路径正常（export 见 KI 相关：`module_manager.def` 无修饰导出名）
2. 当 `IsGameProxyD3D9Active()`：**禁止** `FindDeviceSafe`；仅使用 `OnProxyDeviceCreated` 传入的 device
3. `MmProxyRetryDeviceNotify` + cached device 处理 CreateDevice 早于 manager 加载的竞态

**验证：** proxy 部署后主菜单可见 Module Manager；无注入后 2s 内退出。

**文档：** [troubleshooting.md](../troubleshooting.md#game-auto-exit-after-inject-split-mode)、[module-manager.md](../module-manager.md)

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-06 前 | proxy 模式下仍用 `FindDeviceSafe` 取 device | 进程退出 | R6025 |
| 2026-06 前 | 用 SEH 包裹 `FindDeviceSafe`「防崩溃」 | 仍退出 | R6025 非 SEH 可捕 |
| 2026-06 前 | 不部署 proxy，仅靠 inject + 扫描 device | 不稳定 | 与默认 split 架构冲突；见 KI-2026-001 |

## 相关

- **源码：** `runtime/module_manager/presentation.cpp`, `launcher/proxy/d3d9/d3d9.cpp`, `runtime/module_manager/module_manager.def`

## 变更日志

| 日期 | 说明 |
|------|------|
| 2026-06 | 验证修复 |
| 2026-06-29 | 迁入 known-issues 登记册 |
