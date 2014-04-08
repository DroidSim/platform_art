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

#include "oat_writer.h"

#include <zlib.h>

#include "base/bit_vector.h"
#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#include "class_linker.h"
#include "dex_file-inl.h"
#include "dex/verification_results.h"
#include "gc/space/space.h"
#include "mirror/art_method-inl.h"
#include "mirror/array.h"
#include "mirror/class_loader.h"
#include "mirror/object-inl.h"
#include "os.h"
#include "output_stream.h"
#include "safe_map.h"
#include "scoped_thread_state_change.h"
#include "sirt_ref-inl.h"
#include "verifier/method_verifier.h"

namespace art {

#define DCHECK_OFFSET() \
  DCHECK_EQ(static_cast<off_t>(file_offset + relative_offset), out->Seek(0, kSeekCurrent)) \
    << "file_offset=" << file_offset << " relative_offset=" << relative_offset

#define DCHECK_OFFSET_() \
  DCHECK_EQ(static_cast<off_t>(file_offset + offset_), out->Seek(0, kSeekCurrent)) \
    << "file_offset=" << file_offset << " offset_=" << offset_

static void DCheckCodeAlignment(size_t offset, InstructionSet isa) {
  switch (isa) {
    case kArm:
      // Fall-through.
    case kThumb2:
      DCHECK_ALIGNED(offset, kArmAlignment);
      break;

    case kArm64:
      DCHECK_ALIGNED(offset, kArm64Alignment);
      break;

    case kMips:
      DCHECK_ALIGNED(offset, kMipsAlignment);
      break;

    case kX86_64:
      // Fall-through.
    case kX86:
      DCHECK_ALIGNED(offset, kX86Alignment);
      break;

    case kNone:
      // Use a DCHECK instead of FATAL so that in the non-debug case the whole switch can
      // be optimized away.
      DCHECK(false);
      break;
  }
}

OatWriter::OatWriter(const std::vector<const DexFile*>& dex_files,
                     uint32_t image_file_location_oat_checksum,
                     uintptr_t image_file_location_oat_begin,
                     const std::string& image_file_location,
                     const CompilerDriver* compiler,
                     TimingLogger* timings)
  : compiler_driver_(compiler),
    dex_files_(&dex_files),
    image_file_location_oat_checksum_(image_file_location_oat_checksum),
    image_file_location_oat_begin_(image_file_location_oat_begin),
    image_file_location_(image_file_location),
    oat_header_(NULL),
    size_dex_file_alignment_(0),
    size_executable_offset_alignment_(0),
    size_oat_header_(0),
    size_oat_header_image_file_location_(0),
    size_dex_file_(0),
    size_interpreter_to_interpreter_bridge_(0),
    size_interpreter_to_compiled_code_bridge_(0),
    size_jni_dlsym_lookup_(0),
    size_portable_imt_conflict_trampoline_(0),
    size_portable_resolution_trampoline_(0),
    size_portable_to_interpreter_bridge_(0),
    size_quick_generic_jni_trampoline_(0),
    size_quick_imt_conflict_trampoline_(0),
    size_quick_resolution_trampoline_(0),
    size_quick_to_interpreter_bridge_(0),
    size_trampoline_alignment_(0),
    size_method_header_(0),
    size_code_(0),
    size_code_alignment_(0),
    size_mapping_table_(0),
    size_vmap_table_(0),
    size_gc_map_(0),
    size_oat_dex_file_location_size_(0),
    size_oat_dex_file_location_data_(0),
    size_oat_dex_file_location_checksum_(0),
    size_oat_dex_file_offset_(0),
    size_oat_dex_file_methods_offsets_(0),
    size_oat_class_type_(0),
    size_oat_class_status_(0),
    size_oat_class_method_bitmaps_(0),
    size_oat_class_method_offsets_(0) {
  size_t offset;
  {
    TimingLogger::ScopedSplit split("InitOatHeader", timings);
    offset = InitOatHeader();
  }
  {
    TimingLogger::ScopedSplit split("InitOatDexFiles", timings);
    offset = InitOatDexFiles(offset);
  }
  {
    TimingLogger::ScopedSplit split("InitDexFiles", timings);
    offset = InitDexFiles(offset);
  }
  {
    TimingLogger::ScopedSplit split("InitOatClasses", timings);
    offset = InitOatClasses(offset);
  }
  {
    TimingLogger::ScopedSplit split("InitOatCode", timings);
    offset = InitOatCode(offset);
  }
  {
    TimingLogger::ScopedSplit split("InitOatCodeDexFiles", timings);
    offset = InitOatCodeDexFiles(offset);
  }
  size_ = offset;

  CHECK_EQ(dex_files_->size(), oat_dex_files_.size());
  CHECK(image_file_location.empty() == compiler->IsImage());
}

OatWriter::~OatWriter() {
  delete oat_header_;
  STLDeleteElements(&oat_dex_files_);
  STLDeleteElements(&oat_classes_);
}

struct OatWriter::GcMapBinder {
  static const std::vector<uint8_t>* GetMap(const CompiledMethod* compiled_method) ALWAYS_INLINE {
    return &compiled_method->GetGcMap();
  }

  static uint32_t GetOffset(OatClass* oat_class, size_t method_offsets_index) ALWAYS_INLINE {
    return oat_class->method_offsets_[method_offsets_index].gc_map_offset_;
  }

  static void SetOffset(OatClass* oat_class, size_t method_offsets_index, uint32_t offset)
      ALWAYS_INLINE {
    oat_class->method_offsets_[method_offsets_index].gc_map_offset_ = offset;
  }

  static SafeMap<const std::vector<uint8_t>*, uint32_t>* GetDedupeMap(OatWriter* writer)
      ALWAYS_INLINE {
    return &writer->gc_map_offsets_;
  }

  static const char* MapName() ALWAYS_INLINE {
    return "GC map";
  }
};

struct OatWriter::MappingTableBinder {
  static const std::vector<uint8_t>* GetMap(const CompiledMethod* compiled_method) ALWAYS_INLINE {
    return &compiled_method->GetMappingTable();
  }

  static uint32_t GetOffset(OatClass* oat_class, size_t method_offsets_index) ALWAYS_INLINE {
    return oat_class->method_offsets_[method_offsets_index].mapping_table_offset_;
  }

  static void SetOffset(OatClass* oat_class, size_t method_offsets_index, uint32_t offset)
      ALWAYS_INLINE {
    oat_class->method_offsets_[method_offsets_index].mapping_table_offset_ = offset;
  }

