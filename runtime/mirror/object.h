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

#ifndef ART_RUNTIME_MIRROR_OBJECT_H_
#define ART_RUNTIME_MIRROR_OBJECT_H_

#include "base/casts.h"
#include "base/logging.h"
#include "base/macros.h"
#include "cutils/atomic-inline.h"
#include "monitor.h"
#include "object_reference.h"
#include "offsets.h"
#include "runtime.h"
#include "verify_object.h"

namespace art {

class ImageWriter;
class LockWord;
struct ObjectOffsets;
class Thread;
template <typename T> class SirtRef;

namespace mirror {

class ArtField;
class ArtMethod;
class Array;
class Class;
template<class T> class ObjectArray;
template<class T> class PrimitiveArray;
typedef PrimitiveArray<uint8_t> BooleanArray;
typedef PrimitiveArray<int8_t> ByteArray;
typedef PrimitiveArray<uint16_t> CharArray;
typedef PrimitiveArray<double> DoubleArray;
typedef PrimitiveArray<float> FloatArray;
typedef PrimitiveArray<int32_t> IntArray;
typedef PrimitiveArray<int64_t> LongArray;
typedef PrimitiveArray<int16_t> ShortArray;
class String;
class Throwable;

// Fields within mirror objects aren't accessed directly so that the appropriate amount of
// handshaking is done with GC (for example, read and write barriers). This macro is used to
// compute an offset for the Set/Get methods defined in Object that can safely access fields.
#define OFFSET_OF_OBJECT_MEMBER(type, field) \
    MemberOffset(OFFSETOF_MEMBER(type, field))

// Checks that we don't do field assignments which violate the typing system.
static constexpr bool kCheckFieldAssignments = false;

// C++ mirror of java.lang.Object
class MANAGED LOCKABLE Object {
 public:
  static MemberOffset ClassOffset() {
    return OFFSET_OF_OBJECT_MEMBER(Object, klass_);
  }

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  Class* GetClass() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetClass(Class* new_klass) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  Object* GetBrooksPointer() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void SetBrooksPointer(Object* brooks_pointer) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void AssertSelfBrooksPointer() const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // The verifier treats all interfaces as java.lang.Object and relies on runtime checks in
  // invoke-interface to detect incompatible interface types.
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool VerifierInstanceOf(Class* klass) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool InstanceOf(Class* klass) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  size_t SizeOf() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  Object* Clone(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  int32_t IdentityHashCode() const SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  static MemberOffset MonitorOffset() {
    return OFFSET_OF_OBJECT_MEMBER(Object, monitor_);
  }

  LockWord GetLockWord();
  void SetLockWord(LockWord new_val);
  bool CasLockWord(LockWord old_val, LockWord new_val) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  uint32_t GetLockOwnerThreadId();

  mirror::Object* MonitorEnter(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      EXCLUSIVE_LOCK_FUNCTION();
  bool MonitorExit(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_)
      UNLOCK_FUNCTION();
  void Notify(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void NotifyAll(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void Wait(Thread* self) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void Wait(Thread* self, int64_t timeout, int32_t nanos) SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsClass() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  Class* AsClass() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsObjectArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<class T, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ObjectArray<T>* AsObjectArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsArrayInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  Array* AsArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  BooleanArray* AsBooleanArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ByteArray* AsByteArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ByteArray* AsByteSizedArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  CharArray* AsCharArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ShortArray* AsShortArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ShortArray* AsShortSizedArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  IntArray* AsIntArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  LongArray* AsLongArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  FloatArray* AsFloatArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  DoubleArray* AsDoubleArray() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  String* AsString() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  Throwable* AsThrowable() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsArtMethod() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ArtMethod* AsArtMethod() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsArtField() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  ArtField* AsArtField() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsWeakReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsSoftReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsFinalizerReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool IsPhantomReferenceInstance() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Accessor for Java type fields.
  template<class T, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  T* GetFieldObject(MemberOffset field_offset, bool is_volatile)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetFieldObjectWithoutWriteBarrier(MemberOffset field_offset, Object* new_value,
                                         bool is_volatile)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetFieldObject(MemberOffset field_offset, Object* new_value, bool is_volatile)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool CasFieldObject(MemberOffset field_offset, Object* old_value, Object* new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  HeapReference<Object>* GetFieldObjectReferenceAddr(MemberOffset field_offset);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  int32_t GetField32(MemberOffset field_offset, bool is_volatile)
      NO_THREAD_SAFETY_ANALYSIS;

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetField32(MemberOffset field_offset, int32_t new_value, bool is_volatile);
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool CasField32(MemberOffset field_offset, int32_t old_value, int32_t new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  int64_t GetField64(MemberOffset field_offset, bool is_volatile);
  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  void SetField64(MemberOffset field_offset, int64_t new_value, bool is_volatile);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  bool CasField64(MemberOffset field_offset, int64_t old_value, int64_t new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  template<bool kTransactionActive, bool kCheckTransaction = true,
      VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags, typename T>
  void SetFieldPtr(MemberOffset field_offset, T new_value, bool is_volatile) {
#ifndef __LP64__
    SetField32<kTransactionActive, kCheckTransaction, kVerifyFlags>(
        field_offset, reinterpret_cast<int32_t>(new_value), is_volatile);
#else
    SetField64<kTransactionActive, kCheckTransaction, kVerifyFlags>(
        field_offset, reinterpret_cast<int64_t>(new_value), is_volatile);
#endif
  }

 protected:
  // Accessors for non-Java type fields
  template<class T, VerifyObjectFlags kVerifyFlags = kDefaultVerifyFlags>
  T GetFieldPtr(MemberOffset field_offset, bool is_volatile) NO_THREAD_SAFETY_ANALYSIS {
#ifndef __LP64__
    return reinterpret_cast<T>(GetField32<kVerifyFlags>(field_offset, is_volatile));
#else
    return reinterpret_cast<T>(GetField64<kVerifyFlags>(field_offset, is_volatile));
#endif
  }

 private:
  // Verify the type correctness of stores to fields.
  void CheckFieldAssignmentImpl(MemberOffset field_offset, Object* new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void CheckFieldAssignment(MemberOffset field_offset, Object* new_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) {
    if (kCheckFieldAssignments) {
      CheckFieldAssignmentImpl(field_offset, new_value);
    }
  }

  // Generate an identity hash code.
  static int32_t GenerateIdentityHashCode();

  // The Class representing the type of the object.
  HeapReference<Class> klass_;
  // Monitor and hash code information.
  uint32_t monitor_;

#ifdef USE_BROOKS_POINTER
  // Note names use a 'x' prefix and the x_brooks_ptr_ is of type int
  // instead of Object to go with the alphabetical/by-type field order
  // on the Java side.
  uint32_t x_brooks_ptr_;  // For the Brooks pointer.
  uint32_t x_padding_;     // For 8-byte alignment. TODO: get rid of this.
#endif

  friend class art::ImageWriter;
  friend class art::Monitor;
  friend struct art::ObjectOffsets;  // for verifying offset information
  DISALLOW_IMPLICIT_CONSTRUCTORS(Object);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_OBJECT_H_