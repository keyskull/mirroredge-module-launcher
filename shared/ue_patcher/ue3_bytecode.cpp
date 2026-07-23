// ue3_bytecode.cpp — Bytecode decoder, iterator, pattern scanner, and patcher.
#include "ue3_bytecode.h"
#include "patcher_types.h"

#include <windows.h>
#include <cstring>
#include <cinttypes>

// SDK: for UStruct::ScriptData() / ScriptSize() / HasScript()
#include "me_sdk/me_sdk.h"

namespace UePatcher {
namespace Bytecode {

// ============================================================
// Safe memory access
// ============================================================

bool IsReadableAddress(const void *addr, size_t len) {
	if (!addr || len == 0)
		return false;

	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
		return false;
	if (mbi.State != MEM_COMMIT)
		return false;
	if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
		return false;
	// Check that the range doesn't cross an allocation boundary
	const auto end = reinterpret_cast<const uint8_t *>(addr) + len - 1;
	const auto regionEnd = reinterpret_cast<const uint8_t *>(mbi.BaseAddress)
		+ mbi.RegionSize - 1;
	if (end > regionEnd)
		return false;
	return true;
}

bool SafeReadU16(const uint8_t *addr, uint16_t &out) {
	if (!IsReadableAddress(addr, 2))
		return false;
	__try {
		out = *reinterpret_cast<const uint16_t *>(addr);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SafeReadU32(const uint8_t *addr, uint32_t &out) {
	if (!IsReadableAddress(addr, 4))
		return false;
	__try {
		out = *reinterpret_cast<const uint32_t *>(addr);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool SafeReadF32(const uint8_t *addr, float &out) {
	return SafeReadU32(addr, *reinterpret_cast<uint32_t *>(&out));
}

// ============================================================
// Instruction decoding
// ============================================================

// Helper: bounds-check a read within the script buffer.
static inline bool InBounds(const uint8_t *script, size_t scriptSize,
                            const uint8_t *pos, size_t need) {
	if (!pos || !script || pos < script)
		return false;
	const auto offset = static_cast<size_t>(pos - script);
	return offset + need <= scriptSize;
}

// Measure the size of a null-terminated string starting at `pos`.
// Returns the total bytes consumed (including null terminator), or -1.
static int MeasureStringConst(const uint8_t *script, size_t scriptSize,
                              const uint8_t *pos) {
	if (!InBounds(script, scriptSize, pos, 1))
		return -1;
	const auto *p = pos;
	const auto *end = script + scriptSize;
	while (p < end && *p != 0)
		++p;
	if (p >= end)
		return -1; // unterminated
	return static_cast<int>(p - pos) + 1; // include null
}

// Recursively measure an expression — returns the byte count consumed.
// This is needed for variable-length opcodes whose inner expressions
// have no explicit size field.
int MeasureExpression(const uint8_t *script, size_t scriptSize,
                      const uint8_t *pos) {
	// We need to decode instructions until we hit a terminal.
	// Terminal opcodes in expression context:
	//   EndFunctionParms, Return, EndOfScript
	// Context, DynamicCast, etc. embed inner expressions whose endpoints
	// are either explicit (skipSize in Context) or implicit (next op after
	// expression parameters).
	//
	// Strategy: walk forward, tracking depth of expression nesting.

	int total = 0;
	// We use a simple heuristic: walk until EndFunctionParms or
	// until we hit a "terminal" that makes sense as an expression boundary.
	// The caller should know the context.

	// For now: measure one full instruction including its operands.
	// The caller is responsible for calling this in the right context.
	const auto insn = DecodeInstruction(script, scriptSize, pos);
	if (insn.size <= 0)
		return 0;
	return insn.size;
}

// Decode one instruction.
Instruction DecodeInstruction(const uint8_t *script, size_t scriptSize,
                              const uint8_t *pos) {
	Instruction insn = {}; // zero-init
	insn.start = pos;
	insn.name = "?";

	if (!InBounds(script, scriptSize, pos, 1))
		return insn;

	insn.opcode = *pos;
	insn.name = OpcodeName(insn.opcode);

	const int operandSize = OperandSize(insn.opcode);

	switch (operandSize) {
	case 0:
		insn.size = 1;
		break;

	case -2: { // Null-terminated string
		const int strLen = MeasureStringConst(script, scriptSize, pos + 1);
		if (strLen < 0)
			return insn; // unterminated
		insn.size = 1 + strLen;
		break;
	}

	case -1: { // Variable-length
		// Handle special cases
		switch (insn.opcode) {
		case OP_Context: {
			// objectExpr + u16 skipSize + u16 fieldType + innerExpr
			// skipSize includes fieldType (2 bytes) + innerExpr
			const uint8_t *afterOp = pos + 1;
			// First, walk the object expression
			const int objSize = DecodeInstruction(script, scriptSize, afterOp).size;
			if (objSize <= 0)
				return insn;

			const uint8_t *afterObj = afterOp + objSize;
			uint16_t skipSize = 0;
			if (!InBounds(script, scriptSize, afterObj, 2) ||
			    !SafeReadU16(afterObj, skipSize))
				return insn;

			// skipSize is the number of bytes from after the skipSize field to after innerExpr
			// The fieldType is included in skipSize
			insn.size = 1 + static_cast<int>(afterObj + 2 + skipSize - pos);
			break;
		}

		case OP_DynamicCast: {
			// classRefExpr + innerExpr (walk both)
			const uint8_t *p = pos + 1;
			const int classSize = DecodeInstruction(script, scriptSize, p).size;
			if (classSize <= 0)
				return insn;
			p += classSize;
			const int innerSize = DecodeInstruction(script, scriptSize, p).size;
			if (innerSize <= 0)
				return insn;
			insn.size = 1 + static_cast<int>(p + innerSize - pos);
			break;
		}

		case OP_StructMember: {
			// i32 propRef + i32 structRef + u8 copy + u8 mod + inner
			const uint8_t *p = pos + 1;
			if (!InBounds(script, scriptSize, p, 10))
				return insn; // 4+4+1+1 = 10 bytes header
			p += 10;
			const int innerSize = DecodeInstruction(script, scriptSize, p).size;
			if (innerSize <= 0)
				return insn;
			insn.size = 1 + static_cast<int>(p + innerSize - pos);
			break;
		}

		case OP_PrimitiveCast: {
			// u8 castKind + inner
			const uint8_t *p = pos + 2; // skip opcode + castKind
			const int innerSize = DecodeInstruction(script, scriptSize, p).size;
			if (innerSize <= 0)
				return insn;
			insn.size = 1 + static_cast<int>(p + innerSize - pos);
			break;
		}

		case OP_DelegateFunction: {
			// u8 flags + i32 prop + FName(8) + params... + EndFP
			const uint8_t *p = pos + 1;
			if (!InBounds(script, scriptSize, p, 13)) // 1+4+8 = 13
				return insn;
			p += 13;
			// Walk until EndFunctionParms
			while (InBounds(script, scriptSize, p, 1)) {
				if (*p == OP_EndFunctionParms) {
					p += 1;
					break;
				}
				const int step = DecodeInstruction(script, scriptSize, p).size;
				if (step <= 0)
					break;
				p += step;
			}
			insn.size = static_cast<int>(p - pos);
			break;
		}

		case OP_And_BoolBool: {
			// boolExpr + Skip(u16 skipSize) + boolExpr + EndFP
			const uint8_t *p = pos + 1;
			const int firstSize = DecodeInstruction(script, scriptSize, p).size;
			if (firstSize <= 0)
				return insn;
			p += firstSize;
			if (!InBounds(script, scriptSize, p, 3)) // Skip opcode(1) + uint16(2)
				return insn;
			if (*p != OP_Skip)
				return insn; // malformed
			uint16_t skipSize = 0;
			if (!SafeReadU16(p + 1, skipSize))
				return insn;
			// skipSize covers from after Skip opcode to after EndFP
			// So total after Skip is: 2 (u16 operand) + skipSize
			insn.size = 1 + static_cast<int>(p + 3 + skipSize - pos);
			break;
		}

		default:
			// Unknown variable-length — treat as single byte to avoid infinite loop
			insn.size = 1;
			insn.name = "?(var)";
			break;
		}
		break;
	}

	default: { // Fixed-size operand
		if (operandSize < 0) {
			insn.size = 1; // safeguard
			break;
		}
		if (InBounds(script, scriptSize, pos, 1 + static_cast<size_t>(operandSize))) {
			insn.size = 1 + operandSize;
		}
		break;
	}
	}

	// Sanity: don't return size beyond buffer end
	if (insn.size > 0 &&
	    !InBounds(script, scriptSize, pos, static_cast<size_t>(insn.size))) {
		insn.size = static_cast<int>(script + scriptSize - pos);
		if (insn.size < 1)
			insn.size = 1;
	}

	return insn;
}

// ============================================================
// Bytecode walker
// ============================================================

int WalkBytecode(const uint8_t *script, size_t scriptSize,
                 WalkCallback callback, void *user) {
	if (!script || !scriptSize || !callback)
		return 0;

	int count = 0;
	const auto *pos = script;
	const auto *end = script + scriptSize;

	while (pos < end) {
		const auto insn = DecodeInstruction(script, scriptSize, pos);
		if (insn.size <= 0) {
			++pos; // skip byte
			++count;
			continue;
		}

		if (!callback(insn, user))
			break;

		++count;
		pos += insn.size;
	}

	return count;
}

// ============================================================
// Pattern scanner
// ============================================================

// Simple Boyer-Moore-like byte scan with mask support.
static bool MatchPattern(const uint8_t *data, size_t dataLen,
                         const uint8_t *pattern, const char *mask,
                         size_t patternLen) {
	if (dataLen < patternLen)
		return false;
	for (size_t i = 0; i < patternLen; ++i) {
		if (mask[i] == 'x' && data[i] != pattern[i])
			return false;
	}
	return true;
}

ptrdiff_t FindBytecodePattern(const uint8_t *script, size_t scriptSize,
                              const uint8_t *pattern, const char *mask,
                              ptrdiff_t startOffset) {
	if (!script || !pattern || !mask || scriptSize == 0)
		return -1;

	const size_t patternLen = std::strlen(mask);
	if (patternLen == 0 || patternLen > scriptSize)
		return -1;

	auto start = static_cast<size_t>(startOffset < 0 ? 0 :
	                                (startOffset > static_cast<ptrdiff_t>(scriptSize) ?
	                                 scriptSize : startOffset));

	const size_t maxStart = scriptSize - patternLen;
	for (size_t i = start; i <= maxStart; ++i) {
		if (MatchPattern(script + i, scriptSize - i,
		                 pattern, mask, patternLen)) {
			return static_cast<ptrdiff_t>(i);
		}
	}
	return -1;
}

int FindBytecodePatternAll(const uint8_t *script, size_t scriptSize,
                           const uint8_t *pattern, const char *mask,
                           std::vector<ptrdiff_t> &results) {
	ptrdiff_t offset = 0;
	int count = 0;
	while (true) {
		offset = FindBytecodePattern(script, scriptSize, pattern, mask, offset);
		if (offset < 0)
			break;
		results.push_back(offset);
		++count;
		++offset; // advance past this match
		// Safety: don't loop forever on single-byte patterns
		if (static_cast<size_t>(offset) >= scriptSize)
			break;
	}
	return count;
}

// ============================================================
// Bytecode patcher
// ============================================================

DWORD MakeScriptWritable(Classes::UStruct *uStruct) {
	if (!uStruct || !uStruct->HasScript())
		return 0;

	auto *script = const_cast<uint8_t *>(uStruct->ScriptData());
	const auto size = uStruct->ScriptSize();

	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(script, &mbi, sizeof(mbi)))
		return 0;

	// If already writable, return the current protection
	if (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))
		return mbi.Protect;

	DWORD oldProtect = 0;
	if (!VirtualProtect(script, size, PAGE_EXECUTE_READWRITE, &oldProtect))
		return 0;

	return oldProtect;
}

void RestoreScriptProtection(Classes::UStruct *uStruct, DWORD oldProtect) {
	if (!uStruct || !oldProtect || !uStruct->HasScript())
		return;

	auto *script = const_cast<uint8_t *>(uStruct->ScriptData());
	const auto size = uStruct->ScriptSize();
	DWORD unused;
	VirtualProtect(script, size, oldProtect, &unused);
}

PatchResult PatchBytes(Classes::UStruct *uStruct,
                       ptrdiff_t offset,
                       const uint8_t *replaceBytes, size_t replaceLen,
                       const uint8_t *expect, size_t expectLen) {
	if (!uStruct || !uStruct->HasScript() || !replaceBytes || replaceLen == 0)
		return PatchResult::Error;

	const auto scriptSize = static_cast<ptrdiff_t>(uStruct->ScriptSize());
	if (offset < 0 || offset + static_cast<ptrdiff_t>(replaceLen) > scriptSize)
		return PatchResult::InvalidSize;

	auto *script = const_cast<uint8_t *>(uStruct->ScriptData()) + offset;

	// Verify expected bytes if provided
	if (expect && expectLen > 0) {
		if (expectLen != replaceLen)
			return PatchResult::InvalidSize;
		if (std::memcmp(script, expect, expectLen) != 0)
			return PatchResult::AlreadyPatched; // bytes differ — likely already modified
	}

	// Check if already patched
	if (std::memcmp(script, replaceBytes, replaceLen) == 0)
		return PatchResult::AlreadyPatched;

	// Make writable
	const DWORD oldProtect = MakeScriptWritable(uStruct);
	if (!oldProtect)
		return PatchResult::MemoryProtected;

	std::memcpy(script, replaceBytes, replaceLen);

	RestoreScriptProtection(uStruct, oldProtect);
	return PatchResult::Ok;
}

PatchResult InsertBytes(Classes::UStruct *uStruct,
                        ptrdiff_t offset,
                        const uint8_t *data, size_t dataLen) {
	if (!uStruct || !uStruct->HasScript() || !data || dataLen == 0)
		return PatchResult::Error;

	// Insertion is only supported when the Script array has spare capacity.
	// In cooked packages, Script points into the serialized package buffer
	// where growth is not possible.  Use PatchBytes() for same-size or
	// shrink replacements instead.
	//
	// Future: if needed, allocate a new buffer via the game's allocator,
	// copy the Script there, and update the TArray.
	return PatchResult::InvalidSize;
}

} // namespace Bytecode
} // namespace UePatcher