  static SafeMap<const std::vector<uint8_t>*, uint32_t>* GetDedupeMap(OatWriter* writer)
      ALWAYS_INLINE {
    return &writer->mapping_table_offsets_;
  }

  static const char* MapName() ALWAYS_INLINE {
    return "mapping table";
  }
};

struct OatWriter::VmapTableBinder {
  static const std::vector<uint8_t>* GetMap(const CompiledMethod* compiled_method) ALWAYS_INLINE {
    return &compiled_method->GetVmapTable();
  }

  static uint32_t GetOffset(OatClass* oat_class, size_t method_offsets_index) ALWAYS_INLINE {
    return oat_class->method_offsets_[method_offsets_index].vmap_table_offset_;
  }

  static void SetOffset(OatClass* oat_class, size_t method_offsets_index, uint32_t offset)
      ALWAYS_INLINE {
    oat_class->method_offsets_[method_offsets_index].vmap_table_offset_ = offset;
  }

  static SafeMap<const std::vector<uint8_t>*, uint32_t>* GetDedupeMap(OatWriter* writer)
      ALWAYS_INLINE {
    return &writer->vmap_table_offsets_;
  }

  static const char* MapName() ALWAYS_INLINE {
    return "vmap table";
  }
};

class OatWriter::DexMethodProcessor {
 public:
  DexMethodProcessor(OatWriter* writer, size_t offset)
    : writer_(writer),
      offset_(offset),
      dex_file_(nullptr),
      class_def_index_(0u) {
  }

  virtual bool StartClass(const DexFile* dex_file, size_t class_def_index) {
    dex_file_ = dex_file;
    class_def_index_ = class_def_index;
    return true;
  }

  virtual bool ProcessMethod(size_t class_def_method_index, const ClassDataItemIterator& it) = 0;

  virtual bool EndClass() {
    return true;
  }

  size_t Offset() const {
    return offset_;
  }

 protected:
  virtual ~DexMethodProcessor() { }

  OatWriter* writer_;
  size_t offset_;
  const DexFile* dex_file_;
  size_t class_def_index_;
};

class OatWriter::OatDexMethodProcessor : public DexMethodProcessor {
 public:
  OatDexMethodProcessor(OatWriter* writer, size_t offset)
    : DexMethodProcessor(writer, offset),
      oat_class_index_(0u),
      method_offsets_index_(0u) {
  }

  bool StartClass(const DexFile* dex_file, size_t class_def_index) {
    DexMethodProcessor::StartClass(dex_file, class_def_index);
    DCHECK_LT(oat_class_index_, writer_->oat_classes_.size());
    method_offsets_index_ = 0u;
    return true;
  }

  bool EndClass() {
    ++oat_class_index_;
    return DexMethodProcessor::EndClass();
  }

 protected:
  size_t oat_class_index_;
  size_t method_offsets_index_;
};

class OatWriter::InitOatClassesMethodProcessor : public DexMethodProcessor {
 public:
  InitOatClassesMethodProcessor(OatWriter* writer, size_t offset)
    : DexMethodProcessor(writer, offset),
      compiled_methods_(),
      num_non_null_compiled_methods_(0u) {
    compiled_methods_.reserve(256u);
  }

  bool StartClass(const DexFile* dex_file, size_t class_def_index) {
    DexMethodProcessor::StartClass(dex_file, class_def_index);
    compiled_methods_.clear();
    num_non_null_compiled_methods_ = 0u;
    return true;
  }

  bool ProcessMethod(size_t class_def_method_index, const ClassDataItemIterator& it) {
    // Fill in the compiled_methods_ array for methods that have a
    // CompiledMethod. We track the number of non-null entries in
    // num_non_null_compiled_methods_ since we only want to allocate
    // OatMethodOffsets for the compiled methods.
    uint32_t method_idx = it.GetMemberIndex();
    CompiledMethod* compiled_method =
        writer_->compiler_driver_->GetCompiledMethod(MethodReference(dex_file_, method_idx));
    compiled_methods_.push_back(compiled_method);
    if (compiled_method != nullptr) {
        ++num_non_null_compiled_methods_;
    }
    return true;
  }

  bool EndClass() {
    ClassReference class_ref(dex_file_, class_def_index_);
    CompiledClass* compiled_class = writer_->compiler_driver_->GetCompiledClass(class_ref);
    mirror::Class::Status status;
    if (compiled_class != NULL) {
      status = compiled_class->GetStatus();
    } else if (writer_->compiler_driver_->GetVerificationResults()->IsClassRejected(class_ref)) {
      status = mirror::Class::kStatusError;
    } else {
      status = mirror::Class::kStatusNotReady;
    }

    OatClass* oat_class = new OatClass(offset_, compiled_methods_,
                                       num_non_null_compiled_methods_, status);
    writer_->oat_classes_.push_back(oat_class);
    offset_ += oat_class->SizeOf();
    return DexMethodProcessor::EndClass();
  }

 private:
  std::vector<CompiledMethod*> compiled_methods_;
  size_t num_non_null_compiled_methods_;
};

class OatWriter::InitCodeMethodProcessor : public OatDexMethodProcessor {
 public:
  InitCodeMethodProcessor(OatWriter* writer, size_t offset)
    : OatDexMethodProcessor(writer, offset) {
  }

