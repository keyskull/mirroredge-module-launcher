#include <Windows.h>

#include <Shlwapi.h>

#include "game_paths.h"

#pragma comment(lib, "Shlwapi.lib")

namespace GamePaths {

bool GetGameRootDirectory(std::wstring &directory) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(GetModuleHandleW(L"MirrorsEdge.exe"), path,
                            MAX_PATH)) {
        return false;
    }

    PathRemoveFileSpecW(path);
    directory = path;

    const auto leaf = PathFindFileNameW(directory.c_str());
    if (leaf && _wcsicmp(leaf, L"Binaries") == 0) {
        wchar_t root[MAX_PATH] = {};
        wcscpy(root, directory.c_str());
        PathRemoveFileSpecW(root);
        directory = root;
    }

    return true;
}

bool GetModulesDirectory(std::wstring &directory) {
    if (!GetGameRootDirectory(directory)) {
        return false;
    }

    directory += L"\\modules";
  return true;
}

} // namespace GamePaths
