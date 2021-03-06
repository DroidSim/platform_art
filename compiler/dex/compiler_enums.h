/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_DEX_COMPILER_ENUMS_H_
#define ART_COMPILER_DEX_COMPILER_ENUMS_H_

#include "dex_instruction.h"

namespace art {

enum RegisterClass {
  kCoreReg,
  kFPReg,
  kAnyReg,
};

enum BitsUsed {
  kSize32Bits,
  kSize64Bits,
  kSize128Bits,
  kSize256Bits,
  kSize512Bits,
  kSize1024Bits,
};

enum SpecialTargetRegister {
  kSelf,            // Thread pointer.
  kSuspend,         // Used to reduce suspend checks for some targets.
  kLr,
  kPc,
  kSp,
  kArg0,
  kArg1,
  kArg2,
  kArg3,
  kFArg0,
  kFArg1,
  kFArg2,
  kFArg3,
  kRet0,
  kRet1,
  kInvokeTgt,
  kHiddenArg,
  kHiddenFpArg,
  kCount
};

enum RegLocationType {
  kLocDalvikFrame = 0,  // Normal Dalvik register
  kLocPhysReg,
  kLocCompilerTemp,
  kLocInvalid
};

enum BBType {
  kNullBlock,
  kEntryBlock,
  kDalvikByteCode,
  kExitBlock,
  kExceptionHandling,
  kDead,
};

/*
 * Def/Use encoding in 64-bit use_mask/def_mask.  Low positions used for target-specific
 * registers (and typically use the register number as the position).  High positions
 * reserved for common and abstract resources.
 */

enum ResourceEncodingPos {
  kMustNotAlias = 63,
  kHeapRef = 62,          // Default memory reference type.
  kLiteral = 61,          // Literal pool memory reference.
  kDalvikReg = 60,        // Dalvik v_reg memory reference.
  kFPStatus = 59,
  kCCode = 58,
  kLowestCommonResource = kCCode
};

// Shared pseudo opcodes - must be < 0.
enum LIRPseudoOpcode {
  kPseudoExportedPC = -16,
  kPseudoSafepointPC = -15,
  kPseudoIntrinsicRetry = -14,
  kPseudoSuspendTarget = -13,
  kPseudoThrowTarget = -12,
  kPseudoCaseLabel = -11,
  kPseudoMethodEntry = -10,
  kPseudoMethodExit = -9,
  kPseudoBarrier = -8,
  kPseudoEntryBlock = -7,
  kPseudoExitBlock = -6,
  kPseudoTargetLabel = -5,
  kPseudoDalvikByteCodeBoundary = -4,
  kPseudoPseudoAlign4 = -3,
  kPseudoEHBlockLabel = -2,
  kPseudoNormalBlockLabel = -1,
};

enum ExtendedMIROpcode {
  kMirOpFirst = kNumPackedOpcodes,
  kMirOpPhi = kMirOpFirst,
  kMirOpCopy,
  kMirOpFusedCmplFloat,
  kMirOpFusedCmpgFloat,
  kMirOpFusedCmplDouble,
  kMirOpFusedCmpgDouble,
  kMirOpFusedCmpLong,
  kMirOpNop,
  kMirOpNullCheck,
  kMirOpRangeCheck,
  kMirOpDivZeroCheck,
  kMirOpCheck,
  kMirOpCheckPart2,
  kMirOpSelect,

  // Vector opcodes:
  // TypeSize is an encoded field giving the element type and the vector size.
  // It is encoded as OpSize << 16 | (number of bits in vector)
  //
  // Destination and source are integers that will be interpreted by the
  // backend that supports Vector operations.  Backends are permitted to support only
  // certain vector register sizes.
  //
  // At this point, only two operand instructions are supported.  Three operand instructions
  // could be supported by using a bit in TypeSize and arg[0] where needed.

  // @brief MIR to move constant data to a vector register
  // vA: number of bits in register
  // vB: destination
  // args[0]~args[3]: up to 128 bits of data for initialization
  kMirOpConstVector,

  // @brief MIR to move a vectorized register to another
  // vA: TypeSize
  // vB: destination
  // vC: source
  kMirOpMoveVector,

