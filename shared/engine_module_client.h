#pragma once

#include "mod_host_api.h"

#include <string>

namespace EngineModuleClient {

bool TryFormatStatusJson(std::string &out);

void InstallBorderlessHost(const ModHostApi *host);
bool TryAppendBorderlessStatusJson(std::string &out);

struct BorderlessUiState {
	bool enabled = false;
	float scale = 0.5f;
	int clientWidth = 0;
	int clientHeight = 0;
	int backBufferWidth = 0;
	int backBufferHeight = 0;
};

bool QueryBorderlessUiState(BorderlessUiState &out);
void SetBorderlessEnabled(bool enabled);
void SetBorderlessScale(float scale);
void MarkBorderlessApply();

bool TrySyncViewportResolution(int width, int height);
bool TryCompensateMouseLook(int clientWidth, int clientHeight, int renderWidth = 0,
                            int renderHeight = 0);
bool QueryEngineViewportSize(int &width, int &height);
float GetLastMouseLookScale();

} // namespace EngineModuleClient
