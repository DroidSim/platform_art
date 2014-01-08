/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ART_COMPILER_DEX_COMPILER_IR_H_
#define ART_COMPILER_DEX_COMPILER_IR_H_

#include <vector>
#include <llvm/IR/Module.h>
#include "arena_allocator.h"
#include "compiler_enums.h"
#include "dex/quick/mir_to_lir.h"
#include "dex_instruction.h"
#include "driver/compiler_driver.h"
#include "driver/dex_compilation_unit.h"
#include "llvm/intrinsic_helper.h"
#include "llvm/ir_builder.h"
#include "safe_map.h"
#include "base/timing_logger.h"

namespace art {

class LLVMInfo;
namespace llvm {
class LlvmCompilationUnit;
}  // namespace llvm

struct ArenaMemBlock;
class Backend;
struct Memstats;
class MIRGraph;
class Mir2Lir;

struct CompilationUnit {
  explicit CompilationUnit(ArenaPool* pool);
  ~CompilationUnit();

  void StartTimingSplit(const char* label);
  void NewTimingSplit(const char* label);
  void EndTiming();

  /*
   * Fields needed/generated by common frontend and generally used throughout
   * the compiler.
  */
  CompilerDriver* compiler_driver;
  ClassLinker* class_linker;           // Linker to resolve fields and methods.
  const DexFile* dex_file;             // DexFile containing the method being compiled.
  jobject class_loader;                // compiling method's class loader.
  uint16_t class_def_idx;              // compiling method's defining class definition index.
  uint32_t method_idx;                 // compiling method's index into method_ids of DexFile.
  const DexFile::CodeItem* code_item;  // compiling method's DexFile code_item.
  uint32_t access_flags;               // compiling method's access flags.
  InvokeType invoke_type;              // compiling method's invocation type.
  const char* shorty;                  // compiling method's shorty.
  uint32_t disable_opt;                // opt_control_vector flags.
  uint32_t enable_debug;               // debugControlVector flags.
  bool verbose;
  CompilerBackend compiler_backend;
  InstructionSet instruction_set;

  const InstructionSetFeatures& GetInstructionSetFeatures() {
    return compiler_driver->GetInstructionSetFeatures();
  }
  // TODO: much of this info available elsewhere.  Go to the original source?
  uint16_t num_dalvik_registers;        // method->registers_size.
  const uint16_t* insns;
  uint16_t num_ins;
  uint16_t num_outs;
  uint16_t num_regs;            // Unlike num_dalvik_registers, does not include ins.

  // If non-empty, apply optimizer/debug flags only to matching methods.
  std::string compiler_method_match;
  // Flips sense of compiler_method_match - apply flags if doesn't match.
  bool compiler_flip_match;

  // TODO: move memory management to mir_graph, or just switch to using standard containers.
  ArenaAllocator arena;

  UniquePtr<MIRGraph> mir_graph;   // MIR container.
  UniquePtr<Backend> cg;           // Target-specific codegen.
  TimingLogger timings;
};

}  // namespace art

#endif  // ART_COMPILER_DEX_COMPILER_IR_H_
