// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#include "core/codegen/common/settings.h"

namespace onnxruntime {
namespace nuphar_codegen {

constexpr static const char* kNupharDumpFusedNodes = "nuphar_dump_fused_nodes";
constexpr static const char* kNupharMatmulExec = "nuphar_matmul_exec";
constexpr static const char* kNupharCachePath = "nuphar_cache_path";
constexpr static const char* kNupharCacheVersion = "nuphar_cache_version";
constexpr static const char* kNupharCacheSoName = "nuphar_cache_so_name";
constexpr static const char* kNupharCacheModelChecksum = "nuphar_cache_model_checksum";
constexpr static const char* kNupharCacheForceNoJIT = "nuphar_cache_force_no_jit";

constexpr static const char* kNupharMatMulExec_ExternCpu = "extern_cpu";
constexpr static const char* kNupharSelectActivations_DeepCpu = "deep_cpu";
constexpr static const char* kNupharSelectActivations_Legacy = "legacy";

// Option to control nuphar code generation target (avx2 or avx512)
constexpr static const char* kNupharCodeGenTarget = "nuphar_codegen_target";

// cache version number (MAJOR.MINOR.PATCH) following https://semver.org/
// 1. MAJOR version when you make incompatible changes that old cache files no longer work,
// 2. MINOR version when you add functionality in a backwards - compatible manner, and
// 3. PATCH version when you make backwards - compatible bug fixes.
// NOTE this version needs to be updated when generated code may change
constexpr static const char* kNupharCacheVersion_current = "1.0.0";

constexpr static const char* kNupharCacheSoName_default = "jit.so";

void CreateNupharCodeGenSettings();

}  // namespace nuphar_codegen
}  // namespace onnxruntime
