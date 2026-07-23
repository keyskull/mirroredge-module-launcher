#pragma once

#include <string>

namespace GamePath {

// Game root = folder containing Binaries\MirrorsEdge.exe (or MirrorsEdge.exe at root).
bool ValidateGameRoot(const std::wstring &gameRoot, std::wstring *binariesDirectory);
bool ValidateBinariesDirectory(const std::wstring &binariesDirectory);

bool LoadSavedGameRoot(std::wstring &gameRoot);
bool SaveGameRoot(const std::wstring &gameRoot);
void ClearSavedGameRoot();

// Priority: saved override -> env -> launcher-relative -> Steam/EA registry -> common paths.
bool AutoDetectGameRoot(std::wstring &gameRoot, std::wstring *sourceLabel);
bool ResolveGameRoot(std::wstring &gameRoot, std::wstring *sourceLabel);

bool ResolveBinariesDirectory(std::wstring &binariesDirectory,
                              std::wstring *sourceLabel);

bool HasSecuRomProtection(const std::wstring &gameExecutablePath);

// Folder picker; accepts game root or Binaries folder. Returns false if cancelled.
bool BrowseForGameRoot(HWND owner, std::wstring &gameRoot);

} // namespace GamePath