  bool ProcessMethod(size_t class_def_method_index, const ClassDataItemIterator& it)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    OatClass* oat_class = writer_->oat_classes_[oat_class_index_];
    CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != nullptr) {
      // Derived from CompiledMethod.
      uint32_t quick_code_offset = 0;
      uint32_t frame_size_in_bytes = kStackAlignment;
      uint32_t core_spill_mask = 0;
      uint32_t fp_spill_mask = 0;
      uint32_t mapping_table_offset = 0;
      uint32_t vmap_table_offset = 0;
      uint32_t gc_map_offset = 0;

      const std::vector<uint8_t>* portable_code = compiled_method->GetPortableCode();
      const std::vector<uint8_t>* quick_code = compiled_method->GetQuickCode();
      if (portable_code != nullptr) {
        CHECK(quick_code == nullptr);
        size_t oat_method_offsets_offset =
            oat_class->GetOatMethodOffsetsOffsetFromOatHeader(class_def_method_index);
        compiled_method->AddOatdataOffsetToCompliledCodeOffset(
            oat_method_offsets_offset + OFFSETOF_MEMBER(OatMethodOffsets, code_offset_));
      } else {
        CHECK(quick_code != nullptr);
        offset_ = compiled_method->AlignCode(offset_);
        DCheckCodeAlignment(offset_, compiled_method->GetInstructionSet());
        uint32_t code_size = quick_code->size() * sizeof(uint8_t);
        CHECK_NE(code_size, 0U);
        uint32_t thumb_offset = compiled_method->CodeDelta();
        quick_code_offset = offset_ + sizeof(OatMethodHeader) + thumb_offset;

        std::vector<uint8_t>* cfi_info = writer_->compiler_driver_->GetCallFrameInformation();
        if (cfi_info != nullptr) {
          // Copy in the FDE, if present
          const std::vector<uint8_t>* fde = compiled_method->GetCFIInfo();
          if (fde != nullptr) {
            // Copy the information into cfi_info and then fix the address in the new copy.
            int cur_offset = cfi_info->size();
            cfi_info->insert(cfi_info->end(), fde->begin(), fde->end());

            // Set the 'initial_location' field to address the start of the method.
            uint32_t new_value = quick_code_offset - writer_->oat_header_->GetExecutableOffset();
            uint32_t offset_to_update = cur_offset + 2*sizeof(uint32_t);
            (*cfi_info)[offset_to_update+0] = new_value;
            (*cfi_info)[offset_to_update+1] = new_value >> 8;
            (*cfi_info)[offset_to_update+2] = new_value >> 16;
            (*cfi_info)[offset_to_update+3] = new_value >> 24;
            std::string name = PrettyMethod(it.GetMemberIndex(), *dex_file_, false);
            writer_->method_info_.push_back(DebugInfo(name, new_value, new_value + code_size));
          }
        }

        // Deduplicate code arrays.
        auto code_iter = writer_->code_offsets_.find(quick_code);
        if (code_iter != writer_->code_offsets_.end()) {
          quick_code_offset = code_iter->second;
        } else {
          writer_->code_offsets_.Put(quick_code, quick_code_offset);
          OatMethodHeader method_header(code_size);
          offset_ += sizeof(method_header);  // Method header is prepended before code.
          writer_->oat_header_->UpdateChecksum(&method_header, sizeof(method_header));
          offset_ += code_size;
          writer_->oat_header_->UpdateChecksum(&(*quick_code)[0], code_size);
        }
      }
      frame_size_in_bytes = compiled_method->GetFrameSizeInBytes();
      core_spill_mask = compiled_method->GetCoreSpillMask();
      fp_spill_mask = compiled_method->GetFpSpillMask();

      if (kIsDebugBuild) {
        // We expect GC maps except when the class hasn't been verified or the method is native.
        const CompilerDriver* compiler_driver = writer_->compiler_driver_;
        ClassReference class_ref(dex_file_, class_def_index_);
        CompiledClass* compiled_class = compiler_driver->GetCompiledClass(class_ref);
        mirror::Class::Status status;
        if (compiled_class != NULL) {
          status = compiled_class->GetStatus();
        } else if (compiler_driver->GetVerificationResults()->IsClassRejected(class_ref)) {
          status = mirror::Class::kStatusError;
        } else {
          status = mirror::Class::kStatusNotReady;
        }
        const std::vector<uint8_t>& gc_map = compiled_method->GetGcMap();
        size_t gc_map_size = gc_map.size() * sizeof(gc_map[0]);
        bool is_native = (it.GetMemberAccessFlags() & kAccNative) != 0;
        CHECK(gc_map_size != 0 || is_native || status < mirror::Class::kStatusVerified)
            << &gc_map << " " << gc_map_size << " " << (is_native ? "true" : "false") << " "
            << (status < mirror::Class::kStatusVerified) << " " << status << " "
            << PrettyMethod(it.GetMemberIndex(), *dex_file_);
      }

      DCHECK_LT(method_offsets_index_, oat_class->method_offsets_.size());
      oat_class->method_offsets_[method_offsets_index_] =
          OatMethodOffsets(quick_code_offset,
                           frame_size_in_bytes,
                           core_spill_mask,
                           fp_spill_mask,
                           mapping_table_offset,
                           vmap_table_offset,
                           gc_map_offset);
      ++method_offsets_index_;
    }

    return true;
  }
};

template <typename MapBinder>
class OatWriter::InitMapMethodProcessor : public OatDexMethodProcessor {
 public:
  InitMapMethodProcessor(OatWriter* writer, size_t offset)
    : OatDexMethodProcessor(writer, offset) {
  }

  bool ProcessMethod(size_t class_def_method_index, const ClassDataItemIterator& it)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    OatClass* oat_class = writer_->oat_classes_[oat_class_index_];
    CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != nullptr) {
      DCHECK_LT(method_offsets_index_, oat_class->method_offsets_.size());
      DCHECK_EQ(MapBinder::GetOffset(oat_class, method_offsets_index_), 0u);

      const std::vector<uint8_t>* map = MapBinder::GetMap(compiled_method);
      uint32_t map_size = map->size() * sizeof((*map)[0]);
      if (map_size != 0u) {
        SafeMap<const std::vector<uint8_t>*, uint32_t>* dedupe_map =
            MapBinder::GetDedupeMap(writer_);
        auto it = dedupe_map->find(map);
        if (it != dedupe_map->end()) {
          MapBinder::SetOffset(oat_class, method_offsets_index_, it->second);
        } else {
          MapBinder::SetOffset(oat_class, method_offsets_index_, offset_);
          dedupe_map->Put(map, offset_);
          offset_ += map_size;
          writer_->oat_header_->UpdateChecksum(&(*map)[0], map_size);
        }
      }
      ++method_offsets_index_;
    }

    return true;
  }
};

class OatWriter::InitImageMethodProcessor : public OatDexMethodProcessor {
 public:
  InitImageMethodProcessor(OatWriter* writer, size_t offset)
    : OatDexMethodProcessor(writer, offset) {
  }

  bool ProcessMethod(size_t class_def_method_index, const ClassDataItemIterator& it)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    OatClass* oat_class = writer_->oat_classes_[oat_class_index_];
    CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    OatMethodOffsets offsets(0u, kStackAlignment, 0u, 0u, 0u, 0u, 0u);
    if (compiled_method != nullptr) {
      DCHECK_LT(method_offsets_index_, oat_class->method_offsets_.size());
      offsets = oat_class->method_offsets_[method_offsets_index_];
      ++method_offsets_index_;
    }

