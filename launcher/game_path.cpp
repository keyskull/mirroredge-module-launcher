#include "stdafx.h"

#include "config.h"
#include "game_path.h"
#include "launcher_settings.h"

#include <fstream>
#include <objbase.h>
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr auto kSteamAppId = L"17410";
constexpr size_t kSecuRomScanBytes = 8 * 1024 * 1024;

std::string WideToUtf8(const std::wstring &text) {
	if (text.empty()) {
		return {};
	}

	const auto size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
	                                      static_cast<int>(text.size()), nullptr,
	                                      0, nullptr, nullptr);
	if (size <= 0) {
		return {};
	}

	std::string out(static_cast<size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    out.empty() ? nullptr : &out[0], size, nullptr, nullptr);
	return out;
}

std::wstring Utf8ToWide(const std::string &text) {
	if (text.empty()) {
		return {};
	}

	const auto size =
	    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                        nullptr, 0);
	if (size <= 0) {
		return {};
	}

	std::wstring out(static_cast<size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
	                    out.empty() ? nullptr : &out[0], size);
	return out;
}

std::wstring GetSettingsFilePath() { return LauncherSettings::GetSettingsFilePath(); }

bool ReadRegistryString(HKEY root, const wchar_t *subKey, const wchar_t *valueName,
                        std::wstring &out) {
	DWORD type = 0;
	DWORD size = 0;
	const auto querySize =
	    RegQueryValueExW(root, valueName, nullptr, &type, nullptr, &size);
	if (querySize != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
	    size < sizeof(wchar_t)) {
		return false;
	}

	std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
	if (RegQueryValueExW(root, valueName, nullptr, &type,
	                     reinterpret_cast<LPBYTE>(buffer.data()),
	                     &size) != ERROR_SUCCESS) {
		return false;
	}

	out = buffer.data();
	while (!out.empty() && (out.back() == L'\0' || out.back() == L' ')) {
		out.pop_back();
	}
	return !out.empty();
}

bool TryGameRootCandidate(const std::wstring &candidate,
                          std::wstring &gameRoot) {
	std::wstring binariesDirectory;
	if (!GamePath::ValidateGameRoot(candidate, &binariesDirectory)) {
		return false;
	}

	gameRoot = candidate;
	return true;
}

void AppendUniqueCandidate(std::vector<std::wstring> &candidates,
                           const std::wstring &candidate) {
	if (candidate.empty()) {
		return;
	}

	for (const auto &existing : candidates) {
		if (_wcsicmp(existing.c_str(), candidate.c_str()) == 0) {
			return;
		}
	}

	candidates.push_back(candidate);
}

void AppendSteamLibraryPaths(std::vector<std::wstring> &candidates) {
	std::wstring steamPath;
	if (!ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam",
	                        L"SteamPath", steamPath)) {
		return;
	}

	AppendUniqueCandidate(candidates, steamPath);

	const auto libraryFolders =
	    steamPath + L"\\steamapps\\libraryfolders.vdf";
	std::wifstream file(libraryFolders);
	if (!file) {
		return;
	}

	std::wstring line;
	while (std::getline(file, line)) {
		const auto pathPos = line.find(L"path");
		if (pathPos == std::wstring::npos) {
			continue;
		}

		auto quoteStart = line.find(L'"', pathPos + 4);
		if (quoteStart == std::wstring::npos) {
			continue;
		}
		++quoteStart;

		const auto quoteEnd = line.find(L'"', quoteStart);
		if (quoteEnd == std::wstring::npos || quoteEnd <= quoteStart) {
			continue;
		}

		auto libraryPath = line.substr(quoteStart, quoteEnd - quoteStart);
		for (auto &ch : libraryPath) {
			if (ch == L'/') {
				ch = L'\\';
			}
		}
		AppendUniqueCandidate(candidates, libraryPath);
	}
}

