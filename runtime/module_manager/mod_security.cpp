#include "mod_security.h"

#include "module_contract.h"

#include <Shlwapi.h>
#include <Windows.h>

#include <cwctype>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

namespace ModSecurity {

namespace {

bool StartsWithCaseInsensitive(const std::wstring &value,
                               const std::wstring &prefix) {
    if (value.size() < prefix.size()) {
        return false;
    }
    return _wcsnicmp(value.c_str(), prefix.c_str(),
                     static_cast<int>(prefix.size())) == 0;
}

std::wstring NormalizePath(const std::wstring &path) {
    wchar_t buffer[MAX_PATH] = {};
    if (!GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr)) {
        return path;
    }
    return buffer;
}

bool HasOnlyRelativeSegments(const std::wstring &relative) {
    if (relative.find(L"..") != std::wstring::npos) {
        return false;
    }
    return true;
}

} // namespace

bool IsReservedModuleFolder(const std::wstring &folderName) {
    static const wchar_t *kReserved[] = {L"core", L"imgui", L"module_manager",
                                         L"mm-engine"};
    for (const auto reserved : kReserved) {
        if (_wcsicmp(folderName.c_str(), reserved) == 0) {
            return true;
        }
    }
    return false;
}

bool IsBuiltinModuleId(const std::wstring &moduleId) {
    return _wcsicmp(moduleId.c_str(), L"core") == 0;
}

bool IsReservedModuleDll(const std::wstring &fileName) {
    static const wchar_t *kReserved[] = {
        MMOD_IMGUI_DLL_FILENAME, MMOD_MANAGER_DLL_FILENAME, L"d3d9.dll"};
    for (const auto reserved : kReserved) {
        if (_wcsicmp(fileName.c_str(), reserved) == 0) {
            return true;
        }
    }
    return false;
}

bool IsValidModuleId(const std::wstring &moduleId) {
    if (moduleId.empty() || moduleId.size() > 64) {
        return false;
    }

    for (const auto ch : moduleId) {
        if (iswalnum(ch) || ch == L'_' || ch == L'-') {
            continue;
        }
        return false;
    }

    return true;
}

bool IsPeMachineX86(const std::wstring &dllPath) {
    const auto file =
        CreateFileW(dllPath.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    IMAGE_DOS_HEADER dos = {};
    DWORD read = 0;
    if (!ReadFile(file, &dos, sizeof(dos), &read, nullptr) ||
        read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(file);
        return false;
    }

    if (SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN) ==
        INVALID_SET_FILE_POINTER) {
        CloseHandle(file);
        return false;
    }

    DWORD peSignature = 0;
    if (!ReadFile(file, &peSignature, sizeof(peSignature), &read, nullptr) ||
        read != sizeof(peSignature) || peSignature != IMAGE_NT_SIGNATURE) {
        CloseHandle(file);
        return false;
    }

    IMAGE_FILE_HEADER fileHeader = {};
    if (!ReadFile(file, &fileHeader, sizeof(fileHeader), &read, nullptr) ||
        read != sizeof(fileHeader)) {
        CloseHandle(file);
        return false;
    }

    CloseHandle(file);
    return fileHeader.Machine == IMAGE_FILE_MACHINE_I386;
}

bool ValidateModuleDllPath(const std::wstring &modulesRoot,
                           const std::wstring &dllPath, std::wstring &moduleId,
                           std::string &rejectReason) {
    rejectReason.clear();
    moduleId.clear();

    if (!PathFileExistsW(dllPath.c_str())) {
        rejectReason = "DLL file not found";
        return false;
    }

    const auto root = NormalizePath(modulesRoot);
    const auto fullDll = NormalizePath(dllPath);
    if (root.empty() || fullDll.empty()) {
        rejectReason = "Invalid path";
        return false;
    }

    const auto rootPrefix = root.back() == L'\\' ? root : root + L"\\";
    if (!StartsWithCaseInsensitive(fullDll, rootPrefix)) {
        rejectReason = "Path outside modules directory";
        return false;
    }

    const auto relative = fullDll.substr(rootPrefix.size());
    if (!HasOnlyRelativeSegments(relative)) {
        rejectReason = "Path traversal rejected";
        return false;
    }

    const auto slash = relative.find(L'\\');
    if (slash == std::wstring::npos) {
        rejectReason = "Module must live in modules\\<id>\\";
        return false;
    }

    moduleId = relative.substr(0, slash);
    const auto dllName = relative.substr(slash + 1);
    if (dllName.find(L'\\') != std::wstring::npos) {
        rejectReason = "Nested module paths are not allowed";
        return false;
    }

    if (!IsValidModuleId(moduleId)) {
        rejectReason = "Invalid module id";
        return false;
    }

    if (IsReservedModuleFolder(moduleId) && !IsBuiltinModuleId(moduleId)) {
        rejectReason = "Reserved module folder";
        return false;
    }

    if (IsReservedModuleDll(dllName)) {
        rejectReason = "Reserved system DLL";
        return false;
    }

    if (!IsPeMachineX86(fullDll)) {
        rejectReason = "DLL is not Win32 (x86)";
        return false;
    }

    return true;
}

} // namespace ModSecurity
