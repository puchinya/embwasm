#ifndef EMBWASM_WASM_ENGINE_H_
#define EMBWASM_WASM_ENGINE_H_

#include "wasm_types.h"
#include "wasm_memory_pool.h"
#include "wasm_api.h"

namespace embwasm {

// WASM関数を表す構造体
struct WasmFunction {
    struct InternalFunc {
        const uint8_t* code_ptr;
        uint32_t code_size;
        uint32_t local_count;
    };
    bool is_import;
    uint32_t type_index;
    union {
        HostFunctionId host_func_id;
        InternalFunc internal_func;
    };
};

// WASMエクスポートを表す構造体
struct WasmExportEntry {
    const char* name;
    uint32_t func_index;
};

// ベアメタル環境向け極小WASM実行エンジン
class WasmEngine {
public:
    WasmEngine(WasmMemoryPool& pool) noexcept;

    // WASMバイナリ（バイト配列）の読み込みと解析
    WasmResult Load(const uint8_t* binary, std::size_t size) noexcept;

    // エクスポートされた関数を実行する
    WasmResult Execute(const char* name, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;

    // 統計情報の取得
    std::size_t GetMaxCallStackDepth() const noexcept { return max_call_stack_depth_; }
    std::size_t GetMaxStackDepth() const noexcept { return max_stack_depth_; }

private:
    WasmResult ParseSections(const uint8_t* binary, std::size_t size) noexcept;
    WasmResult ExecuteInternal(uint32_t func_index) noexcept;

    // 文字列をメモリプール上にコピーして保持するヘルパー
    const char* CopyString(const uint8_t*& ptr, uint32_t len, const uint8_t* end) noexcept;

    WasmMemoryPool& pool_;

    // 解析された型シグネチャ情報
    WasmTypeSignature signatures_[kMaxWasmTypes];
    std::size_t signature_count_;

    // 静的配列による関数・エクスポート情報の管理（STL禁止ルール準拠）
    WasmFunction functions_[kMaxWasmFunctions];
    std::size_t function_count_;

    WasmExportEntry exports_[kMaxWasmFunctions];
    std::size_t export_count_;

    // 実行用仮想マシンスタック
    WasmValue stack_[kWasmStackSize];
    std::size_t stack_top_;

    // 実行用仮想マシンのコールスタック
    struct WasmFrame {
        const WasmFunction* func;
        const uint8_t* ip;
        const uint8_t* limit;
        WasmValue locals[kMaxLocals];
        uint32_t total_locals;
    };
    WasmFrame call_stack_[kWasmCallStackSize];
    std::size_t call_stack_top_;

    // 統計情報
    std::size_t max_call_stack_depth_;
    std::size_t max_stack_depth_;
};

} // namespace embwasm

#endif // EMBWASM_WASM_ENGINE_H_
