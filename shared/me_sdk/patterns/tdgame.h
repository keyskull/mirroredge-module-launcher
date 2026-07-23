#pragma once

// Mirror's Edge 1.0 — TdGame-specific patch/hook signatures (main exe).

namespace MeSdk {
namespace Patterns {
namespace TdGame {

static const char StateHandler[] =
    "\xF3\x0F\x10\x05\x00\x00\x00\x00\x83\xEC\x0C\x0F\x2F\x44\x24\x00\x56"
    "\x0F\x87\x00\x00\x00\x00\x8B\x44\x24\x18\x83\xF8\x07\x0F\x8F\x00\x00"
    "\x00\x00\x0F\xB6\x51\x68\x81\xA1\x00\x00\x00\x00\x00\x00\x00\x00";
static const char StateHandlerMask[] =
    "xxxx????xxxxxxx?xxx????xxxxxxxxx????xxxxxx????????";

static const char ForceRoll[] =
    "\x89\x93\x00\x00\x00\x00\xA1\x00\x00\x00\x00\x83\xB8";
static const char ForceRollMask[] = "xx????x????xx";

} // namespace TdGame
} // namespace Patterns
} // namespace MeSdk
