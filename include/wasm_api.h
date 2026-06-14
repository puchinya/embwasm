#ifndef EMBWASM_WASM_API_H_
#define EMBWASM_WASM_API_H_

#include "wasm_types.h"

namespace embwasm {

// 静的に登録されたホストAPIを二分探索で高速検索します。
// O(log N) の計算量で検索が完了します。
HostFunction LookupStaticHostFunction(const char* module_name, const char* field_name) noexcept;

} // namespace embwasm

#endif // EMBWASM_WASM_API_H_
