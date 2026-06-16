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
        const WasmType* local_types;
        uint32_t max_label_depth; // Validate()で算出: 必要な最大ラベルスタック深度（関数ブロック含む）
        uint32_t max_stack_depth; // Validate()で算出: 必要な最大データスタック深度
    };
    bool is_import;
    uint32_t type_index;
    union {
        HostFunctionId host_func_id;
        InternalFunc internal_func;
    };
    // モジュール間リンク用
    class WasmEngine* link_engine;
    const char* link_field_name;
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
    std::size_t name_len;
    uint8_t kind; // 0=Func, 1=Table, 2=Mem, 3=Global
    uint32_t index;
};

// ベアメタル環境向け極小WASM実行エンジン
class WasmEngine {
public:
    WasmEngine() noexcept;
    ~WasmEngine() noexcept;

    // 明示的な初期化と終了
    void Init(WasmMemoryPool& pool) noexcept;
    void Deinit() noexcept;

    // WASMバイナリ（バイト配列）の読み込みと解析
    WasmResult Load(const uint8_t* binary, std::size_t size) noexcept;

    // エクスポートされた関数を実行する
    WasmResult Execute(const char* name, std::size_t name_len, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;
    WasmResult Execute(const char* name, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;

    // 関数名からインデックスを取得
    int32_t GetExportFunctionIndex(const char* name, std::size_t name_len) const noexcept;
    int32_t GetExportFunctionIndex(const char* name) const noexcept;

    // エクスポートされた関数の戻り値数を取得
    uint32_t GetExportFunctionResultCount(const char* name, std::size_t name_len) const noexcept;
    uint32_t GetExportFunctionResultCount(const char* name) const noexcept;

    // エクスポートインデックスから内部関数インデックスを取得
    int32_t GetFunctionIndexByExportIndex(uint32_t export_idx) const noexcept;

    // 統計情報の取得
    std::size_t GetMaxCallStackDepth() const noexcept { return max_call_stack_depth_; }
    std::size_t GetMaxStackDepth() const noexcept { return max_stack_depth_; }

    // ユーザーデータの設定と取得（Host関数向け）
    void* GetUserData() const noexcept { return user_data_; }
    void SetUserData(void* user_data) noexcept { user_data_ = user_data; }

    // モジュールごとのユーザーデータ設定と取得
    void* GetModuleUserData(HostModuleId module_id) const noexcept;
    void SetModuleUserData(HostModuleId module_id, void* user_data) noexcept;

#if EMBWASM_ENABLE_MULTITHREADING
    WasmThreadContext* GetContext() const noexcept { return ctx_; }

    // スケジューラの取得
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
    WasmMemoryPool* GetMemoryPool() const noexcept { return pool_; }

private:
    friend class WasmScheduler;

#if EMBWASM_ENABLE_MULTITHREADING
    void SetContext(WasmThreadContext* ctx) noexcept { ctx_ = ctx; }
    void SetScheduler(class WasmScheduler* scheduler) noexcept { scheduler_ = scheduler; }
#endif

    WasmResult ParseSections(const uint8_t* binary, std::size_t size) noexcept;

    // 事前検査: ParseSections後にLoad内で呼び出す
    WasmResult Validate() noexcept;
    WasmResult ValidateFunctionBody(uint32_t func_idx) noexcept;

    // ロード済みモジュールのプール確保メモリを個別解放するヘルパー
    void FreeLoadedModule() noexcept;

    WasmMemoryPool* pool_;

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
    uint32_t max_linear_memory_pages_; // メモリセクションで指定された最大ページ数(0=制限なし)
    bool is_memory_shared_;

    // 間接関数テーブル (Table section / Element section)
    uint32_t* tables_[kMaxTables];
    std::size_t table_sizes_[kMaxTables];
    uint32_t table_max_sizes_[kMaxTables];
    WasmType table_types_[kMaxTables];
    std::size_t table_count_;
    bool is_table_shared_[kMaxTables];

    // バルクメモリ用セグメント情報
    static constexpr std::size_t kMaxDataSegments = 256;
    static constexpr std::size_t kMaxElemSegments = 256;

    const uint8_t* data_segments_[kMaxDataSegments];
    uint32_t data_segment_sizes_[kMaxDataSegments];
    bool data_segment_dropped_[kMaxDataSegments];
    std::size_t data_segment_count_;

    uint32_t* elem_segments_[kMaxElemSegments];
    uint32_t elem_segment_sizes_[kMaxElemSegments];
    bool elem_segment_dropped_[kMaxElemSegments];
    std::size_t elem_segment_count_;

    // 開始関数インデックス（Start section）
    int32_t start_function_index_;

    // 現在の実行コンテキスト
    WasmThreadContext* ctx_;

#if EMBWASM_ENABLE_MULTITHREADING
    WasmScheduler* scheduler_;
#endif

    // 統計情報
    std::size_t max_call_stack_depth_;
    std::size_t max_stack_depth_;

    // ユーザーデータ
    void* user_data_;
    void** module_user_datas_;
};

} // namespace embwasm

#endif // EMBWASM_WASM_ENGINE_HPP_
