#ifndef EMBWASM_WASM_ENGINE_HPP_
#define EMBWASM_WASM_ENGINE_HPP_

#include "wasm_types.hpp"
#include "wasm_memory_pool.hpp"
#include "wasm_api.hpp"
#include "wasm_thread.hpp"

namespace embwasm {

struct WasmModuleInstance;
struct WasmFunction;

/// @brief WASM 関数の種別。
enum class WasmFunctionKind : uint8_t {
    kLocal = 0,  ///< モジュール内部で定義された関数。
    kHost = 1,   ///< ホスト側（C++）から提供された関数。
    kImport = 2, ///< インポート宣言（ロード時に解決される）。
};

/// @brief モジュール内部に定義された WASM 関数のメタデータ。
struct WasmLocalFunction {
    const uint8_t* code_ptr;   ///< バイトコードの先頭ポインタ（ROM を指す）。
    uint32_t code_size;        ///< バイトコードのバイト数。
    uint32_t local_count;      ///< 引数を除くローカル変数数。
    const WasmType* local_types; ///< ローカル変数の型リスト。
    uint32_t max_label_depth;  ///< `Validate()` で算出した最大ラベルスタック深度（関数ブロック含む）。
    uint32_t max_stack_depth;  ///< `Validate()` で算出した最大データスタック深度。
};

/// @brief ホスト側（C++）から提供される関数のメタデータ。
struct WasmHostFunction {
    HostFunctionId host_func_id; ///< `DispatchHostFunction()` に渡すホスト関数 ID。
};

/// @brief インポート宣言（ロード時に未解決、`Execute()` 時に解決される）。
struct WasmImportFunction {
    const char* module_name;     ///< インポートモジュール名（ROM を指す）。
    size_t module_name_len;      ///< モジュール名の長さ（バイト数）。
    const char* field_name;      ///< インポートフィールド名（ROM を指す）。
    size_t field_name_len;       ///< フィールド名の長さ（バイト数）。
    WasmFunction* resolved_func; ///< 解決後の関数へのポインタ。未解決時は `nullptr`。
};

/// @brief WASM 関数エントリ（内部関数・ホスト関数・インポート宣言を統合）。
struct WasmFunction {
    WasmFunctionKind kind; ///< 関数の種別。
    uint32_t type_index;   ///< 対応するシグネチャのインデックス。
    union {
        WasmHostFunction host;
        WasmLocalFunction local;
        WasmImportFunction import;
    };

    struct WasmModuleInstance* module; ///< この関数が属するモジュールインスタンス。
};

/// @brief WASM グローバル変数。
struct WasmGlobal {
    WasmType type;    ///< 値の型。
    bool is_mutable;  ///< ミュータブルフラグ。
    WasmValue value;  ///< 現在値。
};

/// @brief WASM エクスポートエントリ。
struct WasmExportEntry {
    const char* name;    ///< エクスポート名（ROM を指す）。
    std::size_t name_len; ///< エクスポート名の長さ（バイト数）。
    uint8_t kind;        ///< エクスポート種別（0=Func, 1=Table, 2=Mem, 3=Global）。
    uint32_t index;      ///< エクスポート対象のインデックス。
};

/// @brief ロード済み WASM モジュールのインスタンス情報。
struct WasmModuleInstance {
    bool is_active;         ///< スロットが使用中かどうか。
    bool imports_resolved;  ///< インポート解決済みフラグ（`Execute()` 時に一度だけ解決）。
    char name[64];          ///< モジュール名。
    std::size_t name_len;   ///< モジュール名の長さ。

    WasmTypeSignature* signatures;   ///< 型シグネチャ配列（プールから確保）。
    std::size_t signature_count;

    WasmFunction* functions;         ///< 関数配列（プールから確保）。
    std::size_t function_count;

    WasmExportEntry* exports;        ///< エクスポートエントリ配列（プールから確保）。
    std::size_t export_count;

    WasmGlobal* globals;             ///< グローバル変数配列（プールから確保）。
    std::size_t global_count;

    uint8_t* linear_memory_ptr;             ///< 線形メモリの先頭ポインタ（プールから確保）。
    std::size_t linear_memory_size;         ///< 現在のサイズ（min_pages * 65536）。
    std::size_t linear_memory_capacity;     ///< プールから確保した実際のバイト数。
    uint32_t max_linear_memory_pages;       ///< メモリセクションで指定された最大ページ数（0 = 制限なし）。
    bool is_memory_shared;