    // Derive frame size and spill masks for native methods without code:
    // These are generic JNI methods...
    uint32_t method_idx = it.GetMemberIndex();
    bool is_native = (it.GetMemberAccessFlags() & kAccNative) != 0;
    if (is_native && compiled_method == nullptr) {
      // Compute Sirt size as putting _every_ reference into it, even null ones.
      uint32_t s_len;
      const char* shorty = dex_file_->GetMethodShorty(dex_file_->GetMethodId(method_idx),
                                                      &s_len);
      DCHECK(shorty != nullptr);
      uint32_t refs = 1;    // Native method always has "this" or class.
      for (uint32_t i = 1; i < s_len; ++i) {
        if (shorty[i] == 'L') {
          refs++;
        }
      }
      InstructionSet trg_isa = writer_->compiler_driver_->GetInstructionSet();
      size_t pointer_size = 4;
      if (trg_isa == kArm64 || trg_isa == kX86_64) {
        pointer_size = 8;
      }
      size_t sirt_size = StackIndirectReferenceTable::GetAlignedSirtSizeTarget(pointer_size, refs);

      // Get the generic spill masks and base frame size.
      mirror::ArtMethod* callee_save_method =
          Runtime::Current()->GetCalleeSaveMethod(Runtime::kRefsAndArgs);

      offsets.frame_size_in_bytes_ = callee_save_method->GetFrameSizeInBytes() + sirt_size;
      offsets.core_spill_mask_ = callee_save_method->GetCoreSpillMask();
      offsets.fp_spill_mask_ = callee_save_method->GetFpSpillMask();
      DCHECK_EQ(offsets.mapping_table_offset_, 0u);
      DCHECK_EQ(offsets.vmap_table_offset_, 0u);
      DCHECK_EQ(offsets.gc_map_offset_, 0u);
    }

    ClassLinker* linker = Runtime::Current()->GetClassLinker();
    InvokeType invoke_type = it.GetMethodInvokeType(dex_file_->GetClassDef(class_def_index_));
    // Unchecked as we hold mutator_lock_ on entry.
    ScopedObjectAccessUnchecked soa(Thread::Current());
    SirtRef<mirror::DexCache> dex_cache(soa.Self(), linker->FindDexCache(*dex_file_));
    SirtRef<mirror::ClassLoader> class_loader(soa.Self(), nullptr);
    mirror::ArtMethod* method = linker->ResolveMethod(*dex_file_, method_idx, dex_cache,
                                                      class_loader, nullptr, invoke_type);
    CHECK(method != NULL);
    method->SetFrameSizeInBytes(offsets.frame_size_in_bytes_);
    method->SetCoreSpillMask(offsets.core_spill_mask_);
    method->SetFpSpillMask(offsets.fp_spill_mask_);
    method->SetOatMappingTableOffset(offsets.mapping_table_offset_);
    // Portable code offsets are set by ElfWriterMclinker::FixupCompiledCodeOffset after linking.
    method->SetQuickOatCodeOffset(offsets.code_offset_);
    method->SetOatVmapTableOffset(offsets.vmap_table_offset_);
    method->SetOatNativeGcMapOffset(offsets.gc_map_offset_);

    return true;
  }
};

class OatWriter::WriteCodeMethodProcessor : public OatDexMethodProcessor {
 public:
  WriteCodeMethodProcessor(OatWriter* writer, OutputStream* out, const size_t file_offset,
                             size_t relative_offset)
    : OatDexMethodProcessor(writer, relative_offset),
      out_(out),
      file_offset_(file_offset) {
  }

  bool ProcessMethod(size_t class_def_method_index, const ClassDataItemIterator& it) {
    OatClass* oat_class = writer_->oat_classes_[oat_class_index_];
    const CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != NULL) {  // ie. not an abstract method
      size_t file_offset = file_offset_;
      OutputStream* out = out_;

      const std::vector<uint8_t>* quick_code = compiled_method->GetQuickCode();
      if (quick_code != nullptr) {
        CHECK(compiled_method->GetPortableCode() == nullptr);
        uint32_t aligned_offset = compiled_method->AlignCode(offset_);
        uint32_t aligned_code_delta = aligned_offset - offset_;
        if (aligned_code_delta != 0) {
          static const uint8_t kPadding[] = {
              0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u
          };
          DCHECK_LE(aligned_code_delta, sizeof(kPadding));
          if (UNLIKELY(!out->WriteFully(kPadding, aligned_code_delta))) {
            ReportWriteFailure("code alignment padding", it);
            return false;
          }
          writer_->size_code_alignment_ += aligned_code_delta;
          offset_ += aligned_code_delta;
          DCHECK_OFFSET_();
        }
        DCheckCodeAlignment(offset_, compiled_method->GetInstructionSet());
        uint32_t code_size = quick_code->size() * sizeof(uint8_t);
        CHECK_NE(code_size, 0U);

        // Deduplicate code arrays.
        const OatMethodOffsets& method_offsets = oat_class->method_offsets_[method_offsets_index_];
        DCHECK(writer_->code_offsets_.find(quick_code) != writer_->code_offsets_.end());
        DCHECK(writer_->code_offsets_.find(quick_code)->second == method_offsets.code_offset_);
        DCHECK(method_offsets.code_offset_ < offset_ || method_offsets.code_offset_ ==
                   offset_ + sizeof(OatMethodHeader) + compiled_method->CodeDelta())
            << PrettyMethod(it.GetMemberIndex(), *dex_file_);
        if (method_offsets.code_offset_ >= offset_) {
          OatMethodHeader method_header(code_size);
          if (!out->WriteFully(&method_header, sizeof(method_header))) {
            ReportWriteFailure("method header", it);
            return false;
          }
          writer_->size_method_header_ += sizeof(method_header);
          offset_ += sizeof(method_header);
          DCHECK_OFFSET_();
          if (!out->WriteFully(&(*quick_code)[0], code_size)) {
            ReportWriteFailure("method code", it);
            return false;
          }
          writer_->size_code_ += code_size;
          offset_ += code_size;
        }
        DCHECK_OFFSET_();
      }
      ++method_offsets_index_;
    }

    return true;
  }

 private:
  OutputStream* const out_;
  size_t const file_offset_;

  void ReportWriteFailure(const char* what, const ClassDataItemIterator& it) {
    PLOG(ERROR) << "Failed to write " << what << " for "
        << PrettyMethod(it.GetMemberIndex(), *dex_file_) << " to " << out_->GetLocation();
  }
};

template <typename MapBinder>
class OatWriter::WriteMapMethodProcessor : public OatDexMethodProcessor {
 public:
  WriteMapMethodProcessor(OatWriter* writer, OutputStream* out, const size_t file_offset,
                          size_t relative_offset)
    : OatDexMethodProcessor(writer, relative_offset),
      out_(out),
      file_offset_(file_offset) {
  }

