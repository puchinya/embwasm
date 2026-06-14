#ifndef EMBWASM_WASM_API_H_
#define EMBWASM_WASM_API_H_

#include "wasm_types.h"
#include "wasm_config.h"

namespace embwasm {

// ホストAPI（WASM向け公開API）のレジストリクラス
// 静的登録に移行したため、内部テーブルを持たず、自動生成された高速ルックアップ関数をラップします。
class WasmApiRegistry {
public:
    WasmApiRegistry() noexcept;

    // 非推奨: 静的登録に移行したため、Registerは何もせず常にkOkを返します。
    WasmResult Register(const char* module_name, const char* field_name, HostFunction func) noexcept;

    // 静的登録テーブルから二分探索でAPIを高速検索します。
    HostFunction Lookup(const char* module_name, const char* field_name) const noexcept;
};

// 静的に登録されたホストAPIを二分探索で高速検索します。
// O(log N) の計算量で検索が完了します。
HostFunction LookupStaticHostFunction(const char* module_name, const char* field_name) noexcept;

} // namespace embwasm

#endif // EMBWASM_WASM_API_H_
