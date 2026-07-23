#pragma once

// Launcher <-> in-game mod IPC contract.
// Included by ModuleLauncher and runtime module projects.

#define MMOD_MODULE_ID "core"

#define MMOD_DLL_FILENAME L"core.dll"
#define MMOD_ENGINE_DLL_FILENAME L"engine.dll"
#define MMOD_CONSOLE_DLL_FILENAME L"mm-console.dll"
#define MMOD_MANAGER_DLL_FILENAME L"module_manager.dll"
#define MMOD_IMGUI_DLL_FILENAME L"imgui.dll"
#define MMOD_READY_EVENT_NAME L"Local\\core_ready"
#define MMOD_MANAGER_READY_EVENT_NAME L"Local\\module_manager_ready"
#define MMOD_LOG_PIPE_NAME L"\\\\.\\pipe\\mirroredge_module_log"
#define MMOD_CONTROL_PIPE_NAME L"\\\\.\\pipe\\mirroredge_module_control"
#define MMOD_MANAGER_CONTROL_PIPE_NAME \
    L"\\\\.\\pipe\\mirroredge_module_manager_control"
#define MMOD_SETTINGS_FILENAME "core.settings"
#define MMOD_SETTINGS_JSON_FILENAME "settings.json"
#define MMOD_CORE_CONFIG_FILENAME "core.config.json"

// Legacy names kept for migration tooling.
#define MMOD_LEGACY_MODULE_ID "mmultiplayer"
#define MMOD_LEGACY_CORE_MODULE_ID "mm-core"
#define MMOD_LEGACY_DLL_FILENAME L"mmultiplayer.dll"
#define MMOD_LEGACY_CORE_DLL_FILENAME L"mm-core.dll"
#define MMOD_LEGACY_ENGINE_DLL_FILENAME L"mm-engine.dll"
#define MMOD_LEGACY_READY_EVENT_NAME L"Local\\mmultiplayer_ready"
#define MMOD_LEGACY_CORE_READY_EVENT_NAME L"Local\\mm-core_ready"
#define MMOD_LEGACY_SETTINGS_FILENAME "mmultiplayer.settings"
#define MMOD_LEGACY_CORE_SETTINGS_FILENAME "mm-core.settings"

// Agent debug harness (NDJSON log + harness scripts).
#define MMOD_DEBUG_SESSION_ENV "MMOD_DEBUG_SESSION"
#define MMOD_DEBUG_LOG_ENV "MMOD_DEBUG_LOG"
#define MMOD_DEBUG_DIR_NAME L"mirroredge-debug"

// Cross-environment diagnostics (see docs/diagnostics-logging.md).
#define MMOD_DIAGNOSTICS_ENV "MMOD_DIAGNOSTICS"
#define MMOD_SESSION_LOG_ENV "MMOD_SESSION_LOG"
#define MMOD_GAME_ROOT_ENV "MMOD_GAME_ROOT"
#define MMOD_LOGS_DIR_NAME L"logs"
#define MMOD_PRODUCT_VERSION_ENV "MMOD_PRODUCT_VERSION"

// Minimum game process lifetime before the launcher injects (avoids loading-screen crashes).
#define MMOD_INJECT_MIN_UPTIME_MS 25000U

// Relative to game root (parent of Binaries).
#define MMOD_DEPLOY_SUBDIR L"modules\\core"
#define MMOD_ENGINE_DEPLOY_SUBDIR L"modules\\engine"
#define MMOD_CONSOLE_DEPLOY_SUBDIR L"modules\\mm-console"
#define MMOD_MANAGER_DEPLOY_SUBDIR L"modules\\module_manager"
#define MMOD_IMGUI_DEPLOY_SUBDIR L"modules\\imgui"
#define MMOD_CONFIG_DEPLOY_SUBDIR L"modules"

// Relative to Binaries\ (d3d9 proxy loads mod from here).
#define MMOD_PROXY_LOAD_PATH L"\\..\\modules\\core\\core.dll"
#define MMOD_MANAGER_PROXY_LOAD_PATH L"\\..\\modules\\module_manager\\module_manager.dll"
#define MMOD_IMGUI_PROXY_LOAD_PATH L"\\..\\modules\\imgui\\imgui.dll"
