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

#include "callee_save_frame.h"
#include "common_throws.h"
#include "dex_file-inl.h"
#include "dex_instruction-inl.h"
#include "entrypoints/entrypoint_utils.h"
#include "gc/accounting/card_table-inl.h"
#include "interpreter/interpreter.h"
#include "invoke_arg_array_builder.h"
#include "mirror/art_method-inl.h"
#include "mirror/class-inl.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "object_utils.h"
#include "runtime.h"

namespace art {

// Visits the arguments as saved to the stack by a Runtime::kRefAndArgs callee save frame.
class QuickArgumentVisitor {
  // Size of each spilled GPR.
 protected:
#ifdef __LP64__
  static constexpr size_t kBytesPerGprSpillLocation = 8;
#else
  static constexpr size_t kBytesPerGprSpillLocation = 4;
#endif
  // Number of bytes for each out register in the caller method's frame.
  static constexpr size_t kBytesStackArgLocation = 4;
#if defined(__arm__)
  // The callee save frame is pointed to by SP.
  // | argN       |  |
  // | ...        |  |
  // | arg4       |  |
  // | arg3 spill |  |  Caller's frame
  // | arg2 spill |  |
  // | arg1 spill |  |
  // | Method*    | ---
  // | LR         |
  // | ...        |    callee saves
  // | R3         |    arg3
  // | R2         |    arg2
  // | R1         |    arg1
  // | R0         |    padding
  // | Method*    |  <- sp
  static constexpr bool kQuickSoftFloatAbi = true;  // This is a soft float ABI.
  static constexpr size_t kNumQuickGprArgs = 3;  // 3 arguments passed in GPRs.
  static constexpr size_t kNumQuickFprArgs = 0;  // 0 arguments passed in FPRs.
  static constexpr size_t kBytesPerFprSpillLocation = 4;  // FPR spill size is 4 bytes.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Fpr1Offset = 0;  // Offset of first FPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Gpr1Offset = 8;  // Offset of first GPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_LrOffset = 44;  // Offset of return address.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_FrameSize = 48;  // Frame size.
  static size_t GprIndexToGprOffset(uint32_t gpr_index) {
    return gpr_index * kBytesPerGprSpillLocation;
  }
#elif defined(__mips__)
  // The callee save frame is pointed to by SP.
  // | argN       |  |
  // | ...        |  |
  // | arg4       |  |
  // | arg3 spill |  |  Caller's frame
  // | arg2 spill |  |
  // | arg1 spill |  |
  // | Method*    | ---
  // | RA         |
  // | ...        |    callee saves
  // | A3         |    arg3
  // | A2         |    arg2
  // | A1         |    arg1
  // | A0/Method* |  <- sp
  static constexpr bool kQuickSoftFloatAbi = true;  // This is a soft float ABI.
  static constexpr size_t kNumQuickGprArgs = 3;  // 3 arguments passed in GPRs.
  static constexpr size_t kNumQuickFprArgs = 0;  // 0 arguments passed in FPRs.
  static constexpr size_t kBytesPerFprSpillLocation = 4;  // FPR spill size is 4 bytes.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Fpr1Offset = 0;  // Offset of first FPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Gpr1Offset = 4;  // Offset of first GPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_LrOffset = 60;  // Offset of return address.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_FrameSize = 64;  // Frame size.
  static size_t GprIndexToGprOffset(uint32_t gpr_index) {
    return gpr_index * kBytesPerGprSpillLocation;
  }
#elif defined(__i386__)
  // The callee save frame is pointed to by SP.
  // | argN        |  |
  // | ...         |  |
  // | arg4        |  |
  // | arg3 spill  |  |  Caller's frame
  // | arg2 spill  |  |
  // | arg1 spill  |  |
  // | Method*     | ---
  // | Return      |
  // | EBP,ESI,EDI |    callee saves
  // | EBX         |    arg3
  // | EDX         |    arg2
  // | ECX         |    arg1
  // | EAX/Method* |  <- sp
  static constexpr bool kQuickSoftFloatAbi = true;  // This is a soft float ABI.
  static constexpr size_t kNumQuickGprArgs = 3;  // 3 arguments passed in GPRs.
  static constexpr size_t kNumQuickFprArgs = 0;  // 0 arguments passed in FPRs.
  static constexpr size_t kBytesPerFprSpillLocation = 8;  // FPR spill size is 8 bytes.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Fpr1Offset = 0;  // Offset of first FPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Gpr1Offset = 4;  // Offset of first GPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_LrOffset = 28;  // Offset of return address.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_FrameSize = 32;  // Frame size.
  static size_t GprIndexToGprOffset(uint32_t gpr_index) {
    return gpr_index * kBytesPerGprSpillLocation;
  }
#elif defined(__x86_64__)
  // The callee save frame is pointed to by SP.
  // | argN            |  |
  // | ...             |  |
  // | reg. arg spills |  |  Caller's frame
  // | Method*         | ---
  // | Return          |
  // | R15             |    callee save
  // | R14             |    callee save
  // | R13             |    callee save
  // | R12             |    callee save
  // | R9              |    arg5
  // | R8              |    arg4
  // | RSI/R6          |    arg1
  // | RBP/R5          |    callee save
  // | RBX/R3          |    callee save
  // | RDX/R2          |    arg2
  // | RCX/R1          |    arg3
  // | XMM7            |    float arg 8
  // | XMM6            |    float arg 7
  // | XMM5            |    float arg 6
  // | XMM4            |    float arg 5
  // | XMM3            |    float arg 4
  // | XMM2            |    float arg 3
  // | XMM1            |    float arg 2
  // | XMM0            |    float arg 1
  // | Padding         |
  // | RDI/Method*     |  <- sp
  static constexpr bool kQuickSoftFloatAbi = false;  // This is a hard float ABI.
  static constexpr size_t kNumQuickGprArgs = 5;  // 3 arguments passed in GPRs.
  static constexpr size_t kNumQuickFprArgs = 8;  // 0 arguments passed in FPRs.
  static constexpr size_t kBytesPerFprSpillLocation = 8;  // FPR spill size is 8 bytes.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Fpr1Offset = 16;  // Offset of first FPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_Gpr1Offset = 80;  // Offset of first GPR arg.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_LrOffset = 168;  // Offset of return address.
  static constexpr size_t kQuickCalleeSaveFrame_RefAndArgs_FrameSize = 176;  // Frame size.
  static size_t GprIndexToGprOffset(uint32_t gpr_index) {
    switch (gpr_index) {
      case 0: return (4 * kBytesPerGprSpillLocation);
      case 1: return (1 * kBytesPerGprSpillLocation);
      case 2: return (0 * kBytesPerGprSpillLocation);
      case 3: return (5 * kBytesPerGprSpillLocation);
      case 4: return (6 * kBytesPerGprSpillLocation);
      default:
        LOG(FATAL) << "Unexpected GPR index: " << gpr_index;
        return 0;
    }
  }
#else
#error "Unsupported architecture"
#endif

