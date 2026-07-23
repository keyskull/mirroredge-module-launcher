#include "stdafx.h"

#include "game_patch.h"

#include "config.h"
#include "launcher_i18n.h"
#include "paths.h"
#include "runtime_version.h"
#include "update_check.h"
#include "ui/status_dialog.h"

#include <vector>

namespace GamePatch {
namespace {

constexpr wchar_t kPhysXBackupSuffix[] = L".mmphysx.bak";

struct FileMeta {
	ULONGLONG size = 0;
	FILETIME writeTime = {};
	std::wstring version;
};

bool ArePathsEquivalent(const std::wstring &left, const std::wstring &right) {
	return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool EndsWithIgnoreCase(const std::wstring &value, const wchar_t *suffix) {
	const size_t suffixLen = wcslen(suffix);
	if (value.size() < suffixLen) {
		return false;
	}
	return _wcsicmp(value.c_str() + (value.size() - suffixLen), suffix) == 0;
}

bool IsRuntimeModuleFile(const std::wstring &relativePath) {
	// Skip linker/build side-products that MSVC drops next to DLLs (.exp/.lib/.pdb).
	static const wchar_t *kSkip[] = {
	    L".exp", L".lib", L".pdb", L".ilk", L".obj", L".iobj", L".ipdb",
	    L".map", L".idb",
	};
	for (const wchar_t *ext : kSkip) {
		if (EndsWithIgnoreCase(relativePath, ext)) {
			return false;
		}
	}
	return true;
}

bool IsPeDllPath(const std::wstring &path) {
	return EndsWithIgnoreCase(path, L".dll");
}

bool GetFileMeta(const std::wstring &path, FileMeta &out) {
	WIN32_FILE_ATTRIBUTE_DATA data = {};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
		return false;
	}
	ULARGE_INTEGER size = {};
	size.LowPart = data.nFileSizeLow;
	size.HighPart = data.nFileSizeHigh;
	out.size = size.QuadPart;
	out.writeTime = data.ftLastWriteTime;
	out.version.clear();

	// Never LoadLibrary non-DLL artifacts (.exp triggers Bad Image 0xc000012f).
	if (!IsPeDllPath(path)) {
		return true;
	}

	const UINT previousErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	const HMODULE module =
	    LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
	SetErrorMode(previousErrorMode);
	if (module) {
		const auto getVersion = reinterpret_cast<MMOD_GetRuntimeVersionFn>(
		    GetProcAddress(module, MMOD_RUNTIME_VERSION_EXPORT));
		if (getVersion) {
			const MmodRuntimeVersion *info = getVersion();
			if (info && info->string) {
				const int needed =
				    MultiByteToWideChar(CP_UTF8, 0, info->string, -1, nullptr, 0);
				if (needed > 1) {
					out.version.assign(static_cast<size_t>(needed - 1), L'\0');
					MultiByteToWideChar(CP_UTF8, 0, info->string, -1, &out.version[0],
					                    needed);
				}
			}
		}
		FreeLibrary(module);
	}
	return true;
}

bool FilesMatch(const FileMeta &local, const FileMeta &game) {
	if (local.size != game.size || local.size == 0) {
		return false;
	}
	if (!local.version.empty() && !game.version.empty()) {
		return _wcsicmp(local.version.c_str(), game.version.c_str()) == 0;
	}
	return true;
}

bool BackupAndCopyFile(const std::wstring &sourcePath,
                       const std::wstring &destinationPath,
                       const std::wstring &backupPath) {
	if (PathFileExistsW(destinationPath.c_str())) {
		if (PathFileExistsW(backupPath.c_str())) {
			DeleteFileW(backupPath.c_str());
		}
		if (!MoveFileW(destinationPath.c_str(), backupPath.c_str())) {
			return false;
		}
	}
	return CopyFileW(sourcePath.c_str(), destinationPath.c_str(), FALSE) != FALSE;
}

bool EnsureDirectory(const std::wstring &directory) {
	if (directory.empty()) {
		return false;
	}
	if (PathFileExistsW(directory.c_str())) {
		return (GetFileAttributesW(directory.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}
	const size_t slash = directory.find_last_of(L"\\/");
	if (slash != std::wstring::npos && slash > 0) {
		EnsureDirectory(directory.substr(0, slash));
	}
	return CreateDirectoryW(directory.c_str(), nullptr) != FALSE ||
	       GetLastError() == ERROR_ALREADY_EXISTS;
}

void CollectRelativeFiles(const std::wstring &root, const std::wstring &relative,
                          std::vector<std::wstring> &out, bool runtimeOnly) {
	const std::wstring search = root + L"\\" + relative + L"*";
	WIN32_FIND_DATAW findData = {};
	const HANDLE find = FindFirstFileW(search.c_str(), &findData);
	if (find == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		if (wcscmp(findData.cFileName, L".") == 0 ||
		    wcscmp(findData.cFileName, L"..") == 0) {
			continue;
		}
		const std::wstring childRel = relative + findData.cFileName;
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			CollectRelativeFiles(root, childRel + L"\\", out, runtimeOnly);
			continue;
		}
		if (runtimeOnly && !IsRuntimeModuleFile(childRel)) {
			continue;
		}
		out.push_back(childRel);
	} while (FindNextFileW(find, &findData));
	FindClose(find);
}

void CollectSubdirectories(const std::wstring &root, const std::wstring &relative,
                           std::vector<std::wstring> &out) {
	const std::wstring search = root + L"\\" + relative + L"*";
	WIN32_FIND_DATAW findData = {};
	const HANDLE find = FindFirstFileW(search.c_str(), &findData);
	if (find == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		if (wcscmp(findData.cFileName, L".") == 0 ||
		    wcscmp(findData.cFileName, L"..") == 0) {
			continue;
		}
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}
		const std::wstring childRel = relative + findData.cFileName;
		CollectSubdirectories(root, childRel + L"\\", out);
		out.push_back(root + L"\\" + childRel);
	} while (FindNextFileW(find, &findData));
	FindClose(find);
}

bool ClearDirectoryContents(const std::wstring &directory) {
	if (!PathFileExistsW(directory.c_str())) {
		return true;
	}
	std::vector<std::wstring> files;
	CollectRelativeFiles(directory, L"", files, false);
	for (const auto &rel : files) {
		DeleteFileW((directory + L"\\" + rel).c_str());
	}

	std::vector<std::wstring> dirs;
	CollectSubdirectories(directory, L"", dirs);
	for (const auto &dir : dirs) {
		RemoveDirectoryW(dir.c_str());
	}
	return true;
}

bool CopyDirectoryTree(const std::wstring &sourceRoot,
                       const std::wstring &destRoot) {
	if (!EnsureDirectory(destRoot)) {
		return false;
	}
	std::vector<std::wstring> files;
	CollectRelativeFiles(sourceRoot, L"", files, true);
	if (files.empty()) {
		return false;
	}
	for (const auto &rel : files) {
		const std::wstring src = sourceRoot + L"\\" + rel;
		const std::wstring dst = destRoot + L"\\" + rel;
		const size_t slash = dst.find_last_of(L"\\/");
		if (slash != std::wstring::npos) {
			EnsureDirectory(dst.substr(0, slash));
		}
		if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
			StatusDialog::AppendLogf(L"Copy failed (%lu): %s", GetLastError(),
			                         rel.c_str());
			return false;
		}
	}
	return true;
}

std::wstring ReadProductVersionFile(const std::wstring &root) {
	const std::wstring candidates[] = {
	    root + L"\\VERSION.json",
	    root + L"\\version.json",
	};
	for (const auto &path : candidates) {
		const HANDLE file =
		    CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			continue;
		}
		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
		    size.QuadPart > 64 * 1024) {
			CloseHandle(file);
			continue;
		}
		std::string utf8(static_cast<size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		const BOOL ok =
		    ReadFile(file, &utf8[0], static_cast<DWORD>(utf8.size()), &read, nullptr);
		CloseHandle(file);
		if (!ok || read == 0) {
			continue;
		}
		utf8.resize(read);
		const char *key = "\"version\"";
		const size_t pos = utf8.find(key);
		if (pos == std::string::npos) {
			continue;
		}
		const size_t colon = utf8.find(':', pos + strlen(key));
		if (colon == std::string::npos) {
			continue;
		}
		const size_t q1 = utf8.find('"', colon + 1);
		if (q1 == std::string::npos) {
			continue;
		}
		const size_t q2 = utf8.find('"', q1 + 1);
		if (q2 == std::string::npos || q2 <= q1 + 1) {
			continue;
		}
		const std::string ver = utf8.substr(q1 + 1, q2 - q1 - 1);
		std::wstring wide(ver.begin(), ver.end());
		return wide;
	}
	return L"";
}


