// ue3_opcodes.cpp — Opcode name lookup.
#include "ue3_opcodes.h"

namespace UePatcher {
namespace Bytecode {

const char *OpcodeName(uint8_t op) {
	switch (op) {
	case OP_LocalVariable:       return "LocalVariable";
	case OP_InstanceVariable:    return "InstanceVariable";
	case OP_Return:              return "Return";
	case OP_Jump:                return "Jump";
	case OP_JumpIfNot:           return "JumpIfNot";
	case OP_IntConst:            return "IntConst";
	case OP_Nothing:             return "Nothing";
	case OP_IntOne:              return "IntOne";
	case OP_FloatConst:          return "FloatConst";
	case OP_EatReturnValue:      return "EatReturnValue";
	case OP_Let:                 return "Let";
	case OP_LetBool:             return "LetBool";
	case OP_EndFunctionParms:    return "EndFunctionParms";
	case OP_Self:                return "Self";
	case OP_Skip:                return "Skip";
	case OP_Context:             return "Context";
	case OP_True_0x27:           return "True";
	case OP_False_0x28:          return "False";
	case OP_NoObject:            return "NoObject";
	case OP_VirtualFunction:     return "VirtualFunction";
	case OP_FinalFunction:       return "FinalFunction";
	case OP_FloatConst_0x1E:     return "FloatConst";
	case OP_StringConst:         return "StringConst";
	case OP_ObjectConst:         return "ObjectConst";
	case OP_NameConst:           return "NameConst";
	case OP_ByteConst:           return "ByteConst";
	case OP_IntConstByte:        return "IntConstByte";
	case OP_BoolVariable:        return "BoolVariable";
	case OP_DynamicCast:         return "DynamicCast";
	case OP_StructMember:        return "StructMember";
	case OP_PrimitiveCast:       return "PrimitiveCast";
	case OP_DelegateFunction:    return "DelegateFunction";
	case OP_EndOfScript:         return "EndOfScript";
	case OP_NotEqual_ObjectObject: return "NotEqual_ObjectObject";
	case OP_Not_PreBool:         return "Not_PreBool";
	case OP_And_BoolBool:        return "And_BoolBool";
	case OP_Multiply_FloatFloat: return "Multiply_FloatFloat";
	case OP_Divide_FloatFloat:   return "Divide_FloatFloat";
	case OP_Add_FloatFloat:      return "Add_FloatFloat";
	case OP_Subtract_FloatFloat: return "Subtract_FloatFloat";
	case OP_Greater_FloatFloat:  return "Greater_FloatFloat";
	case OP_MultiplyEqual_FloatFloat: return "MultiplyEqual_FloatFloat";
	case OP_Tan:                 return "Tan";
	case OP_Atan:                return "Atan";
	case OP_FMin:                return "FMin";
	case OP_FMax:                return "FMax";
	default:                     return "Unknown";
	}
}

} // namespace Bytecode
} // namespace UePatcher
