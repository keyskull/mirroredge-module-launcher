#pragma once

#include <string>

namespace ModSecurity {

bool IsReservedModuleFolder(const std::wstring &folderName);
bool IsBuiltinModuleId(const std::wstring &moduleId);
bool IsReservedModuleDll(const std::wstring &fileName);
bool IsValidModuleId(const std::wstring &moduleId);
bool IsPeMachineX86(const std::wstring &dllPath);
bool ValidateModuleDllPath(const std::wstring &modulesRoot,
                           const std::wstring &dllPath, std::wstring &moduleId,
                           std::string &rejectReason);

} // namespace ModSecurity
