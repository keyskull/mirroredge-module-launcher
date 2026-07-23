#pragma once

#include <cstddef>
#include <cstdint>

namespace MeSdk {
namespace GameSignature {

// Mirror's Edge 1.0 main executable (loaded as process image).
static const char kMainModuleName[] = "MirrorsEdge.exe";

// Loaded-module SizeOfImage bounds (retail / Steam SecuROM-free builds).
static const uint32_t kMinImageSize = 0x03000000;
static const uint32_t kMaxImageSize = 0x04000000;

// FNV-1a probe of code bytes (skip PE headers). Logged when debug session active.
static const uint32_t kCodeProbeOffset = 0x1000;
static const uint32_t kCodeProbeLength = 4096;

// When kKnownCodeProbeFnvCount > 0, code probe FNV must match one entry.
// MSVC rejects zero-length arrays; storage is unused while count stays 0.
static const uint32_t kKnownCodeProbeFnvStorage[] = {0u};
static const uint32_t *const kKnownCodeProbeFnv = kKnownCodeProbeFnvStorage;
static const size_t kKnownCodeProbeFnvCount = 0;

} // namespace GameSignature
} // namespace MeSdk