 public:
  static mirror::ArtMethod* GetCallingMethod(mirror::ArtMethod** sp)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK((*sp)->IsCalleeSaveMethod());
    byte* previous_sp = reinterpret_cast<byte*>(sp) + kQuickCalleeSaveFrame_RefAndArgs_FrameSize;
    return *reinterpret_cast<mirror::ArtMethod**>(previous_sp);
  }

  // For the given quick ref and args quick frame, return the caller's PC.
  static uintptr_t GetCallingPc(mirror::ArtMethod** sp)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    DCHECK((*sp)->IsCalleeSaveMethod());
    byte* lr = reinterpret_cast<byte*>(sp) + kQuickCalleeSaveFrame_RefAndArgs_LrOffset;
    return *reinterpret_cast<uintptr_t*>(lr);
  }

  QuickArgumentVisitor(mirror::ArtMethod** sp, bool is_static,
                       const char* shorty, uint32_t shorty_len)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) :
      is_static_(is_static), shorty_(shorty), shorty_len_(shorty_len),
      gpr_args_(reinterpret_cast<byte*>(sp) + kQuickCalleeSaveFrame_RefAndArgs_Gpr1Offset),
      fpr_args_(reinterpret_cast<byte*>(sp) + kQuickCalleeSaveFrame_RefAndArgs_Fpr1Offset),
      stack_args_(reinterpret_cast<byte*>(sp) + kQuickCalleeSaveFrame_RefAndArgs_FrameSize
                  + StackArgumentStartFromShorty(is_static, shorty, shorty_len)),
      gpr_index_(0), fpr_index_(0), stack_index_(0), cur_type_(Primitive::kPrimVoid),
      is_split_long_or_double_(false) {
    DCHECK_EQ(kQuickCalleeSaveFrame_RefAndArgs_FrameSize,
              Runtime::Current()->GetCalleeSaveMethod(Runtime::kRefsAndArgs)->GetFrameSizeInBytes());
  }

  virtual ~QuickArgumentVisitor() {}

  virtual void Visit() = 0;

  Primitive::Type GetParamPrimitiveType() const {
    return cur_type_;
  }

  byte* GetParamAddress() const {
    if (!kQuickSoftFloatAbi) {
      Primitive::Type type = GetParamPrimitiveType();
      if (UNLIKELY((type == Primitive::kPrimDouble) || (type == Primitive::kPrimFloat))) {
        if ((kNumQuickFprArgs != 0) && (fpr_index_ + 1 < kNumQuickFprArgs + 1)) {
          return fpr_args_ + (fpr_index_ * kBytesPerFprSpillLocation);
        }
      }
    }
    if (gpr_index_ < kNumQuickGprArgs) {
      return gpr_args_ + GprIndexToGprOffset(gpr_index_);
    }
    return stack_args_ + (stack_index_ * kBytesStackArgLocation);
  }

  bool IsSplitLongOrDouble() const {
    if ((kBytesPerGprSpillLocation == 4) || (kBytesPerFprSpillLocation == 4)) {
      return is_split_long_or_double_;
    } else {
      return false;  // An optimization for when GPR and FPRs are 64bit.
    }
  }

  bool IsParamAReference() const {
    return GetParamPrimitiveType() == Primitive::kPrimNot;
  }

  bool IsParamALongOrDouble() const {
    Primitive::Type type = GetParamPrimitiveType();
    return type == Primitive::kPrimLong || type == Primitive::kPrimDouble;
  }

  uint64_t ReadSplitLongParam() const {
    DCHECK(IsSplitLongOrDouble());
    uint64_t low_half = *reinterpret_cast<uint32_t*>(GetParamAddress());
    uint64_t high_half = *reinterpret_cast<uint32_t*>(stack_args_);
    return (low_half & 0xffffffffULL) | (high_half << 32);
  }

  void VisitArguments() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    gpr_index_ = 0;
    fpr_index_ = 0;
    stack_index_ = 0;
    if (!is_static_) {  // Handle this.
      cur_type_ = Primitive::kPrimNot;
      is_split_long_or_double_ = false;
      Visit();
      if (kNumQuickGprArgs > 0) {
        gpr_index_++;
      } else {
        stack_index_++;
      }
    }
    for (uint32_t shorty_index = 1; shorty_index < shorty_len_; ++shorty_index) {
      cur_type_ = Primitive::GetType(shorty_[shorty_index]);
      switch (cur_type_) {
        case Primitive::kPrimNot:
        case Primitive::kPrimBoolean:
        case Primitive::kPrimByte:
        case Primitive::kPrimChar:
        case Primitive::kPrimShort:
        case Primitive::kPrimInt:
          is_split_long_or_double_ = false;
          Visit();
          if (gpr_index_ < kNumQuickGprArgs) {
            gpr_index_++;
          } else {
            stack_index_++;
          }
          break;
        case Primitive::kPrimFloat:
          is_split_long_or_double_ = false;
          Visit();
          if (kQuickSoftFloatAbi) {
            if (gpr_index_ < kNumQuickGprArgs) {
              gpr_index_++;
            } else {
              stack_index_++;
            }
          } else {
            if ((kNumQuickFprArgs != 0) && (fpr_index_ + 1 < kNumQuickFprArgs + 1)) {
              fpr_index_++;
            } else {
              stack_index_++;
            }
          }
          break;
        case Primitive::kPrimDouble:
        case Primitive::kPrimLong:
          if (kQuickSoftFloatAbi || (cur_type_ == Primitive::kPrimLong)) {
            is_split_long_or_double_ = (kBytesPerGprSpillLocation == 4) &&
                ((gpr_index_ + 1) == kNumQuickGprArgs);
            Visit();
            if (gpr_index_ < kNumQuickGprArgs) {
              gpr_index_++;
              if (kBytesPerGprSpillLocation == 4) {
                if (gpr_index_ < kNumQuickGprArgs) {
                  gpr_index_++;
                } else {
                  stack_index_++;
                }
              }
            } else {
              if (kBytesStackArgLocation == 4) {
                stack_index_+= 2;
              } else {
                CHECK_EQ(kBytesStackArgLocation, 8U);
                stack_index_++;
              }
            }
          } else {
            is_split_long_or_double_ = (kBytesPerFprSpillLocation == 4) &&
                ((fpr_index_ + 1) == kNumQuickFprArgs);
            Visit();
            if ((kNumQuickFprArgs != 0) && (fpr_index_ + 1 < kNumQuickFprArgs + 1)) {
              fpr_index_++;
              if (kBytesPerFprSpillLocation == 4) {
                if ((kNumQuickFprArgs != 0) && (fpr_index_ + 1 < kNumQuickFprArgs + 1)) {
                  fpr_index_++;
                } else {
                  stack_index_++;
                }
              }
            } else {
              if (kBytesStackArgLocation == 4) {
                stack_index_+= 2;
              } else {
                CHECK_EQ(kBytesStackArgLocation, 8U);
                stack_index_++;
              }
            }
          }
          break;
        default:
          LOG(FATAL) << "Unexpected type: " << cur_type_ << " in " << shorty_;
      }
    }
  }

 private:
  static size_t StackArgumentStartFromShorty(bool is_static, const char* shorty,
                                             uint32_t shorty_len) {
    if (kQuickSoftFloatAbi) {
      CHECK_EQ(kNumQuickFprArgs, 0U);
      return (kNumQuickGprArgs * kBytesPerGprSpillLocation) + kBytesPerGprSpillLocation /* ArtMethod* */;
    } else {
      size_t offset = kBytesPerGprSpillLocation;  // Skip Method*.
      size_t gprs_seen = 0;
      size_t fprs_seen = 0;
      if (!is_static && (gprs_seen < kNumQuickGprArgs)) {
        gprs_seen++;
        offset += kBytesStackArgLocation;
      }
      for (uint32_t i = 1; i < shorty_len; ++i) {
        switch (shorty[i]) {
          case 'Z':
          case 'B':
          case 'C':
          case 'S':
          case 'I':
          case 'L':
            if (gprs_seen < kNumQuickGprArgs) {
              gprs_seen++;
              offset += kBytesStackArgLocation;
            }
            break;
          case 'J':
            if (gprs_seen < kNumQuickGprArgs) {
              gprs_seen++;
              offset += 2 * kBytesStackArgLocation;
              if (kBytesPerGprSpillLocation == 4) {
                if (gprs_seen < kNumQuickGprArgs) {
                  gprs_seen++;
                }
              }
            }
            break;
          case 'F':
            if ((kNumQuickFprArgs != 0) && (fprs_seen + 1 < kNumQuickFprArgs + 1)) {
              fprs_seen++;
              offset += kBytesStackArgLocation;
            }
            break;
          case 'D':
            if ((kNumQuickFprArgs != 0) && (fprs_seen + 1 < kNumQuickFprArgs + 1)) {
              fprs_seen++;
              offset += 2 * kBytesStackArgLocation;
              if (kBytesPerFprSpillLocation == 4) {
                if ((kNumQuickFprArgs != 0) && (fprs_seen + 1 < kNumQuickFprArgs + 1)) {
                  fprs_seen++;
                }
              }
            }
            break;
          default:
            LOG(FATAL) << "Unexpected shorty character: " << shorty[i] << " in " << shorty;
        }
      }
      return offset;
    }
  }

  const bool is_static_;
  const char* const shorty_;
  const uint32_t shorty_len_;
  byte* const gpr_args_;  // Address of GPR arguments in callee save frame.
  byte* const fpr_args_;  // Address of FPR arguments in callee save frame.
  byte* const stack_args_;  // Address of stack arguments in caller's frame.
  uint32_t gpr_index_;  // Index into spilled GPRs.
  uint32_t fpr_index_;  // Index into spilled FPRs.
  uint32_t stack_index_;  // Index into arguments on the stack.
  // The current type of argument during VisitArguments.
  Primitive::Type cur_type_;
  // Does a 64bit parameter straddle the register and stack arguments?
  bool is_split_long_or_double_;
};

