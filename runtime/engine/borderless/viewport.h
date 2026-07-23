#pragma once

namespace EngineBorderlessSync {

bool TrySyncViewportResolution(int width, int height);
bool TryCompensateMouseLook(int clientWidth, int clientHeight, int renderWidth = 0,
                            int renderHeight = 0);
bool QueryEngineViewportSize(int &width, int &height);
float GetLastMouseLookScale();

} // namespace EngineBorderlessSync