void AppendCommonInstallCandidates(std::vector<std::wstring> &candidates) {
	wchar_t programFiles[MAX_PATH] = {};
	wchar_t programFilesX86[MAX_PATH] = {};

	if (SUCCEEDED(
	        SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr,
	                         SHGFP_TYPE_CURRENT, programFiles))) {
		AppendUniqueCandidate(
		    candidates,
		    std::wstring(programFiles) + L"\\EA Games\\Mirror's Edge");
		AppendUniqueCandidate(
		    candidates,
		    std::wstring(programFiles) + L"\\Steam\\steamapps\\common\\Mirror's Edge");
	}

	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr,
	                               SHGFP_TYPE_CURRENT, programFilesX86))) {
		AppendUniqueCandidate(
		    candidates,
		    std::wstring(programFilesX86) + L"\\EA Games\\Mirror's Edge");
		AppendUniqueCandidate(
		    candidates,
		    std::wstring(programFilesX86) +
		        L"\\Origin Games\\Mirror's Edge");
		AppendUniqueCandidate(
		    candidates,
		    std::wstring(programFilesX86) +
		        L"\\Steam\\steamapps\\common\\Mirror's Edge");
	}

	wchar_t documents[MAX_PATH] = {};
	if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
	                               SHGFP_TYPE_CURRENT, documents))) {
		wchar_t parent[MAX_PATH] = {};
		wcscpy(parent, documents);
		if (PathRemoveFileSpecW(parent)) {
			AppendUniqueCandidate(
			    candidates,
			    std::wstring(parent) + L"\\EA Games\\Mirrors Edge");
			AppendUniqueCandidate(
			    candidates,
			    std::wstring(parent) + L"\\EA Games\\Mirror's Edge");
		}
	}
}

void AppendEnvCandidates(std::vector<std::wstring> &candidates) {
	wchar_t buffer[MAX_PATH] = {};

	if (GetEnvironmentVariableW(L"ME_GAME_PATH", buffer, MAX_PATH) > 0) {
		AppendUniqueCandidate(candidates, buffer);
	}
	ZeroMemory(buffer, sizeof(buffer));
	if (GetEnvironmentVariableW(L"ME_DEPLOY_PATH", buffer, MAX_PATH) > 0) {
		AppendUniqueCandidate(candidates, buffer);
	}
}

void AppendLauncherRelativeCandidate(std::vector<std::wstring> &candidates) {
	wchar_t launcherPath[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, launcherPath, MAX_PATH)) {
		return;
	}
	if (!PathRemoveFileSpecW(launcherPath)) {
		return;
	}

	const auto &config = LauncherConfig::Get();
	auto probe = std::wstring(launcherPath);
	for (auto depth = 0; depth <= 6; ++depth) {
		const auto binariesCandidate =
		    probe + L"\\" + config.gameBinariesSubdir;
		const auto exeInBinaries =
		    binariesCandidate + L"\\" + config.gameExecutable;
		if (PathFileExistsW(exeInBinaries.c_str())) {
			AppendUniqueCandidate(candidates, probe);
			return;
		}

		const auto exeAtRoot = probe + L"\\" + config.gameExecutable;
		if (PathFileExistsW(exeAtRoot.c_str())) {
			AppendUniqueCandidate(candidates, probe);
			return;
		}

		if (depth == 6) {
			break;
		}

		wchar_t parent[MAX_PATH] = {};
		wcscpy(parent, probe.c_str());
		if (!PathRemoveFileSpecW(parent)) {
			break;
		}
		probe = parent;
	}
}

void AppendSteamGameCandidates(const std::vector<std::wstring> &libraries,
                               std::vector<std::wstring> &candidates) {
	static const wchar_t *kFolderNames[] = {L"Mirror's Edge", L"Mirrors Edge",
	                                        L"mirrors edge"};

	for (const auto &library : libraries) {
		for (const auto *folderName : kFolderNames) {
			AppendUniqueCandidate(
			    candidates, library + L"\\steamapps\\common\\" + folderName);
		}

		const auto manifestPath = library + L"\\steamapps\\appmanifest_" +
		                          kSteamAppId + L".acf";
		if (PathFileExistsW(manifestPath.c_str())) {
			std::wifstream manifest(manifestPath);
			std::wstring line;
			while (std::getline(manifest, line)) {
				const auto keyPos = line.find(L"installdir");
				if (keyPos == std::wstring::npos) {
					continue;
				}

				auto quoteStart = line.find(L'"', keyPos + 10);
				if (quoteStart == std::wstring::npos) {
					continue;
				}
				++quoteStart;

				const auto quoteEnd = line.find(L'"', quoteStart);
				if (quoteEnd == std::wstring::npos || quoteEnd <= quoteStart) {
					continue;
				}

				const auto installDir =
				    line.substr(quoteStart, quoteEnd - quoteStart);
				AppendUniqueCandidate(
				    candidates, library + L"\\steamapps\\common\\" + installDir);
				break;
			}
		}
	}
}

