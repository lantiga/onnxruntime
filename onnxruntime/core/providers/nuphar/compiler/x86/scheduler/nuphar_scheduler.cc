// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/nuphar/compiler/x86/scheduler/nuphar_scheduler.h"

#include "core/providers/nuphar/compiler/nuphar_codegen_ctx.h"
#include "core/providers/nuphar/common/analysis/subgraph_gen_stats.h"

namespace onnxruntime {
namespace tvm_codegen {

Scheduler* SCHEDULE_DISPATCHER_CLASS(NupharX86UseCount)::
    Find(const tvm::Tensor&, const Node* node, tvm_codegen::CodeGenContext& ctx) {
  if (nullptr == node)
    return nullptr;

  NupharCodeGenCtx* ctx_nuphar = Promote<NupharCodeGenCtx>(&ctx);
  bool reused = codegen::Promote<codegen::SubGraphStats>(ctx_nuphar->GetGraphStats())->NodeUseCount(node) > 1;

  if (reused)
    return DispatcherBase::Get("True");
  return DispatcherBase::Get("False");
}

Scheduler* SCHEDULE_DISPATCHER_CLASS(NupharX86PartialResult)::
    Find(const tvm::Tensor&, const Node* node, tvm_codegen::CodeGenContext&) {
  if (nullptr == node)
    return DispatcherBase::Get("True");
  return nullptr;
}

Scheduler* SCHEDULE_DISPATCHER_CLASS(NupharX86Tensorize)::
    Find(const tvm::Tensor& tensor, const Node* node, tvm_codegen::CodeGenContext&) {
  if (nullptr == node)
    return nullptr;

  // special checking to bypass tensorization
  // when fall back to extern function call
  if (tensor->op->InputTensors().size() > 0) {
    auto& imatmul = tensor->op->InputTensors()[0];
    auto extern_op = imatmul->op.as<tvm::ExternOpNode>();
    // Extern function call
    if (nullptr != extern_op)
      return nullptr;
  }

  return DispatcherBase::Get(node->OpType());
}

}  // namespace tvm_codegen
}  // namespace onnxruntime