constexpr char kPhysXInstallerUrl[] =
    "https://us.download.nvidia.com/Windows/9.23.1019/"
    "PhysX_9.23.1019_SystemSoftware.exe";
constexpr wchar_t kPhysXInstallerName[] =
    L"PhysX_9.23.1019_SystemSoftware.exe";

struct PhysXFileSpec {
	const wchar_t *relative;
	const wchar_t *fileName;
};

constexpr PhysXFileSpec kPhysXFiles[] = {
    {L"NxCharacter.dll", L"NxCharacter.dll"},
    {L"NxCooking.dll", L"NxCooking.dll"},
    {L"PhysXCore.dll", L"PhysXCore.dll"},
    {L"PhysXDevice.dll", L"PhysXDevice.dll"},
    {L"PhysXExtensions.dll", L"PhysXExtensions.dll"},
    {L"PhysXLocal\\PhysXLoader.dll", L"PhysXLoader.dll"},
};

bool IsDirectoryPath(const std::wstring &path) {
	if (path.empty() || !PathFileExistsW(path.c_str())) {
		return false;
	}
	return (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool DirectoryHasAllPhysXFiles(const std::wstring &directory) {
	if (!IsDirectoryPath(directory)) {
		return false;
	}
	for (const auto &entry : kPhysXFiles) {
		const std::wstring path = directory + L"\\" + entry.relative;
		if (!PathFileExistsW(path.c_str())) {
			return false;
		}
	}
	return true;
}

bool FindFileRecursive(const std::wstring &rootPath, const wchar_t *fileName,
                       std::wstring &outPath, int depth = 0) {
	if (depth > 8 || rootPath.empty() || !fileName) {
		return false;
	}
	const std::wstring direct = rootPath + L"\\" + fileName;
	if (PathFileExistsW(direct.c_str()) &&
	    (GetFileAttributesW(direct.c_str()) & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		outPath = direct;
		return true;
	}

	const std::wstring pattern = rootPath + L"\\*";
	WIN32_FIND_DATAW fd = {};
	const HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
	if (find == INVALID_HANDLE_VALUE) {
		return false;
	}
	bool found = false;
	do {
		if (fd.cFileName[0] == L'.' &&
		    (fd.cFileName[1] == L'\0' ||
		     (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0'))) {
			continue;
		}
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			if (_wcsicmp(fd.cFileName, fileName) == 0) {
				outPath = rootPath + L"\\" + fd.cFileName;
				found = true;
				break;
			}
			continue;
		}
		if (FindFileRecursive(rootPath + L"\\" + fd.cFileName, fileName, outPath,
		                      depth + 1)) {
			found = true;
			break;
		}
	} while (FindNextFileW(find, &fd));
	FindClose(find);
	return found;
}

bool StagePhysXFromSearchRoots(const std::wstring *roots, size_t rootCount,
                               std::wstring &stagingDir) {
	wchar_t tempPath[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0) {
		return false;
	}
	stagingDir = std::wstring(tempPath) + L"mirroredge-physx-stage";
	if (!EnsureDirectory(stagingDir)) {
		return false;
	}
	if (!EnsureDirectory(stagingDir + L"\\PhysXLocal")) {
		return false;
	}

	for (const auto &entry : kPhysXFiles) {
		std::wstring found;
		for (size_t i = 0; i < rootCount; ++i) {
			if (roots[i].empty() || !IsDirectoryPath(roots[i])) {
				continue;
			}
			if (FindFileRecursive(roots[i], entry.fileName, found)) {
				break;
			}
		}
		if (found.empty()) {
			return false;
		}
		const std::wstring dest = stagingDir + L"\\" + entry.relative;
		if (!CopyFileW(found.c_str(), dest.c_str(), FALSE)) {
			return false;
		}
	}
	return DirectoryHasAllPhysXFiles(stagingDir);
}

bool TryResolveLauncherPhysXPack(std::wstring &directory) {
	std::wstring launcherDirectory;
	if (!Paths::GetLauncherDirectory(launcherDirectory)) {
		return false;
	}

	const std::wstring candidates[] = {
	    launcherDirectory + L"\\physx",
	    launcherDirectory + L"\\dist\\physx",
	};
	for (const auto &candidate : candidates) {
		if (DirectoryHasAllPhysXFiles(candidate)) {
			directory = candidate;
			return true;
		}
	}

	wchar_t parent[MAX_PATH] = {};
	wcsncpy_s(parent, launcherDirectory.c_str(), _TRUNCATE);
	if (PathRemoveFileSpecW(parent)) {
		const std::wstring fromParent = std::wstring(parent) + L"\\physx";
		if (DirectoryHasAllPhysXFiles(fromParent)) {
			directory = fromParent;
			return true;
		}
	}
	return false;
}

bool TryResolveNvidiaPhysXPack(std::wstring &directory) {
	std::wstring roots[4];
	size_t count = 0;

	wchar_t programFilesX86[MAX_PATH] = {};
	if (GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86,
	                            MAX_PATH) > 0) {
		roots[count++] =
		    std::wstring(programFilesX86) + L"\\NVIDIA Corporation\\PhysX";
	}
	wchar_t programFiles[MAX_PATH] = {};
	if (GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH) > 0) {
		roots[count++] =
		    std::wstring(programFiles) + L"\\NVIDIA Corporation\\PhysX";
	}

	if (count == 0) {
		return false;
	}

	for (size_t i = 0; i < count; ++i) {
		const std::wstring common = roots[i] + L"\\Common";
		if (DirectoryHasAllPhysXFiles(common)) {
			directory = common;
			return true;
		}
		if (DirectoryHasAllPhysXFiles(roots[i])) {
			directory = roots[i];
			return true;
		}
	}

	std::wstring staging;
	if (!StagePhysXFromSearchRoots(roots, count, staging)) {
		return false;
	}
	directory = staging;
	return true;
}

bool TryResolveGamePhysXPack(std::wstring &directory) {
	std::wstring gameBinaries;
	if (!Paths::GetGameBinariesDirectory(gameBinaries)) {
		return false;
	}
	if (!DirectoryHasAllPhysXFiles(gameBinaries)) {
		return false;
	}
	directory = gameBinaries;
	return true;
}

bool ResolvePhysXSource(std::wstring &directory, std::wstring &sourceLabel) {
	if (TryResolveLauncherPhysXPack(directory)) {
		sourceLabel = LauncherI18n::T(LauncherI18n::Str::PhysXSourcePack);
		return true;
	}
	if (TryResolveNvidiaPhysXPack(directory)) {
		sourceLabel = LauncherI18n::T(LauncherI18n::Str::PhysXSourceNvidia);
		return true;
	}
	if (TryResolveGamePhysXPack(directory)) {
		sourceLabel = LauncherI18n::T(LauncherI18n::Str::PhysXSourceGame);
		return true;
	}
	directory.clear();
	sourceLabel.clear();
	return false;
}

bool OfferOfficialPhysXDownload() {
	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::PhysXDownloading));

	wchar_t tempPath[MAX_PATH] = {};
	if (GetTempPathW(MAX_PATH, tempPath) == 0) {
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::PhysXDownloadFailedFmt),
		    L"GetTempPath");
		return false;
	}

	const std::wstring dest = std::wstring(tempPath) + kPhysXInstallerName;
	std::wstring error;
	if (!UpdateCheck::DownloadFile(kPhysXInstallerUrl, dest, &error)) {
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::PhysXDownloadFailedFmt),
		    error.empty() ? L"download failed" : error.c_str());
		return false;
	}

	const HINSTANCE launched = ShellExecuteW(
	    nullptr, L"open", dest.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<INT_PTR>(launched) <= 32) {
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::PhysXDownloadFailedFmt),
		    L"ShellExecute failed");
		return false;
	}

	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::PhysXInstallerLaunched));
	return true;
}

} // namespace

