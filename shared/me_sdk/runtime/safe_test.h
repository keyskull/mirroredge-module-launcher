#pragma once

#include <string>

namespace MeSdk {
namespace Safe {
namespace Test {

// Iterates GObjects, identifies instances of all new safe-wrapper types,
// calls Try* accessors, and returns a JSON summary of passes/failures.
// Must be called on the game main thread (GObjects traversal).
std::string RunAllSafeWrapperTests();

} // namespace Test
} // namespace Safe
} // namespace MeSdk
