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

#include "heap_bitmap.h"

#include "gc/space/space.h"

namespace art {
namespace gc {
namespace accounting {

void HeapBitmap::ReplaceBitmap(SpaceBitmap* old_bitmap, SpaceBitmap* new_bitmap) {
  for (auto& bitmap : continuous_space_bitmaps_) {
    if (bitmap == old_bitmap) {
      bitmap = new_bitmap;
      return;
    }
  }
  LOG(FATAL) << "bitmap " << static_cast<const void*>(old_bitmap) << " not found";
}

void HeapBitmap::ReplaceObjectSet(ObjectSet* old_set, ObjectSet* new_set) {
  for (auto& space_set : discontinuous_space_sets_) {
    if (space_set == old_set) {
      space_set = new_set;
      return;
    }
  }
  LOG(FATAL) << "object set " << static_cast<const void*>(old_set) << " not found";
}

void HeapBitmap::AddContinuousSpaceBitmap(accounting::SpaceBitmap* bitmap) {
  DCHECK(bitmap != NULL);

  // Check for interval overlap.
  for (const auto& cur_bitmap : continuous_space_bitmaps_) {
    CHECK(!(
        bitmap->HeapBegin() < cur_bitmap->HeapLimit() &&
        bitmap->HeapLimit() > cur_bitmap->HeapBegin()))
        << "Bitmap " << bitmap->Dump() << " overlaps with existing bitmap " << cur_bitmap->Dump();
  }
  continuous_space_bitmaps_.push_back(bitmap);
}

void HeapBitmap::RemoveContinuousSpaceBitmap(accounting::SpaceBitmap* bitmap) {
  auto it = std::find(continuous_space_bitmaps_.begin(), continuous_space_bitmaps_.end(), bitmap);
  DCHECK(it != continuous_space_bitmaps_.end());
  continuous_space_bitmaps_.erase(it);
}

void HeapBitmap::AddDiscontinuousObjectSet(ObjectSet* set) {
  DCHECK(set != NULL);
  discontinuous_space_sets_.push_back(set);
}

void HeapBitmap::RemoveDiscontinuousObjectSet(ObjectSet* set) {
  auto it = std::find(discontinuous_space_sets_.begin(), discontinuous_space_sets_.end(), set);
  DCHECK(it != discontinuous_space_sets_.end());
  discontinuous_space_sets_.erase(it);
}

void HeapBitmap::Walk(ObjectCallback* callback, void* arg) {
  for (const auto& bitmap : continuous_space_bitmaps_) {
    bitmap->Walk(callback, arg);
  }
  for (const auto& space_set : discontinuous_space_sets_) {
    space_set->Walk(callback, arg);
  }
}

}  // namespace accounting
}  // namespace gc
}  // namespace art