  // @brief Packed multiply of units in two vector registers: vB = vB .* vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: source
  kMirOpPackedMultiply,

  // @brief Packed addition of units in two vector registers: vB = vB .+ vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: source
  kMirOpPackedAddition,

  // @brief Packed subtraction of units in two vector registers: vB = vB .- vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: source
  kMirOpPackedSubtract,

  // @brief Packed shift left of units in two vector registers: vB = vB .<< vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: immediate
  kMirOpPackedShiftLeft,

  // @brief Packed signed shift right of units in two vector registers: vB = vB .>> vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: immediate
  kMirOpPackedSignedShiftRight,

  // @brief Packed unsigned shift right of units in two vector registers: vB = vB .>>> vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: immediate
  kMirOpPackedUnsignedShiftRight,

  // @brief Packed bitwise and of units in two vector registers: vB = vB .& vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: source
  kMirOpPackedAnd,

  // @brief Packed bitwise or of units in two vector registers: vB = vB .| vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: source
  kMirOpPackedOr,

  // @brief Packed bitwise xor of units in two vector registers: vB = vB .^ vC using vA to know the type of the vector.
  // vA: TypeSize
  // vB: destination and source
  // vC: source
  kMirOpPackedXor,

  // @brief Reduce a 128-bit packed element into a single VR by taking lower bits
  // @details Instruction does a horizontal addition of the packed elements and then adds it to VR
  // vA: TypeSize
  // vB: destination and source VR (not vector register)
  // vC: source (vector register)
  kMirOpPackedAddReduce,

  // @brief Extract a packed element into a single VR.
  // vA: TypeSize
  // vB: destination VR (not vector register)
  // vC: source (vector register)
  // arg[0]: The index to use for extraction from vector register (which packed element)
  kMirOpPackedReduce,

  // @brief Create a vector value, with all TypeSize values equal to vC
  // vA: TypeSize
  // vB: destination vector register
  // vC: source VR (not vector register)
  kMirOpPackedSet,

