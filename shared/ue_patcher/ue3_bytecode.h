#pragma once
// ue3_bytecode.h — UE3 bytecode decoder, iterator, pattern scanner, and patcher.
//
// Works on UFunction::Script arrays.  All read operations are SEH-guarded
// against garbage-collected or corrupted bytecode buffers.

#include <windows.h>
#include "ue3_opcodes.h"

#include <cstdint>
#include <string>
#include <vector>

// Forward: SDK class that holds the Script TArray.
namespace Classes { class UStruct; }

namespace UePatcher {
namespace Bytecode {

// ---- Single decoded instruction ----

struct Instruction {
	const uint8_t *start;   // pointer into Script buffer
	uint8_t        opcode;  // the opcode byte at start
	int            size;    // total bytes consumed (including operands; -1 if unknown)
	const char     *name;   // human-readable opcode name (points to static string)
};

// ---- Bytecode walker ----

// Safely decode one instruction at `pos`.  Returns {pos, opcode, size, name}.
// If `pos` is outside Script bounds or opcode is invalid, returns {pos, 0, 0, "?"}.
Instruction DecodeInstruction(const uint8_t *script, size_t scriptSize,
                              const uint8_t *pos);

// Walk bytecode from start to end, calling `callback` for each instruction.
// Callback signature: bool(const Instruction &insn) — return false to stop.
// Returns the number of instructions decoded.
using WalkCallback = bool (*)(const Instruction &insn, void *user);
int WalkBytecode(const uint8_t *script, size_t scriptSize,
                 WalkCallback callback, void *user);

// Calculate the byte size of a variable-length instruction body.
// Recursively decodes inner expressions.
// `script` and `scriptSize` bound the entire buffer; `pos` points past the opcode byte.
int MeasureExpression(const uint8_t *script, size_t scriptSize,
                      const uint8_t *pos);


// ---- Pattern scanner ----

// Find a byte pattern in a Script buffer.  Uses the same mask format as
// Pattern::FindPattern: 'x' = match exact byte, '?' = wildcard.
// Returns offset from script start, or -1 if not found.
ptrdiff_t FindBytecodePattern(const uint8_t *script, size_t scriptSize,
                              const uint8_t *pattern, const char *mask,
                              ptrdiff_t startOffset = 0);

// Find all occurrences of a pattern.  Appends offsets to `results`.
// Returns count of matches found.
int FindBytecodePatternAll(const uint8_t *script, size_t scriptSize,
                           const uint8_t *pattern, const char *mask,
                           std::vector<ptrdiff_t> &results);


// ---- Bytecode patcher ----

// Result of a bytecode patch operation.
enum class PatchResult {
	Ok,
	NotFound,
	AlreadyPatched,
	MemoryProtected,
	InvalidSize,
	Error,
};

// Replace a byte sequence at `offset` in the Script array with `replaceBytes`.
// The Script buffer is made writable via VirtualProtect before modification.
// `expect` (optional) — verify current bytes match before patching.
PatchResult PatchBytes(Classes::UStruct *uStruct,
                       ptrdiff_t offset,
                       const uint8_t *replaceBytes, size_t replaceLen,
                       const uint8_t *expect = nullptr, size_t expectLen = 0);

// Insert `insertLen` bytes at `offset`, shifting subsequent bytes forward.
// The Script array is grown if necessary.  WARNING: this invalidates all
// absolute jump offsets (OP_Jump, OP_JumpIfNot) that target addresses
// after the insertion point.
PatchResult InsertBytes(Classes::UStruct *uStruct,
                        ptrdiff_t offset,
                        const uint8_t *data, size_t dataLen);

// Make a UStruct's Script buffer writable.  Must call before direct modification.
// Returns old protection, or 0 on failure.
DWORD MakeScriptWritable(Classes::UStruct *uStruct);

// Restore script protection after modification.
void RestoreScriptProtection(Classes::UStruct *uStruct, DWORD oldProtect);


// ---- Safe memory access helpers ----

bool IsReadableAddress(const void *addr, size_t len);

// Read a uint16/uint32 from potentially-unsafe memory (SEH-guarded).
bool SafeReadU16(const uint8_t *addr, uint16_t &out);
bool SafeReadU32(const uint8_t *addr, uint32_t &out);
bool SafeReadF32(const uint8_t *addr, float &out);


} // namespace Bytecode
} // namespace UePatcher