// Visits arguments on the stack placing them into the shadow frame.
class BuildQuickShadowFrameVisitor FINAL : public QuickArgumentVisitor {
 public:
  BuildQuickShadowFrameVisitor(mirror::ArtMethod** sp, bool is_static, const char* shorty,
                               uint32_t shorty_len, ShadowFrame* sf, size_t first_arg_reg) :
    QuickArgumentVisitor(sp, is_static, shorty, shorty_len), sf_(sf), cur_reg_(first_arg_reg) {}

  void Visit() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE {
    Primitive::Type type = GetParamPrimitiveType();
    switch (type) {
      case Primitive::kPrimLong:  // Fall-through.
      case Primitive::kPrimDouble:
        if (IsSplitLongOrDouble()) {
          sf_->SetVRegLong(cur_reg_, ReadSplitLongParam());
        } else {
          sf_->SetVRegLong(cur_reg_, *reinterpret_cast<jlong*>(GetParamAddress()));
        }
        ++cur_reg_;
        break;
      case Primitive::kPrimNot: {
          StackReference<mirror::Object>* stack_ref =
              reinterpret_cast<StackReference<mirror::Object>*>(GetParamAddress());
          sf_->SetVRegReference(cur_reg_, stack_ref->AsMirrorPtr());
        }
        break;
      case Primitive::kPrimBoolean:  // Fall-through.
      case Primitive::kPrimByte:     // Fall-through.
      case Primitive::kPrimChar:     // Fall-through.
      case Primitive::kPrimShort:    // Fall-through.
      case Primitive::kPrimInt:      // Fall-through.
      case Primitive::kPrimFloat:
        sf_->SetVReg(cur_reg_, *reinterpret_cast<jint*>(GetParamAddress()));
        break;
      case Primitive::kPrimVoid:
        LOG(FATAL) << "UNREACHABLE";
        break;
    }
    ++cur_reg_;
  }

