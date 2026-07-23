#pragma once

#include <Windows.h>

namespace ConsoleLoader {

bool EnsureLoaded(HMODULE hostModule);
HMODULE GetModule();

} // namespace ConsoleLoader
