# 技术文档索引（AI 查阅）

本目录为 **mirroredge-module-launcher** 的结构化技术文档，供 AI Agent 与开发者快速理解代码库。

用户向的使用说明见根目录 [README.md](../README.md)。AI 入口见 [AGENTS.md](../AGENTS.md)。

## 文档列表

| 文档 | 内容 |
|------|------|
| [architecture.md](architecture.md) | 整体架构、目录结构、关键文件职责、命名约定 |
| [injection-and-ipc.md](injection-and-ipc.md) | 启动器代理等待流程、命名管道与事件 IPC |
| [module-manager.md](module-manager.md) | module_manager 宿主、d3d9 代理加载、导出与 overlay 要点 |
| [mmultiplayer-mod.md](mmultiplayer-mod.md) | Archived monolith internals (`legacy/mmultiplayer/`); see core/engine in architecture.md |
| [d3d9proxy.md](d3d9proxy.md) | D3D9 图形代理原理、启用方式、与 inject 模式对比 |
| [adding-a-mod.md](adding-a-mod.md) | 新增模组分步指南（vcxproj、契约、config、部署） |
| [build-deploy.md](build-deploy.md) | `build.ps1` 构建、部署路径、`deploy.config.json`、GitHub Release / 自动升级 |
| [troubleshooting.md](troubleshooting.md) | 常见问题：注入失败、崩溃、无日志、菜单不显示 |
| [known-issues-workflow.md](known-issues-workflow.md) | **已知错误登记流程**：修 bug 前检索、记录失败方案、防重复修复 |
| [known-issues/](known-issues/README.md) | 已知问题登记册（KI 条目索引 + `_template.md`） |
| [ai-debug-harness.md](ai-debug-harness.md) | **AI 自动 debug**：NDJSON 探针、harness 场景、MCP 工具 |
| [diagnostics-logging.md](diagnostics-logging.md) | **跨环境诊断日志**：`session.log`、收集脚本、非 harness 复现 |
| [sdk-verification.md](sdk-verification.md) | **SDK 验证**：纯指令扫描、布局参考、运行时 IsA 检查、可选 Ghidra 参考 |
| [me-sdk.md](me-sdk.md) | **SDK 技术文档（中文）**：类型映射、Safe 访问层、初始化流程、Pattern 扫描 |
| [test-environments.md](test-environments.md) | **多机测试环境**、deploy 配置、`test-logs/` 结果共享 with merge |
| [testing-character-spawn.md](testing-character-spawn.md) | **角色与Bot生成测试**：模型ID映射、调试和手动测试流程 |
| [mp-set-gameplay-runbook.md](mp-set-gameplay-runbook.md) | **Set Gameplay / bot 可见性（AI 必读）**：v1.2.6 关键 8 门禁、黄金日志、勿重试、测试步骤 |
| [tdpawn-remote-eval.md](tdpawn-remote-eval.md) | **Historical** TdPawn remote spike (removed 2026-07-22; mesh-only remotes) |


## 源码权威位置

修改行为时以源码为准，文档仅作导航：

| 主题 | 权威文件 |
|------|----------|
| 跨进程契约 | `shared/module_contract.h` |
| 宿主插件 API | `shared/mod_host_api.h` |
| 插件 UI 门面 | `shared/plugin_ui.h`, `shared/plugin_ui_api.h` |
| ImGui 源码 | `shared/imgui/`（静态链入 module_manager） |
| 启动器默认配置 | `launcher/config.h` |
| 注入编排 | `launcher/injection_flow.cpp` → `RunLauncherFlow` |
| 日志中继 | `launcher/log_server.cpp` |
| Module Manager 宿主 | `runtime/module_manager/presentation.cpp` — hooks, Alt+Tab, ImGui device lifecycle |
| 插件注册/安全 | `runtime/module_manager/mod_registry.cpp`, `mod_security.cpp` |
| 游戏内控制台 | `runtime/console/console.cpp` (host: `runtime/module_manager/console_host.cpp`) |
| 模组入口 | `runtime/core/main.cpp` |
| 渲染/游戏钩子 | `runtime/engine/engine.cpp`, `engine_hooks_gameplay.cpp` |
| D3D9 代理 | `launcher/proxy/d3d9/d3d9.cpp` |
| 模组控制管道（mod 侧） | `runtime/core/mod_ipc.cpp` |
| Module Manager 控制管道 | `runtime/module_manager/mod_ipc.cpp` |
| UE3 SDK (ME 1.0) | `shared/me_sdk/` — `runtime/`, `patterns/`, `util/`, `generated/`; umbrella `me_sdk/me_sdk.h` |
| SDK 验证 | `shared/me_sdk/sdk_verify.h`, `sdk_verify_generated.h`; `tools/sdk-verify/` |
| JSON (nlohmann) | `shared/json.h` |
| Agent NDJSON 日志 | `shared/debug_log.h` |
| 产品版本 | `version.json`, `CHANGELOG.md`, `shared/product_version.h` |
| 跨环境文本诊断日志 | `shared/session_file_log.h`, `docs/diagnostics-logging.md` |
| 诊断收集脚本 | `tools/collect-diagnostics.ps1` |
| Debug harness | `tools/debug-harness/` |
| Debug MCP server | `tools/mcp-debug-server/` |

## 维护约定

- 功能或流程变更时，同步更新对应 `docs/*.md`。
- 易复发或曾多次误修的问题：按 [known-issues-workflow.md](known-issues-workflow.md) 更新登记册，并在 [troubleshooting.md](troubleshooting.md) 交叉链接 KI ID。
- 单篇文档聚焦一个主题，避免与 README 重复用户操作步骤。
- 过时信息（如已删除的 `game_probe.cpp`、`Client.dll` 默认注入）不得保留。
