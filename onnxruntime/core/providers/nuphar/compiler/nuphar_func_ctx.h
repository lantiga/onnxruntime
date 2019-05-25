// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "core/common/common.h"
#include "core/codegen/common/common.h"
#include "core/graph/graph.h"
#include "core/framework/data_types.h"
#include "core/framework/tensor.h"
#include "core/providers/nuphar/compiler/nuphar_codegen_ctx.h"

#include <type_traits>

#include <tvm/tvm.h>
#include <tvm/build_module.h>

namespace onnxruntime {
namespace tvm_codegen {

enum class ControlFlowInfoType : unsigned int {
  Scan = 1,
};

struct ControlFlowInfo {
 private:
  ControlFlowInfoType type;

 public:
  ControlFlowInfo(ControlFlowInfoType _type) : type(_type) {}

  DYN_PROMOTE_BASE(ControlFlowInfo, ControlFlowInfoType, type)
};

// Add Promote support for ControlFlowInfo
// Note here we need to use DYN_PROMOTE instread of DYNAMIC_PROMOTE
// since ControlFlowInfo is a critical path
DYN_PROMOTE(ControlFlowInfo)

// NupharFuncInfo holds tvm::runtime::PackedFunc (the generated function)
// And corresponding static meta information to call it, like number of argument and offset
// Note NupharFuncInfo includes ONLY parameters from codegen
// but DOES NOT include any runtime information.

// The owner of NupharFuncInfo is currently NupharFunctionState.
// NupharFuncInfo is created in TVMCompiler and is consumed by ExecBlock
// Note all of vectors use numbers of PackedFunc's parameters as vector bounds
// (meaning vector.size() == numbers of PackedFunc's parameters)
// except those denoted with ort, which use numbers of ort op's parameters as vector bounds.
// -1 might be inserted a bubble to keep positions and sizes for later lookup.

struct NupharFuncInfo {
  // speicial value for *_func_indices
  enum : int {
    FuncIndex_Initializer = -1,
    FuncIndex_AliasedOutput = -2,
    FuncIndex_NonAliasedOutput = -3,
  };
  // PackedFunc name
  std::string name;
  // PackedFunc
  tvm::runtime::PackedFunc packed_func;
  // TVM DLDevice
  DLDeviceType device_type;

  struct FuncArgMeta {
    MLDataType dtype;
    // shapes with dimensions statically know or inferred at compile time
    // symbolic dim would have Dimension_Unknown and will be patched at runtime
    std::vector<int64_t> inferred_shape;
    std::vector<std::pair<size_t, std::string>> dim_symbols;
  };

  // Input meta
  std::vector<FuncArgMeta> input_metas;

  std::vector<int> ort_input_to_func_indices;

  // initializers meta
  std::vector<const Tensor*> intializers;

  // Output meta
  std::vector<FuncArgMeta> output_metas;
  std::vector<int> ort_output_to_func_indices;
  std::vector<std::pair<int, size_t>> ort_aliased_output_to_func_indices;  // A pair of (Ort dst index, TVM src index)

  // Tvm arg meta
  // Note the total arg number == input_count + output_count
  size_t input_count;   // input_count == real inputs + initializers
  size_t output_count;  // real outputs
  // tvm args (including input and outputs )
  std::vector<int> type_codes;

  std::unique_ptr<ControlFlowInfo> cf_info;
};

void FillNupharFuncInfo(NupharFuncInfo* func_info,
                        const Node& node,
                        const NupharCodeGenCtx& codegen_ctx,
                        tvm::Target tvm_target,
                        tvm::runtime::PackedFunc packed_func,
                        const std::string& name);

}  // namespace tvm_codegen
}  // namespace onnxruntime
