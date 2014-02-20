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

#include "codegen_x86.h"
#include "dex/compiler_internals.h"
#include "dex/quick/mir_to_lir-inl.h"
#include "x86_lir.h"

#include <string>

namespace art {

// FIXME: restore "static" when usage uncovered
/*static*/ int core_regs[] = {
  rAX, rCX, rDX, rBX, rX86_SP, rBP, rSI, rDI
#ifdef TARGET_REX_SUPPORT
  r8, r9, r10, r11, r12, r13, r14, 15
#endif
};
/*static*/ int ReservedRegs[] = {rX86_SP};
/*static*/ int core_temps[] = {rAX, rCX, rDX, rBX};
/*static*/ int FpRegs[] = {
  fr0, fr1, fr2, fr3, fr4, fr5, fr6, fr7,
#ifdef TARGET_REX_SUPPORT
  fr8, fr9, fr10, fr11, fr12, fr13, fr14, fr15
#endif
};
/*static*/ int fp_temps[] = {
  fr0, fr1, fr2, fr3, fr4, fr5, fr6, fr7,
#ifdef TARGET_REX_SUPPORT
  fr8, fr9, fr10, fr11, fr12, fr13, fr14, fr15
#endif
};

RegLocation X86Mir2Lir::LocCReturn() {
  return x86_loc_c_return;
}

RegLocation X86Mir2Lir::LocCReturnWide() {
  return x86_loc_c_return_wide;
}

RegLocation X86Mir2Lir::LocCReturnFloat() {
  return x86_loc_c_return_float;
}

RegLocation X86Mir2Lir::LocCReturnDouble() {
  return x86_loc_c_return_double;
}

// Return a target-dependent special register.
int X86Mir2Lir::TargetReg(SpecialTargetRegister reg) {
  int res = INVALID_REG;
  switch (reg) {
    case kSelf: res = rX86_SELF; break;
    case kSuspend: res =  rX86_SUSPEND; break;
    case kLr: res =  rX86_LR; break;
    case kPc: res =  rX86_PC; break;
    case kSp: res =  rX86_SP; break;
    case kArg0: res = rX86_ARG0; break;
    case kArg1: res = rX86_ARG1; break;
    case kArg2: res = rX86_ARG2; break;
    case kArg3: res = rX86_ARG3; break;
    case kFArg0: res = rX86_FARG0; break;
    case kFArg1: res = rX86_FARG1; break;
    case kFArg2: res = rX86_FARG2; break;
    case kFArg3: res = rX86_FARG3; break;
    case kRet0: res = rX86_RET0; break;
    case kRet1: res = rX86_RET1; break;
    case kInvokeTgt: res = rX86_INVOKE_TGT; break;
    case kHiddenArg: res = rAX; break;
    case kHiddenFpArg: res = fr0; break;
    case kCount: res = rX86_COUNT; break;
  }
  return res;
}

int X86Mir2Lir::GetArgMappingToPhysicalReg(int arg_num) {
  // For the 32-bit internal ABI, the first 3 arguments are passed in registers.
  // TODO: This is not 64-bit compliant and depends on new internal ABI.
  switch (arg_num) {
    case 0:
      return rX86_ARG1;
    case 1:
      return rX86_ARG2;
    case 2:
      return rX86_ARG3;
    default:
      return INVALID_REG;
  }
}

// Create a double from a pair of singles.
int X86Mir2Lir::S2d(int low_reg, int high_reg) {
  return X86_S2D(low_reg, high_reg);
}

// Return mask to strip off fp reg flags and bias.
uint32_t X86Mir2Lir::FpRegMask() {
  return X86_FP_REG_MASK;
}

// True if both regs single, both core or both double.
bool X86Mir2Lir::SameRegType(int reg1, int reg2) {
  return (X86_REGTYPE(reg1) == X86_REGTYPE(reg2));
}

/*
 * Decode the register id.
 */
uint64_t X86Mir2Lir::GetRegMaskCommon(int reg) {
  uint64_t seed;
  int shift;
  int reg_id;

  reg_id = reg & 0xf;
  /* Double registers in x86 are just a single FP register */
  seed = 1;
  /* FP register starts at bit position 16 */
  shift = X86_FPREG(reg) ? kX86FPReg0 : 0;
  /* Expand the double register id into single offset */
  shift += reg_id;
  return (seed << shift);
}

uint64_t X86Mir2Lir::GetPCUseDefEncoding() {
  /*
   * FIXME: might make sense to use a virtual resource encoding bit for pc.  Might be
   * able to clean up some of the x86/Arm_Mips differences
   */
  LOG(FATAL) << "Unexpected call to GetPCUseDefEncoding for x86";
  return 0ULL;
}

void X86Mir2Lir::SetupTargetResourceMasks(LIR* lir, uint64_t flags) {
  DCHECK_EQ(cu_->instruction_set, kX86);
  DCHECK(!lir->flags.use_def_invalid);

  // X86-specific resource map setup here.
  if (flags & REG_USE_SP) {
    lir->u.m.use_mask |= ENCODE_X86_REG_SP;
  }

  if (flags & REG_DEF_SP) {
    lir->u.m.def_mask |= ENCODE_X86_REG_SP;
  }

  if (flags & REG_DEFA) {
    SetupRegMask(&lir->u.m.def_mask, rAX);
  }

  if (flags & REG_DEFD) {
    SetupRegMask(&lir->u.m.def_mask, rDX);
  }
  if (flags & REG_USEA) {
    SetupRegMask(&lir->u.m.use_mask, rAX);
  }

  if (flags & REG_USEC) {
    SetupRegMask(&lir->u.m.use_mask, rCX);
  }

  if (flags & REG_USED) {
    SetupRegMask(&lir->u.m.use_mask, rDX);
  }

  if (flags & REG_USEB) {
    SetupRegMask(&lir->u.m.use_mask, rBX);
  }
}

/* For dumping instructions */
static const char* x86RegName[] = {
  "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static const char* x86CondName[] = {
  "O",
  "NO",
  "B/NAE/C",
  "NB/AE/NC",
  "Z/EQ",
  "NZ/NE",
  "BE/NA",
  "NBE/A",
  "S",
  "NS",
  "P/PE",
  "NP/PO",
  "L/NGE",
  "NL/GE",
  "LE/NG",
  "NLE/G"
};

/*
 * Interpret a format string and build a string no longer than size
 * See format key in Assemble.cc.
 */
std::string X86Mir2Lir::BuildInsnString(const char *fmt, LIR *lir, unsigned char* base_addr) {
  std::string buf;
  size_t i = 0;
  size_t fmt_len = strlen(fmt);
  while (i < fmt_len) {
    if (fmt[i] != '!') {
      buf += fmt[i];
      i++;
    } else {
      i++;
      DCHECK_LT(i, fmt_len);
      char operand_number_ch = fmt[i];
      i++;
      if (operand_number_ch == '!') {
        buf += "!";
      } else {
        int operand_number = operand_number_ch - '0';
        DCHECK_LT(operand_number, 6);  // Expect upto 6 LIR operands.
        DCHECK_LT(i, fmt_len);
        int operand = lir->operands[operand_number];
        switch (fmt[i]) {
          case 'c':
            DCHECK_LT(static_cast<size_t>(operand), sizeof(x86CondName));
            buf += x86CondName[operand];
            break;
          case 'd':
            buf += StringPrintf("%d", operand);
            break;
          case 'p': {
            EmbeddedData *tab_rec = reinterpret_cast<EmbeddedData*>(UnwrapPointer(operand));
            buf += StringPrintf("0x%08x", tab_rec->offset);
            break;
          }
          case 'r':
            if (X86_FPREG(operand) || X86_DOUBLEREG(operand)) {
              int fp_reg = operand & X86_FP_REG_MASK;
              buf += StringPrintf("xmm%d", fp_reg);
            } else {
              DCHECK_LT(static_cast<size_t>(operand), sizeof(x86RegName));
              buf += x86RegName[operand];
            }
            break;
          case 't':
            buf += StringPrintf("0x%08" PRIxPTR " (L%p)",
                                reinterpret_cast<uintptr_t>(base_addr) + lir->offset + operand,
                                lir->target);
            break;
          default:
            buf += StringPrintf("DecodeError '%c'", fmt[i]);
            break;
        }
        i++;
      }
    }
  }
  return buf;
}

void X86Mir2Lir::DumpResourceMask(LIR *x86LIR, uint64_t mask, const char *prefix) {
  char buf[256];
  buf[0] = 0;

  if (mask == ENCODE_ALL) {
    strcpy(buf, "all");
  } else {
    char num[8];
    int i;

    for (i = 0; i < kX86RegEnd; i++) {
      if (mask & (1ULL << i)) {
        snprintf(num, arraysize(num), "%d ", i);
        strcat(buf, num);
      }
    }

    if (mask & ENCODE_CCODE) {
      strcat(buf, "cc ");
    }
    /* Memory bits */
    if (x86LIR && (mask & ENCODE_DALVIK_REG)) {
      snprintf(buf + strlen(buf), arraysize(buf) - strlen(buf), "dr%d%s",
               DECODE_ALIAS_INFO_REG(x86LIR->flags.alias_info),
               (DECODE_ALIAS_INFO_WIDE(x86LIR->flags.alias_info)) ? "(+1)" : "");
    }
    if (mask & ENCODE_LITERAL) {
      strcat(buf, "lit ");
    }

    if (mask & ENCODE_HEAP_REF) {
      strcat(buf, "heap ");
    }
    if (mask & ENCODE_MUST_NOT_ALIAS) {
      strcat(buf, "noalias ");
    }
  }
  if (buf[0]) {
    LOG(INFO) << prefix << ": " <<  buf;
  }
}

void X86Mir2Lir::AdjustSpillMask() {
  // Adjustment for LR spilling, x86 has no LR so nothing to do here
  core_spill_mask_ |= (1 << rRET);
  num_core_spills_++;
}

/*
 * Mark a callee-save fp register as promoted.  Note that
 * vpush/vpop uses contiguous register lists so we must
 * include any holes in the mask.  Associate holes with
 * Dalvik register INVALID_VREG (0xFFFFU).
 */
void X86Mir2Lir::MarkPreservedSingle(int v_reg, int reg) {
  UNIMPLEMENTED(WARNING) << "MarkPreservedSingle";
#if 0
  LOG(FATAL) << "No support yet for promoted FP regs";
#endif
}

void X86Mir2Lir::FlushRegWide(int reg1, int reg2) {
  RegisterInfo* info1 = GetRegInfo(reg1);
  RegisterInfo* info2 = GetRegInfo(reg2);
  DCHECK(info1 && info2 && info1->pair && info2->pair &&
         (info1->partner == info2->reg) &&
         (info2->partner == info1->reg));
  if ((info1->live && info1->dirty) || (info2->live && info2->dirty)) {
    if (!(info1->is_temp && info2->is_temp)) {
      /* Should not happen.  If it does, there's a problem in eval_loc */
      LOG(FATAL) << "Long half-temp, half-promoted";
    }

    info1->dirty = false;
    info2->dirty = false;
    if (mir_graph_->SRegToVReg(info2->s_reg) < mir_graph_->SRegToVReg(info1->s_reg))
      info1 = info2;
    int v_reg = mir_graph_->SRegToVReg(info1->s_reg);
    StoreBaseDispWide(rX86_SP, VRegOffset(v_reg), info1->reg, info1->partner);
  }
}

void X86Mir2Lir::FlushReg(int reg) {
  RegisterInfo* info = GetRegInfo(reg);
  if (info->live && info->dirty) {
    info->dirty = false;
    int v_reg = mir_graph_->SRegToVReg(info->s_reg);
    StoreBaseDisp(rX86_SP, VRegOffset(v_reg), reg, kWord);
  }
}

/* Give access to the target-dependent FP register encoding to common code */
bool X86Mir2Lir::IsFpReg(int reg) {
  return X86_FPREG(reg);
}

/* Clobber all regs that might be used by an external C call */
void X86Mir2Lir::ClobberCallerSave() {
  Clobber(rAX);
  Clobber(rCX);
  Clobber(rDX);
  Clobber(rBX);
}

RegLocation X86Mir2Lir::GetReturnWideAlt() {
  RegLocation res = LocCReturnWide();
  CHECK(res.reg.GetReg() == rAX);
  CHECK(res.reg.GetHighReg() == rDX);
  Clobber(rAX);
  Clobber(rDX);
  MarkInUse(rAX);
  MarkInUse(rDX);
  MarkPair(res.reg.GetReg(), res.reg.GetHighReg());
  return res;
}

RegLocation X86Mir2Lir::GetReturnAlt() {
  RegLocation res = LocCReturn();
  res.reg.SetReg(rDX);
  Clobber(rDX);
  MarkInUse(rDX);
  return res;
}

/* To be used when explicitly managing register use */
void X86Mir2Lir::LockCallTemps() {
  LockTemp(rX86_ARG0);
  LockTemp(rX86_ARG1);
  LockTemp(rX86_ARG2);
  LockTemp(rX86_ARG3);
}

/* To be used when explicitly managing register use */
void X86Mir2Lir::FreeCallTemps() {
  FreeTemp(rX86_ARG0);
  FreeTemp(rX86_ARG1);
  FreeTemp(rX86_ARG2);
  FreeTemp(rX86_ARG3);
}

void X86Mir2Lir::GenMemBarrier(MemBarrierKind barrier_kind) {
#if ANDROID_SMP != 0
  // TODO: optimize fences
  NewLIR0(kX86Mfence);
#endif
}

// Alloc a pair of core registers, or a double.
RegStorage X86Mir2Lir::AllocTypedTempWide(bool fp_hint, int reg_class) {
  int high_reg;
  int low_reg;

  if (((reg_class == kAnyReg) && fp_hint) || (reg_class == kFPReg)) {
    low_reg = AllocTempDouble();
    high_reg = low_reg;  // only one allocated!
    // TODO: take advantage of 64-bit notation.
    return RegStorage(RegStorage::k64BitPair, low_reg, high_reg);
  }
  low_reg = AllocTemp();
  high_reg = AllocTemp();
  return RegStorage(RegStorage::k64BitPair, low_reg, high_reg);
}

int X86Mir2Lir::AllocTypedTemp(bool fp_hint, int reg_class) {
  if (((reg_class == kAnyReg) && fp_hint) || (reg_class == kFPReg)) {
    return AllocTempFloat();
  }
  return AllocTemp();
}

void X86Mir2Lir::CompilerInitializeRegAlloc() {
  int num_regs = sizeof(core_regs)/sizeof(*core_regs);
  int num_reserved = sizeof(ReservedRegs)/sizeof(*ReservedRegs);
  int num_temps = sizeof(core_temps)/sizeof(*core_temps);
  int num_fp_regs = sizeof(FpRegs)/sizeof(*FpRegs);
  int num_fp_temps = sizeof(fp_temps)/sizeof(*fp_temps);
  reg_pool_ = static_cast<RegisterPool*>(arena_->Alloc(sizeof(*reg_pool_),
                                                       ArenaAllocator::kAllocRegAlloc));
  reg_pool_->num_core_regs = num_regs;
  reg_pool_->core_regs =
      static_cast<RegisterInfo*>(arena_->Alloc(num_regs * sizeof(*reg_pool_->core_regs),
                                               ArenaAllocator::kAllocRegAlloc));
  reg_pool_->num_fp_regs = num_fp_regs;
  reg_pool_->FPRegs =
      static_cast<RegisterInfo *>(arena_->Alloc(num_fp_regs * sizeof(*reg_pool_->FPRegs),
                                                ArenaAllocator::kAllocRegAlloc));
  CompilerInitPool(reg_pool_->core_regs, core_regs, reg_pool_->num_core_regs);
  CompilerInitPool(reg_pool_->FPRegs, FpRegs, reg_pool_->num_fp_regs);
  // Keep special registers from being allocated
  for (int i = 0; i < num_reserved; i++) {
    MarkInUse(ReservedRegs[i]);
  }
  // Mark temp regs - all others not in use can be used for promotion
  for (int i = 0; i < num_temps; i++) {
    MarkTemp(core_temps[i]);
  }
  for (int i = 0; i < num_fp_temps; i++) {
    MarkTemp(fp_temps[i]);
  }
}

void X86Mir2Lir::FreeRegLocTemps(RegLocation rl_keep,
                     RegLocation rl_free) {
  if ((rl_free.reg.GetReg() != rl_keep.reg.GetReg()) && (rl_free.reg.GetReg() != rl_keep.reg.GetHighReg()) &&
      (rl_free.reg.GetHighReg() != rl_keep.reg.GetReg()) && (rl_free.reg.GetHighReg() != rl_keep.reg.GetHighReg())) {
    // No overlap, free both
    FreeTemp(rl_free.reg.GetReg());
    FreeTemp(rl_free.reg.GetHighReg());
  }
}

void X86Mir2Lir::SpillCoreRegs() {
  if (num_core_spills_ == 0) {
    return;
  }
  // Spill mask not including fake return address register
  uint32_t mask = core_spill_mask_ & ~(1 << rRET);
  int offset = frame_size_ - (4 * num_core_spills_);
  for (int reg = 0; mask; mask >>= 1, reg++) {
    if (mask & 0x1) {
      StoreWordDisp(rX86_SP, offset, reg);
      offset += 4;
    }
  }
}

void X86Mir2Lir::UnSpillCoreRegs() {
  if (num_core_spills_ == 0) {
    return;
  }
  // Spill mask not including fake return address register
  uint32_t mask = core_spill_mask_ & ~(1 << rRET);
  int offset = frame_size_ - (4 * num_core_spills_);
  for (int reg = 0; mask; mask >>= 1, reg++) {
    if (mask & 0x1) {
      LoadWordDisp(rX86_SP, offset, reg);
      offset += 4;
    }
  }
}

bool X86Mir2Lir::IsUnconditionalBranch(LIR* lir) {
  return (lir->opcode == kX86Jmp8 || lir->opcode == kX86Jmp32);
}

X86Mir2Lir::X86Mir2Lir(CompilationUnit* cu, MIRGraph* mir_graph, ArenaAllocator* arena)
    : Mir2Lir(cu, mir_graph, arena),
      method_address_insns_(arena, 100, kGrowableArrayMisc),
      class_type_address_insns_(arena, 100, kGrowableArrayMisc),
      call_method_insns_(arena, 100, kGrowableArrayMisc) {
  store_method_addr_used_ = false;
  for (int i = 0; i < kX86Last; i++) {
    if (X86Mir2Lir::EncodingMap[i].opcode != i) {
      LOG(FATAL) << "Encoding order for " << X86Mir2Lir::EncodingMap[i].name
                 << " is wrong: expecting " << i << ", seeing "
                 << static_cast<int>(X86Mir2Lir::EncodingMap[i].opcode);
    }
  }
}

Mir2Lir* X86CodeGenerator(CompilationUnit* const cu, MIRGraph* const mir_graph,
                          ArenaAllocator* const arena) {
  return new X86Mir2Lir(cu, mir_graph, arena);
}

// Not used in x86
int X86Mir2Lir::LoadHelper(ThreadOffset offset) {
  LOG(FATAL) << "Unexpected use of LoadHelper in x86";
  return INVALID_REG;
}

uint64_t X86Mir2Lir::GetTargetInstFlags(int opcode) {
  DCHECK(!IsPseudoLirOp(opcode));
  return X86Mir2Lir::EncodingMap[opcode].flags;
}

const char* X86Mir2Lir::GetTargetInstName(int opcode) {
  DCHECK(!IsPseudoLirOp(opcode));
  return X86Mir2Lir::EncodingMap[opcode].name;
}

const char* X86Mir2Lir::GetTargetInstFmt(int opcode) {
  DCHECK(!IsPseudoLirOp(opcode));
  return X86Mir2Lir::EncodingMap[opcode].fmt;
}

/*
 * Return an updated location record with current in-register status.
 * If the value lives in live temps, reflect that fact.  No code
 * is generated.  If the live value is part of an older pair,
 * clobber both low and high.
 */
// TODO: Reunify with common code after 'pair mess' has been fixed
RegLocation X86Mir2Lir::UpdateLocWide(RegLocation loc) {
  DCHECK(loc.wide);
  DCHECK(CheckCorePoolSanity());
  if (loc.location != kLocPhysReg) {
    DCHECK((loc.location == kLocDalvikFrame) ||
         (loc.location == kLocCompilerTemp));
    // Are the dalvik regs already live in physical registers?
    RegisterInfo* info_lo = AllocLive(loc.s_reg_low, kAnyReg);

    // Handle FP registers specially on x86.
    if (info_lo && IsFpReg(info_lo->reg)) {
      bool match = true;

      // We can't match a FP register with a pair of Core registers.
      match = match && (info_lo->pair == 0);

      if (match) {
        // We can reuse;update the register usage info.
        loc.location = kLocPhysReg;
        loc.vec_len = kVectorLength8;
        // TODO: use k64BitVector
        loc.reg = RegStorage(RegStorage::k64BitPair, info_lo->reg, info_lo->reg);
        DCHECK(IsFpReg(loc.reg.GetReg()));
        return loc;
      }
      // We can't easily reuse; clobber and free any overlaps.
      if (info_lo) {
        Clobber(info_lo->reg);
        FreeTemp(info_lo->reg);
        if (info_lo->pair)
          Clobber(info_lo->partner);
      }
    } else {
      RegisterInfo* info_hi = AllocLive(GetSRegHi(loc.s_reg_low), kAnyReg);
      bool match = true;
      match = match && (info_lo != NULL);
      match = match && (info_hi != NULL);
      // Are they both core or both FP?
      match = match && (IsFpReg(info_lo->reg) == IsFpReg(info_hi->reg));
      // If a pair of floating point singles, are they properly aligned?
      if (match && IsFpReg(info_lo->reg)) {
        match &= ((info_lo->reg & 0x1) == 0);
        match &= ((info_hi->reg - info_lo->reg) == 1);
      }
      // If previously used as a pair, it is the same pair?
      if (match && (info_lo->pair || info_hi->pair)) {
        match = (info_lo->pair == info_hi->pair);
        match &= ((info_lo->reg == info_hi->partner) &&
              (info_hi->reg == info_lo->partner));
      }
      if (match) {
        // Can reuse - update the register usage info
        loc.reg = RegStorage(RegStorage::k64BitPair, info_lo->reg, info_hi->reg);
        loc.location = kLocPhysReg;
        MarkPair(loc.reg.GetReg(), loc.reg.GetHighReg());
        DCHECK(!IsFpReg(loc.reg.GetReg()) || ((loc.reg.GetReg() & 0x1) == 0));
        return loc;
      }
      // Can't easily reuse - clobber and free any overlaps
      if (info_lo) {
        Clobber(info_lo->reg);
        FreeTemp(info_lo->reg);
        if (info_lo->pair)
          Clobber(info_lo->partner);
      }
      if (info_hi) {
        Clobber(info_hi->reg);
        FreeTemp(info_hi->reg);
        if (info_hi->pair)
          Clobber(info_hi->partner);
      }
    }
  }
  return loc;
}

// TODO: Reunify with common code after 'pair mess' has been fixed
RegLocation X86Mir2Lir::EvalLocWide(RegLocation loc, int reg_class, bool update) {
  DCHECK(loc.wide);
  int32_t low_reg;
  int32_t high_reg;

  loc = UpdateLocWide(loc);

  /* If it is already in a register, we can assume proper form.  Is it the right reg class? */
  if (loc.location == kLocPhysReg) {
    DCHECK_EQ(IsFpReg(loc.reg.GetReg()), loc.IsVectorScalar());
    if (!RegClassMatches(reg_class, loc.reg.GetReg())) {
      /* It is the wrong register class.  Reallocate and copy. */
      if (!IsFpReg(loc.reg.GetReg())) {
        // We want this in a FP reg, and it is in core registers.
        DCHECK(reg_class != kCoreReg);
        // Allocate this into any FP reg, and mark it with the right size.
        low_reg = AllocTypedTemp(true, reg_class);
        OpVectorRegCopyWide(low_reg, loc.reg.GetReg(), loc.reg.GetHighReg());
        CopyRegInfo(low_reg, loc.reg.GetReg());
        Clobber(loc.reg.GetReg());
        Clobber(loc.reg.GetHighReg());
        loc.reg.SetReg(low_reg);
        loc.reg.SetHighReg(low_reg);  // Play nice with existing code.
        loc.vec_len = kVectorLength8;
      } else {
        // The value is in a FP register, and we want it in a pair of core registers.
        DCHECK_EQ(reg_class, kCoreReg);
        DCHECK_EQ(loc.reg.GetReg(), loc.reg.GetHighReg());
        RegStorage new_regs = AllocTypedTempWide(false, kCoreReg);  // Force to core registers.
        low_reg = new_regs.GetReg();
        high_reg = new_regs.GetHighReg();
        DCHECK_NE(low_reg, high_reg);
        OpRegCopyWide(low_reg, high_reg, loc.reg.GetReg(), loc.reg.GetHighReg());
        CopyRegInfo(low_reg, loc.reg.GetReg());
        CopyRegInfo(high_reg, loc.reg.GetHighReg());
        Clobber(loc.reg.GetReg());
        Clobber(loc.reg.GetHighReg());
        loc.reg = new_regs;
        MarkPair(loc.reg.GetReg(), loc.reg.GetHighReg());
        DCHECK(!IsFpReg(loc.reg.GetReg()) || ((loc.reg.GetReg() & 0x1) == 0));
      }
    }
    return loc;
  }

  DCHECK_NE(loc.s_reg_low, INVALID_SREG);
  DCHECK_NE(GetSRegHi(loc.s_reg_low), INVALID_SREG);

  loc.reg = AllocTypedTempWide(loc.fp, reg_class);

  // FIXME: take advantage of RegStorage notation.
  if (loc.reg.GetReg() == loc.reg.GetHighReg()) {
    DCHECK(IsFpReg(loc.reg.GetReg()));
    loc.vec_len = kVectorLength8;
  } else {
    MarkPair(loc.reg.GetReg(), loc.reg.GetHighReg());
  }
  if (update) {
    loc.location = kLocPhysReg;
    MarkLive(loc.reg.GetReg(), loc.s_reg_low);
    if (loc.reg.GetReg() != loc.reg.GetHighReg()) {
      MarkLive(loc.reg.GetHighReg(), GetSRegHi(loc.s_reg_low));
    }
  }
  return loc;
}

// TODO: Reunify with common code after 'pair mess' has been fixed
RegLocation X86Mir2Lir::EvalLoc(RegLocation loc, int reg_class, bool update) {
  int new_reg;

  if (loc.wide)
    return EvalLocWide(loc, reg_class, update);

  loc = UpdateLoc(loc);

  if (loc.location == kLocPhysReg) {
    if (!RegClassMatches(reg_class, loc.reg.GetReg())) {
      /* Wrong register class.  Realloc, copy and transfer ownership. */
      new_reg = AllocTypedTemp(loc.fp, reg_class);
      OpRegCopy(new_reg, loc.reg.GetReg());
      CopyRegInfo(new_reg, loc.reg.GetReg());
      Clobber(loc.reg.GetReg());
      loc.reg.SetReg(new_reg);
      if (IsFpReg(loc.reg.GetReg()) && reg_class != kCoreReg)
        loc.vec_len = kVectorLength4;
    }
    return loc;
  }

  DCHECK_NE(loc.s_reg_low, INVALID_SREG);

  loc.reg = RegStorage(RegStorage::k32BitSolo, AllocTypedTemp(loc.fp, reg_class));
  if (IsFpReg(loc.reg.GetReg()) && reg_class != kCoreReg)
    loc.vec_len = kVectorLength4;

  if (update) {
    loc.location = kLocPhysReg;
    MarkLive(loc.reg.GetReg(), loc.s_reg_low);
  }
  return loc;
}

int X86Mir2Lir::AllocTempDouble() {
  // We really don't need a pair of registers.
  return AllocTempFloat();
}

// TODO: Reunify with common code after 'pair mess' has been fixed
void X86Mir2Lir::ResetDefLocWide(RegLocation rl) {
  DCHECK(rl.wide);
  RegisterInfo* p_low = IsTemp(rl.reg.GetReg());
  if (IsFpReg(rl.reg.GetReg())) {
    // We are using only the low register.
    if (p_low && !(cu_->disable_opt & (1 << kSuppressLoads))) {
      NullifyRange(p_low->def_start, p_low->def_end, p_low->s_reg, rl.s_reg_low);
    }
    ResetDef(rl.reg.GetReg());
  } else {
    RegisterInfo* p_high = IsTemp(rl.reg.GetHighReg());
    if (p_low && !(cu_->disable_opt & (1 << kSuppressLoads))) {
      DCHECK(p_low->pair);
      NullifyRange(p_low->def_start, p_low->def_end, p_low->s_reg, rl.s_reg_low);
    }
    if (p_high && !(cu_->disable_opt & (1 << kSuppressLoads))) {
      DCHECK(p_high->pair);
    }
    ResetDef(rl.reg.GetReg());
    ResetDef(rl.reg.GetHighReg());
  }
}

void X86Mir2Lir::GenConstWide(RegLocation rl_dest, int64_t value) {
  // Can we do this directly to memory?
  rl_dest = UpdateLocWide(rl_dest);
  if ((rl_dest.location == kLocDalvikFrame) ||
      (rl_dest.location == kLocCompilerTemp)) {
    int32_t val_lo = Low32Bits(value);
    int32_t val_hi = High32Bits(value);
    int rBase = TargetReg(kSp);
    int displacement = SRegOffset(rl_dest.s_reg_low);

    LIR * store = NewLIR3(kX86Mov32MI, rBase, displacement + LOWORD_OFFSET, val_lo);
    AnnotateDalvikRegAccess(store, (displacement + LOWORD_OFFSET) >> 2,
                              false /* is_load */, true /* is64bit */);
    store = NewLIR3(kX86Mov32MI, rBase, displacement + HIWORD_OFFSET, val_hi);
    AnnotateDalvikRegAccess(store, (displacement + HIWORD_OFFSET) >> 2,
                              false /* is_load */, true /* is64bit */);
    return;
  }

  // Just use the standard code to do the generation.
  Mir2Lir::GenConstWide(rl_dest, value);
}

// TODO: Merge with existing RegLocation dumper in vreg_analysis.cc
void X86Mir2Lir::DumpRegLocation(RegLocation loc) {
  LOG(INFO)  << "location: " << loc.location << ','
             << (loc.wide ? " w" : "  ")
             << (loc.defined ? " D" : "  ")
             << (loc.is_const ? " c" : "  ")
             << (loc.fp ? " F" : "  ")
             << (loc.core ? " C" : "  ")
             << (loc.ref ? " r" : "  ")
             << (loc.high_word ? " h" : "  ")
             << (loc.home ? " H" : "  ")
             << " vec_len: " << loc.vec_len
             << ", low: " << static_cast<int>(loc.reg.GetReg())
             << ", high: " << static_cast<int>(loc.reg.GetHighReg())
             << ", s_reg: " << loc.s_reg_low
             << ", orig: " << loc.orig_sreg;
}

void X86Mir2Lir::Materialize() {
  // A good place to put the analysis before starting.
  AnalyzeMIR();

  // Now continue with regular code generation.
  Mir2Lir::Materialize();
}

void X86Mir2Lir::LoadMethodAddress(int dex_method_index, InvokeType type,
                                   SpecialTargetRegister symbolic_reg) {
  /*
   * For x86, just generate a 32 bit move immediate instruction, that will be filled
   * in at 'link time'.  For now, put a unique value based on target to ensure that
   * code deduplication works.
   */
  const DexFile::MethodId& id = cu_->dex_file->GetMethodId(dex_method_index);
  uintptr_t ptr = reinterpret_cast<uintptr_t>(&id);

  // Generate the move instruction with the unique pointer and save index and type.
  LIR *move = RawLIR(current_dalvik_offset_, kX86Mov32RI, TargetReg(symbolic_reg),
                     static_cast<int>(ptr), dex_method_index, type);
  AppendLIR(move);
  method_address_insns_.Insert(move);
}

void X86Mir2Lir::LoadClassType(uint32_t type_idx, SpecialTargetRegister symbolic_reg) {
  /*
   * For x86, just generate a 32 bit move immediate instruction, that will be filled
   * in at 'link time'.  For now, put a unique value based on target to ensure that
   * code deduplication works.
   */
  const DexFile::TypeId& id = cu_->dex_file->GetTypeId(type_idx);
  uintptr_t ptr = reinterpret_cast<uintptr_t>(&id);

  // Generate the move instruction with the unique pointer and save index and type.
  LIR *move = RawLIR(current_dalvik_offset_, kX86Mov32RI, TargetReg(symbolic_reg),
                     static_cast<int>(ptr), type_idx);
  AppendLIR(move);
  class_type_address_insns_.Insert(move);
}

LIR *X86Mir2Lir::CallWithLinkerFixup(int dex_method_index, InvokeType type) {
  /*
   * For x86, just generate a 32 bit call relative instruction, that will be filled
   * in at 'link time'.  For now, put a unique value based on target to ensure that
   * code deduplication works.
   */
  const DexFile::MethodId& id = cu_->dex_file->GetMethodId(dex_method_index);
  uintptr_t ptr = reinterpret_cast<uintptr_t>(&id);

  // Generate the call instruction with the unique pointer and save index and type.
  LIR *call = RawLIR(current_dalvik_offset_, kX86CallI, static_cast<int>(ptr), dex_method_index,
                     type);
  AppendLIR(call);
  call_method_insns_.Insert(call);
  return call;
}

void X86Mir2Lir::InstallLiteralPools() {
  // These are handled differently for x86.
  DCHECK(code_literal_list_ == nullptr);
  DCHECK(method_literal_list_ == nullptr);
  DCHECK(class_literal_list_ == nullptr);

  // Handle the fixups for methods.
  for (uint32_t i = 0; i < method_address_insns_.Size(); i++) {
      LIR* p = method_address_insns_.Get(i);
      DCHECK_EQ(p->opcode, kX86Mov32RI);
      uint32_t target = p->operands[2];

      // The offset to patch is the last 4 bytes of the instruction.
      int patch_offset = p->offset + p->flags.size - 4;
      cu_->compiler_driver->AddMethodPatch(cu_->dex_file, cu_->class_def_idx,
                                           cu_->method_idx, cu_->invoke_type,
                                           target, static_cast<InvokeType>(p->operands[3]),
                                           patch_offset);
  }

  // Handle the fixups for class types.
  for (uint32_t i = 0; i < class_type_address_insns_.Size(); i++) {
      LIR* p = class_type_address_insns_.Get(i);
      DCHECK_EQ(p->opcode, kX86Mov32RI);
      uint32_t target = p->operands[2];

      // The offset to patch is the last 4 bytes of the instruction.
      int patch_offset = p->offset + p->flags.size - 4;
      cu_->compiler_driver->AddClassPatch(cu_->dex_file, cu_->class_def_idx,
                                          cu_->method_idx, target, patch_offset);
  }

  // And now the PC-relative calls to methods.
  for (uint32_t i = 0; i < call_method_insns_.Size(); i++) {
      LIR* p = call_method_insns_.Get(i);
      DCHECK_EQ(p->opcode, kX86CallI);
      uint32_t target = p->operands[1];

      // The offset to patch is the last 4 bytes of the instruction.
      int patch_offset = p->offset + p->flags.size - 4;
      cu_->compiler_driver->AddRelativeCodePatch(cu_->dex_file, cu_->class_def_idx,
                                                 cu_->method_idx, cu_->invoke_type, target,
                                                 static_cast<InvokeType>(p->operands[2]),
                                                 patch_offset, -4 /* offset */);
  }

  // And do the normal processing.
  Mir2Lir::InstallLiteralPools();
}

}  // namespace art
