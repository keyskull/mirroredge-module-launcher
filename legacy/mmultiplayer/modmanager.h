#pragma once

#include "addon.h"
#include "addons/client.h"

#include <string>
#include <vector>

namespace ModManager {

struct ModInfo {
    std::string id;
    std::string name;
    std::string description;
    bool enabled = false;
};

void Register(Addon *mod);
Addon *FindAddon(const char *id);
Client *GetClient();
bool EnableSavedMods();
bool Initialize();
bool IsEnabled(const char *id);
void SetEnabled(const char *id, bool enabled);
// Synchronous enable/disable on the game main thread (hosted IPC path).
void ApplyEnabledChange(const char *id, bool enabled);
int GetEnabledCount();
void RenderTab();
bool RegisterMenuTab();
std::vector<ModInfo> ListMods();

} // namespace ModManager