  kMirOpLast,
};

enum MIROptimizationFlagPositions {
  kMIRIgnoreNullCheck = 0,
  kMIRNullCheckOnly,
  kMIRIgnoreRangeCheck,
  kMIRRangeCheckOnly,
  kMIRIgnoreClInitCheck,
  kMIRInlined,                        // Invoke is inlined (ie dead).
  kMIRInlinedPred,                    // Invoke is inlined via prediction.
  kMIRCallee,                         // Instruction is inlined from callee.
  kMIRIgnoreSuspendCheck,
  kMIRDup,
  kMIRMark,                           // Temporary node mark.
  kMIRLastMIRFlag,
};

// For successor_block_list.
enum BlockListType {
  kNotUsed = 0,
  kCatch,
  kPackedSwitch,
  kSparseSwitch,
};

enum AssemblerStatus {
  kSuccess,
  kRetryAll,
};

enum OpSize {
  kWord,            // Natural word size of target (32/64).
  k32,
  k64,
  kReference,       // Object reference; compressed on 64-bit targets.
  kSingle,
  kDouble,
  kUnsignedHalf,
  kSignedHalf,
  kUnsignedByte,
  kSignedByte,
};

std::ostream& operator<<(std::ostream& os, const OpSize& kind);

enum OpKind {
  kOpMov,
  kOpCmov,
  kOpMvn,
  kOpCmp,
  kOpLsl,
  kOpLsr,
  kOpAsr,
  kOpRor,
  kOpNot,
  kOpAnd,
  kOpOr,
  kOpXor,
  kOpNeg,
  kOpAdd,
  kOpAdc,
  kOpSub,
  kOpSbc,
  kOpRsub,
  kOpMul,
  kOpDiv,
  kOpRem,
  kOpBic,
  kOpCmn,
  kOpTst,
  kOpRev,
  kOpRevsh,
  kOpBkpt,
  kOpBlx,
  kOpPush,
  kOpPop,
  kOp2Char,
  kOp2Short,
  kOp2Byte,
  kOpCondBr,
  kOpUncondBr,
  kOpBx,
  kOpInvalid,
};

enum MoveType {
  kMov8GP,      // Move 8-bit general purpose register.
  kMov16GP,     // Move 16-bit general purpose register.
  kMov32GP,     // Move 32-bit general purpose register.
  kMov64GP,     // Move 64-bit general purpose register.
  kMov32FP,     // Move 32-bit FP register.
  kMov64FP,     // Move 64-bit FP register.
  kMovLo64FP,   // Move low 32-bits of 64-bit FP register.
  kMovHi64FP,   // Move high 32-bits of 64-bit FP register.
  kMovU128FP,   // Move 128-bit FP register to/from possibly unaligned region.
  kMov128FP = kMovU128FP,
  kMovA128FP,   // Move 128-bit FP register to/from region surely aligned to 16-bytes.
  kMovLo128FP,  // Move low 64-bits of 128-bit FP register.
  kMovHi128FP,  // Move high 64-bits of 128-bit FP register.
};

std::ostream& operator<<(std::ostream& os, const OpKind& kind);

enum ConditionCode {
  kCondEq,  // equal
  kCondNe,  // not equal
  kCondCs,  // carry set
  kCondCc,  // carry clear
  kCondUlt,  // unsigned less than
  kCondUge,  // unsigned greater than or same
  kCondMi,  // minus
  kCondPl,  // plus, positive or zero
  kCondVs,  // overflow
  kCondVc,  // no overflow
  kCondHi,  // unsigned greater than
  kCondLs,  // unsigned lower or same
  kCondGe,  // signed greater than or equal
  kCondLt,  // signed less than
  kCondGt,  // signed greater than
  kCondLe,  // signed less than or equal
  kCondAl,  // always
  kCondNv,  // never
};

std::ostream& operator<<(std::ostream& os, const ConditionCode& kind);

// Target specific condition encodings
enum ArmConditionCode {
  kArmCondEq = 0x0,  // 0000
  kArmCondNe = 0x1,  // 0001
  kArmCondCs = 0x2,  // 0010
  kArmCondCc = 0x3,  // 0011
  kArmCondMi = 0x4,  // 0100
  kArmCondPl = 0x5,  // 0101
  kArmCondVs = 0x6,  // 0110
  kArmCondVc = 0x7,  // 0111
  kArmCondHi = 0x8,  // 1000
  kArmCondLs = 0x9,  // 1001
  kArmCondGe = 0xa,  // 1010
  kArmCondLt = 0xb,  // 1011
  kArmCondGt = 0xc,  // 1100
  kArmCondLe = 0xd,  // 1101
  kArmCondAl = 0xe,  // 1110
  kArmCondNv = 0xf,  // 1111
};

std::ostream& operator<<(std::ostream& os, const ArmConditionCode& kind);

enum X86ConditionCode {
  kX86CondO   = 0x0,    // overflow
  kX86CondNo  = 0x1,    // not overflow

  kX86CondB   = 0x2,    // below
  kX86CondNae = kX86CondB,  // not-above-equal
  kX86CondC   = kX86CondB,  // carry

  kX86CondNb  = 0x3,    // not-below
  kX86CondAe  = kX86CondNb,  // above-equal
  kX86CondNc  = kX86CondNb,  // not-carry

  kX86CondZ   = 0x4,    // zero
  kX86CondEq  = kX86CondZ,  // equal

  kX86CondNz  = 0x5,    // not-zero
  kX86CondNe  = kX86CondNz,  // not-equal

  kX86CondBe  = 0x6,    // below-equal
  kX86CondNa  = kX86CondBe,  // not-above

  kX86CondNbe = 0x7,    // not-below-equal
  kX86CondA   = kX86CondNbe,  // above

  kX86CondS   = 0x8,    // sign
  kX86CondNs  = 0x9,    // not-sign

  kX86CondP   = 0xa,    // 8-bit parity even
  kX86CondPE  = kX86CondP,

  kX86CondNp  = 0xb,    // 8-bit parity odd
  kX86CondPo  = kX86CondNp,

  kX86CondL   = 0xc,    // less-than
  kX86CondNge = kX86CondL,  // not-greater-equal