    uint32_t** tables;
    std::size_t* table_sizes;
    uint32_t* table_max_sizes;
    WasmType* table_types;
    bool* is_table_shared;
    std::size_t table_count;
    std::size_t table_capacity;

    const uint8_t** data_segments;
    uint32_t* data_segment_sizes;
    bool* data_segment_dropped;
    std::size_t data_segment_count;
    std::size_t data_segment_capacity;

    uint32_t** elem_segments;
    uint32_t* elem_segment_sizes;
    bool* elem_segment_dropped;
    std::size_t elem_segment_count;
    std::size_t elem_segment_capacity;

    int32_t start_function_index; ///< Start セクションで指定された関数インデックス（-1 = なし）。

    /// @brief 線形メモリの先頭ポインタを返します。
    uint8_t *GetLinearMemory() const noexcept {
        return linear_memory_ptr;
    }

    /// @brief 線形メモリの現在サイズを返します。
    std::size_t GetLinearMemorySize() const noexcept {
        return linear_memory_size;
    }
};

/// @brief ベアメタル環境向け極小 WASM 実行エンジン。
///
/// STL・例外・RTTI・動的メモリを一切使用しません。
/// `Init()` で渡した `WasmMemoryPool` からのみメモリを確保します。
class WasmEngine {
public:
    WasmEngine() noexcept;
    ~WasmEngine() noexcept;

    /// @brief エンジンを初期化します。使用前に必ず呼んでください。
    /// @param pool  エンジンが使用するメモリプール。
    void Init(WasmMemoryPool& pool) noexcept;

    /// @brief エンジンを終了し、すべてのリソースを解放します。
    void Deinit() noexcept;

    /// @brief WASM バイナリをロードし、指定名のモジュールとして登録します。
    /// @param module_name      モジュール名文字列。
    /// @param module_name_len  モジュール名の長さ（バイト数）。
    /// @param binary           WASM バイナリデータの先頭ポインタ。
    /// @param size             バイナリのバイト数。
    /// @return 0 以上のインスタンス ID（成功）。負値のエラーコード（失敗）。
    int32_t Load(const char* module_name, std::size_t module_name_len, const uint8_t* binary, std::size_t size) noexcept;

    /// @brief すべてのモジュールをアンロードし、プール上のメモリを解放します。
    void UnloadAll() noexcept;

    /// @brief WASM バイナリをロードする後方互換 API（"default" モジュールとして登録）。
    /// @param binary  WASM バイナリデータの先頭ポインタ。
    /// @param size    バイナリのバイト数。
    /// @return kOk（成功）またはエラーコード。
    WasmResult Load(const uint8_t* binary, std::size_t size) noexcept {
        int32_t r = Load("default", 7, binary, size);
        if (r >= 0) return WasmResult::kOk;
        return static_cast<WasmResult>(r);
    }