  bool ProcessMethod(size_t class_def_method_index, const ClassDataItemIterator& it) {
    OatClass* oat_class = writer_->oat_classes_[oat_class_index_];
    const CompiledMethod* compiled_method = oat_class->GetCompiledMethod(class_def_method_index);

    if (compiled_method != NULL) {  // ie. not an abstract method
      size_t file_offset = file_offset_;
      OutputStream* out = out_;

      uint32_t map_offset = MapBinder::GetOffset(oat_class, method_offsets_index_);
      ++method_offsets_index_;

      // Write deduplicated map.
      const std::vector<uint8_t>* map = MapBinder::GetMap(compiled_method);
      size_t map_size = map->size() * sizeof((*map)[0]);
      if (kIsDebugBuild) {
        CHECK((map_size == 0u && map_offset == 0u) ||
              (map_size != 0u && map_offset != 0u && map_offset <= offset_))
            << PrettyMethod(it.GetMemberIndex(), *dex_file_);
        if (map_size != 0u) {
          SafeMap<const std::vector<uint8_t>*, uint32_t>* dedupe_map =
              MapBinder::GetDedupeMap(writer_);
          auto map_iter = dedupe_map->find(map);
          CHECK(map_iter != dedupe_map->end());
          CHECK_EQ(map_iter->second, map_offset);
        }
      }
      if (map_size != 0u && map_offset == offset_) {
        if (UNLIKELY(!out->WriteFully(&(*map)[0], map_size))) {
          ReportWriteFailure(it);
          return false;
        }
        offset_ += map_size;
      }
      DCHECK_OFFSET_();
    }

    return true;
  }

 private:
  OutputStream* const out_;
  size_t const file_offset_;

  void ReportWriteFailure(const ClassDataItemIterator& it) {
    PLOG(ERROR) << "Failed to write " << MapBinder::MapName() << " for "
        << PrettyMethod(it.GetMemberIndex(), *dex_file_) << " to " << out_->GetLocation();
  }
};

// Process all methods from all classes in all dex files with the specified processor.
bool OatWriter::ProcessDexMethods(DexMethodProcessor* processor) {
  for (const DexFile* dex_file : *dex_files_) {
    const size_t class_def_count = dex_file->NumClassDefs();
    for (size_t class_def_index = 0; class_def_index != class_def_count; ++class_def_index) {
      if (UNLIKELY(!processor->StartClass(dex_file, class_def_index))) {
        return false;
      }
      const DexFile::ClassDef& class_def = dex_file->GetClassDef(class_def_index);
      const byte* class_data = dex_file->GetClassData(class_def);
      if (class_data != NULL) {  // ie not an empty class, such as a marker interface
        ClassDataItemIterator it(*dex_file, class_data);
        while (it.HasNextStaticField()) {
          it.Next();
        }
        while (it.HasNextInstanceField()) {
          it.Next();
        }
        size_t class_def_method_index = 0u;
        while (it.HasNextDirectMethod()) {
          if (!processor->ProcessMethod(class_def_method_index, it)) {
            return false;
          }
          ++class_def_method_index;
          it.Next();
        }
        while (it.HasNextVirtualMethod()) {
          if (UNLIKELY(!processor->ProcessMethod(class_def_method_index, it))) {
            return false;
          }
          ++class_def_method_index;
          it.Next();
        }
      }
      if (UNLIKELY(!processor->EndClass())) {
        return false;
      }
    }
  }
  return true;
}

size_t OatWriter::InitOatHeader() {
  // create the OatHeader
  oat_header_ = new OatHeader(compiler_driver_->GetInstructionSet(),
                              compiler_driver_->GetInstructionSetFeatures(),
                              dex_files_,
                              image_file_location_oat_checksum_,
                              image_file_location_oat_begin_,
                              image_file_location_);
  size_t offset = sizeof(*oat_header_);
  offset += image_file_location_.size();
  return offset;
}

size_t OatWriter::InitOatDexFiles(size_t offset) {
  // create the OatDexFiles
  for (size_t i = 0; i != dex_files_->size(); ++i) {
    const DexFile* dex_file = (*dex_files_)[i];
    CHECK(dex_file != NULL);
    OatDexFile* oat_dex_file = new OatDexFile(offset, *dex_file);
    oat_dex_files_.push_back(oat_dex_file);
    offset += oat_dex_file->SizeOf();
  }
  return offset;
}

size_t OatWriter::InitDexFiles(size_t offset) {
  // calculate the offsets within OatDexFiles to the DexFiles
  for (size_t i = 0; i != dex_files_->size(); ++i) {
    // dex files are required to be 4 byte aligned
    size_t original_offset = offset;
    offset = RoundUp(offset, 4);
    size_dex_file_alignment_ += offset - original_offset;

    // set offset in OatDexFile to DexFile
    oat_dex_files_[i]->dex_file_offset_ = offset;

    const DexFile* dex_file = (*dex_files_)[i];
    offset += dex_file->GetHeader().file_size_;
  }
  return offset;
}

size_t OatWriter::InitOatClasses(size_t offset) {
  // calculate the offsets within OatDexFiles to OatClasses
  InitOatClassesMethodProcessor processor(this, offset);
  bool success = ProcessDexMethods(&processor);
  CHECK(success);
  offset = processor.Offset();

  // Update oat_dex_files_.
  auto oat_class_it = oat_classes_.begin();
  for (OatDexFile* oat_dex_file : oat_dex_files_) {
    for (uint32_t& offset : oat_dex_file->methods_offsets_) {
      DCHECK(oat_class_it != oat_classes_.end());
      offset = (*oat_class_it)->offset_;
      ++oat_class_it;
    }
    oat_dex_file->UpdateChecksum(oat_header_);
  }
  CHECK(oat_class_it == oat_classes_.end());

  return offset;
}