  kX86CondNl  = 0xd,    // not-less-than
  kX86CondGe  = kX86CondNl,  // not-greater-equal

  kX86CondLe  = 0xe,    // less-than-equal
  kX86CondNg  = kX86CondLe,  // not-greater

  kX86CondNle = 0xf,    // not-less-than
  kX86CondG   = kX86CondNle,  // greater
};

std::ostream& operator<<(std::ostream& os, const X86ConditionCode& kind);

enum ThrowKind {
  kThrowNoSuchMethod,
};

enum DividePattern {
  DivideNone,
  Divide3,
  Divide5,
  Divide7,
};

std::ostream& operator<<(std::ostream& os, const DividePattern& pattern);

/**
 * @brief Memory barrier types (see "The JSR-133 Cookbook for Compiler Writers").
 * @details Without context sensitive analysis, the most conservative set of barriers
 * must be issued to ensure the Java Memory Model. Thus the recipe is as follows:
 * -# Use StoreStore barrier before volatile store.
 * -# Use StoreLoad barrier after volatile store.
 * -# Use LoadLoad and LoadStore barrier after each volatile load.
 * -# Use StoreStore barrier after all stores but before return from any constructor whose
 * class has final fields.
 */
enum MemBarrierKind {
  kLoadStore,
  kLoadLoad,
  kStoreStore,
  kStoreLoad
};

std::ostream& operator<<(std::ostream& os, const MemBarrierKind& kind);

enum OpFeatureFlags {
  kIsBranch = 0,
  kNoOperand,
  kIsUnaryOp,
  kIsBinaryOp,
  kIsTertiaryOp,
  kIsQuadOp,
  kIsQuinOp,
  kIsSextupleOp,
  kIsIT,
  kMemLoad,
  kMemStore,
  kPCRelFixup,  // x86 FIXME: add NEEDS_FIXUP to instruction attributes.
  kRegDef0,
  kRegDef1,
  kRegDef2,
  kRegDefA,
  kRegDefD,
  kRegDefFPCSList0,
  kRegDefFPCSList2,
  kRegDefList0,
  kRegDefList1,
  kRegDefList2,
  kRegDefLR,
  kRegDefSP,
  kRegUse0,
  kRegUse1,
  kRegUse2,
  kRegUse3,
  kRegUse4,
  kRegUseA,
  kRegUseC,
  kRegUseD,
  kRegUseB,
  kRegUseFPCSList0,
  kRegUseFPCSList2,
  kRegUseList0,
  kRegUseList1,
  kRegUseLR,
  kRegUsePC,
  kRegUseSP,
  kSetsCCodes,
  kUsesCCodes,
  kUseFpStack,
  kUseHi,
  kUseLo,
  kDefHi,
  kDefLo
};

enum SelectInstructionKind {
  kSelectNone,
  kSelectConst,
  kSelectMove,
  kSelectGoto
};

std::ostream& operator<<(std::ostream& os, const SelectInstructionKind& kind);

// LIR fixup kinds for Arm
enum FixupKind {
  kFixupNone,
  kFixupLabel,       // For labels we just adjust the offset.
  kFixupLoad,        // Mostly for immediates.
  kFixupVLoad,       // FP load which *may* be pc-relative.
  kFixupCBxZ,        // Cbz, Cbnz.
  kFixupPushPop,     // Not really pc relative, but changes size based on args.
  kFixupCondBranch,  // Conditional branch
  kFixupT1Branch,    // Thumb1 Unconditional branch
  kFixupT2Branch,    // Thumb2 Unconditional branch
  kFixupBlx1,        // Blx1 (start of Blx1/Blx2 pair).
  kFixupBl1,         // Bl1 (start of Bl1/Bl2 pair).
  kFixupAdr,         // Adr.
  kFixupMovImmLST,   // kThumb2MovImm16LST.
  kFixupMovImmHST,   // kThumb2MovImm16HST.
  kFixupAlign4,      // Align to 4-byte boundary.
};

std::ostream& operator<<(std::ostream& os, const FixupKind& kind);

}  // namespace art

#endif  // ART_COMPILER_DEX_COMPILER_ENUMS_H_
