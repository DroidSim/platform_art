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

#ifndef ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_
#define ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_

#include "base/logging.h"
#include "gc_allocator.h"
#include "object_callbacks.h"
#include "space_bitmap.h"

namespace art {
namespace gc {

class Heap;

namespace accounting {

class HeapBitmap {
 public:
  bool Test(const mirror::Object* obj) SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    SpaceBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != nullptr)) {
      return bitmap->Test(obj);
    } else {
      return GetDiscontinuousSpaceObjectSet(obj) != NULL;
    }
  }

  void Clear(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    SpaceBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Clear(obj);
    } else {
      ObjectSet* set = GetDiscontinuousSpaceObjectSet(obj);
      DCHECK(set != NULL);
      set->Clear(obj);
    }
  }

  void Set(const mirror::Object* obj) EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_) {
    SpaceBitmap* bitmap = GetContinuousSpaceBitmap(obj);
    if (LIKELY(bitmap != NULL)) {
      bitmap->Set(obj);
    } else {
      ObjectSet* set = GetDiscontinuousSpaceObjectSet(obj);
      DCHECK(set != NULL);
      set->Set(obj);
    }
  }

  SpaceBitmap* GetContinuousSpaceBitmap(const mirror::Object* obj) {
    for (const auto& bitmap : continuous_space_bitmaps_) {
      if (bitmap->HasAddress(obj)) {
        return bitmap;
      }
    }
    return nullptr;
  }

  ObjectSet* GetDiscontinuousSpaceObjectSet(const mirror::Object* obj) {
    for (const auto& space_set : discontinuous_space_sets_) {
      if (space_set->Test(obj)) {
        return space_set;
      }
    }
    return nullptr;
  }

  void Walk(ObjectCallback* callback, void* arg)
      SHARED_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  template <typename Visitor>
  void Visit(const Visitor& visitor)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Find and replace a bitmap pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceBitmap(SpaceBitmap* old_bitmap, SpaceBitmap* new_bitmap)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  // Find and replace a object set pointer, this is used by for the bitmap swapping in the GC.
  void ReplaceObjectSet(ObjectSet* old_set, ObjectSet* new_set)
      EXCLUSIVE_LOCKS_REQUIRED(Locks::heap_bitmap_lock_);

  explicit HeapBitmap(Heap* heap) : heap_(heap) {}

 private:
  const Heap* const heap_;

  void AddContinuousSpaceBitmap(SpaceBitmap* bitmap);
  void RemoveContinuousSpaceBitmap(SpaceBitmap* bitmap);
  void AddDiscontinuousObjectSet(ObjectSet* set);
  void RemoveDiscontinuousObjectSet(ObjectSet* set);

  // Bitmaps covering continuous spaces.
  std::vector<SpaceBitmap*, GcAllocator<SpaceBitmap*> > continuous_space_bitmaps_;

  // Sets covering discontinuous spaces.
  std::vector<ObjectSet*, GcAllocator<ObjectSet*> > discontinuous_space_sets_;

  friend class art::gc::Heap;
};

}  // namespace accounting
}  // namespace gc
}  // namespace art

#endif  // ART_RUNTIME_GC_ACCOUNTING_HEAP_BITMAP_H_