bool ResolveLocalModulesDirectory(std::wstring &directory) {
	std::wstring launcherDirectory;
	if (!Paths::GetLauncherDirectory(launcherDirectory)) {
		return false;
	}

	const std::wstring candidates[] = {
	    launcherDirectory + L"\\modules",
	    launcherDirectory + L"\\dist\\modules",
	};
	for (const auto &candidate : candidates) {
		const auto manager =
		    candidate + L"\\module_manager\\module_manager.dll";
		if (PathFileExistsW(manager.c_str())) {
			directory = candidate;
			return true;
		}
	}
	return false;
}

bool ResolvePhysXPatchDirectory(std::wstring &directory) {
	std::wstring sourceLabel;
	return ResolvePhysXSource(directory, sourceLabel);
}

ModulesSyncStatus EvaluateModulesSync(const std::wstring &gameRoot) {
	ModulesSyncStatus status;
	std::wstring localModules;
	if (!ResolveLocalModulesDirectory(localModules)) {
		status.state = ModulesSyncState::NoLocal;
		status.summary = LauncherI18n::T(LauncherI18n::Str::ModulesNoLocal);
		return status;
	}

	if (gameRoot.empty() || !PathFileExistsW(gameRoot.c_str())) {
		status.state = ModulesSyncState::NoGame;
		status.summary = LauncherI18n::T(LauncherI18n::Str::ModulesNoGame);
		return status;
	}

	const std::wstring gameModules = gameRoot + L"\\modules";
	status.localVersion = ReadProductVersionFile(
	    localModules.substr(0, localModules.find_last_of(L"\\/")));
	if (status.localVersion.empty()) {
		std::wstring launcherDir;
		if (Paths::GetLauncherDirectory(launcherDir)) {
			status.localVersion = ReadProductVersionFile(launcherDir);
		}
	}
	status.gameVersion = ReadProductVersionFile(gameRoot);

	if (ArePathsEquivalent(localModules, gameModules)) {
		status.state = ModulesSyncState::Matched;
		status.matchedCount = 1;
		status.summary = LauncherI18n::T(LauncherI18n::Str::ModulesMatchedSamePath);
		return status;
	}

	std::vector<std::wstring> files;
	CollectRelativeFiles(localModules, L"", files, true);
	if (files.empty()) {
		status.state = ModulesSyncState::NoLocal;
		status.summary = LauncherI18n::T(LauncherI18n::Str::ModulesNoLocal);
		return status;
	}

	for (const auto &rel : files) {
		FileMeta localMeta;
		if (!GetFileMeta(localModules + L"\\" + rel, localMeta)) {
			continue;
		}
		const std::wstring gamePath = gameModules + L"\\" + rel;
		FileMeta gameMeta;
		if (!GetFileMeta(gamePath, gameMeta)) {
			++status.missingCount;
			continue;
		}
		if (FilesMatch(localMeta, gameMeta)) {
			++status.matchedCount;
		} else {
			++status.mismatchCount;
		}
	}

	if (status.missingCount == 0 && status.mismatchCount == 0) {
		status.state = ModulesSyncState::Matched;
	} else if (status.matchedCount == 0 &&
	           !PathFileExistsW(gameModules.c_str())) {
		status.state = ModulesSyncState::Missing;
	} else if (status.missingCount > 0 && status.mismatchCount == 0 &&
	           status.matchedCount == 0) {
		status.state = ModulesSyncState::Missing;
	} else {
		status.state = ModulesSyncState::Mismatch;
	}

	wchar_t line[384] = {};
	if (status.state == ModulesSyncState::Matched) {
		_snwprintf(line, 383,
		           LauncherI18n::T(LauncherI18n::Str::ModulesMatchedFmt),
		           status.matchedCount,
		           status.localVersion.empty() ? L"-" : status.localVersion.c_str());
	} else {
		_snwprintf(line, 383,
		           LauncherI18n::T(LauncherI18n::Str::ModulesOutdatedFmt),
		           status.matchedCount, status.missingCount, status.mismatchCount,
		           status.localVersion.empty() ? L"-" : status.localVersion.c_str(),
		           status.gameVersion.empty() ? L"-" : status.gameVersion.c_str());
	}
	line[383] = L'\0';
	status.summary = line;
	return status;
}

