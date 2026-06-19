#ifndef EMBWASM_WASM_ENGINE_HPP_
#define EMBWASM_WASM_ENGINE_HPP_

#include "wasm_types.hpp"
#include "wasm_memory_pool.hpp"
#include "wasm_api.hpp"
#include "wasm_thread.hpp"

namespace embwasm {

struct WasmModuleInstance;
struct WasmFunction;

enum class WasmFunctionKind : uint8_t {
    kLocal = 0,
    kHost = 1,
    kImport = 2,
};

struct WasmLocalFunction {
    const uint8_t* code_ptr;
    uint32_t code_size;
    uint32_t local_count;
    const WasmType* local_types;
    uint32_t max_label_depth; // Validate()で算出: 必要な最大ラベルスタック深度（関数ブロック含む）
    uint32_t max_stack_depth; // Validate()で算出: 必要な最大データスタック深度
};

struct WasmHostFunction {
    HostFunctionId host_func_id;
};

struct WasmImportFunction {
    const char* module_name;
    size_t module_name_len;
    const char* field_name;
    size_t field_name_len;
    WasmFunction* resolved_func;
};

struct WasmFunction {
    WasmFunctionKind kind;
    uint32_t type_index;
    union {
        WasmHostFunction host;
        WasmLocalFunction local;
        WasmImportFunction import;
    };

    // この関数が属するモジュールインスタンス
    struct WasmModuleInstance* module;
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

// WASMモジュールのインスタンス情報を表す構造体
struct WasmModuleInstance {
    bool is_active;
    bool imports_resolved; // Execute時に一度解決済みかのフラグ
    char name[64]; // モジュール名
    std::size_t name_len; // モジュール名の長さ

    // 動的確保 (PreScanSections で確定したサイズをLoad時に確保)
    WasmTypeSignature* signatures;
    std::size_t signature_count;
    std::size_t signature_capacity;

    WasmFunction* functions;
    std::size_t function_count;
    std::size_t function_capacity;

    WasmExportEntry* exports;
    std::size_t export_count;
    std::size_t export_capacity;

    WasmGlobal* globals;
    std::size_t global_count;
    std::size_t global_capacity;

    // 線形メモリ（Memory section / Data section）
    uint8_t* linear_memory_ptr;
    std::size_t linear_memory_size;         // 現在の使用サイズ (min_pages * 65536)
    std::size_t linear_memory_capacity;     // プールから確保した実際のバイト数
    uint32_t max_linear_memory_pages;       // メモリセクションで指定された最大ページ数(0=制限なし)
    bool is_memory_shared;

    // テーブルメタデータ配列（各テーブルのサイズ分だけ確保）
    uint32_t** tables;
    std::size_t* table_sizes;
    uint32_t* table_max_sizes;
    WasmType* table_types;
    bool* is_table_shared;
    std::size_t table_count;
    std::size_t table_capacity;

    // バルクメモリ用セグメント情報（動的確保）
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

    // 開始関数インデックス（Start section）
    int32_t start_function_index;
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
    // ロード後にインスタンスID (0以上) を返す。エラー時は負のエラーコードを返す。
    int32_t Load(const char* module_name, std::size_t module_name_len, const uint8_t* binary, std::size_t size) noexcept;

    // すべてのモジュールをアンロードして、メモリを解放する
    void UnloadAll() noexcept;

    // WASMバイナリのロード（後方互換API：最初の空きスロットに "default" として登録）
    WasmResult Load(const uint8_t* binary, std::size_t size) noexcept {
        int32_t r = Load("default", 7, binary, size);
        if (r >= 0) return WasmResult::kOk;
        return static_cast<WasmResult>(-r);
    }

    // エクスポートされた関数を実行する
    WasmResult Execute(const char* module_name, std::size_t module_name_len, const char* name, std::size_t name_len, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;

    // 後方互換API：最初のアクティブモジュールで実行
    WasmResult Execute(const char* name, std::size_t name_len, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept {
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i].is_active)
                return Execute(modules_[i].name, modules_[i].name_len, name, name_len, args, arg_count, results, result_count);
        }
        return WasmResult::kErrorFunctionNotFound;
    }

    // 関数名からインデックスを取得
    int32_t GetExportFunctionIndex(const char* module_name, std::size_t module_name_len, const char* name, std::size_t name_len) const noexcept;

    // 後方互換API：最初のアクティブモジュールから検索
    int32_t GetExportFunctionIndex(const char* name, std::size_t name_len) const noexcept {
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i].is_active)
                return GetExportFunctionIndex(modules_[i].name, modules_[i].name_len, name, name_len);
        }
        return -1;
    }

    // エクスポートされた関数の戻り値数を取得
    uint32_t GetExportFunctionResultCount(const char* module_name, std::size_t module_name_len, const char* name, std::size_t name_len) const noexcept;

    // 後方互換API：最初のアクティブモジュールから検索
    uint32_t GetExportFunctionResultCount(const char* name, std::size_t name_len) const noexcept {
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i].is_active)
                return GetExportFunctionResultCount(modules_[i].name, modules_[i].name_len, name, name_len);
        }
        return 0;
    }

    // エクスポートインデックスから内部関数インデックスを取得
    int32_t GetFunctionIndexByExportIndex(int32_t instance_id, uint32_t export_idx) const noexcept;

    // 後方互換API：最初のアクティブモジュールから検索
    int32_t GetFunctionIndexByExportIndex(uint32_t export_idx) const noexcept {
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i].is_active)
                return GetFunctionIndexByExportIndex(static_cast<int32_t>(i), export_idx);
        }
        return -1;
    }

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
    WasmScheduler* GetScheduler() noexcept { return &scheduler_; }
