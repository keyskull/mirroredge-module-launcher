#include "mod_registry_internal.h"

#include "engine_module_client.h"
#include "host_api.h"
#include "menu.h"
#include "mod_console.h"
#include "presentation.h"
#include "memory_fault_log.h"
#include "runtime_status_json.h"
#include "runtime_version.h"
#include "version.h"
#include "mod_host_api.h"
#include "mod_security.h"
#include "module_contract.h"

#include <Windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace ModRegistryInternal {
void AppendJsonEscaped(std::string &out, const char *value) {
    out.push_back('"');
    if (!value) {
        out.push_back('"');
        return;
    }
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value);
         *p; ++p) {
        const unsigned char c = *p;
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    out.push_back('"');
}

std::string WideToUtf8(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr,
                                           0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &out[0], needed, nullptr,
                        nullptr);
    return out;
}

void FormatModuleList(std::string &out) {
    out.clear();
    std::vector<ModuleEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        snapshot = g_modules;
    }

    if (snapshot.empty()) {
        out = "No injectable modules found under modules\\.";
        return;
    }

    char line[512] = {};
    for (const auto &entry : snapshot) {
        if (ModSecurity::IsBuiltinModuleId(entry.id)) {
            continue;
        }
        snprintf(line, sizeof(line), "%ls  [%s]  %ls", entry.id.c_str(),
                 entry.status.c_str(), entry.dllPath.c_str());
        if (!out.empty()) {
            out.push_back('\n');
        }
        out += line;
    }
}

void FormatStatusList(std::string &out) {
    out = "module_manager v";
    out += MMOD_MANAGER_VERSION_STRING;
    out += "\noverlay hooks ";
    out += Presentation::AreHooksInstalled() ? "installed" : "pending";
    out += "\noverlay ready: ";
    out += Presentation::IsOverlayReady() ? "yes" : "no";

    std::string engineJson;
    if (EngineModuleClient::TryFormatStatusJson(engineJson)) {
        out += "\nengine: ";
        out += engineJson;
    } else {
        out += "\nengine: not loaded";
    }

    if (const HMODULE core = GetModuleHandleW(L"core.dll")) {
        const auto getVersion = reinterpret_cast<MMOD_GetRuntimeVersionFn>(
            GetProcAddress(core, MMOD_RUNTIME_VERSION_EXPORT));
        if (getVersion && getVersion()->string) {
            out += "\ncore v";
            out += getVersion()->string;
        }
    } else {
        out += "\ncore: not loaded";
    }

    size_t loaded = 0;
    size_t total = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        total = g_modules.size();
        for (const auto &entry : g_modules) {
            if (entry.loaded) {
                ++loaded;
            }
        }
    }

    char counts[96] = {};
    snprintf(counts, sizeof(counts), "\nmodules loaded: %zu / %zu", loaded,
             total);
    out += counts;
}

void FormatStatusJson(std::string &out) {
    out.clear();
    out.reserve(1024);
    out += "{\"component\":\"module_manager\"";
    out += ",\"version\":\"";
    out += MMOD_MANAGER_VERSION_STRING;
    out += "\"";

    char productVersion[32] = {};
    if (GetEnvironmentVariableA(MMOD_PRODUCT_VERSION_ENV, productVersion,
                                static_cast<DWORD>(sizeof(productVersion))) > 0 &&
        productVersion[0]) {
        out += ",\"productVersion\":";
        AppendJsonEscaped(out, productVersion);
    }

    out += ",\"hooksInstalled\":";
    out += Presentation::AreHooksInstalled() ? "true" : "false";
    out += ",\"overlayReady\":";
    out += Presentation::IsOverlayReady() ? "true" : "false";
    out += ",\"endSceneCalls\":";
    out += std::to_string(Presentation::GetEndSceneCallCount());
    out += ",\"presentCalls\":";
    out += std::to_string(Presentation::GetPresentCallCount());
    out += ",\"menuOpen\":";
    out += HostMenu::IsOpen() ? "true" : "false";
    out += ",\"consoleOpen\":";
    out += ModConsole::IsOpen() ? "true" : "false";
    out += ",\"activeTab\":";
    if (HostMenu::IsOpen()) {
        AppendJsonEscaped(out, HostMenu::GetActiveTabName());
    } else {
        out += "null";
    }
    out += ",\"tabs\":[";
    {
        std::vector<std::string> tabNames;
        HostMenu::GetTabNames(tabNames);
        for (size_t i = 0; i < tabNames.size(); ++i) {
            if (i > 0) {
                out.push_back(',');
            }
            AppendJsonEscaped(out, tabNames[i].c_str());
        }
    }
    out += "]";
    out += ",\"wantsOverlay\":";
    out += HostApi_WantsOverlay() ? "true" : "false";
    {
        const HWND focusHwnd = Presentation::GetLayoutTargetWindow();
        const HWND titleHwnd = Presentation::GetGameWindow();
        out += ",\"focusHwnd\":";
        out += std::to_string(reinterpret_cast<uintptr_t>(focusHwnd));
        out += ",\"gameHwnd\":";
        out += std::to_string(reinterpret_cast<uintptr_t>(titleHwnd));
    }
    EngineModuleClient::TryAppendBorderlessStatusJson(out);

    std::vector<ModuleEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        snapshot = g_modules;
    }

    size_t loaded = 0;
    for (const auto &entry : snapshot) {
        if (entry.loaded) {
            ++loaded;
        }
    }

    char counts[96] = {};
    snprintf(counts, sizeof(counts), ",\"modulesTotal\":%zu,\"modulesLoaded\":%zu",
             snapshot.size(), loaded);
    out += counts;
    out += ",\"modules\":[";

    for (size_t i = 0; i < snapshot.size(); ++i) {
        const auto &entry = snapshot[i];
        if (i > 0) {
            out.push_back(',');
        }
        out += "{\"id\":";
        AppendJsonEscaped(out, WideToUtf8(entry.id).c_str());
        out += ",\"loaded\":";
        out += entry.loaded ? "true" : "false";
        out += ",\"busy\":";
        out += entry.busy ? "true" : "false";
        out += ",\"status\":";
        AppendJsonEscaped(out, entry.status.c_str());
        out += "}";
    }

    out += "]";
    AppendJsonEngineStatus(out);
    MemoryFaultLog::AppendJson(out);
    out += "}";
}
} // namespace ModRegistryInternal

