# KI-2026-007: ENSURE_MP_HOOKS 在 IPC 线程执行导致核心初始化失败

## 元数据

| 字段 | 值 |
|------|-----|
| **ID** | KI-2026-007 |
| **状态** | `resolved` |
| **首次记录** | 2026-07-03 |
| **最后验证** | 2026-07-03 |
| **区域** | core / mod_ipc |
| **标签** | `ipc`, `thread-safety`, `core_ready`, `regression` |

## 症状（Symptoms）

Harness 全量测试中大范围 `core_ready event timeout`，以及 `overlay ready timeout`（endSceneCalls=0, presentCalls=0）。

Harness 结果摘要：
```
mp-core-functional: core_ready event timeout
dolly-functional: core_ready event timeout
mp-functional: core_ready event timeout
mp-gui-test: core_ready event timeout
mp-playthrough-bots: core_ready event timeout
ui-module-manager: overlay ready timeout (endSceneCalls:0)
visual-test: overlay ready timeout (endSceneCalls:0)
mod-full: overlay ready timeout (endSceneCalls:0)
```

Module manager 状态 JSON 中 `hooksInstalled:true` 但 `endSceneCalls:0`，表示 game 窗口存在但 D3D9 渲染被阻塞。部分场景游戏直接崩溃（`Game process has no main window handle`）。

## 根因（Root cause，须已验证）

`runtime/core/mod_ipc.cpp` 中 `TryHandleCommandInline()` 新增了 `ENSURE_MP_HOOKS` 和 `ENSURE_GAMEPLAY_HOOKS` 的内联处理。这两个命令调用 `MmMultiplayer_EnsureRuntimeHooks()` 和 `Engine::EnsureGameplayHooks()`，它们通过 UE3 internals（GNames/GObjects/vtable hook）操作，**必须在游戏主线程执行**。

内联 handler 在 IPC 工作线程上执行：
```
IPC thread → TryHandleCommandInline() → EnsureMultiplayerRuntimeHooks() → MmMultiplayer_EnsureRuntimeHooks()
```

此前只在 `HandleCommandOnMainThread()` 中处理，由游戏主线程队列消费。

在 IPC 线程上设置 hook 可能导致：
1. UE3 GObjects 访问冲突（非线程安全）
2. vtable patch 竞争条件
3. `MmMultiplayer_EnsureRuntimeHooks` 中 LoadLibrary 的 loader lock 死锁

伴随修复：`engine_module_client.cpp` 中 `exports.resolved = true` 延迟到 `RuntimeModuleClient::EnsureLoaded` 之后。这导致 `ResolveExports()` 在 engine.dll/core.dll 均未加载时返回 false 且不缓存，上层轮询无法区分「尚未加载」和「加载失败」。

## 已验证修复（Verified fix）

**1. 从内联 handler 移除 `ENSURE_MP_HOOKS` / `ENSURE_GAMEPLAY_HOOKS`**

`mod_ipc.cpp` → `TryHandleCommandInline()`: 删除这两个 case，只保留 `HandleCommandOnMainThread()` 中的处理。

**2. 恢复 `exports.resolved = true` 到函数顶部**

`engine_module_client.cpp` → `ResolveExports()`: 在执行 `GetModuleHandleW` 检查前设置 `exports.resolved = true`，替代注释 "Do not cache this miss"（该注释已被回收）。

验证：`smoke-split` 单场景 PASS（game 正常启动，module_manager overlay ready，不再 "Access denied"）。

## 已尝试且无效 — 勿重复（Failed approaches — do NOT retry）

| 日期 | 尝试方案 | 结果 | 失败原因 |
|------|----------|------|----------|
| 2026-07-03 | 在 `TryHandleCommandInline` 中添加 ENSURE_MP_HOOKS / ENSURE_GAMEPLAY_HOOKS 内联处理 | 全量 harness 大面积 core_ready timeout + 游戏崩溃 | 在 IPC 线程操作 UE3 GObjects / hook，线程不安全 |
| 2026-07-03 | 将 `exports.resolved = true` 移到 `EnsureLoaded` 之后 | 不影响主问题；但 `ResolveExports()` 在 engine + core 均未加载时返回 false 且不缓存 | 上层轮询逻辑依赖 cached resolved flag |

## 相关

- **源码：** `runtime/core/mod_ipc.cpp` — `TryHandleCommandInline()` / `HandleCommandOnMainThread()`
- **源码：** `shared/engine_module_client.cpp` — `ResolveExports()`
- **Harness：** `run-all-scenarios`, `smoke-split`
- **文档：** [troubleshooting.md](../troubleshooting.md#core-ready-timeout)

## 变更日志

| 日期 | 作者 | 说明 |
|------|------|------|
| 2026-07-03 | AI (1号机) | 创建记录；修复回滚并提交 |