 private:
  ShadowFrame* const sf_;
  uint32_t cur_reg_;

  DISALLOW_COPY_AND_ASSIGN(BuildQuickShadowFrameVisitor);
};

extern "C" uint64_t artQuickToInterpreterBridge(mirror::ArtMethod* method, Thread* self,
                                                mirror::ArtMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  // Ensure we don't get thread suspension until the object arguments are safely in the shadow
  // frame.
  FinishCalleeSaveFrameSetup(self, sp, Runtime::kRefsAndArgs);

  if (method->IsAbstract()) {
    ThrowAbstractMethodError(method);
    return 0;
  } else {
    DCHECK(!method->IsNative()) << PrettyMethod(method);
    const char* old_cause = self->StartAssertNoThreadSuspension("Building interpreter shadow frame");
    MethodHelper mh(method);
    const DexFile::CodeItem* code_item = mh.GetCodeItem();
    DCHECK(code_item != nullptr) << PrettyMethod(method);
    uint16_t num_regs = code_item->registers_size_;
    void* memory = alloca(ShadowFrame::ComputeSize(num_regs));
    ShadowFrame* shadow_frame(ShadowFrame::Create(num_regs, NULL,  // No last shadow coming from quick.
                                                  method, 0, memory));
    size_t first_arg_reg = code_item->registers_size_ - code_item->ins_size_;
    BuildQuickShadowFrameVisitor shadow_frame_builder(sp, mh.IsStatic(), mh.GetShorty(),
                                                      mh.GetShortyLength(),
                                                      shadow_frame, first_arg_reg);
    shadow_frame_builder.VisitArguments();
    // Push a transition back into managed code onto the linked list in thread.
    ManagedStack fragment;
    self->PushManagedStackFragment(&fragment);
    self->PushShadowFrame(shadow_frame);
    self->EndAssertNoThreadSuspension(old_cause);

    if (method->IsStatic() && !method->GetDeclaringClass()->IsInitializing()) {
      // Ensure static method's class is initialized.
      SirtRef<mirror::Class> sirt_c(self, method->GetDeclaringClass());
      if (!Runtime::Current()->GetClassLinker()->EnsureInitialized(sirt_c, true, true)) {
        DCHECK(Thread::Current()->IsExceptionPending()) << PrettyMethod(method);
        self->PopManagedStackFragment(fragment);
        return 0;
      }
    }

    JValue result = interpreter::EnterInterpreterFromStub(self, mh, code_item, *shadow_frame);
    // Pop transition.
    self->PopManagedStackFragment(fragment);
    // No need to restore the args since the method has already been run by the interpreter.
    return result.GetJ();
  }
}

// Visits arguments on the stack placing them into the args vector, Object* arguments are converted
// to jobjects.
class BuildQuickArgumentVisitor FINAL : public QuickArgumentVisitor {
 public:
  BuildQuickArgumentVisitor(mirror::ArtMethod** sp, bool is_static, const char* shorty,
                            uint32_t shorty_len, ScopedObjectAccessUnchecked* soa,
                            std::vector<jvalue>* args) :
    QuickArgumentVisitor(sp, is_static, shorty, shorty_len), soa_(soa), args_(args) {}

  void Visit() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE {
    jvalue val;
    Primitive::Type type = GetParamPrimitiveType();
    switch (type) {
      case Primitive::kPrimNot: {
        StackReference<mirror::Object>* stack_ref =
            reinterpret_cast<StackReference<mirror::Object>*>(GetParamAddress());
        val.l = soa_->AddLocalReference<jobject>(stack_ref->AsMirrorPtr());
        references_.push_back(std::make_pair(val.l, stack_ref));
        break;
      }
      case Primitive::kPrimLong:  // Fall-through.
      case Primitive::kPrimDouble:
        if (IsSplitLongOrDouble()) {
          val.j = ReadSplitLongParam();
        } else {
          val.j = *reinterpret_cast<jlong*>(GetParamAddress());
        }
        break;
      case Primitive::kPrimBoolean:  // Fall-through.
      case Primitive::kPrimByte:     // Fall-through.
      case Primitive::kPrimChar:     // Fall-through.
      case Primitive::kPrimShort:    // Fall-through.
      case Primitive::kPrimInt:      // Fall-through.
      case Primitive::kPrimFloat:
        val.i = *reinterpret_cast<jint*>(GetParamAddress());
        break;
      case Primitive::kPrimVoid:
        LOG(FATAL) << "UNREACHABLE";
        val.j = 0;
        break;
    }
    args_->push_back(val);
  }

  void FixupReferences() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // Fixup any references which may have changed.
    for (const auto& pair : references_) {
      pair.second->Assign(soa_->Decode<mirror::Object*>(pair.first));
    }
  }

 private:
  ScopedObjectAccessUnchecked* soa_;
  std::vector<jvalue>* args_;
  // References which we must update when exiting in case the GC moved the objects.
  std::vector<std::pair<jobject, StackReference<mirror::Object>*> > references_;
  DISALLOW_COPY_AND_ASSIGN(BuildQuickArgumentVisitor);
};

