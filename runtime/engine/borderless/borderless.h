#pragma once

#include "mod_host_api.h"

#include <d3d9.h>
#include <string>

namespace EngineBorderless {

void InstallHost(const ModHostApi *host);
void Initialize();
void AppendStatusJson(std::string &out);

struct UiState {
	bool enabled = false;
	float scale = 0.5f;
	int clientWidth = 0;
	int clientHeight = 0;
	int backBufferWidth = 0;
	int backBufferHeight = 0;
};

bool QueryUiState(UiState &out);
void SetEnabled(bool enabled);
void SetScale(float scale);
void MarkApply();

} // namespace EngineBorderless
