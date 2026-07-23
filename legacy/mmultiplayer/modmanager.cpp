#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "addon_safe.h"
#include "agent_log.h"
#include "addons/client.h"
#include "engine.h"
#include "plugin_ui.h"
#include "ui_harness_plugin.h"
#include "menu.h"
#include "modhost.h"
#include "modmanager.h"
#include "mod_log.h"
#include "settings.h"

static std::vector<Addon *> mods;
static std::unordered_map<std::string, Addon *> modsById;

void ModManager::Register(Addon *mod) {
    mods.push_back(mod);
    modsById[mod->GetId()] = mod;
}

Addon *ModManager::FindAddon(const char *id) {
    const auto it = modsById.find(id);
    return it != modsById.end() ? it->second : nullptr;
}

std::vector<ModManager::ModInfo> ModManager::ListMods() {
    std::vector<ModInfo> result;
    result.reserve(mods.size());

    for (const auto *mod : mods) {
        result.push_back({mod->GetId(), mod->GetName(), mod->GetDescription(),
                          mod->IsEnabled()});
    }

    return result;
}

bool ModManager::IsEnabled(const char *id) {
    const auto it = modsById.find(id);
    return it != modsById.end() && it->second->IsEnabled();
}

static std::mutex g_setEnabledMutex;
static std::string g_pendingSetEnabledId;
static bool g_pendingSetEnabledValue = false;
static bool g_hasPendingSetEnabled = false;

static void SetEnabledImpl(const char *id, bool enabled);

static bool SafeInstallGameplayHooks() {
    if (Engine::AreGameplayHooksInstalled()) {
        return true;
    }

    __try {
        if (ModHost::IsAttached()) {
            return Engine::TryInstallGameplayHooksSync();
        }
        return Engine::EnsureGameplayHooks();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ModLog::Writef("modmanager: gameplay hooks crashed code=0x%08lx",
                       GetExceptionCode());
        return false;
    }
}

static bool SafeCallAddonEnable(Addon *mod) {
    bool enabled = false;
    DWORD code = 0;
    if (!AddonSafe::TryEnable(mod, &enabled, &code)) {
        if (code != 0) {
            ModLog::Writef("modmanager: %s Enable crashed code=0x%08lx",
                           mod->GetId().c_str(), code);
        }
        return false;
    }
    return enabled;
}

static void SafeCallAddonDisable(Addon *mod) {
    DWORD code = 0;
    if (!AddonSafe::TryDisable(mod, &code) && code != 0) {
        ModLog::Writef("modmanager: %s Disable crashed code=0x%08lx",
                       mod->GetId().c_str(), code);
    }
}

static void PumpSetEnabledTask() {
    std::string id;
    bool enabled = false;
    {
        std::lock_guard<std::mutex> lock(g_setEnabledMutex);
        if (!g_hasPendingSetEnabled) {
            return;
        }
        id = g_pendingSetEnabledId;
        enabled = g_pendingSetEnabledValue;
        g_hasPendingSetEnabled = false;
    }

    SetEnabledImpl(id.c_str(), enabled);
}

static void SetEnabledImpl(const char *id, bool enabled) {
    const auto it = modsById.find(id);
    if (it == modsById.end()) {
        return;
    }

    auto *mod = it->second;

    if (enabled && !mod->IsEnabled()) {
        // #region agent log
        MpDebugLog("modmanager.cpp:SetEnabled", "ensure_hooks", "H-H7");
        // #endregion
        const bool hooksOk = SafeInstallGameplayHooks();
        if (!hooksOk) {
            // #region agent log
            MpDebugLog("modmanager.cpp:SetEnabled", "hooks_fail", "H-H7");
            // #endregion
            return;
        }
        // #region agent log
        MpDebugLog("modmanager.cpp:SetEnabled", "hooks_ok", "H-H7");
        // #endregion

        if (!SafeCallAddonEnable(mod)) {
            // #region agent log
            MpDebugLog("modmanager.cpp:SetEnabled", "enable_fail", "H-H7");
            // #endregion
            return;
        }
        // #region agent log
        MpDebugLog("modmanager.cpp:SetEnabled", "enable_ok", "H-H7");
        // #endregion
    } else if (!enabled && mod->IsEnabled()) {
        SafeCallAddonDisable(mod);
    }

    Settings::SetSetting("mods", id, enabled);
}

