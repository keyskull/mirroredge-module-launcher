#pragma once

#include <string>
#include <vector>

namespace Paths {
bool GetLauncherDirectory(std::wstring &directory);
bool GetGameBinariesDirectory(std::wstring &directory);
bool GetGameRootDirectory(std::wstring &directory);
bool ResolveGameExecutable(std::wstring &path);
bool ResolveManagerDll(std::wstring &path);
bool ResolveGraphicsProxyDll(std::wstring &path);
bool ResolveDll(const std::vector<std::wstring> &dllNames,
                const std::vector<std::wstring> &searchSubdirs,
                std::wstring &path);
} // namespace Paths