// Handler for invocation on proxy methods. On entry a frame will exist for the proxy object method
// which is responsible for recording callee save registers. We explicitly place into jobjects the
// incoming reference arguments (so they survive GC). We invoke the invocation handler, which is a
// field within the proxy object, which will box the primitive arguments and deal with error cases.
extern "C" uint64_t artQuickProxyInvokeHandler(mirror::ArtMethod* proxy_method,
                                               mirror::Object* receiver,
                                               Thread* self, mirror::ArtMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  DCHECK(proxy_method->IsProxyMethod()) << PrettyMethod(proxy_method);
  DCHECK(receiver->GetClass()->IsProxyClass()) << PrettyMethod(proxy_method);
  // Ensure we don't get thread suspension until the object arguments are safely in jobjects.
  const char* old_cause =
      self->StartAssertNoThreadSuspension("Adding to IRT proxy object arguments");
  // Register the top of the managed stack, making stack crawlable.
  DCHECK_EQ(*sp, proxy_method) << PrettyMethod(proxy_method);
  self->SetTopOfStack(sp, 0);
  DCHECK_EQ(proxy_method->GetFrameSizeInBytes(),
            Runtime::Current()->GetCalleeSaveMethod(Runtime::kRefsAndArgs)->GetFrameSizeInBytes())
      << PrettyMethod(proxy_method);
  self->VerifyStack();
  // Start new JNI local reference state.
  JNIEnvExt* env = self->GetJniEnv();
  ScopedObjectAccessUnchecked soa(env);
  ScopedJniEnvLocalRefState env_state(env);
  // Create local ref. copies of proxy method and the receiver.
  jobject rcvr_jobj = soa.AddLocalReference<jobject>(receiver);

  // Placing arguments into args vector and remove the receiver.
  MethodHelper proxy_mh(proxy_method);
  DCHECK(!proxy_mh.IsStatic()) << PrettyMethod(proxy_method);
  std::vector<jvalue> args;
  BuildQuickArgumentVisitor local_ref_visitor(sp, proxy_mh.IsStatic(), proxy_mh.GetShorty(),
                                              proxy_mh.GetShortyLength(), &soa, &args);

  local_ref_visitor.VisitArguments();
  DCHECK_GT(args.size(), 0U) << PrettyMethod(proxy_method);
  args.erase(args.begin());

  // Convert proxy method into expected interface method.
  mirror::ArtMethod* interface_method = proxy_method->FindOverriddenMethod();
  DCHECK(interface_method != NULL) << PrettyMethod(proxy_method);
  DCHECK(!interface_method->IsProxyMethod()) << PrettyMethod(interface_method);
  jobject interface_method_jobj = soa.AddLocalReference<jobject>(interface_method);

  // All naked Object*s should now be in jobjects, so its safe to go into the main invoke code
  // that performs allocations.
  self->EndAssertNoThreadSuspension(old_cause);
  JValue result = InvokeProxyInvocationHandler(soa, proxy_mh.GetShorty(),
                                               rcvr_jobj, interface_method_jobj, args);
  // Restore references which might have moved.
  local_ref_visitor.FixupReferences();
  return result.GetJ();
}

// Read object references held in arguments from quick frames and place in a JNI local references,
// so they don't get garbage collected.
class RememberForGcArgumentVisitor FINAL : public QuickArgumentVisitor {
 public:
  RememberForGcArgumentVisitor(mirror::ArtMethod** sp, bool is_static, const char* shorty,
                               uint32_t shorty_len, ScopedObjectAccessUnchecked* soa) :
    QuickArgumentVisitor(sp, is_static, shorty, shorty_len), soa_(soa) {}

  void Visit() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE {
    if (IsParamAReference()) {
      StackReference<mirror::Object>* stack_ref =
          reinterpret_cast<StackReference<mirror::Object>*>(GetParamAddress());
      jobject reference =
          soa_->AddLocalReference<jobject>(stack_ref->AsMirrorPtr());
      references_.push_back(std::make_pair(reference, stack_ref));
    }
  }

  void FixupReferences() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // Fixup any references which may have changed.
    for (const auto& pair : references_) {
      pair.second->Assign(soa_->Decode<mirror::Object*>(pair.first));
    }
  }

 private:
  ScopedObjectAccessUnchecked* soa_;
  // References which we must update when exiting in case the GC moved the objects.
  std::vector<std::pair<jobject, StackReference<mirror::Object>*> > references_;
  DISALLOW_COPY_AND_ASSIGN(RememberForGcArgumentVisitor);
};

