#include "stdafx.h"

#include "config.h"
#include "game_path.h"
#include "paths.h"

namespace Paths {

bool GetLauncherDirectory(std::wstring &directory) {
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
		return false;
	}

	PathRemoveFileSpecW(path);
	directory = path;
	return true;
}

static bool HasGameExecutable(const std::wstring &directory) {
	const auto &config = LauncherConfig::Get();
	const auto candidate = directory + L"\\" + config.gameExecutable;
	return PathFileExistsW(candidate.c_str()) != FALSE;
}

static bool TryBinariesUnderRoot(const std::wstring &root, std::wstring &directory) {
	const auto &config = LauncherConfig::Get();
	const auto binariesDirectory = root + L"\\" + config.gameBinariesSubdir;
	if (HasGameExecutable(binariesDirectory)) {
		directory = binariesDirectory;
		return true;
	}
	if (HasGameExecutable(root)) {
		directory = root;
		return true;
	}
	return false;
}

static bool GetGameBinariesDirectoryFromLauncher(std::wstring &directory) {
	std::wstring launcherDirectory;
	if (!GetLauncherDirectory(launcherDirectory)) {
		return false;
	}

	if (TryBinariesUnderRoot(launcherDirectory, directory)) {
		return true;
	}

	std::wstring probe = launcherDirectory;
	for (auto depth = 0; depth < 6; ++depth) {
		wchar_t parentPath[MAX_PATH] = {};
		wcscpy(parentPath, probe.c_str());
		if (!PathRemoveFileSpecW(parentPath)) {
			break;
		}

		probe = parentPath;
		if (TryBinariesUnderRoot(probe, directory)) {
			return true;
		}
	}

	return false;
}

bool GetGameBinariesDirectory(std::wstring &directory) {
	std::wstring sourceLabel;
	if (GamePath::ResolveBinariesDirectory(directory, &sourceLabel)) {
		return true;
	}

	return GetGameBinariesDirectoryFromLauncher(directory);
}

bool GetGameRootDirectory(std::wstring &directory) {
	std::wstring binariesDirectory;
	if (!GetGameBinariesDirectory(binariesDirectory)) {
		return false;
	}

	wchar_t path[MAX_PATH] = {};
	wcscpy(path, binariesDirectory.c_str());
	if (!PathRemoveFileSpecW(path)) {
		return false;
	}

	directory = path;
	return true;
}

bool ResolveGameExecutable(std::wstring &path) {
	std::wstring gameDirectory;
	if (!GetGameBinariesDirectory(gameDirectory)) {
		return false;
	}

	const auto &config = LauncherConfig::Get();
	path = gameDirectory + L"\\" + config.gameExecutable;
	return PathFileExistsW(path.c_str()) != FALSE;
}

bool ResolveDll(const std::vector<std::wstring> &dllNames,
                const std::vector<std::wstring> &searchSubdirs,
                std::wstring &path) {
	std::wstring launcherDirectory;
	if (!GetLauncherDirectory(launcherDirectory)) {
		return false;
	}

	std::wstring gameDirectory;
	const auto hasGameDirectory = GetGameBinariesDirectory(gameDirectory);

	std::vector<std::wstring> searchRoots = {launcherDirectory};
	if (hasGameDirectory) {
		if (_wcsicmp(gameDirectory.c_str(), launcherDirectory.c_str()) != 0) {
			searchRoots.push_back(gameDirectory);
		}

		std::wstring gameRootDirectory;
		if (GetGameRootDirectory(gameRootDirectory) &&
		    _wcsicmp(gameRootDirectory.c_str(), launcherDirectory.c_str()) != 0 &&
		    _wcsicmp(gameRootDirectory.c_str(), gameDirectory.c_str()) != 0) {
			searchRoots.push_back(gameRootDirectory);
		}
	}

	for (const auto &root : searchRoots) {
		for (const auto &subdirectory : searchSubdirs) {
			for (const auto &dllName : dllNames) {
				const auto candidate =
				    subdirectory == L"."
				        ? root + L"\\" + dllName
				        : root + L"\\" + subdirectory + L"\\" + dllName;
				if (PathFileExistsW(candidate.c_str())) {
					path = candidate;
					return true;
				}
			}
		}

		for (const auto &dllName : dllNames) {
			const auto candidate = root + L"\\" + dllName;
			if (PathFileExistsW(candidate.c_str())) {
				path = candidate;
				return true;
			}
		}
	}

	return false;
}

bool ResolveManagerDll(std::wstring &path) {
	const auto &config = LauncherConfig::Get();
	return ResolveDll(config.managerDllNames, config.managerSearchSubdirs, path);
}

bool ResolveGraphicsProxyDll(std::wstring &path) {
	std::wstring launcherDirectory;
	if (!GetLauncherDirectory(launcherDirectory)) {
		return false;
	}

	const auto &config = LauncherConfig::Get();
	const std::wstring dllName = config.graphicsProxyDllName;

	const std::vector<std::wstring> candidates = {
	    launcherDirectory + L"\\dist\\" + dllName,
	    launcherDirectory + L"\\" + dllName,
	};

	for (const auto &candidate : candidates) {
		if (PathFileExistsW(candidate.c_str())) {
			path = candidate;
			return true;
		}
	}

	return false;
}

} // namespace Paths