bool NormalizeToGameRoot(const std::wstring &selectedPath,
                         std::wstring &gameRoot) {
	if (TryGameRootCandidate(selectedPath, gameRoot)) {
		return true;
	}

	const auto &config = LauncherConfig::Get();
	if (_wcsicmp(PathFindFileNameW(selectedPath.c_str()),
	             config.gameBinariesSubdir.c_str()) == 0) {
		wchar_t parent[MAX_PATH] = {};
		wcscpy(parent, selectedPath.c_str());
		if (PathRemoveFileSpecW(parent) &&
		    TryGameRootCandidate(parent, gameRoot)) {
			return true;
		}
	}

	return false;
}

} // namespace

namespace GamePath {

bool ValidateBinariesDirectory(const std::wstring &binariesDirectory) {
	const auto &config = LauncherConfig::Get();
	const auto exePath =
	    binariesDirectory + L"\\" + config.gameExecutable;
	return PathFileExistsW(exePath.c_str()) != FALSE;
}

bool ValidateGameRoot(const std::wstring &gameRoot,
                      std::wstring *binariesDirectory) {
	if (gameRoot.empty()) {
		return false;
	}

	const auto &config = LauncherConfig::Get();
	const auto binariesPath = gameRoot + L"\\" + config.gameBinariesSubdir;
	if (ValidateBinariesDirectory(binariesPath)) {
		if (binariesDirectory) {
			*binariesDirectory = binariesPath;
		}
		return true;
	}

	if (ValidateBinariesDirectory(gameRoot)) {
		if (binariesDirectory) {
			*binariesDirectory = gameRoot;
		}
		return true;
	}

	return false;
}

bool LoadSavedGameRoot(std::wstring &gameRoot) {
	return LauncherSettings::LoadGameRoot(gameRoot);
}

bool SaveGameRoot(const std::wstring &gameRoot) {
	return LauncherSettings::SaveGameRoot(gameRoot);
}

void ClearSavedGameRoot() { DeleteFileW(GetSettingsFilePath().c_str()); }

bool AutoDetectGameRoot(std::wstring &gameRoot, std::wstring *sourceLabel) {
	std::vector<std::wstring> envCandidates;
	AppendEnvCandidates(envCandidates);
	for (const auto &candidate : envCandidates) {
		if (TryGameRootCandidate(candidate, gameRoot)) {
			if (sourceLabel) {
				*sourceLabel = L"env";
			}
			return true;
		}
	}

	std::vector<std::wstring> launcherCandidates;
	AppendLauncherRelativeCandidate(launcherCandidates);
	for (const auto &candidate : launcherCandidates) {
		if (TryGameRootCandidate(candidate, gameRoot)) {
			if (sourceLabel) {
				*sourceLabel = L"launcher-relative";
			}
			return true;
		}
	}

	std::vector<std::wstring> candidates;
	AppendEnvCandidates(candidates);
	AppendLauncherRelativeCandidate(candidates);

	std::vector<std::wstring> steamLibraries;
	{
		std::wstring steamPath;
		if (ReadRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam",
		                       L"SteamPath", steamPath)) {
			AppendUniqueCandidate(steamLibraries, steamPath);
		}
		AppendSteamLibraryPaths(steamLibraries);
	}
	AppendSteamGameCandidates(steamLibraries, candidates);
	AppendCommonInstallCandidates(candidates);

	for (const auto &candidate : candidates) {
		if (TryGameRootCandidate(candidate, gameRoot)) {
			if (sourceLabel) {
				*sourceLabel = L"auto";
			}
			return true;
		}
	}

	return false;
}

bool ResolveGameRoot(std::wstring &gameRoot, std::wstring *sourceLabel) {
	if (LoadSavedGameRoot(gameRoot)) {
		if (sourceLabel) {
			*sourceLabel = L"saved";
		}
		return true;
	}

	if (AutoDetectGameRoot(gameRoot, sourceLabel)) {
		(void)SaveGameRoot(gameRoot);
		return true;
	}

	return false;
}

