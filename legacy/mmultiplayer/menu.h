#pragma once

#include <string>

typedef void (*MenuTabCallback)();

typedef struct {
    std::string Name;
    MenuTabCallback Callback;
} MenuTab;

namespace Menu {

void AddTab(const char *name, MenuTabCallback callback);
void InsertTab(int index, const char *name, MenuTabCallback callback);
void RemoveTab(const char *name);
void Hide();
void Show();
bool IsOpen();
bool WantsOverlay();
void PollToggle();
bool Initialize();
void RefreshSettings();

} // namespace Menu
