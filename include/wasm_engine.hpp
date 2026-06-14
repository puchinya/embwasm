#ifndef EMBWASM_WASM_ENGINE_HPP_
#define EMBWASM_WASM_ENGINE_HPP_

#include "wasm_types.hpp"
#include "wasm_memory_pool.hpp"
#include "wasm_api.hpp"
#include "wasm_thread.hpp"

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

// グローバル変数を表す構造体
struct WasmGlobal {
    WasmType type;
    bool is_mutable;
    WasmValue value;
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

    // エクスポートインデックスから内部関数インデックスを取得
    int32_t GetFunctionIndexByExportIndex(uint32_t export_idx) const noexcept;

    // 統計情報の取得
    std::size_t GetMaxCallStackDepth() const noexcept { return max_call_stack_depth_; }
    std::size_t GetMaxStackDepth() const noexcept { return max_stack_depth_; }

#if EMBWASM_ENABLE_MULTITHREADING
    // スレッドコンテキストの設定
    void SetContext(WasmThreadContext* ctx) noexcept { ctx_ = ctx; }
    WasmThreadContext* GetContext() const noexcept { return ctx_; }

    // スケジューラの設定と取得
    void SetScheduler(class WasmScheduler* scheduler) noexcept { scheduler_ = scheduler; }
    class WasmScheduler* GetScheduler() const noexcept { return scheduler_; }
#endif

public:
    WasmResult ExecuteInternal(uint32_t func_index) noexcept;

    // 公開関数を外部から解決可能にする
    const WasmExportEntry* GetExports() const noexcept { return exports_; }
    std::size_t GetExportCount() const noexcept { return export_count_; }

    // 線形メモリへのアクセス
    uint8_t* GetLinearMemory() const noexcept { return linear_memory_ptr_; }
    std::size_t GetLinearMemorySize() const noexcept { return linear_memory_size_; }

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

    // グローバル変数
    WasmGlobal globals_[kMaxGlobals];
    std::size_t global_count_;

    // 線形メモリ（Memory section / Data section）
    uint8_t* linear_memory_ptr_;
    std::size_t linear_memory_size_;

    // 間接関数テーブル (Table section / Element section)
    uint32_t* table_ptr_;
    std::size_t table_size_;

    // 現在の実行コンテキスト
    WasmThreadContext* ctx_;

#if EMBWASM_ENABLE_MULTITHREADING
    WasmScheduler* scheduler_;
#endif

    // 統計情報
    std::size_t max_call_stack_depth_;
    std::size_t max_stack_depth_;
};

} // namespace embwasm

#endif // EMBWASM_WASM_ENGINE_HPP_