size_t OatWriter::InitOatCode(size_t offset) {
  // calculate the offsets within OatHeader to executable code
  size_t old_offset = offset;
  // required to be on a new page boundary
  offset = RoundUp(offset, kPageSize);
  oat_header_->SetExecutableOffset(offset);
  size_executable_offset_alignment_ = offset - old_offset;
  if (compiler_driver_->IsImage()) {
    InstructionSet instruction_set = compiler_driver_->GetInstructionSet();

    #define DO_TRAMPOLINE(field, fn_name) \
      offset = CompiledCode::AlignCode(offset, instruction_set); \
      oat_header_->Set ## fn_name ## Offset(offset); \
      field.reset(compiler_driver_->Create ## fn_name()); \
      offset += field->size();

    DO_TRAMPOLINE(interpreter_to_interpreter_bridge_, InterpreterToInterpreterBridge);
    DO_TRAMPOLINE(interpreter_to_compiled_code_bridge_, InterpreterToCompiledCodeBridge);
    DO_TRAMPOLINE(jni_dlsym_lookup_, JniDlsymLookup);
    DO_TRAMPOLINE(portable_imt_conflict_trampoline_, PortableImtConflictTrampoline);
    DO_TRAMPOLINE(portable_resolution_trampoline_, PortableResolutionTrampoline);
    DO_TRAMPOLINE(portable_to_interpreter_bridge_, PortableToInterpreterBridge);
    DO_TRAMPOLINE(quick_generic_jni_trampoline_, QuickGenericJniTrampoline);
    DO_TRAMPOLINE(quick_imt_conflict_trampoline_, QuickImtConflictTrampoline);
    DO_TRAMPOLINE(quick_resolution_trampoline_, QuickResolutionTrampoline);
    DO_TRAMPOLINE(quick_to_interpreter_bridge_, QuickToInterpreterBridge);

    #undef DO_TRAMPOLINE
  } else {
    oat_header_->SetInterpreterToInterpreterBridgeOffset(0);
    oat_header_->SetInterpreterToCompiledCodeBridgeOffset(0);
    oat_header_->SetJniDlsymLookupOffset(0);
    oat_header_->SetPortableImtConflictTrampolineOffset(0);
    oat_header_->SetPortableResolutionTrampolineOffset(0);
    oat_header_->SetPortableToInterpreterBridgeOffset(0);
    oat_header_->SetQuickGenericJniTrampolineOffset(0);
    oat_header_->SetQuickImtConflictTrampolineOffset(0);
    oat_header_->SetQuickResolutionTrampolineOffset(0);
    oat_header_->SetQuickToInterpreterBridgeOffset(0);
  }
  return offset;
}

size_t OatWriter::InitOatCodeDexFiles(size_t offset) {
  #define PROCESS(ProcessorType)                      \
    do {                                              \
      ProcessorType processor(this, offset);          \
      bool success = ProcessDexMethods(&processor);   \
      DCHECK(success);                                \
      offset = processor.Offset();                    \
    } while (false)

  PROCESS(InitCodeMethodProcessor);
  PROCESS(InitMapMethodProcessor<GcMapBinder>);
  PROCESS(InitMapMethodProcessor<MappingTableBinder>);
  PROCESS(InitMapMethodProcessor<VmapTableBinder>);
  if (compiler_driver_->IsImage()) {
    PROCESS(InitImageMethodProcessor);
  }

  #undef PROCESS

  return offset;
}

bool OatWriter::Write(OutputStream* out) {
  const size_t file_offset = out->Seek(0, kSeekCurrent);

  if (!out->WriteFully(oat_header_, sizeof(*oat_header_))) {
    PLOG(ERROR) << "Failed to write oat header to " << out->GetLocation();
    return false;
  }
  size_oat_header_ += sizeof(*oat_header_);

  if (!out->WriteFully(image_file_location_.data(), image_file_location_.size())) {
    PLOG(ERROR) << "Failed to write oat header image file location to " << out->GetLocation();
    return false;
  }
  size_oat_header_image_file_location_ += image_file_location_.size();

  if (!WriteTables(out, file_offset)) {
    LOG(ERROR) << "Failed to write oat tables to " << out->GetLocation();
    return false;
  }

  size_t relative_offset = WriteCode(out, file_offset);
  if (relative_offset == 0) {
    LOG(ERROR) << "Failed to write oat code to " << out->GetLocation();
    return false;
  }

  relative_offset = WriteCodeDexFiles(out, file_offset, relative_offset);
  if (relative_offset == 0) {
    LOG(ERROR) << "Failed to write oat code for dex files to " << out->GetLocation();
    return false;
  }

  if (kIsDebugBuild) {
    uint32_t size_total = 0;
    #define DO_STAT(x) \
      VLOG(compiler) << #x "=" << PrettySize(x) << " (" << x << "B)"; \
      size_total += x;

    DO_STAT(size_dex_file_alignment_);
    DO_STAT(size_executable_offset_alignment_);
    DO_STAT(size_oat_header_);
    DO_STAT(size_oat_header_image_file_location_);
    DO_STAT(size_dex_file_);
    DO_STAT(size_interpreter_to_interpreter_bridge_);
    DO_STAT(size_interpreter_to_compiled_code_bridge_);
    DO_STAT(size_jni_dlsym_lookup_);
    DO_STAT(size_portable_imt_conflict_trampoline_);
    DO_STAT(size_portable_resolution_trampoline_);
    DO_STAT(size_portable_to_interpreter_bridge_);
    DO_STAT(size_quick_generic_jni_trampoline_);
    DO_STAT(size_quick_imt_conflict_trampoline_);
    DO_STAT(size_quick_resolution_trampoline_);
    DO_STAT(size_quick_to_interpreter_bridge_);
    DO_STAT(size_trampoline_alignment_);
    DO_STAT(size_method_header_);
    DO_STAT(size_code_);
    DO_STAT(size_code_alignment_);
    DO_STAT(size_mapping_table_);
    DO_STAT(size_vmap_table_);
    DO_STAT(size_gc_map_);
    DO_STAT(size_oat_dex_file_location_size_);
    DO_STAT(size_oat_dex_file_location_data_);
    DO_STAT(size_oat_dex_file_location_checksum_);
    DO_STAT(size_oat_dex_file_offset_);
    DO_STAT(size_oat_dex_file_methods_offsets_);
    DO_STAT(size_oat_class_type_);
    DO_STAT(size_oat_class_status_);
    DO_STAT(size_oat_class_method_bitmaps_);
    DO_STAT(size_oat_class_method_offsets_);
    #undef DO_STAT

    VLOG(compiler) << "size_total=" << PrettySize(size_total) << " (" << size_total << "B)"; \
    CHECK_EQ(size_, size_total) << size_mapping_table_;
    CHECK_EQ(file_offset + size_total, static_cast<uint32_t>(out->Seek(0, kSeekCurrent)));
  }

  CHECK_EQ(file_offset + size_, static_cast<uint32_t>(out->Seek(0, kSeekCurrent)));
  CHECK_EQ(size_, relative_offset);

  return true;
}

bool OatWriter::WriteTables(OutputStream* out, const size_t file_offset) {
  for (size_t i = 0; i != oat_dex_files_.size(); ++i) {
    if (!oat_dex_files_[i]->Write(this, out, file_offset)) {
      PLOG(ERROR) << "Failed to write oat dex information to " << out->GetLocation();
      return false;
    }
  }
  for (size_t i = 0; i != oat_dex_files_.size(); ++i) {
    uint32_t expected_offset = file_offset + oat_dex_files_[i]->dex_file_offset_;
    off_t actual_offset = out->Seek(expected_offset, kSeekSet);
    if (static_cast<uint32_t>(actual_offset) != expected_offset) {
      const DexFile* dex_file = (*dex_files_)[i];
      PLOG(ERROR) << "Failed to seek to dex file section. Actual: " << actual_offset
                  << " Expected: " << expected_offset << " File: " << dex_file->GetLocation();
      return false;
    }
    const DexFile* dex_file = (*dex_files_)[i];
    if (!out->WriteFully(&dex_file->GetHeader(), dex_file->GetHeader().file_size_)) {
      PLOG(ERROR) << "Failed to write dex file " << dex_file->GetLocation()
                  << " to " << out->GetLocation();
      return false;
    }
    size_dex_file_ += dex_file->GetHeader().file_size_;
  }
  for (size_t i = 0; i != oat_classes_.size(); ++i) {
    if (!oat_classes_[i]->Write(this, out, file_offset)) {
      PLOG(ERROR) << "Failed to write oat methods information to " << out->GetLocation();
      return false;
    }
  }
  return true;
}

size_t OatWriter::WriteCode(OutputStream* out, const size_t file_offset) {
  size_t relative_offset = oat_header_->GetExecutableOffset();
  off_t new_offset = out->Seek(size_executable_offset_alignment_, kSeekCurrent);
  size_t expected_file_offset = file_offset + relative_offset;
  if (static_cast<uint32_t>(new_offset) != expected_file_offset) {
    PLOG(ERROR) << "Failed to seek to oat code section. Actual: " << new_offset
                << " Expected: " << expected_file_offset << " File: " << out->GetLocation();
    return 0;
  }
  DCHECK_OFFSET();
  if (compiler_driver_->IsImage()) {
    InstructionSet instruction_set = compiler_driver_->GetInstructionSet();

    #define DO_TRAMPOLINE(field) \
      do { \
        uint32_t aligned_offset = CompiledCode::AlignCode(relative_offset, instruction_set); \
        uint32_t alignment_padding = aligned_offset - relative_offset; \
        out->Seek(alignment_padding, kSeekCurrent); \
        size_trampoline_alignment_ += alignment_padding; \
        if (!out->WriteFully(&(*field)[0], field->size())) { \
          PLOG(ERROR) << "Failed to write " # field " to " << out->GetLocation(); \
          return false; \
        } \
        size_ ## field += field->size(); \
        relative_offset += alignment_padding + field->size(); \
        DCHECK_OFFSET(); \
      } while (false)

    DO_TRAMPOLINE(interpreter_to_interpreter_bridge_);
    DO_TRAMPOLINE(interpreter_to_compiled_code_bridge_);
    DO_TRAMPOLINE(jni_dlsym_lookup_);
    DO_TRAMPOLINE(portable_imt_conflict_trampoline_);
    DO_TRAMPOLINE(portable_resolution_trampoline_);
    DO_TRAMPOLINE(portable_to_interpreter_bridge_);
    DO_TRAMPOLINE(quick_generic_jni_trampoline_);
    DO_TRAMPOLINE(quick_imt_conflict_trampoline_);
    DO_TRAMPOLINE(quick_resolution_trampoline_);
    DO_TRAMPOLINE(quick_to_interpreter_bridge_);
    #undef DO_TRAMPOLINE
  }
  return relative_offset;
}

size_t OatWriter::WriteCodeDexFiles(OutputStream* out,
                                    const size_t file_offset,
                                    size_t relative_offset) {
  #define PROCESS(ProcessorType)                                          \
    do {                                                                  \
      ProcessorType processor(this, out, file_offset, relative_offset);   \
      if (UNLIKELY(!ProcessDexMethods(&processor))) {                     \
        return 0;                                                         \
      }                                                                   \
      relative_offset = processor.Offset();                               \
    } while (false)

  PROCESS(WriteCodeMethodProcessor);

  size_t gc_maps_offset = relative_offset;
  PROCESS(WriteMapMethodProcessor<GcMapBinder>);
  size_gc_map_ = relative_offset - gc_maps_offset;

  size_t mapping_tables_offset = relative_offset;
  PROCESS(WriteMapMethodProcessor<MappingTableBinder>);
  size_mapping_table_ = relative_offset - mapping_tables_offset;

  size_t vmap_tables_offset = relative_offset;
  PROCESS(WriteMapMethodProcessor<VmapTableBinder>);
  size_vmap_table_ = relative_offset - vmap_tables_offset;

  #undef PROCESS

  return relative_offset;
}

OatWriter::OatDexFile::OatDexFile(size_t offset, const DexFile& dex_file) {
  offset_ = offset;
  const std::string& location(dex_file.GetLocation());
  dex_file_location_size_ = location.size();
  dex_file_location_data_ = reinterpret_cast<const uint8_t*>(location.data());
  dex_file_location_checksum_ = dex_file.GetLocationChecksum();
  dex_file_offset_ = 0;
  methods_offsets_.resize(dex_file.NumClassDefs());
}

size_t OatWriter::OatDexFile::SizeOf() const {
  return sizeof(dex_file_location_size_)
          + dex_file_location_size_
          + sizeof(dex_file_location_checksum_)
          + sizeof(dex_file_offset_)
          + (sizeof(methods_offsets_[0]) * methods_offsets_.size());
}

void OatWriter::OatDexFile::UpdateChecksum(OatHeader* oat_header) const {
  oat_header->UpdateChecksum(&dex_file_location_size_, sizeof(dex_file_location_size_));
  oat_header->UpdateChecksum(dex_file_location_data_, dex_file_location_size_);
  oat_header->UpdateChecksum(&dex_file_location_checksum_, sizeof(dex_file_location_checksum_));
  oat_header->UpdateChecksum(&dex_file_offset_, sizeof(dex_file_offset_));
  oat_header->UpdateChecksum(&methods_offsets_[0],
                            sizeof(methods_offsets_[0]) * methods_offsets_.size());
}

bool OatWriter::OatDexFile::Write(OatWriter* oat_writer,
                                  OutputStream* out,
                                  const size_t file_offset) const {
  DCHECK_OFFSET_();
  if (!out->WriteFully(&dex_file_location_size_, sizeof(dex_file_location_size_))) {
    PLOG(ERROR) << "Failed to write dex file location length to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_location_size_ += sizeof(dex_file_location_size_);
  if (!out->WriteFully(dex_file_location_data_, dex_file_location_size_)) {
    PLOG(ERROR) << "Failed to write dex file location data to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_location_data_ += dex_file_location_size_;
  if (!out->WriteFully(&dex_file_location_checksum_, sizeof(dex_file_location_checksum_))) {
    PLOG(ERROR) << "Failed to write dex file location checksum to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_location_checksum_ += sizeof(dex_file_location_checksum_);
  if (!out->WriteFully(&dex_file_offset_, sizeof(dex_file_offset_))) {
    PLOG(ERROR) << "Failed to write dex file offset to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_offset_ += sizeof(dex_file_offset_);
  if (!out->WriteFully(&methods_offsets_[0],
                      sizeof(methods_offsets_[0]) * methods_offsets_.size())) {
    PLOG(ERROR) << "Failed to write methods offsets to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_dex_file_methods_offsets_ +=
      sizeof(methods_offsets_[0]) * methods_offsets_.size();
  return true;
}

OatWriter::OatClass::OatClass(size_t offset,
                              const std::vector<CompiledMethod*>& compiled_methods,
                              uint32_t num_non_null_compiled_methods,
                              mirror::Class::Status status)
    : compiled_methods_(compiled_methods) {
  uint32_t num_methods = compiled_methods.size();
  CHECK_LE(num_non_null_compiled_methods, num_methods);

  offset_ = offset;
  oat_method_offsets_offsets_from_oat_class_.resize(num_methods);

  // Since both kOatClassNoneCompiled and kOatClassAllCompiled could
  // apply when there are 0 methods, we just arbitrarily say that 0
  // methods means kOatClassNoneCompiled and that we won't use
  // kOatClassAllCompiled unless there is at least one compiled
  // method. This means in an interpretter only system, we can assert
  // that all classes are kOatClassNoneCompiled.
  if (num_non_null_compiled_methods == 0) {
    type_ = kOatClassNoneCompiled;
  } else if (num_non_null_compiled_methods == num_methods) {
    type_ = kOatClassAllCompiled;
  } else {
    type_ = kOatClassSomeCompiled;
  }

  status_ = status;
  method_offsets_.resize(num_non_null_compiled_methods);

  uint32_t oat_method_offsets_offset_from_oat_class = sizeof(type_) + sizeof(status_);
  if (type_ == kOatClassSomeCompiled) {
    method_bitmap_ = new BitVector(num_methods, false, Allocator::GetMallocAllocator());
    method_bitmap_size_ = method_bitmap_->GetSizeOf();
    oat_method_offsets_offset_from_oat_class += sizeof(method_bitmap_size_);
    oat_method_offsets_offset_from_oat_class += method_bitmap_size_;
  } else {
    method_bitmap_ = NULL;
    method_bitmap_size_ = 0;
  }

  for (size_t i = 0; i < num_methods; i++) {
    CompiledMethod* compiled_method = compiled_methods_[i];
    if (compiled_method == NULL) {
      oat_method_offsets_offsets_from_oat_class_[i] = 0;
    } else {
      oat_method_offsets_offsets_from_oat_class_[i] = oat_method_offsets_offset_from_oat_class;
      oat_method_offsets_offset_from_oat_class += sizeof(OatMethodOffsets);
      if (type_ == kOatClassSomeCompiled) {
        method_bitmap_->SetBit(i);
      }
    }
  }
}

OatWriter::OatClass::~OatClass() {
  delete method_bitmap_;
}

size_t OatWriter::OatClass::GetOatMethodOffsetsOffsetFromOatHeader(
    size_t class_def_method_index_) const {
  uint32_t method_offset = GetOatMethodOffsetsOffsetFromOatClass(class_def_method_index_);
  if (method_offset == 0) {
    return 0;
  }
  return offset_ + method_offset;
}

size_t OatWriter::OatClass::GetOatMethodOffsetsOffsetFromOatClass(
    size_t class_def_method_index_) const {
  return oat_method_offsets_offsets_from_oat_class_[class_def_method_index_];
}

size_t OatWriter::OatClass::SizeOf() const {
  return sizeof(status_)
          + sizeof(type_)
          + ((method_bitmap_size_ == 0) ? 0 : sizeof(method_bitmap_size_))
          + method_bitmap_size_
          + (sizeof(method_offsets_[0]) * method_offsets_.size());
}

void OatWriter::OatClass::UpdateChecksum(OatHeader* oat_header) const {
  oat_header->UpdateChecksum(&status_, sizeof(status_));
  oat_header->UpdateChecksum(&type_, sizeof(type_));
  if (method_bitmap_size_ != 0) {
    CHECK_EQ(kOatClassSomeCompiled, type_);
    oat_header->UpdateChecksum(&method_bitmap_size_, sizeof(method_bitmap_size_));
    oat_header->UpdateChecksum(method_bitmap_->GetRawStorage(), method_bitmap_size_);
  }
  oat_header->UpdateChecksum(&method_offsets_[0],
                             sizeof(method_offsets_[0]) * method_offsets_.size());
}

bool OatWriter::OatClass::Write(OatWriter* oat_writer,
                                OutputStream* out,
                                const size_t file_offset) const {
  DCHECK_OFFSET_();
  if (!out->WriteFully(&status_, sizeof(status_))) {
    PLOG(ERROR) << "Failed to write class status to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_class_status_ += sizeof(status_);
  if (!out->WriteFully(&type_, sizeof(type_))) {
    PLOG(ERROR) << "Failed to write oat class type to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_class_type_ += sizeof(type_);
  if (method_bitmap_size_ != 0) {
    CHECK_EQ(kOatClassSomeCompiled, type_);
    if (!out->WriteFully(&method_bitmap_size_, sizeof(method_bitmap_size_))) {
      PLOG(ERROR) << "Failed to write method bitmap size to " << out->GetLocation();
      return false;
    }
    oat_writer->size_oat_class_method_bitmaps_ += sizeof(method_bitmap_size_);
    if (!out->WriteFully(method_bitmap_->GetRawStorage(), method_bitmap_size_)) {
      PLOG(ERROR) << "Failed to write method bitmap to " << out->GetLocation();
      return false;
    }
    oat_writer->size_oat_class_method_bitmaps_ += method_bitmap_size_;
  }
  if (!out->WriteFully(&method_offsets_[0],
                      sizeof(method_offsets_[0]) * method_offsets_.size())) {
    PLOG(ERROR) << "Failed to write method offsets to " << out->GetLocation();
    return false;
  }
  oat_writer->size_oat_class_method_offsets_ += sizeof(method_offsets_[0]) * method_offsets_.size();
  return true;
}

}  // namespace art
