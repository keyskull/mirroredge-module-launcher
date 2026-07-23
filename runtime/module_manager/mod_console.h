#pragma once

#include <d3d9.h>

#include <cstddef>

namespace ModConsole {

void Initialize();
void Show();
void Hide();
bool IsOpen();
void Toggle();
void PollToggle();
void Render(IDirect3DDevice9 *device);
void RenderRecent(size_t maxLines);
void ExecuteCommand(const char *line);

} // namespace ModConsole
