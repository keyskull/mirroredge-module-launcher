#include "mod_registry_internal.h"

#include "game_paths.h"
#include "mod_log.h"
#include "mod_security.h"
#include "mod_plugin_info.h"
#include "module_contract.h"

#include <Shlwapi.h>
#include <Windows.h>

#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

namespace ModRegistryInternal {
bool PickModuleDll(const std::wstring &folder, const std::wstring &moduleId,
                   std::wstring &dllPath) {
    const auto preferred = folder + L"\\" + moduleId + L".dll";
    if (PathFileExistsW(preferred.c_str())) {
        dllPath = preferred;
        return true;
    }

    const auto pattern = folder + L"\\*.dll";
    WIN32_FIND_DATAW dllData = {};
    const auto dllFind = FindFirstFileW(pattern.c_str(), &dllData);
    if (dllFind == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool found = false;
    do {
        if (dllData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (ModSecurity::IsReservedModuleDll(dllData.cFileName)) {
            continue;
        }

        dllPath = folder + L"\\" + dllData.cFileName;
        found = true;
        break;
    } while (FindNextFileW(dllFind, &dllData));

    FindClose(dllFind);
    return found;
}

bool TryReadPluginInfo(HMODULE module, ModuleEntry &entry) {
    const auto getInfo = reinterpret_cast<MMOD_GetPluginInfoFn>(
        GetProcAddress(module, MMOD_PLUGIN_INFO_EXPORT));
    if (!getInfo) {
        entry.hasPluginInfo = false;
        return false;
    }

    const ModPluginInfo *info = getInfo();
    if (!info || info->apiVersion != MMOD_PLUGIN_INFO_API_VERSION || !info->id) {
        entry.hasPluginInfo = false;
        return false;
    }

    entry.hasPluginInfo = true;
    entry.pluginMajor = info->major;
    entry.pluginMinor = info->minor;
    entry.pluginPatch = info->patch;
    entry.pluginDisplayName = info->displayName ? info->displayName : info->id;
    entry.requiresId = info->requiresId ? info->requiresId : "";
    entry.requiresMinVersion = info->requiresMinVersion;
    return true;
}

void ProbePluginInfoFromDll(ModuleEntry &entry) {
    entry.hasPluginInfo = false;
    entry.pluginMajor = entry.pluginMinor = entry.pluginPatch = 0;
    entry.pluginDisplayName.clear();
    entry.requiresId.clear();
    entry.requiresMinVersion = 0;

    if (_wcsicmp(entry.id.c_str(), L"core") == 0 ||
        _wcsicmp(entry.id.c_str(), L"mm-core") == 0 ||
        _wcsicmp(entry.id.c_str(), L"mmultiplayer") == 0 ||
        _wcsicmp(entry.id.c_str(), L"module_manager") == 0) {
        return;
    }

    const HMODULE probe = LoadLibraryW(entry.dllPath.c_str());
    if (!probe) {
        return;
    }

    TryReadPluginInfo(probe, entry);
    FreeLibrary(probe);
}

bool GetLoadedPluginVersion(const std::string &moduleId, unsigned &packedVersion) {
    std::wstring wideId(moduleId.begin(), moduleId.end());
    const auto *entry = FindEntryById(wideId);
    if (!entry || !entry->loaded || !entry->hasPluginInfo) {
        return false;
    }

    packedVersion = MMOD_PACK_VERSION(entry->pluginMajor, entry->pluginMinor,
                                      entry->pluginPatch);
    return true;
}

std::string ValidatePluginDependenciesLocked(const ModuleEntry &entry) {
    if (entry.requiresId.empty()) {
        return {};
    }

    std::wstring requireWide(entry.requiresId.begin(), entry.requiresId.end());
    const auto *dep = FindEntryById(requireWide);
    if (!dep || !dep->loaded) {
        return "Requires " + entry.requiresId + " (inject core first)";
    }

    if (entry.requiresMinVersion == 0) {
        return {};
    }

    if (!dep->hasPluginInfo) {
        return "Requires " + entry.requiresId + " plugin info";
    }

    const unsigned loadedVersion =
        MMOD_PACK_VERSION(dep->pluginMajor, dep->pluginMinor, dep->pluginPatch);
    if (loadedVersion < entry.requiresMinVersion) {
        char buf[128] = {};
        snprintf(buf, sizeof(buf), "Requires %s >= %u.%u.%u",
                 entry.requiresId.c_str(),
                 MMOD_UNPACK_VERSION_MAJOR(entry.requiresMinVersion),
                 MMOD_UNPACK_VERSION_MINOR(entry.requiresMinVersion),
                 MMOD_UNPACK_VERSION_PATCH(entry.requiresMinVersion));
        return buf;
    }

    return {};
}

void EnsureModuleEntryLocked(const std::wstring &moduleId) {
    if (FindEntryById(moduleId)) {
        return;
    }

    if (_wcsicmp(moduleId.c_str(), L"core") != 0) {
        return;
    }

    std::wstring modulesDir;
    if (!GamePaths::GetModulesDirectory(modulesDir)) {
        return;
    }

    const auto dllPath = modulesDir + L"\\core\\core.dll";
    if (!PathFileExistsW(dllPath.c_str())) {
        ModLog::Write("mod_registry: core.dll missing; cannot ensure core entry");
        return;
    }

    ModuleEntry entry;
    entry.id = L"core";
    entry.dllPath = dllPath;
    entry.status = "Not loaded";
    g_modules.insert(g_modules.begin(), entry);
}

void DiscoverModulesLocked() {
    const std::vector<ModuleEntry> previous = g_modules;
    g_modules.clear();

    std::wstring modulesDir;
    if (!GamePaths::GetModulesDirectory(modulesDir)) {
        return;
    }

    const auto pattern = modulesDir + L"\\*";
    WIN32_FIND_DATAW findData = {};
    const auto find = FindFirstFileW(pattern.c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            wcscmp(findData.cFileName, L".") == 0 ||
            wcscmp(findData.cFileName, L"..") == 0) {
            continue;
        }

        if (ModSecurity::IsReservedModuleFolder(findData.cFileName)) {
            continue;
        }

        if (!ModSecurity::IsValidModuleId(findData.cFileName)) {
            continue;
        }

        const auto folder = modulesDir + L"\\" + findData.cFileName;
        std::wstring dllPath;
        if (!PickModuleDll(folder, findData.cFileName, dllPath)) {
            continue;
        }

        std::wstring moduleId;
        std::string rejectReason;
        if (!ModSecurity::ValidateModuleDllPath(modulesDir, dllPath, moduleId,
                                                rejectReason)) {
            ModLog::Writef("mod_registry: skipped %ls (%s)", dllPath.c_str(),
                           rejectReason.c_str());
            continue;
        }

        ModuleEntry entry;
        entry.id = moduleId;
        entry.dllPath = dllPath;
        entry.status = "Not loaded";
        ProbePluginInfoFromDll(entry);

        for (const auto &prev : previous) {
            if (_wcsicmp(prev.id.c_str(), entry.id.c_str()) == 0) {
                if (prev.loaded && prev.module) {
                    entry.module = prev.module;
                    entry.loaded = prev.loaded;
                    entry.status = prev.status;
                }
                entry.busy = prev.busy;
                entry.loadPhase = prev.loadPhase;
                entry.pendingInit = prev.pendingInit;
                break;
            }
        }

        g_modules.push_back(entry);
    } while (FindNextFileW(find, &findData));

    FindClose(find);
    ModLog::Writef("mod_registry: discovered %zu module(s)", g_modules.size());
}
} // namespace ModRegistryInternal