#endif

public:
    WasmResult ExecuteInternal(WasmModuleInstance* module, uint32_t func_index) noexcept;

    // 公開関数を外部から解決可能にする
    const WasmExportEntry* GetExports(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id].is_active)
            ? modules_[instance_id].exports : nullptr;
    }
    std::size_t GetExportCount(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id].is_active)
            ? modules_[instance_id].export_count : 0;
    }

    // 線形メモリへのアクセス
    uint8_t* GetLinearMemory(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id].is_active)
            ? modules_[instance_id].linear_memory_ptr : nullptr;
    }
    std::size_t GetLinearMemorySize(int32_t instance_id) const noexcept {
        return (instance_id >= 0 && instance_id < static_cast<int32_t>(kMaxModules) && modules_[instance_id].is_active)
            ? modules_[instance_id].linear_memory_size : 0;
    }
    // 現在実行中のモジュールのメモリを返す（ホストモジュール向け後方互換API）
    uint8_t* GetLinearMemory() const noexcept {
        if (ctx_ && ctx_->call_stack_top > 0) {
            const WasmFrame& frame = ctx_->call_stack[ctx_->call_stack_top - 1];
            if (frame.func && frame.func->module) {
                return frame.func->module->linear_memory_ptr;
            }
        }
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i].is_active && modules_[i].linear_memory_ptr) {
                return modules_[i].linear_memory_ptr;
            }
        }
        return nullptr;
    }
    std::size_t GetLinearMemorySize() const noexcept {
        if (ctx_ && ctx_->call_stack_top > 0) {
            const WasmFrame& frame = ctx_->call_stack[ctx_->call_stack_top - 1];
            if (frame.func && frame.func->module) {
                return frame.func->module->linear_memory_size;
            }
        }
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i].is_active && modules_[i].linear_memory_ptr) {
                return modules_[i].linear_memory_size;
            }
        }
        return 0;
    }
    WasmMemoryPool* GetMemoryPool() const noexcept { return pool_; }

    // 内部的なモジュール解決ヘルパー
    WasmModuleInstance* GetModuleInstance(const char* name, std::size_t name_len) noexcept;
    const WasmModuleInstance* GetModuleInstance(const char* name, std::size_t name_len) const noexcept;
    WasmModuleInstance* GetModuleInstanceById(int32_t id) noexcept {
        return (id >= 0 && id < static_cast<int32_t>(kMaxModules) && modules_[id].is_active) ? &modules_[id] : nullptr;
    }
    const WasmModuleInstance* GetModuleInstanceById(int32_t id) const noexcept {
        return (id >= 0 && id < static_cast<int32_t>(kMaxModules) && modules_[id].is_active) ? &modules_[id] : nullptr;
    }

    // モジュールをアンロード（名前で指定）
    void Unload(const char* name, std::size_t name_len) noexcept;

    // モジュールに別名を登録（インポート解決で使用）
    void RegisterAlias(const char* real_name, std::size_t real_name_len, const char* alias_name, std::size_t alias_name_len) noexcept;

private:
    friend class WasmScheduler;

#if EMBWASM_ENABLE_MULTITHREADING
    void SetContext(WasmThreadContext* ctx) noexcept { ctx_ = ctx; }
#endif

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

    // 事前検査: ParseSections後にLoad内で呼び出す
    WasmResult Validate(WasmModuleInstance* mod) noexcept;
    WasmResult ValidateFunctionBody(WasmModuleInstance* mod, uint32_t func_idx) noexcept;

    // Execute時に未解決のkImportをすべて解決する
    void ResolveImports(WasmModuleInstance* mod) noexcept;

    // ロード済みモジュールのプール確保メモリを個別解放するヘルパー
    void FreeModuleInstance(WasmModuleInstance* mod) noexcept;

    // エイリアス名 → 実モジュール名の対応表
    struct NameAlias {
        char alias[64];
        std::size_t alias_len;
        char real[64];
        std::size_t real_len;
    };
    static constexpr std::size_t kMaxAliases = 32;
    NameAlias name_aliases_[kMaxAliases];
    std::size_t name_alias_count_;

    // エイリアスを考慮したモジュール名解決（out_len に解決後の長さを返す）
    const char* ResolveAlias(const char* name, std::size_t name_len, std::size_t& out_len) const noexcept;

    WasmMemoryPool* pool_;

    // 複数モジュールを格納する配列
    WasmModuleInstance modules_[kMaxModules];

    // 現在の実行コンテキスト
    WasmThreadContext* ctx_;

#if EMBWASM_ENABLE_MULTITHREADING
    WasmScheduler scheduler_;
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
