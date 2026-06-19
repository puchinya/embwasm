#ifndef EMBWASM_WASM_API_HPP_
#define EMBWASM_WASM_API_HPP_

#include "wasm_types.hpp"

namespace embwasm {

/// @brief ホスト関数を一意に識別する ID 型。
///
/// 各アプリケーション固有の関数 ID は `constexpr` 定数としてキャストして定義します。
/// `gen_api.py` が自動生成します。
enum class HostFunctionId : uint32_t {
    kInvalid = 0xFFFFFFFF
};

/// @brief ホストモジュールを一意に識別する ID 型。`gen_api.py` が自動生成します。
enum class HostModuleId : uint32_t;

/// @brief 登録済みホストモジュールの総数。`gen_api.py` が自動生成します。
extern const std::size_t kHostModuleCount;

/// @brief モジュール名から静的ホストモジュール ID を二分探索で検索します。
/// @param module_name  モジュール名文字列（NULL 終端不要）。
/// @param module_len   モジュール名の長さ（バイト数）。
/// @return 対応する HostModuleId。見つからない場合は未定義値を返します。
HostModuleId LookupStaticHostModuleId(const char* module_name, std::size_t module_len) noexcept;

/// @brief モジュール名とフィールド名から静的ホスト API の ID を二分探索で検索します（O(log N)）。
/// @param module_name  インポートモジュール名（NULL 終端不要）。
/// @param module_len   モジュール名の長さ（バイト数）。
/// @param field_name   インポートフィールド名（NULL 終端不要）。
/// @param field_len    フィールド名の長さ（バイト数）。
/// @return 対応する HostFunctionId。見つからない場合は HostFunctionId::kInvalid を返します。
HostFunctionId LookupStaticHostFunctionId(const char* module_name, std::size_t module_len, const char* field_name, std::size_t field_len) noexcept;

class WasmEngine;

/// @brief すべての静的ホストモジュールを初期化します。`WasmEngine::Init()` から呼ばれます。
/// @param engine  初期化対象のエンジンインスタンス。
void InitializeAllHostModules(WasmEngine& engine) noexcept;

/// @brief すべての静的ホストモジュールを終了処理します。`WasmEngine::Deinit()` から呼ばれます。
/// @param engine  終了対象のエンジンインスタンス。
void DeinitializeAllHostModules(WasmEngine& engine) noexcept;

/// @brief ホスト API を ID に基づいて直接呼び出します（`switch` 文による静的ディスパッチ）。
///
/// 関数ポインタを排除し、静的解析ツールによる最悪スタック消費量の算出を可能にします。
/// @param engine       エンジンインスタンスへの参照。
/// @param id           呼び出す関数の HostFunctionId。
/// @param args         WASM 側から渡された引数配列。
/// @param arg_count    引数の個数。
/// @param results      WASM 側へ返す結果を格納する配列。
/// @param result_count 結果の個数。
/// @return 実行結果を示す WasmResult。
WasmResult DispatchHostFunction(
    WasmEngine& engine,
    HostFunctionId id,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept;

} // namespace embwasm

#endif // EMBWASM_WASM_API_HPP_