    /// @brief 指定モジュールのエクスポート関数を実行します。
    /// @param module_name      モジュール名。
    /// @param module_name_len  モジュール名の長さ（バイト数）。
    /// @param name             エクスポート関数名。
    /// @param name_len         関数名の長さ（バイト数）。
    /// @param args             引数配列（引数なしの場合は `nullptr`）。
    /// @param arg_count        引数の個数。
    /// @param results          結果格納配列（戻り値なしの場合は `nullptr`）。
    /// @param result_count     戻り値の個数。
    /// @return 実行結果を示す WasmResult。
    WasmResult Execute(const char* module_name, std::size_t module_name_len, const char* name, std::size_t name_len, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;

    /// @brief 指定モジュールのエクスポート関数インデックスを返します。
    /// @param module_name      モジュール名。
    /// @param module_name_len  モジュール名の長さ（バイト数）。
    /// @param name             エクスポート関数名。
    /// @param name_len         関数名の長さ（バイト数）。
    /// @return 関数インデックス（0 以上）。見つからない場合は -1。
    int32_t GetExportFunctionIndex(const char* module_name, std::size_t module_name_len, const char* name, std::size_t name_len) const noexcept;

    /// @brief 指定モジュールのエクスポート関数の戻り値数を返します。
    /// @param module_name      モジュール名。
    /// @param module_name_len  モジュール名の長さ（バイト数）。
    /// @param name             エクスポート関数名。
    /// @param name_len         関数名の長さ（バイト数）。
    /// @return 戻り値の個数。関数が見つからない場合は 0。
    uint32_t GetExportFunctionResultCount(const char* module_name, std::size_t module_name_len, const char* name, std::size_t name_len) const noexcept;

    /// @brief エクスポートインデックスから内部関数インデックスを返します。
    /// @param instance_id  モジュールのインスタンス ID。
    /// @param export_idx   エクスポートテーブル上のインデックス。
    /// @return 内部関数インデックス。無効な場合は -1。
    int32_t GetFunctionIndexByExportIndex(int32_t instance_id, uint32_t export_idx) const noexcept;

    /// @brief 実行中に到達した最大コールスタック深度を返します（統計情報）。
    std::size_t GetMaxCallStackDepth() const noexcept { return max_call_stack_depth_; }

    /// @brief 実行中に到達した最大データスタック深度を返します（統計情報）。
    std::size_t GetMaxStackDepth() const noexcept { return max_stack_depth_; }

    /// @brief ホスト関数向けユーザーデータポインタを返します。
    void* GetUserData() const noexcept { return user_data_; }

    /// @brief ホスト関数向けユーザーデータポインタを設定します。
    /// @param user_data  任意のポインタ。
    void SetUserData(void* user_data) noexcept { user_data_ = user_data; }

    /// @brief 指定ホストモジュールのユーザーデータポインタを返します。
    /// @param module_id  対象のホストモジュール ID。
    void* GetModuleUserData(HostModuleId module_id) const noexcept;

    /// @brief 指定ホストモジュールのユーザーデータポインタを設定します。
    /// @param module_id  対象のホストモジュール ID。
    /// @param user_data  任意のポインタ。
    void SetModuleUserData(HostModuleId module_id, void* user_data) noexcept;

#if EMBWASM_ENABLE_MULTITHREADING
    /// @brief エンジン内蔵の協調スケジューラへのポインタを返します。
    WasmScheduler* GetScheduler() noexcept { return &scheduler_; }
#endif

public:
    /// @brief 指定モジュール・関数インデックスで実行ループを起動します（内部 API）。
    WasmResult ExecuteInternal(WasmModuleInstance* module, uint32_t func_index) noexcept;

    /// @brief 指定インスタンスのエクスポートエントリ配列を返します。
    /// @param instance_id  インスタンス ID。
    /// @return エクスポートエントリの先頭ポインタ。無効な ID の場合は `nullptr`。
    const WasmExportEntry* GetExports(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id] && modules_[instance_id]->is_active)
            ? modules_[instance_id]->exports : nullptr;
    }

