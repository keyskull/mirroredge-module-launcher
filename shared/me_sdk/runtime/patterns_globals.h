#pragma once

// Mirror's Edge 1.0 — GNames / GObjects instruction signatures (main exe).
// Single source of truth for init.cpp and readiness probes.

namespace MeSdk {
namespace Patterns {

static const char GNames[] =
    "\x8B\x0D\x00\x00\x00\x00\x8B\x84\x24\x00\x00\x00\x00\x8B\x04\x81";
static const char GNamesMask[] = "xx????xxx????xxx";

static const char GObjects[] =
    "\x8B\x15\x00\x00\x00\x00\x8B\x0C\xB2\x8D\x44\x24\x30";
static const char GObjectsMask[] = "xx????xxxxxxx";

} // namespace Patterns
} // namespace MeSdk
