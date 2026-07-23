#pragma once
// ue3_opcodes.h — UE3 bytecode opcode definitions for Mirror's Edge v536.
//
// Values cross-referenced from MirrorsEdgeTweaks' BytecodeBuilder.cs.
// Operand sizes describe what the decoder must consume after the opcode byte.

#include <cstdint>

namespace UePatcher {
namespace Bytecode {

// ---- Opcodes ----

enum EOp : uint8_t {
	// Variable access
	OP_LocalVariable      = 0x00, // int32: local var table index
	OP_InstanceVariable   = 0x01, // int32: import/export package index
	// 0x02–0x03 not used in ME v536

	// Control flow
	OP_Return             = 0x04, // (none)
	// 0x05 not used
	OP_Jump               = 0x06, // uint16: absolute code offset
	OP_JumpIfNot          = 0x07, // uint16: absolute code offset (pops bool)
	// 0x08–0x09 not used in ME

	// Constants
	OP_IntConst           = 0x0A, // int32: literal integer
	OP_Nothing            = 0x0B, // (none) — nop
	OP_IntOne             = 0x0C, // (none) — push int 1
	OP_FloatConst         = 0x0D, // float32: literal float
	OP_EatReturnValue     = 0x0E, // int32: return value local index
	OP_Let                = 0x0F, // (none) — assignment; followed by dest, value

	// 0x10–0x13 not used in ME

	// Boolean operators — each has implicit EndFunctionParms at expression end
	OP_LetBool            = 0x14, // (none)
	// 0x15 not used
	OP_EndFunctionParms   = 0x16, // (none) — terminates native function arguments
	OP_Self               = 0x17, // (none) — push 'self' onto stack
	OP_Skip               = 0x18, // uint16: skip N bytes forward
	OP_Context            = 0x19, // variable: object expr + uint16 skipSize + uint16 fieldType + inner expr
	OP_True               = 0x1A, // (none) — push true (0x27 per MET)

	// Wait — let me recheck. MET has:
	// OP_TRUE = 0x27, OP_FALSE = 0x28
	// But standard UE3 has 0x1A = EX_True, 0x27 = EX_Iterator
	// ME must use the non-standard values since MET works

	// Function calls
	OP_VirtualFunction    = 0x1B, // FName (8 bytes: int32 nameIndex + int32 number)
	OP_FinalFunction      = 0x1C, // FName (8 bytes)
	// 0x1D not used
	OP_FloatConst_0x1E    = 0x1E, // float32 — MET uses 0x1E for float const
	OP_StringConst        = 0x1F, // null-terminated ANSI string

	// More constants
	OP_ObjectConst        = 0x20, // int32: object package index
	OP_NameConst          = 0x21, // FName (8 bytes)
	// 0x22–0x23 not used in ME (?)
	OP_ByteConst          = 0x24, // uint8
	// 0x25–0x26 not used
	OP_True_0x27          = 0x27, // (none) — MET's true
	OP_False_0x28         = 0x28, // (none) — MET's false
	OP_NoObject           = 0x2A, // (none) — push null
	OP_IntConstByte       = 0x2C, // uint8: 0-255 int as single byte
	OP_BoolVariable       = 0x2D, // int32: bool var ref

	// Casts & member access
	OP_DynamicCast        = 0x2E, // variable: class + inner expr
	// 0x2F–0x34...
	OP_StructMember       = 0x35, // int32 propRef + int32 structRef + uint8 copy + uint8 mod + inner
	// 0x36–0x37...
	OP_PrimitiveCast      = 0x38, // uint8 castKind + inner expr
	// Cast kinds (after 0x38):
	PRIMITIVE_CAST_INT_TO_FLOAT    = 0x3F,
	PRIMITIVE_CAST_FLOAT_TO_INT    = 0x44,
	PRIMITIVE_CAST_INT_TO_STRING   = 0x53,
	PRIMITIVE_CAST_FLOAT_TO_STRING = 0x55,

	// Delegate
	OP_DelegateFunction   = 0x42, // uint8 + int32 prop + FName + params + EndFP

	// End of script
	OP_EndOfScript        = 0x53, // (none) — terminal marker