// Lazily resolve a method for quick. Called by stub code.
extern "C" const void* artQuickResolutionTrampoline(mirror::ArtMethod* called,
                                                    mirror::Object* receiver,
                                                    Thread* self, mirror::ArtMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  FinishCalleeSaveFrameSetup(self, sp, Runtime::kRefsAndArgs);
  // Start new JNI local reference state
  JNIEnvExt* env = self->GetJniEnv();
  ScopedObjectAccessUnchecked soa(env);
  ScopedJniEnvLocalRefState env_state(env);
  const char* old_cause = self->StartAssertNoThreadSuspension("Quick method resolution set up");

  // Compute details about the called method (avoid GCs)
  ClassLinker* linker = Runtime::Current()->GetClassLinker();
  mirror::ArtMethod* caller = QuickArgumentVisitor::GetCallingMethod(sp);
  InvokeType invoke_type;
  const DexFile* dex_file;
  uint32_t dex_method_idx;
  if (called->IsRuntimeMethod()) {
    uint32_t dex_pc = caller->ToDexPc(QuickArgumentVisitor::GetCallingPc(sp));
    const DexFile::CodeItem* code;
    {
      MethodHelper mh(caller);
      dex_file = &mh.GetDexFile();
      code = mh.GetCodeItem();
    }
    CHECK_LT(dex_pc, code->insns_size_in_code_units_);
    const Instruction* instr = Instruction::At(&code->insns_[dex_pc]);
    Instruction::Code instr_code = instr->Opcode();
    bool is_range;
    switch (instr_code) {
      case Instruction::INVOKE_DIRECT:
        invoke_type = kDirect;
        is_range = false;
        break;
      case Instruction::INVOKE_DIRECT_RANGE:
        invoke_type = kDirect;
        is_range = true;
        break;
      case Instruction::INVOKE_STATIC:
        invoke_type = kStatic;
        is_range = false;
        break;
      case Instruction::INVOKE_STATIC_RANGE:
        invoke_type = kStatic;
        is_range = true;
        break;
      case Instruction::INVOKE_SUPER:
        invoke_type = kSuper;
        is_range = false;
        break;
      case Instruction::INVOKE_SUPER_RANGE:
        invoke_type = kSuper;
        is_range = true;
        break;
      case Instruction::INVOKE_VIRTUAL:
        invoke_type = kVirtual;
        is_range = false;
        break;
      case Instruction::INVOKE_VIRTUAL_RANGE:
        invoke_type = kVirtual;
        is_range = true;
        break;
      case Instruction::INVOKE_INTERFACE:
        invoke_type = kInterface;
        is_range = false;
        break;
      case Instruction::INVOKE_INTERFACE_RANGE:
        invoke_type = kInterface;
        is_range = true;
        break;
      default:
        LOG(FATAL) << "Unexpected call into trampoline: " << instr->DumpString(NULL);
        // Avoid used uninitialized warnings.
        invoke_type = kDirect;
        is_range = false;
    }
    dex_method_idx = (is_range) ? instr->VRegB_3rc() : instr->VRegB_35c();

  } else {
    invoke_type = kStatic;
    dex_file = &MethodHelper(called).GetDexFile();
    dex_method_idx = called->GetDexMethodIndex();
  }
  uint32_t shorty_len;
  const char* shorty =
      dex_file->GetMethodShorty(dex_file->GetMethodId(dex_method_idx), &shorty_len);
  RememberForGcArgumentVisitor visitor(sp, invoke_type == kStatic, shorty, shorty_len, &soa);
  visitor.VisitArguments();
  self->EndAssertNoThreadSuspension(old_cause);
  bool virtual_or_interface = invoke_type == kVirtual || invoke_type == kInterface;
  // Resolve method filling in dex cache.
  if (called->IsRuntimeMethod()) {
    SirtRef<mirror::Object> sirt_receiver(soa.Self(), virtual_or_interface ? receiver : nullptr);
    called = linker->ResolveMethod(dex_method_idx, caller, invoke_type);
    receiver = sirt_receiver.get();
  }
  const void* code = NULL;
  if (LIKELY(!self->IsExceptionPending())) {
    // Incompatible class change should have been handled in resolve method.
    CHECK(!called->CheckIncompatibleClassChange(invoke_type))
        << PrettyMethod(called) << " " << invoke_type;
    if (virtual_or_interface) {
      // Refine called method based on receiver.
      CHECK(receiver != nullptr) << invoke_type;
      if (invoke_type == kVirtual) {
        called = receiver->GetClass()->FindVirtualMethodForVirtual(called);
      } else {
        called = receiver->GetClass()->FindVirtualMethodForInterface(called);
      }
      // We came here because of sharpening. Ensure the dex cache is up-to-date on the method index
      // of the sharpened method.
      if (called->GetDexCacheResolvedMethods() == caller->GetDexCacheResolvedMethods()) {
        caller->GetDexCacheResolvedMethods()->Set<false>(called->GetDexMethodIndex(), called);
      } else {
        // Calling from one dex file to another, need to compute the method index appropriate to
        // the caller's dex file. Since we get here only if the original called was a runtime
        // method, we've got the correct dex_file and a dex_method_idx from above.
        DCHECK(&MethodHelper(caller).GetDexFile() == dex_file);
        uint32_t method_index =
            MethodHelper(called).FindDexMethodIndexInOtherDexFile(*dex_file, dex_method_idx);
        if (method_index != DexFile::kDexNoIndex) {
          caller->GetDexCacheResolvedMethods()->Set<false>(method_index, called);
        }
      }
    }
    // Ensure that the called method's class is initialized.
    SirtRef<mirror::Class> called_class(soa.Self(), called->GetDeclaringClass());
    linker->EnsureInitialized(called_class, true, true);
    if (LIKELY(called_class->IsInitialized())) {
      code = called->GetEntryPointFromQuickCompiledCode();
    } else if (called_class->IsInitializing()) {
      if (invoke_type == kStatic) {
        // Class is still initializing, go to oat and grab code (trampoline must be left in place
        // until class is initialized to stop races between threads).
        code = linker->GetQuickOatCodeFor(called);
      } else {
        // No trampoline for non-static methods.
        code = called->GetEntryPointFromQuickCompiledCode();
      }
    } else {
      DCHECK(called_class->IsErroneous());
    }
  }
  CHECK_EQ(code == NULL, self->IsExceptionPending());
  // Fixup any locally saved objects may have moved during a GC.
  visitor.FixupReferences();
  // Place called method in callee-save frame to be placed as first argument to quick method.
  *sp = called;
  return code;
}

// Visits arguments on the stack placing them into a region lower down the stack for the benefit
// of transitioning into native code.
class BuildGenericJniFrameVisitor FINAL : public QuickArgumentVisitor {
#if defined(__arm__)
  // TODO: These are all dummy values!
  static constexpr bool kNativeSoftFloatAbi = false;  // This is a hard float ABI.
  static constexpr size_t kNumNativeGprArgs = 3;  // 3 arguments passed in GPRs.
  static constexpr size_t kNumNativeFprArgs = 0;  // 0 arguments passed in FPRs.

  static constexpr size_t kGprStackOffset = 4336;
  static constexpr size_t kFprStackOffset = 4336 - 6*8;
  static constexpr size_t kCallStackStackOffset = 4336 - 112;

  static constexpr size_t kRegistersNeededForLong = 2;
  static constexpr size_t kRegistersNeededForDouble = 2;
#elif defined(__mips__)
  // TODO: These are all dummy values!
  static constexpr bool kNativeSoftFloatAbi = true;  // This is a hard float ABI.
  static constexpr size_t kNumNativeGprArgs = 0;  // 6 arguments passed in GPRs.
  static constexpr size_t kNumNativeFprArgs = 0;  // 8 arguments passed in FPRs.

  // update these
  static constexpr size_t kGprStackOffset = 4336;
  static constexpr size_t kFprStackOffset = 4336 - 6*8;
  static constexpr size_t kCallStackStackOffset = 4336 - 112;

  static constexpr size_t kRegistersNeededForLong = 2;
  static constexpr size_t kRegistersNeededForDouble = 2;
#elif defined(__i386__)
  // TODO: Check these!
  static constexpr bool kNativeSoftFloatAbi = true;  // This is a soft float ABI.
  static constexpr size_t kNumNativeGprArgs = 0;  // 6 arguments passed in GPRs.
  static constexpr size_t kNumNativeFprArgs = 0;  // 8 arguments passed in FPRs.

  // update these
  static constexpr size_t kGprStackOffset = 4336;
  static constexpr size_t kFprStackOffset = 4336 - 6*8;
  static constexpr size_t kCallStackStackOffset = 4336 - 112;

  static constexpr size_t kRegistersNeededForLong = 2;
  static constexpr size_t kRegistersNeededForDouble = 2;
#elif defined(__x86_64__)
  static constexpr bool kNativeSoftFloatAbi = false;  // This is a hard float ABI.
  static constexpr size_t kNumNativeGprArgs = 6;  // 6 arguments passed in GPRs.
  static constexpr size_t kNumNativeFprArgs = 8;  // 8 arguments passed in FPRs.