bool PatchModulesToGame(const std::wstring &gameRoot) {
	std::wstring localModules;
	if (!ResolveLocalModulesDirectory(localModules)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::WarnNoLocalModules));
		return false;
	}
	if (gameRoot.empty()) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::ModulesNoGame));
		return false;
	}

	const std::wstring gameModules = gameRoot + L"\\modules";
	if (ArePathsEquivalent(localModules, gameModules)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::ModulesMatchedSamePath));
		return true;
	}

	EnsureDirectory(gameRoot);
	if (PathFileExistsW(gameModules.c_str())) {
		ClearDirectoryContents(gameModules);
	}
	if (!CopyDirectoryTree(localModules, gameModules)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::WarnPatchModulesFailed));
		return false;
	}

	std::wstring launcherDir;
	if (Paths::GetLauncherDirectory(launcherDir)) {
		const std::wstring versionSrc = launcherDir + L"\\VERSION.json";
		if (PathFileExistsW(versionSrc.c_str())) {
			CopyFileW(versionSrc.c_str(), (gameRoot + L"\\VERSION.json").c_str(),
			          FALSE);
		}
	}

	StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::PatchedModulesFmt),
	                         gameModules.c_str());
	return true;
}

bool PatchGraphicsProxy(const std::wstring &gameBinariesDirectory) {
	const auto &config = LauncherConfig::Get();

	std::wstring proxySource;
	if (!Paths::ResolveGraphicsProxyDll(proxySource)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::WarnNoProxyDll));
		return false;
	}

	const auto destinationPath =
	    gameBinariesDirectory + L"\\" + config.graphicsProxyDllName;
	const auto backupPath =
	    gameBinariesDirectory + L"\\" + config.graphicsProxyBackup;

	if (ArePathsEquivalent(proxySource, destinationPath)) {
		return true;
	}

	if (!BackupAndCopyFile(proxySource, destinationPath, backupPath)) {
		StatusDialog::AppendLogf(
		    LauncherI18n::T(LauncherI18n::Str::WarnDeployProxyFmt),
		    GetLastError());
		return false;
	}

	StatusDialog::AppendLogf(LauncherI18n::T(LauncherI18n::Str::DeployedProxyFmt),
	                         destinationPath.c_str());
	return true;
}

