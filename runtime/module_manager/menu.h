#pragma once

#include <d3d9.h>

#include <string>
#include <vector>

typedef void (*HostMenuTabCallback)();

namespace HostMenu {

void AddTab(const char *name, HostMenuTabCallback callback);
void InsertTab(int index, const char *name, HostMenuTabCallback callback);
void RemoveTab(const char *name);
void Show();
void Hide();
bool IsOpen();
void SetBlockInput(bool block);
void PollToggle();
void Render(IDirect3DDevice9 *device);
void Initialize();
void GetTabNames(std::vector<std::string> &out);
const char *GetActiveTabName();
bool SelectTab(const char *name);

} // namespace HostMenu

void HostMenu_SetBlockInput(bool block);