  static constexpr size_t kGprStackOffset = 4336;
  static constexpr size_t kFprStackOffset = 4336 - 6*8;
  static constexpr size_t kCallStackStackOffset = 4336 - 112;

  static constexpr size_t kRegistersNeededForLong = 1;
  static constexpr size_t kRegistersNeededForDouble = 1;
#else
#error "Unsupported architecture"
#endif


 public:
  BuildGenericJniFrameVisitor(mirror::ArtMethod** sp, bool is_static, const char* shorty,
                              uint32_t shorty_len, Thread* self) :
      QuickArgumentVisitor(sp, is_static, shorty, shorty_len) {
    // size of cookie plus padding
    uint8_t* sp8 = reinterpret_cast<uint8_t*>(sp);
    top_of_sirt_ =  sp8 - 8;
    cur_sirt_entry_ = reinterpret_cast<StackReference<mirror::Object>*>(top_of_sirt_) - 1;
    sirt_number_of_references_ = 0;
    gpr_index_ = kNumNativeGprArgs;
    fpr_index_ = kNumNativeFprArgs;

    cur_gpr_reg_ = reinterpret_cast<uintptr_t*>(sp8 - kGprStackOffset);
    cur_fpr_reg_ = reinterpret_cast<uint32_t*>(sp8 - kFprStackOffset);
    cur_stack_arg_ = reinterpret_cast<uintptr_t*>(sp8 - kCallStackStackOffset);

    // jni environment is always first argument
    PushPointer(self->GetJniEnv());

    if (is_static) {
      PushArgumentInSirt((*sp)->GetDeclaringClass());
    }
  }

  void Visit() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE {
    Primitive::Type type = GetParamPrimitiveType();
    switch (type) {
      case Primitive::kPrimLong: {
        jlong long_arg;
        if (IsSplitLongOrDouble()) {
          long_arg = ReadSplitLongParam();
        } else {
          long_arg = *reinterpret_cast<jlong*>(GetParamAddress());
        }
        PushLongArgument(long_arg);
        break;
      }
      case Primitive::kPrimDouble: {
        uint64_t double_arg;
        if (IsSplitLongOrDouble()) {
          // Read into union so that we don't case to a double.
          double_arg = ReadSplitLongParam();
        } else {
          double_arg = *reinterpret_cast<uint64_t*>(GetParamAddress());
        }
        PushDoubleArgument(double_arg);
        break;
      }
      case Primitive::kPrimNot: {
        StackReference<mirror::Object>* stack_ref =
            reinterpret_cast<StackReference<mirror::Object>*>(GetParamAddress());
        PushArgumentInSirt(stack_ref->AsMirrorPtr());
        break;
      }
      case Primitive::kPrimFloat:
        PushFloatArgument(*reinterpret_cast<int32_t*>(GetParamAddress()));
        break;
      case Primitive::kPrimBoolean:  // Fall-through.
      case Primitive::kPrimByte:     // Fall-through.
      case Primitive::kPrimChar:     // Fall-through.
      case Primitive::kPrimShort:    // Fall-through.
      case Primitive::kPrimInt:      // Fall-through.
        PushIntArgument(*reinterpret_cast<jint*>(GetParamAddress()));
        break;
      case Primitive::kPrimVoid:
        LOG(FATAL) << "UNREACHABLE";
        break;
    }
  }

  void FinalizeSirt(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if (!IsAligned<8>(StackIndirectReferenceTable::SizeOf(sirt_number_of_references_))) {
      sirt_number_of_references_++;
      *cur_sirt_entry_ = StackReference<mirror::Object>();
      cur_sirt_entry_--;
    }
    CHECK(IsAligned<8>(StackIndirectReferenceTable::SizeOf(sirt_number_of_references_)));
    StackIndirectReferenceTable* sirt = reinterpret_cast<StackIndirectReferenceTable*>(
        top_of_sirt_ - StackIndirectReferenceTable::SizeOf(sirt_number_of_references_));

    sirt->SetNumberOfReferences(sirt_number_of_references_);
    self->PushSirt(sirt);
  }

  jobject GetFirstSirtEntry() {
    return reinterpret_cast<jobject>(reinterpret_cast<StackReference<mirror::Object>*>(top_of_sirt_) - 1);
  }

