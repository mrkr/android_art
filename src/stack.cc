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

#include "stack.h"

#include "compiler.h"
#include "object.h"
#include "object_utils.h"
#include "thread_list.h"

int oatVRegOffset(const art::DexFile::CodeItem* code_item,
                  uint32_t core_spills, uint32_t fp_spills,
                  size_t frame_size, int reg);

namespace art {

bool Frame::HasMethod() const {
  return GetMethod() != NULL && (!GetMethod()->IsCalleeSaveMethod());
}

void Frame::Next() {
  size_t frame_size = GetMethod()->GetFrameSizeInBytes();
  DCHECK_NE(frame_size, 0u);
  DCHECK_LT(frame_size, 1024u);
  byte* next_sp = reinterpret_cast<byte*>(sp_) + frame_size;
  sp_ = reinterpret_cast<Method**>(next_sp);
  if (*sp_ != NULL) {
    DCHECK((*sp_)->GetClass() == Method::GetMethodClass() ||
        (*sp_)->GetClass() == Method::GetConstructorClass());
  }
}

uintptr_t Frame::GetReturnPC() const {
  byte* pc_addr = reinterpret_cast<byte*>(sp_) + GetMethod()->GetReturnPcOffsetInBytes();
  return *reinterpret_cast<uintptr_t*>(pc_addr);
}

void Frame::SetReturnPC(uintptr_t pc) {
  byte* pc_addr = reinterpret_cast<byte*>(sp_) + GetMethod()->GetReturnPcOffsetInBytes();
  *reinterpret_cast<uintptr_t*>(pc_addr) = pc;
}

uint32_t Frame::GetVReg(const DexFile::CodeItem* code_item, uint32_t core_spills,
                        uint32_t fp_spills, size_t frame_size, int vreg) const {
  int offset = oatVRegOffset(code_item, core_spills, fp_spills, frame_size, vreg);
  byte* vreg_addr = reinterpret_cast<byte*>(sp_) + offset;
  return *reinterpret_cast<uint32_t*>(vreg_addr);
}

uint32_t Frame::GetVReg(Method* m, int vreg) const {
  DCHECK(m == GetMethod());
  const art::DexFile::CodeItem* code_item = MethodHelper(m).GetCodeItem();
  DCHECK(code_item != NULL);  // can't be NULL or how would we compile its instructions?
  uint32_t core_spills = m->GetCoreSpillMask();
  uint32_t fp_spills = m->GetFpSpillMask();
  size_t frame_size = m->GetFrameSizeInBytes();
  return GetVReg(code_item, core_spills, fp_spills, frame_size, vreg);
}

void Frame::SetVReg(Method* m, int vreg, uint32_t new_value) {
  DCHECK(m == GetMethod());
  const art::DexFile::CodeItem* code_item = MethodHelper(m).GetCodeItem();
  DCHECK(code_item != NULL);  // can't be NULL or how would we compile its instructions?
  uint32_t core_spills = m->GetCoreSpillMask();
  uint32_t fp_spills = m->GetFpSpillMask();
  size_t frame_size = m->GetFrameSizeInBytes();
  int offset = oatVRegOffset(code_item, core_spills, fp_spills, frame_size, vreg);
  byte* vreg_addr = reinterpret_cast<byte*>(sp_) + offset;
  *reinterpret_cast<uint32_t*>(vreg_addr) = new_value;
}

uintptr_t Frame::LoadCalleeSave(int num) const {
  // Callee saves are held at the top of the frame
  Method* method = GetMethod();
  DCHECK(method != NULL);
  size_t frame_size = method->GetFrameSizeInBytes();
  byte* save_addr = reinterpret_cast<byte*>(sp_) + frame_size - ((num + 1) * kPointerSize);
#if defined(__i386__)
  save_addr -= kPointerSize;  // account for return address
#endif
  return *reinterpret_cast<uintptr_t*>(save_addr);
}

Method* Frame::NextMethod() const {
  byte* next_sp = reinterpret_cast<byte*>(sp_) + GetMethod()->GetFrameSizeInBytes();
  return *reinterpret_cast<Method**>(next_sp);
}

class StackGetter {
 public:
  StackGetter(JNIEnv* env, Thread* thread) : env_(env), thread_(thread), trace_(NULL) {
  }

  static void Callback(void* arg) {
    reinterpret_cast<StackGetter*>(arg)->Callback();
  }

  jobject GetTrace() {
    return trace_;
  }

 private:
  void Callback() {
    trace_ = thread_->CreateInternalStackTrace(env_);
  }

  JNIEnv* env_;
  Thread* thread_;
  jobject trace_;
};

jobject GetThreadStack(JNIEnv* env, Thread* thread) {
  ThreadList* thread_list = Runtime::Current()->GetThreadList();
  StackGetter stack_getter(env, thread);
  thread_list->RunWhileSuspended(thread, StackGetter::Callback, &stack_getter);
  return stack_getter.GetTrace();
}

}  // namespace art
