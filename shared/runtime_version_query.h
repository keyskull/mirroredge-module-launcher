#pragma once

#include "runtime_version.h"

#include <Windows.h>

namespace RuntimeVersionQuery {

const MmodRuntimeVersion *QueryLoadedModule(const wchar_t *moduleFileName);

void FormatComponentVersionsLine(char *out, size_t outSize);

} // namespace RuntimeVersionQuery
