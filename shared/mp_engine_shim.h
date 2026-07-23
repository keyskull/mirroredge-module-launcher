#pragma once

#include "mmultiplayer_api.h"

namespace MpEngineShim {

extern const MmultiplayerApi *g_api;

inline void Bind(const MmultiplayerApi *api) { g_api = api; }

inline const MmultiplayerApi *Get() { return g_api; }

} // namespace MpEngineShim