    /// @brief 指定インスタンスのエクスポート数を返します。
    /// @param instance_id  インスタンス ID。
    std::size_t GetExportCount(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id] && modules_[instance_id]->is_active)
            ? modules_[instance_id]->export_count : 0;
    }

    /// @brief 指定インスタンスの線形メモリ先頭ポインタを返します。
    /// @param instance_id  インスタンス ID。
    /// @return 線形メモリの先頭ポインタ。メモリなし / 無効 ID の場合は `nullptr`。
    uint8_t* GetLinearMemory(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id] && modules_[instance_id]->is_active)
            ? modules_[instance_id]->linear_memory_ptr : nullptr;
    }

    /// @brief 指定インスタンスの線形メモリサイズを返します。
    /// @param instance_id  インスタンス ID。
    std::size_t GetLinearMemorySize(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id] && modules_[instance_id]->is_active)
            ? modules_[instance_id]->linear_memory_size : 0;
    }

    /// @brief 現在実行中のモジュールの線形メモリ先頭ポインタを返します（ホストモジュール向け後方互換 API）。
    uint8_t* GetLinearMemory() const noexcept {
#if EMBWASM_ENABLE_MULTITHREADING
        const WasmThreadContext* ctx = scheduler_.GetCurrentThreadContext();
#else
        const WasmThreadContext* ctx = ctx_;
#endif
        if (ctx && ctx->call_stack_top > 0) {
            const WasmFrame& frame = ctx->call_stack[ctx->call_stack_top - 1];
            if (frame.func && frame.func->module) {
                return frame.func->module->linear_memory_ptr;
            }
        }
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i] && modules_[i]->is_active && modules_[i]->linear_memory_ptr) {
                return modules_[i]->linear_memory_ptr;
            }
        }
        return nullptr;
    }

    /// @brief 現在実行中のモジュールの線形メモリサイズを返します（ホストモジュール向け後方互換 API）。
    std::size_t GetLinearMemorySize() const noexcept {
#if EMBWASM_ENABLE_MULTITHREADING
        const WasmThreadContext* ctx = scheduler_.GetCurrentThreadContext();
#else
        const WasmThreadContext* ctx = ctx_;
#endif
        if (ctx && ctx->call_stack_top > 0) {
            const WasmFrame& frame = ctx->call_stack[ctx->call_stack_top - 1];
            if (frame.func && frame.func->module) {
                return frame.func->module->linear_memory_size;
            }
        }
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i] && modules_[i]->is_active && modules_[i]->linear_memory_ptr) {
                return modules_[i]->linear_memory_size;
            }
        }
        return 0;
    }

    /// @brief エンジンが使用するメモリプールへのポインタを返します。
    WasmMemoryPool* GetMemoryPool() const noexcept { return pool_; }

    /// @brief モジュール名でモジュールインスタンスを検索します。
    WasmModuleInstance* GetModuleInstance(const char* name, std::size_t name_len) noexcept;
    /// @brief モジュール名でモジュールインスタンスを検索します（const 版）。
    const WasmModuleInstance* GetModuleInstance(const char* name, std::size_t name_len) const noexcept;

    /// @brief インスタンス ID でモジュールインスタンスを返します。
    /// @param id  インスタンス ID（`Load()` の戻り値）。
    WasmModuleInstance* GetModuleInstanceById(int32_t id) noexcept {
        return (id >= 0 && id < static_cast<int32_t>(kMaxModules) && modules_[id] && modules_[id]->is_active) ? modules_[id] : nullptr;
    }
    /// @brief インスタンス ID でモジュールインスタンスを返します（const 版）。
    const WasmModuleInstance* GetModuleInstanceById(int32_t id) const noexcept {
        return (id >= 0 && id < static_cast<int32_t>(kMaxModules) && modules_[id] && modules_[id]->is_active) ? modules_[id] : nullptr;
    }

    /// @brief 指定名のモジュールをアンロードします。
    /// @param name      モジュール名。
    /// @param name_len  モジュール名の長さ（バイト数）。
    void Unload(const char* name, std::size_t name_len) noexcept;

    /// @brief モジュールに別名を登録します（インポート解決で使用）。
    /// @param real_name      実モジュール名。
    /// @param real_name_len  実モジュール名の長さ（バイト数）。
    /// @param alias_name     別名。
    /// @param alias_name_len 別名の長さ（バイト数）。
    void RegisterAlias(const char* real_name, std::size_t real_name_len, const char* alias_name, std::size_t alias_name_len) noexcept;

private:
    friend class WasmScheduler;

    struct WasmModuleCounts {
        std::size_t type_count;
        std::size_t func_count;
        std::size_t export_count;
        std::size_t global_count;
        std::size_t table_count;
        std::size_t data_count;
        std::size_t elem_count;
    };
    static WasmModuleCounts PreScanSections(const uint8_t* binary, std::size_t size) noexcept;

    WasmResult ParseSections(WasmModuleInstance* mod, const uint8_t* binary, std::size_t size) noexcept;

    WasmResult Validate(WasmModuleInstance* mod) noexcept;
    WasmResult ValidateFunctionBody(WasmModuleInstance* mod, uint32_t func_idx) noexcept;

    void ResolveImports(WasmModuleInstance* mod) noexcept;
    void FreeModuleInstance(WasmModuleInstance* mod) noexcept;

    struct NameAlias {
        char alias[64];
        std::size_t alias_len;
        char real[64];
        std::size_t real_len;
    };
    static constexpr std::size_t kMaxAliases = 32;
    NameAlias name_aliases_[kMaxAliases];
    std::size_t name_alias_count_;

    const char* ResolveAlias(const char* name, std::size_t name_len, std::size_t& out_len) const noexcept;

    WasmMemoryPool* pool_;
    WasmModuleInstance* modules_[kMaxModules];

#if EMBWASM_ENABLE_MULTITHREADING
    WasmScheduler scheduler_;
#else
    WasmThreadContext* ctx_;
#endif

    std::size_t max_call_stack_depth_;
    std::size_t max_stack_depth_;
    void* user_data_;
    void** module_user_datas_;
};

} // namespace embwasm

#endif // EMBWASM_WASM_ENGINE_HPP_
