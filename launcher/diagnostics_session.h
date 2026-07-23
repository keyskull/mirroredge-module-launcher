#pragma once

#include <string>

namespace DiagnosticsSession {

// When diagnostics are enabled (env or settings.json), sets process env for the
// game child: MMOD_DEBUG_SESSION, MMOD_DEBUG_LOG, MMOD_SESSION_LOG, MMOD_GAME_ROOT.
// Writes <gameRoot>/logs/last-session.json. Returns true when env was configured.
bool PrepareForGameLaunch(const std::wstring &gameRoot);

bool IsEnabledForLaunch(const std::wstring &gameRoot);

} // namespace DiagnosticsSession
