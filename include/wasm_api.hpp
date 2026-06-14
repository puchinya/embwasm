#ifndef EMBWASM_WASM_API_HPP_
#define EMBWASM_WASM_API_HPP_

#include "wasm_types.hpp"

namespace embwasm {

// ホスト関数のIDを表す型。
// 各アプリケーション固有の関数IDは constexpr 定数としてキャストして定義します。
enum class HostFunctionId : uint32_t {
    kInvalid = 0xFFFFFFFF
};

// ホストモジュールのIDを表す型（自動生成）
enum class HostModuleId : uint32_t;
extern const std::size_t kHostModuleCount;

// 静的に登録されたホストモジュールのIDを検索します。
HostModuleId LookupStaticHostModuleId(const char* module_name) noexcept;

// 静的に登録されたホストAPIのIDを検索します。
HostFunctionId LookupStaticHostFunctionId(const char* module_name, const char* field_name) noexcept;

class WasmEngine;

void InitializeAllHostModules(WasmEngine& engine) noexcept;
void DeinitializeAllHostModules(WasmEngine& engine) noexcept;

// ホストAPIのディスパッチャ（switch文による直接呼び出しを実装し、関数ポインタを排除します）
WasmResult DispatchHostFunction(
    WasmEngine& engine,
    HostFunctionId id,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept;

} // namespace embwasm

#endif // EMBWASM_WASM_API_HPP_