	// Math / native ops (stack-based: no explicit operands, consume from stack)
	OP_NotEqual_ObjectObject = 0x77,
	OP_Not_PreBool           = 0x81,
	OP_And_BoolBool          = 0x82, // bool expr + skip(uint16) + bool expr + EndFP
	OP_Multiply_FloatFloat   = 0xAB,
	OP_Divide_FloatFloat     = 0xAC,
	OP_Add_FloatFloat        = 0xAE,
	OP_Subtract_FloatFloat   = 0xAF,
	OP_Greater_FloatFloat    = 0xB1,
	OP_MultiplyEqual_FloatFloat = 0xB6,
	OP_Tan                   = 0xBD,
	OP_Atan                  = 0xBE,
	OP_FMin                  = 0xF4,
	OP_FMax                  = 0xF5,

	// Native function indices (used with OP_FinalFunction)
	NATIVE_ConcatStrStr      = 0x70,
};

// ---- Operand size calculation ----

// Returns the number of bytes to skip after the opcode to reach the next
// opcode boundary.  Returns -1 for variable-length operands (Context,
// DynamicCast, StructMember, PrimitiveCast, DelegateFunction, And_BoolBool)
// where the caller must do deeper inspection.
//
// Does NOT account for the expression-final EndFunctionParms (0x16) that
// terminates many expressions — the caller is responsible for tracking
// expression boundaries.

inline int OperandSize(uint8_t op) {
	switch (op) {
	// No operand
	case OP_Return:           return 0;
	case OP_Nothing:          return 0;
	case OP_IntOne:           return 0;
	case OP_Let:              return 0;
	case OP_LetBool:          return 0;
	case OP_EndFunctionParms: return 0;
	case OP_Self:             return 0;
	case OP_True_0x27:        return 0;
	case OP_False_0x28:       return 0;
	case OP_NoObject:         return 0;
	case OP_EndOfScript:      return 0;
	// Math ops — stack-based, no inline operands
	case OP_NotEqual_ObjectObject: return 0;
	case OP_Not_PreBool:           return 0;
	case OP_Multiply_FloatFloat:   return 0;
	case OP_Divide_FloatFloat:     return 0;
	case OP_Add_FloatFloat:        return 0;
	case OP_Subtract_FloatFloat:   return 0;
	case OP_Greater_FloatFloat:    return 0;
	case OP_MultiplyEqual_FloatFloat: return 0;
	case OP_Tan:                return 0;
	case OP_Atan:               return 0;
	case OP_FMin:               return 0;
	case OP_FMax:               return 0;

	// Fixed-size operands
	case OP_LocalVariable:    return 4; // int32
	case OP_InstanceVariable: return 4; // int32
	case OP_Jump:             return 2; // uint16
	case OP_JumpIfNot:        return 2; // uint16
	case OP_IntConst:         return 4; // int32
	case OP_EatReturnValue:   return 4; // int32
	case OP_Skip:             return 2; // uint16
	case OP_VirtualFunction:  return 8; // FName
	case OP_FinalFunction:    return 8; // FName
	case OP_FloatConst_0x1E:  return 4; // float32
	case OP_ObjectConst:      return 4; // int32
	case OP_NameConst:        return 8; // FName
	case OP_ByteConst:        return 1; // uint8
	case OP_IntConstByte:     return 1; // uint8
	case OP_BoolVariable:     return 4; // int32

	// String — read until null terminator
	case OP_StringConst:      return -2; // special: nul-terminated

	// Variable-length: see deeper parsing
	case OP_Context:          return -1; // obj expr + u16 + u16 + inner
	case OP_DynamicCast:      return -1; // class ref + inner
	case OP_StructMember:     return -1; // i32 + i32 + u8 + u8 + inner
	case OP_PrimitiveCast:    return -1; // u8 castKind + inner
	case OP_DelegateFunction: return -1; // u8 + i32 + FName + params + EndFP
	case OP_And_BoolBool:     return -1; // bool expr + Skip(u16) + bool expr + EndFP

	// Native calls via FinalFunction
	case NATIVE_ConcatStrStr: return 0; // treated as a math-style op
	// Float const (standard UE3: 0x0D)
	case OP_FloatConst:       return 4; // float32

	default:                   return -1; // unknown
	}
}

// Get a human-readable name for an opcode.
const char *OpcodeName(uint8_t op);

} // namespace Bytecode
} // namespace UePatcher
