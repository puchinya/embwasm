// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This API registry has been designed and implemented entirely from scratch
// to map host functions to WASM imports without dynamic memory allocation or 
// dependency on third-party runtime systems.
// =============================================================================

#include "wasm_api.h"

namespace embwasm {

WasmApiRegistry::WasmApiRegistry() noexcept {}

WasmResult WasmApiRegistry::Register(const char* module_name, const char* field_name, HostFunction func) noexcept {
    // 静的登録に移行したため、実行時の動的登録は行いません。
    (void)module_name;
    (void)field_name;
    (void)func;
    return WasmResult::kOk;
}

HostFunction WasmApiRegistry::Lookup(const char* module_name, const char* field_name) const noexcept {
    // 自動生成された静的テーブルから二分探索で検索
    return LookupStaticHostFunction(module_name, field_name);
}

} // namespace embwasm