bool ResolveBinariesDirectory(std::wstring &binariesDirectory,
                              std::wstring *sourceLabel) {
	std::wstring gameRoot;
	if (!ResolveGameRoot(gameRoot, sourceLabel)) {
		return false;
	}

	return ValidateGameRoot(gameRoot, &binariesDirectory);
}

bool HasSecuRomProtection(const std::wstring &gameExecutablePath) {
	static std::wstring cachedPath;
	static FILETIME cachedWriteTime = {};
	static bool cachedResult = false;
	static bool cachedValid = false;

	WIN32_FILE_ATTRIBUTE_DATA attributes = {};
	if (!GetFileAttributesExW(gameExecutablePath.c_str(), GetFileExInfoStandard,
	                          &attributes)) {
		return false;
	}

	if (cachedValid && _wcsicmp(cachedPath.c_str(), gameExecutablePath.c_str()) == 0 &&
	    CompareFileTime(&attributes.ftLastWriteTime, &cachedWriteTime) == 0) {
		return cachedResult;
	}

	HANDLE file =
	    CreateFileW(gameExecutablePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
	                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	const char *needle = "SecuROM";
	const size_t needleLen = 7;
	const DWORD chunkSize = 256 * 1024;
	std::vector<char> buffer(chunkSize);
	ULONGLONG offset = 0;
	bool found = false;

	while (offset < kSecuRomScanBytes) {
		const ULONGLONG remaining = kSecuRomScanBytes - offset;
		const DWORD toRead =
		    static_cast<DWORD>(remaining > chunkSize ? chunkSize : remaining);
		DWORD bytesRead = 0;
		if (!ReadFile(file, buffer.data(), toRead, &bytesRead, nullptr) ||
		    bytesRead < needleLen) {
			break;
		}

		for (DWORD i = 0; i + needleLen <= bytesRead; ++i) {
			if (memcmp(buffer.data() + i, needle, needleLen) == 0) {
				found = true;
				break;
			}
		}

		if (found) {
			break;
		}

		offset += bytesRead;
		if (bytesRead < toRead) {
			break;
		}
	}

	CloseHandle(file);

	cachedPath = gameExecutablePath;
	cachedWriteTime = attributes.ftLastWriteTime;
	cachedResult = found;
	cachedValid = true;
	return found;
}

bool BrowseForGameRoot(HWND owner, std::wstring &gameRoot) {
	const auto comInit =
	    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const auto comInitialized =
	    comInit == S_OK || comInit == S_FALSE;

	BROWSEINFOW browseInfo = {};
	wchar_t displayName[MAX_PATH] = {};

	browseInfo.hwndOwner = owner;
	browseInfo.pszDisplayName = displayName;
	browseInfo.lpszTitle =
	    L"Select Mirror's Edge folder (game root or Binaries)";
	browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

	const auto pidl = SHBrowseForFolderW(&browseInfo);
	if (!pidl) {
		if (comInitialized) {
			CoUninitialize();
		}
		return false;
	}

	wchar_t selected[MAX_PATH] = {};
	const auto gotPath = SHGetPathFromIDListW(pidl, selected);
	CoTaskMemFree(pidl);
	if (!gotPath) {
		CoTaskMemFree(pidl);
		if (comInitialized) {
			CoUninitialize();
		}
		return false;
	}

	std::wstring normalizedRoot;
	if (!NormalizeToGameRoot(selected, normalizedRoot)) {
		MessageBoxW(
		    owner,
		    L"Selected folder does not contain Binaries\\MirrorsEdge.exe.",
		    L"Mirror's Edge Module Launcher", MB_ICONWARNING | MB_OK);
		if (comInitialized) {
			CoUninitialize();
		}
		return false;
	}

	if (!SaveGameRoot(normalizedRoot)) {
		MessageBoxW(owner,
		            L"Could not save the selected game path.",
		            L"Mirror's Edge Module Launcher", MB_ICONERROR | MB_OK);
		if (comInitialized) {
			CoUninitialize();
		}
		return false;
	}

	gameRoot = normalizedRoot;
	if (comInitialized) {
		CoUninitialize();
	}
	return true;
}

} // namespace GamePath
