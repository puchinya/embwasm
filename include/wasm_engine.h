#ifndef EMBWASM_WASM_ENGINE_H_
#define EMBWASM_WASM_ENGINE_H_

#include "wasm_types.h"
#include "wasm_memory_pool.h"
#include "wasm_api.h"
#include "wasm_thread.h"

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

    // 関数名からインデックスを取得
    int32_t GetExportFunctionIndex(const char* name) const noexcept;

    // 統計情報の取得
    std::size_t GetMaxCallStackDepth() const noexcept { return max_call_stack_depth_; }
    std::size_t GetMaxStackDepth() const noexcept { return max_stack_depth_; }

#if EMBWASM_ENABLE_MULTITHREADING
    // スレッドコンテキストの設定
    void SetContext(WasmThreadContext* ctx) noexcept { ctx_ = ctx; }
    WasmThreadContext* GetContext() const noexcept { return ctx_; }
#endif

public:
    WasmResult ExecuteInternal(uint32_t func_index) noexcept;

private:
    WasmResult ParseSections(const uint8_t* binary, std::size_t size) noexcept;

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

    // 現在の実行コンテキスト
    WasmThreadContext* ctx_;

    // 統計情報
    std::size_t max_call_stack_depth_;
    std::size_t max_stack_depth_;
};

} // namespace embwasm

#endif // EMBWASM_WASM_ENGINE_H_
