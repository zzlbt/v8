// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/builtins/builtins-utils-gen.h"
#include "src/code-stub-assembler.h"

namespace v8 {
namespace internal {

class IntlBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit IntlBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}
};

TF_BUILTIN(StringPrototypeToLowerCaseIntl, IntlBuiltinsAssembler) {
  Node* const maybe_string = Parameter(Descriptor::kReceiver);
  Node* const context = Parameter(Descriptor::kContext);

  Node* const string =
      ToThisString(context, maybe_string, "String.prototype.toLowerCase");

  Label call_c(this), return_string(this), runtime(this, Label::kDeferred);

  // Early exit on empty strings.
  Node* const length = SmiUntag(LoadStringLength(string));
  GotoIf(IntPtrEqual(length, IntPtrConstant(0)), &return_string);

  // Unpack strings if possible, and bail to runtime unless we get a one-byte
  // flat string.
  ToDirectStringAssembler to_direct(
      state(), string, ToDirectStringAssembler::kDontUnpackSlicedStrings);
  to_direct.TryToDirect(&runtime);

  Node* const instance_type = to_direct.instance_type();
  CSA_ASSERT(this,
             Word32BinaryNot(IsIndirectStringInstanceType(instance_type)));
  GotoIfNot(IsOneByteStringInstanceType(instance_type), &runtime);

  // For short strings, do the conversion in CSA through the lookup table.

  Node* const dst = AllocateSeqOneByteString(context, length);

  const int kMaxShortStringLength = 24;  // Determined empirically.
  GotoIf(IntPtrGreaterThan(length, IntPtrConstant(kMaxShortStringLength)),
         &call_c);

  {
    Node* const dst_ptr = PointerToSeqStringData(dst);
    VARIABLE(var_cursor, MachineType::PointerRepresentation(),
             IntPtrConstant(0));

    Node* const start_address = to_direct.PointerToData(&call_c);
    Node* const end_address = IntPtrAdd(start_address, length);

    Node* const to_lower_table_addr = ExternalConstant(
        ExternalReference::intl_to_latin1_lower_table(isolate()));

    VariableList push_vars({&var_cursor}, zone());
    BuildFastLoop(
        push_vars, start_address, end_address,
        [=, &var_cursor](Node* current) {
          Node* c = ChangeInt32ToIntPtr(Load(MachineType::Uint8(), current));
          Node* lower = Load(MachineType::Uint8(), to_lower_table_addr, c);
          StoreNoWriteBarrier(MachineRepresentation::kWord8, dst_ptr,
                              var_cursor.value(), lower);
          Increment(var_cursor);
        },
        kCharSize, INTPTR_PARAMETERS, IndexAdvanceMode::kPost);

    // All lower-case.
    Return(dst);
  }

  // Call into C for case conversion. The signature is:
  // Object* ConvertOneByteToLower(String* src, String* dst, Isolate* isolate);
  BIND(&call_c);
  {
    Node* const src = to_direct.string();

    Node* const function_addr = ExternalConstant(
        ExternalReference::intl_convert_one_byte_to_lower(isolate()));
    Node* const isolate_ptr =
        ExternalConstant(ExternalReference::isolate_address(isolate()));

    MachineType type_ptr = MachineType::Pointer();
    MachineType type_tagged = MachineType::AnyTagged();

    Node* const result =
        CallCFunction3(type_tagged, type_tagged, type_tagged, type_ptr,
                       function_addr, src, dst, isolate_ptr);

    Return(result);
  }

  BIND(&return_string);
  Return(string);

  BIND(&runtime);
  {
    Node* const result =
        CallRuntime(Runtime::kStringToLowerCaseIntl, context, string);
    Return(result);
  }
}

}  // namespace internal
}  // namespace v8