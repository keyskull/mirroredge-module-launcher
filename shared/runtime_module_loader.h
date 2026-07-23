#pragma once

#include <Windows.h>

#include <string>

namespace RuntimeModuleLoader {

std::wstring ResolveSiblingModulePath(HMODULE hostModule, const wchar_t *subfolder,
                                      const wchar_t *dllFileName);

HMODULE EnsureSiblingModule(HMODULE hostModule, const wchar_t *subfolder,
                            const wchar_t *dllFileName, HMODULE *cached);

} // namespace RuntimeModuleLoader