void ModManager::ApplyEnabledChange(const char *id, bool enabled) {
    SetEnabledImpl(id, enabled);
}

void ModManager::SetEnabled(const char *id, bool enabled) {
    if (ModHost::IsAttached()) {
        {
            std::lock_guard<std::mutex> lock(g_setEnabledMutex);
            g_pendingSetEnabledId = id;
            g_pendingSetEnabledValue = enabled;
            g_hasPendingSetEnabled = true;
        }
        Engine::QueueMainThreadTask(PumpSetEnabledTask);
        return;
    }

    SetEnabledImpl(id, enabled);
}

int ModManager::GetEnabledCount() {
    auto count = 0;
    for (const auto *mod : mods) {
        if (mod->IsEnabled()) {
            ++count;
        }
    }

    return count;
}

void ModManager::RenderTab() {
    // #region agent log
    MpDebugLog("modmanager.cpp:RenderTab", "tab_enter", "H-T");
    // #endregion

    HarnessUi::BeginFrame();

    ImGui::TextWrapped(
        "Enable mods to load their tabs and in-game features.");
    // #region agent log
    MpDebugLog("modmanager.cpp:RenderTab", "after_text", "H-IMGUI");
    // #endregion
    ImGui::Text("Active mods: %d / %d", GetEnabledCount(), mods.size());
    ImGui::Separator();

    for (auto *mod : mods) {
        ImGui::PushID(mod->GetId().c_str());

        auto enabled = mod->IsEnabled();
        const bool checkboxClicked =
            ImGui::Checkbox(mod->GetName().c_str(), &enabled);
        {
            const std::string targetId =
                std::string("mm/multiplayer/mod/") + mod->GetId();
            HarnessUi::Record(targetId.c_str(), Engine::GetWindow());
        }
        ImVec2 rectMin{};
        ImVec2 rectMax{};
        ImVec2 mouse{};
        const bool hasRect =
            ImGui::GetItemRectMin(&rectMin) && ImGui::GetItemRectMax(&rectMax);
        const bool hasMouse = ImGui::GetMousePos(&mouse);
        const bool inRect = hasRect && hasMouse && mouse.x >= rectMin.x &&
                            mouse.x <= rectMax.x && mouse.y >= rectMin.y &&
                            mouse.y <= rectMax.y;
        const bool hovered = ImGui::IsItemHovered();
        const bool releaseClick =
            ImGui::IsMouseReleased(0) && (hovered || inRect);
        if (checkboxClicked) {
            SetEnabled(mod->GetId().c_str(), enabled);
        } else if (releaseClick) {
            SetEnabled(mod->GetId().c_str(), !mod->IsEnabled());
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", mod->GetDescription().c_str());
        }

        ImGui::Indent();
        ImGui::TextDisabled("%s", mod->GetDescription().c_str());
        ImGui::Text("State: %s", mod->IsEnabled() ? "Loaded" : "Unloaded");

        ImGui::Unindent();
        ImGui::Spacing();

        ImGui::PopID();
    }
}

bool ModManager::RegisterMenuTab() {
    Menu::InsertTab(0, "Modules", RenderTab);
    return true;
}

bool ModManager::EnableSavedMods() {
    for (auto *mod : mods) {
        const auto enabled = Settings::GetSetting("mods", mod->GetId().c_str(),
                                                  mod->IsEnabledByDefault())
                                 .get<bool>();

        if (enabled) {
            if (ModHost::IsAttached()) {
                SetEnabledImpl(mod->GetId().c_str(), true);
            } else {
                SetEnabled(mod->GetId().c_str(), true);
            }
        }
    }

    return true;
}

bool ModManager::Initialize() {
	if (ModHost::IsAttached()) {
		Menu::InsertTab(1, "mmultiplayer Mods", RenderTab);
	} else if (!RegisterMenuTab()) {
		return false;
	}

	return true;
}

Client *ModManager::GetClient() {
    return dynamic_cast<Client *>(FindAddon("multiplayer"));
}