bool PatchDependenciesToGame() {
	std::wstring gameRoot;
	std::wstring gameBinaries;
	if (!Paths::GetGameRootDirectory(gameRoot) ||
	    !Paths::GetGameBinariesDirectory(gameBinaries)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::NoBinariesSkipPrep));
		return false;
	}

	StatusDialog::AppendLog(
	    LauncherI18n::T(LauncherI18n::Str::PatchingDependencies));
	const bool modulesOk = PatchModulesToGame(gameRoot);
	const bool proxyOk = PatchGraphicsProxy(gameBinaries);
	return modulesOk && proxyOk;
}

bool PatchPhysXToGame() {
	std::wstring physxDir;
	std::wstring sourceLabel;
	if (!ResolvePhysXSource(physxDir, sourceLabel)) {
		if (!OfferOfficialPhysXDownload()) {
			StatusDialog::AppendLog(
			    LauncherI18n::T(LauncherI18n::Str::WarnNoPhysXPack));
		}
		return false;
	}

	std::wstring gameBinaries;
	if (!Paths::GetGameBinariesDirectory(gameBinaries)) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::NoBinariesSkipPrep));
		return false;
	}

	StatusDialog::AppendLogf(
	    LauncherI18n::T(LauncherI18n::Str::PhysXUsingSourceFmt),
	    sourceLabel.c_str(), physxDir.c_str());

	int copied = 0;
	int skipped = 0;
	for (const auto &entry : kPhysXFiles) {
		const std::wstring source = physxDir + L"\\" + entry.relative;
		if (!PathFileExistsW(source.c_str())) {
			StatusDialog::AppendLogf(
			    LauncherI18n::T(LauncherI18n::Str::WarnPhysXMissingFileFmt),
			    entry.relative);
			return false;
		}

		const std::wstring destination = gameBinaries + L"\\" + entry.relative;
		if (ArePathsEquivalent(source, destination)) {
			++skipped;
			continue;
		}

		const size_t slash = destination.find_last_of(L"\\/");
		if (slash != std::wstring::npos) {
			EnsureDirectory(destination.substr(0, slash));
		}

		const std::wstring backup = destination + kPhysXBackupSuffix;
		if (!BackupAndCopyFile(source, destination, backup)) {
			StatusDialog::AppendLogf(
			    LauncherI18n::T(LauncherI18n::Str::WarnPhysXCopyFailedFmt),
			    entry.relative, GetLastError());
			return false;
		}
		++copied;
	}

	StatusDialog::AppendLogf(
	    LauncherI18n::T(LauncherI18n::Str::PatchedPhysXFromFmt), copied,
	    sourceLabel.c_str(), gameBinaries.c_str());
	if (skipped > 0 && copied == 0) {
		StatusDialog::AppendLog(
		    LauncherI18n::T(LauncherI18n::Str::PhysXAlreadyCurrent));
	}
	return true;
}

} // namespace GamePatch
