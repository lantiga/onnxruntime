// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/nuphar/compiler/x86/scheduler/nuphar_scheduler.h"

#include "core/providers/nuphar/common/analysis/subgraph_gen_stats.h"
#include "core/providers/nuphar/compiler/nuphar_codegen_ctx.h"
#include "core/codegen/target/generic/scheduler/schedule_utils.h"
#include "core/providers/nuphar/compiler/x86/scheduler/tensorize/intrin_gemv_ll_extern.h"
#include "core/providers/nuphar/compiler/x86/scheduler/tensorize/intrin_gemv_ll_ir.h"
#include "core/framework/op_kernel_info.h"
#include <tvm/tvm.h>

namespace onnxruntime {
namespace tvm_codegen {

bool TVM_SCHEDULER_CLASS(Softmax, NupharX86OrtOpType)::Evaluate(
    const tvm::Tensor& tensor,
    const Node*,
    CodeGenContext&,
    ScheduleContext& ctx_sched) {
  // TODO change it to the value from Target
  int64_t natural_vector_size = 16;

  // compute root the exp since it is reused more than once
  auto& tensor_exp = tensor->op->InputTensors()[0];
  bool status_vec = TryVectorization(tensor_exp, natural_vector_size, ctx_sched);
  bool status_root = InsertRootSchedule(tensor_exp, ctx_sched);
  return status_vec || status_root;
}

// Illustration purpose only for tensorization
static Status MatMulTensorization(const tvm::Tensor& tensor,
                                  ScheduleContext& ctx) {
  if (tensor->shape.size() != 2)
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Gemm output shape should be 2D");

  // TODO: remove compute_root
  InsertRootScheduleAndClosure(tensor, ctx);

// Demo for Tensorization with llvm extern function
#if 1
  int32_t factor_int32 = 16;
  NaiveLLVMExternGemvTensorization tensorization_method("NaiveLLVMExternGemv_Example", {factor_int32, factor_int32});

  auto shape = tensorization_method.Shape();
  auto compute_op = tensor->op.as<tvm::ComputeOpNode>();
  auto xy = compute_op->axis;
  auto x = xy[0];
  auto y = xy[1];
  auto z = compute_op->reduce_axis[0];

  tvm::IterVar yo, yi;
  ctx.schedule[tensor->op].split(y, shape[0], &yo, &yi);
  tvm::IterVar zo, zi;
  ctx.schedule[tensor->op].split(z, shape[1], &zo, &zi);
  ctx.schedule[tensor->op].reorder({x, yo, zo, yi, zi});
  ctx.schedule[tensor->op].tensorize(yi, tensorization_method.CreateTensorIntrin());
  ctx.schedule[tensor->op].pragma(yo, "import_llvm", tensorization_method.LLVMImportDef());
#endif

// Demo for Tensorization with llvm intrisic IR
#if 0
  NaiveLLVMIRGemvTensorization tensorization_method("NaiveLLVMIRGemv_Example");

  auto shape = tensorization_method.Shape();
  auto compute_op = tensor->op.as<tvm::ComputeOpNode>();
  auto xy = compute_op->axis;
  auto x = xy[0];
  auto y = xy[1];
  auto z = compute_op->reduce_axis[0];

  tvm::IterVar yo, yi;
  ctx.schedule[tensor->op].split(y, shape[0], &yo, &yi);
  tvm::IterVar zo, zi;
  ctx.schedule[tensor->op].split(z, shape[1], &zo, &zi);
  ctx.schedule[tensor->op].reorder({x, yo, zo, yi, zi});
  ctx.schedule[tensor->op].tensorize(yi, tensorization_method.CreateTensorIntrin());
#endif

  return Status::OK();
}

// this is not tested in onnxruntime_test_all, since extern has higher priority
// don't register it
bool TVM_SCHEDULER_CLASS(Gemm, NupharX86OrtOpType)::Evaluate(
    const tvm::Tensor& tensor,
    const Node* node,
    CodeGenContext&,
    ScheduleContext& ctx_sched) {
  ProtoHelperNodeContext ctx(*node);
  OpNodeProtoHelper<ProtoHelperNodeContext> attrs(&ctx);
  int64_t trans_A_64, trans_B_64;
  bool status_a = attrs.GetAttr<int64_t>("transA", &trans_A_64).IsOK();
  ORT_ENFORCE(status_a);
  bool status_b = attrs.GetAttr<int64_t>("transB", &trans_B_64).IsOK();
  ORT_ENFORCE(status_b);

  if (trans_A_64 == 0 && trans_B_64 == 1) {
    return MatMulTensorization(tensor, ctx_sched).IsOK();
  }
  return InsertRootSchedule(tensor, ctx_sched);
}

// OLD code from Conv schedule
static Status ConvScheduleX86(const tvm::Tensor& tensor,
                              NupharCodeGenCtx& ctx_codegen,
                              ScheduleContext& ctx_sched,
                              int block_size) {
  if (tensor->shape.size() != 4)
    return ORT_MAKE_STATUS(ONNXRUNTIME, FAIL, "Conv output shape should be 4D");

  InsertRootScheduleAndClosure(tensor, ctx_sched);

  auto compute_op = tensor->op.as<tvm::ComputeOpNode>();
  auto ncyx = compute_op->axis;
  auto b = ncyx[0];
  auto oc = ncyx[1];
  auto y = ncyx[2];
  auto x = ncyx[3];
  auto ic = compute_op->reduce_axis[0];
  auto m = compute_op->reduce_axis[1];
  auto n = compute_op->reduce_axis[2];

  tvm::Expr kfactor(4);  // todo: this factor for vectorization is tuned for conv2d_performance on AVX2, will need to be addressed later
  tvm::IterVar oc_chunk, oc_block;
  ctx_sched.schedule[tensor->op].split(oc, kfactor, &oc_chunk, &oc_block);

  tvm::Expr factor(block_size);  // factor for tiling and blocking
  tvm::IterVar ic_chunk, ic_block;
  ctx_sched.schedule[tensor->op].split(ic, factor, &ic_chunk, &ic_block);

  tvm::IterVar xo, xi;
  ctx_sched.schedule[tensor->op].split(x, factor, &xo, &xi);

  ctx_sched.schedule[tensor->op].reorder({b, oc_chunk, y, xo, ic_chunk, m, n, ic_block, xi, oc_block});

  if (ctx_codegen.GetCodeGenHandle()->enable_per_node_parallelized) {
    tvm::Array<tvm::IterVar> fused_axis;
    fused_axis.push_back(b);
    fused_axis.push_back(oc_chunk);
    fused_axis.push_back(y);
    fused_axis.push_back(xo);
    tvm::IterVar parallel_axis;
    ctx_sched.schedule[tensor->op].fuse(fused_axis, &parallel_axis);
    ctx_sched.schedule[tensor->op].parallel(parallel_axis);
  }
  ctx_sched.schedule[tensor->op].vectorize(oc_block);

  return Status::OK();
}

bool TVM_SCHEDULER_CLASS(Conv, NupharX86OrtOpType)::Evaluate(
    const tvm::Tensor& tensor,
    const Node* node,
    CodeGenContext& ctx_codegen,
    ScheduleContext& ctx_sched) {
  NupharCodeGenCtx* ctx_nuphar = Promote<NupharCodeGenCtx>(&ctx_codegen);
  return ConvScheduleX86(tensor, *ctx_nuphar, ctx_sched, 16).IsOK();
}  // namespace tvm_codegen

// seems only tested in double path
static Status MatMul_2DWeight_Schedule(
    const tvm::Tensor& tensor_C,
    NupharCodeGenCtx& ctx_codegen,
    ScheduleContext& ctx_sched,
    int block_size) {
  // implementation adapted from:
  // https://docs.tvm.ai/tutorials/optimize/opt_gemm.html#sphx-glr-tutorials-optimize-opt-gemm-py
  InsertRootScheduleAndClosure(tensor_C, ctx_sched);

  // write cache, note this needs to happen before any axis ops in tensor_C
  auto CC = ctx_sched.schedule.cache_write(tensor_C, "global");

  const auto& C_axis = tensor_C->op.as<tvm::ComputeOpNode>()->axis;
  auto C_rank = C_axis.size();
  auto x = C_axis[C_rank - 2];
  auto y = C_axis[C_rank - 1];
  tvm::Expr block(block_size);
  tvm::IterVar xo, yo, xi, yi;
  ctx_sched.schedule[tensor_C->op].tile(x, y, block, block, &xo, &yo, &xi, &yi);
  ctx_sched.schedule[CC->op].compute_at(ctx_sched.schedule[tensor_C->op], yo);

  // new inner axes
  const auto& CC_axis = CC->op.as<tvm::ComputeOpNode>()->axis;
  auto xc = CC_axis[C_rank - 2];
  auto yc = CC_axis[C_rank - 1];

  constexpr int num_unrolls = 4;
  auto split_factor = tvm::Expr(num_unrolls);
  auto k = ctx_sched.schedule[CC->op]->op.as<tvm::ComputeOpNode>()->reduce_axis[0];
  tvm::IterVar ko, ki;
  ctx_sched.schedule[CC->op].split(k, split_factor, &ko, &ki);
  tvm::Array<tvm::IterVar> reordered_axis;
  for (size_t d = 0; d < C_rank - 2; ++d)
    reordered_axis.push_back(CC_axis[d]);
  reordered_axis.push_back(ko);
  reordered_axis.push_back(xc);
  reordered_axis.push_back(ki);
  reordered_axis.push_back(yc);
  ctx_sched.schedule[CC->op].reorder(reordered_axis);
  ctx_sched.schedule[CC->op].unroll(ki);
  ctx_sched.schedule[CC->op].vectorize(yc);

  if (ctx_codegen.GetCodeGenHandle()->enable_per_node_parallelized) {
    // parallelize
    tvm::Array<tvm::IterVar> fused_axis;
    for (size_t d = 0; d < C_rank - 2; ++d)
      fused_axis.push_back(C_axis[d]);
    fused_axis.push_back(xo);
    tvm::IterVar fused_xo;
    ctx_sched.schedule[tensor_C->op].fuse(fused_axis, &fused_xo);
    ctx_sched.schedule[tensor_C->op].parallel(fused_xo);
  }

  return Status::OK();
}

bool TVM_SCHEDULER_CLASS(MatMul, NupharX86OrtOpType)::Evaluate(
    const tvm::Tensor& tensor,
    const Node* node,
    CodeGenContext& ctx_codegen,
    ScheduleContext& ctx_sched) {
  NupharCodeGenCtx* ctx_nuphar = Promote<NupharCodeGenCtx>(&ctx_codegen);

  if (tensor->dtype != HalideIR::Float(32)) {
    return MatMul_2DWeight_Schedule(tensor, *ctx_nuphar, ctx_sched, 16).IsOK();
  }
  return InsertRootSchedule(tensor, ctx_sched);
}

}  // namespace tvm_codegen
}  // namespace onnxruntime