 private:
  void PushArgumentInSirt(mirror::Object* obj) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    // Do something to push into the SIRT.
    uintptr_t sirt_or_null;
    if (obj != nullptr) {
      sirt_number_of_references_++;
      *cur_sirt_entry_ = StackReference<mirror::Object>::FromMirrorPtr(obj);
      sirt_or_null = reinterpret_cast<uintptr_t>(cur_sirt_entry_);
      cur_sirt_entry_--;
    } else {
      sirt_or_null = reinterpret_cast<uintptr_t>(nullptr);
    }
    // Push the GPR or stack arg.
    if (gpr_index_ > 0) {
      *cur_gpr_reg_ = sirt_or_null;
      cur_gpr_reg_++;
      gpr_index_--;
    } else {
      *cur_stack_arg_ = sirt_or_null;
      cur_stack_arg_++;
    }
  }

  void PushPointer(void* val) {
    if (gpr_index_ > 0) {
      *cur_gpr_reg_ = reinterpret_cast<uintptr_t>(val);
      cur_gpr_reg_++;
      gpr_index_--;
    } else {
      *cur_stack_arg_ = reinterpret_cast<uintptr_t>(val);
      cur_stack_arg_++;
    }
  }

  void PushIntArgument(jint val) {
    if (gpr_index_ > 0) {
      *cur_gpr_reg_ = val;
      cur_gpr_reg_++;
      gpr_index_--;
    } else {
      *cur_stack_arg_ = val;
      cur_stack_arg_++;
    }
  }

  void PushLongArgument(jlong val) {
    // This is an ugly hack for the following problem:
    //  Assume odd number of 32b registers. Then having exactly kRegsNeeded left needs to spill!
    if (gpr_index_ >= kRegistersNeededForLong + (kNumNativeGprArgs % kRegistersNeededForLong)) {
      if (kRegistersNeededForLong > 1 && ((kNumNativeGprArgs - gpr_index_) & 1) == 1) {
        // Pad.
        gpr_index_--;
        cur_gpr_reg_++;
      }
      uint64_t* tmp = reinterpret_cast<uint64_t*>(cur_gpr_reg_);
      *tmp = val;
      cur_gpr_reg_ += kRegistersNeededForLong;
      gpr_index_ -= kRegistersNeededForLong;
    } else {
      uint64_t* tmp = reinterpret_cast<uint64_t*>(cur_stack_arg_);
      *tmp = val;
      cur_stack_arg_ += kRegistersNeededForLong;

      gpr_index_ = 0;                   // can't use GPRs anymore
    }
  }

  void PushFloatArgument(int32_t val) {
    if (kNativeSoftFloatAbi) {
      PushIntArgument(val);
    } else {
      if (fpr_index_ > 0) {
        *cur_fpr_reg_ = val;
        cur_fpr_reg_++;
        if (kRegistersNeededForDouble == 1) {
          // will pop 64 bits from the stack
          // TODO: extend/clear bits???
          cur_fpr_reg_++;
        }
        fpr_index_--;
      } else {
        // TODO: Check ABI for floats.
        *cur_stack_arg_ = val;
        cur_stack_arg_++;
      }
    }
  }

  void PushDoubleArgument(uint64_t val) {
    // See PushLongArgument for explanation
    if (fpr_index_ >= kRegistersNeededForDouble + (kNumNativeFprArgs % kRegistersNeededForDouble)) {
      if (kRegistersNeededForDouble > 1 && ((kNumNativeFprArgs - fpr_index_) & 1) == 1) {
        // Pad.
        fpr_index_--;
        cur_fpr_reg_++;
      }
      uint64_t* tmp = reinterpret_cast<uint64_t*>(cur_fpr_reg_);
      *tmp = val;
      // TODO: the whole thing doesn't make sense if we take uint32_t*...
      cur_fpr_reg_ += 2;        // kRegistersNeededForDouble;
      fpr_index_ -= kRegistersNeededForDouble;
    } else {
      if (!IsAligned<8>(cur_stack_arg_)) {
        cur_stack_arg_++;  // Pad.
      }
      uint64_t* tmp = reinterpret_cast<uint64_t*>(cur_stack_arg_);
      *tmp = val;
      cur_stack_arg_ += kRegistersNeededForDouble;

      fpr_index_ = 0;                   // can't use FPRs anymore
    }
  }

  uint32_t sirt_number_of_references_;
  StackReference<mirror::Object>* cur_sirt_entry_;
  uint32_t gpr_index_;           // should be uint, but gives error because on some archs no regs
  uintptr_t* cur_gpr_reg_;
  uint32_t fpr_index_;           //                      ----- # -----
  uint32_t* cur_fpr_reg_;
  uintptr_t* cur_stack_arg_;
  uint8_t* top_of_sirt_;

  DISALLOW_COPY_AND_ASSIGN(BuildGenericJniFrameVisitor);
};

extern "C" const void* artQuickGenericJniTrampoline(Thread* self, mirror::ArtMethod** sp)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  uint32_t* sp32 = reinterpret_cast<uint32_t*>(sp);
  mirror::ArtMethod* called = *sp;
  DCHECK(called->IsNative());

  // run the visitor
  MethodHelper mh(called);
  BuildGenericJniFrameVisitor visitor(sp, called->IsStatic(), mh.GetShorty(), mh.GetShortyLength(),
                                      self);
  visitor.VisitArguments();
  visitor.FinalizeSirt(self);

  // fix up managed-stack things in Thread
  self->SetTopOfStack(sp, 0);

  // start JNI, save the cookie
  uint32_t cookie;
  if (called->IsSynchronized()) {
    cookie = JniMethodStartSynchronized(visitor.GetFirstSirtEntry(), self);
    // TODO: error checking.
    if (self->IsExceptionPending()) {
      self->PopSirt();
      return nullptr;
    }
  } else {
    cookie = JniMethodStart(self);
  }
  *(sp32-1) = cookie;

  // retrieve native code
  const void* nativeCode = called->GetNativeMethod();
  if (nativeCode == nullptr) {
    // TODO: is this really an error, or do we need to try to find native code?
    LOG(FATAL) << "Finding native code not implemented yet.";
  }

  return nativeCode;
}

/*
 * Is called after the native JNI code. Responsible for cleanup (SIRT, saved state) and
 * unlocking.
 */
extern "C" uint64_t artQuickGenericJniEndTrampoline(Thread* self, mirror::ArtMethod** sp,
                                                    jvalue result, uint64_t result_f)
    SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
  uint32_t* sp32 = reinterpret_cast<uint32_t*>(sp);
  mirror::ArtMethod* called = *sp;
  uint32_t cookie = *(sp32-1);

  // TODO: synchronized.
  MethodHelper mh(called);
  char return_shorty_char = mh.GetShorty()[0];

  if (return_shorty_char == 'L') {
    // the only special ending call
    if (called->IsSynchronized()) {
      BuildGenericJniFrameVisitor visitor(sp, called->IsStatic(), mh.GetShorty(),
                                          mh.GetShortyLength(), self);
      return reinterpret_cast<uint64_t>(JniMethodEndWithReferenceSynchronized(result.l, cookie,
                                                                              visitor.GetFirstSirtEntry(),
                                                                              self));
    } else {
      return reinterpret_cast<uint64_t>(JniMethodEndWithReference(result.l, cookie, self));
    }
  } else {
    if (called->IsSynchronized()) {
      // run the visitor
      BuildGenericJniFrameVisitor visitor(sp, called->IsStatic(), mh.GetShorty(),
                                          mh.GetShortyLength(), self);
      JniMethodEndSynchronized(cookie, visitor.GetFirstSirtEntry(), self);
    } else {
      JniMethodEnd(cookie, self);
    }

    switch (return_shorty_char) {
      case 'F':  // Fall-through.
      case 'D':
        return result_f;
      case 'Z':
        return result.z;
      case 'B':
        return result.b;
      case 'C':
        return result.c;
      case 'S':
        return result.s;
      case 'I':
        return result.i;
      case 'J':
        return result.j;
      case 'V':
        return 0;
      default:
        LOG(FATAL) << "Unexpected return shorty character " << return_shorty_char;
        return 0;
    }
  }
}

}  // namespace art