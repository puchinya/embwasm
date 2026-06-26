#ifndef EMBWASM_WASM_ENGINE_HPP_
#define EMBWASM_WASM_ENGINE_HPP_

#include "wasm_types.hpp"
#include "wasm_memory_pool.hpp"
#include "wasm_api.hpp"
#include "wasm_config.hpp"

namespace embwasm {

struct WasmModuleInstance;
struct WasmFunction;

/// @brief 制御ブロック（`block` / `loop` / `if`）のラベル情報。
///
/// 分岐（`br`）時のジャンプ先 IP とスタック巻き戻し位置を保持します。
struct WasmLabel {
    const uint8_t* pc;    ///< ジャンプ先 IP。`block`/`if` は `end` の次、`loop` はループ先頭。
    uint32_t stack_top;   ///< ブロック進入時のデータスタック高さ（`br` 時にここまで巻き戻す）。
    uint8_t opcode;         ///< ブロック種別（0x02: block, 0x03: loop, 0x04: if）。
    uint16_t param_count;   ///< ブロックのパラメータ数。
    uint16_t result_count;  ///< ブロックの結果数。
};

/// @brief WASM 関数呼び出し 1 フレームの実行コンテキスト。
///
/// コールスタック（`WasmThreadContext::call_stack`）に積まれ、
/// 再帰を使わずループで関数呼び出しを実現します。
struct WasmFrame {
    const WasmFunction* func; ///< 実行中の関数。
    const uint8_t* ip;        ///< 現在のインストラクションポインタ。
    const uint8_t* limit;     ///< バイトコードの終端ポインタ。
    WasmValue* locals;        ///< `WasmThreadContext::stack` 内のローカル変数スライスへのポインタ。
    uint16_t total_locals;    ///< 引数＋ローカル変数の合計数。
    uint16_t label_capacity;      ///< このフレームに割り当てたラベルスロット数。
    WasmLabel* labels;            ///< `WasmThreadContext::labels_pool` 内のスライスへのポインタ。
    uint32_t label_stack_top;     ///< ラベルスタックの現在深さ。
};

/// @brief スレッドの実行状態。
enum class ThreadState : uint8_t {
    kReady,      ///< 実行可能状態。スケジューラに選択される待ち。
    kRunning,    ///< 現在実行中。
    kWaiting,    ///< 何らかの待機中（WaitKind で詳細を判別）。
    kTerminated  ///< 実行終了。
};

/// @brief スレッドの待機種別。
enum class WaitKind : uint8_t {
    kNone,    ///< 待機なし。
    kEvent,   ///< WasmEvent 待ち。
    kNotify,  ///< ThreadNotify 待ち（非同期 I/O 完了通知など）。
    kSleep,   ///< タイムアウト待ち。
};

/// @brief 待機パラメータ（WaitKind に応じて使用するフィールドが異なる）。
union WaitParam {
    uint32_t event_id;      ///< kEvent: 待機するイベント ID。
    uint32_t wake_time_ms;  ///< kSleep: 起床する絶対時刻（ms）。
};

/// @brief スレッドごとの実行コンテキスト。
///
/// データスタック・コールスタック・ラベルプールは WasmMemoryPool から確保したバッファを指します。
/// Init() / CreateThread() 時にサイズ分だけ確保し、以後は再確保しません。
struct WasmThreadContext {
    uint32_t id;        ///< スレッド ID（1-based）。
    ThreadState state;  ///< 現在の実行状態。

    /// @brief 統合スタック（プールから確保）。演算値とフレームごとのローカル変数を一本の配列で管理します。
    /// レイアウト: [frame0 locals][frame0 operands][frame1 locals][frame1 operands]...
    WasmValue* stack;
    uint32_t stack_size; ///< 確保済み要素数（WasmEngineConfig で設定）。
    uint32_t stack_top;  ///< スタックの現在深さ（ローカル変数領域を含む）。

    WasmFrame* call_stack;   ///< WASM コールスタック（プールから確保）。
    uint32_t call_stack_size; ///< 確保済み要素数（WasmEngineConfig で設定）。
    uint32_t call_stack_top;  ///< コールスタックの現在深さ。

    /// @brief 全フレーム共有のラベルプール（プールから確保）。フレームごとに必要数を切り出します。
    WasmLabel* labels_pool;
    uint32_t labels_pool_size; ///< 確保済み要素数（WasmEngineConfig で設定）。
    uint32_t labels_pool_top;  ///< 現在の使用済み先頭インデックス。

    WaitKind  wait_kind;      ///< 待機種別（state == kWaiting 時のみ有効）。
    WaitParam wait_param;     ///< 待機パラメータ（wait_kind に応じて使用するフィールドが異なる）。
    uint8_t   notify_pending; ///< ThreadNotify が ThreadWait より先に届いた場合のフラグ。

    uint32_t start_func_index;     ///< 初回 `ExecuteInternal` に渡す関数インデックス。
    WasmModuleInstance* start_module; ///< 初回 `ExecuteInternal` に渡すモジュールインスタンス。

    /// @brief コンテキストを初期状態（`kTerminated`）にリセットします。
    void Reset() noexcept {
        id = 0;
        state = ThreadState::kTerminated;
        stack_top = 0;
        call_stack_top = 0;
        labels_pool_top = 0;
        wait_kind = WaitKind::kNone;
        wait_param.event_id = 0;
        notify_pending = false;
        start_func_index = 0;
        start_module = nullptr;
    }

    WasmModuleInstance *GetCurrentModule() const noexcept;
};

/// @brief WasmEngine 初期化設定。Init() の第2引数として渡す。
struct WasmEngineConfig {
    uint32_t stack_size       = kDefaultUnifiedStackSize;
    uint32_t call_stack_size  = kDefaultWasmCallStackSize;
    uint32_t labels_pool_size = kDefaultLabelsPoolSize;
};

/// @brief WASM 関数の種別。
enum class WasmFunctionKind : uint8_t {
    kLocal = 0,  ///< モジュール内部で定義された関数。
    kHost = 1,   ///< ホスト側（C++）から提供された関数。
    kImport = 2, ///< インポート宣言（ロード時に解決される）。
};

/// @brief block / if 命令の end・else 位置を保持するジャンプテーブルエントリ。
///
/// ValidateFunctionBody() で構築し、WasmLocalFunction に保持する。
/// block_jump_table は body_offset の昇順にソート済み（バイナリサーチ可能）。
struct BlockJumpEntry {
    uint32_t body_offset;   ///< code_ptr からの block body 開始オフセット（block_type decode 後の ip）。
    uint32_t end_offset;    ///< code_ptr からの end の次バイトオフセット。
    uint32_t else_offset;   ///< if のみ: else body 開始オフセット。else なし・block は 0。
};

/// @brief モジュール内部に定義された WASM 関数のメタデータ。
struct WasmLocalFunction {
    const uint8_t* code_ptr;   ///< バイトコードの先頭ポインタ（ROM を指す）。
    uint32_t code_size;        ///< バイトコードのバイト数。
    uint16_t local_count;      ///< 引数を除くローカル変数数。
    uint16_t max_label_depth;  ///< `Validate()` で算出した最大ラベルスタック深度（関数ブロック含む）。
    uint32_t max_stack_depth;  ///< `Validate()` で算出した最大データスタック深度。
    BlockJumpEntry* block_jump_table; ///< block/if の end 位置ジャンプテーブル（body_offset 昇順）。
    uint32_t        block_count;      ///< block_jump_table のエントリ数。
};

/// @brief ホスト側（C++）から提供される関数のメタデータ。
struct WasmHostFunction {
    HostFunctionId host_func_id; ///< `DispatchHostFunction()` に渡すホスト関数 ID。
};

/// @brief インポート宣言（ロード時に未解決、`Execute()` 時に解決される）。
struct WasmImportFunction {
    const char* module_name;       ///< インポートモジュール名（ROM を指す）。
    uint32_t module_name_len;      ///< モジュール名の長さ（バイト数）。
    const char* field_name;        ///< インポートフィールド名（ROM を指す）。
    uint32_t field_name_len;       ///< フィールド名の長さ（バイト数）。
    WasmFunction* resolved_func; ///< 解決後の関数へのポインタ。未解決時は `nullptr`。
};

/// @brief WASM 関数エントリ（内部関数・ホスト関数・インポート宣言を統合）。
struct WasmFunction {
    WasmFunctionKind kind; ///< 関数の種別。
    uint32_t type_index;   ///< 対応するシグネチャのインデックス。
    WasmModuleInstance* module; ///< この関数が属するモジュールインスタンス。
    union {
        WasmHostFunction host;
        WasmLocalFunction local;
        WasmImportFunction import;
    };
};

inline WasmModuleInstance* WasmThreadContext::GetCurrentModule() const noexcept {
    return call_stack[call_stack_top - 1].func->module;
}

/// @brief WASM グローバル変数。
struct WasmGlobal {
    WasmType type;             ///< 値の型。
    uint8_t is_mutable;        ///< ミュータブルフラグ。
    WasmValue value;           ///< 現在値。
    uint32_t init_global_ref;  ///< global.get 初期化の参照先インデックス。UINT32_MAX なら定数初期化。
};

/// @brief WASM エクスポートエントリ。
struct WasmExportEntry {
    const char* name;     ///< エクスポート名（ROM を指す）。
    uint32_t name_len;    ///< エクスポート名の長さ（バイト数）。
    uint8_t kind;        ///< エクスポート種別（0=Func, 1=Table, 2=Mem, 3=Global）。
    uint32_t index;      ///< エクスポート対象のインデックス。
};

/// @brief ロード済み WASM モジュールのインスタンス情報。
struct WasmModuleInstance {
    uint8_t is_active;         ///< スロットが使用中かどうか。
    uint8_t imports_resolved;  ///< 関数インポート解決済みフラグ。
    uint8_t is_instantiated;   ///< InstantiateModules() 完了後 true。
    uint8_t has_memory;        ///< Memory/MemoryImport 宣言あり（バリデーション・インスタンス化用）。
    char name[64];             ///< モジュール名。
    uint32_t name_len;         ///< モジュール名の長さ。

    WasmTypeSignature** signatures;  ///< 型シグネチャのポインタ配列（各要素はプールから可変長確保）。
    uint32_t signature_count;

    WasmFunction* functions;         ///< 関数配列（プールから確保）。
    uint32_t function_count;

    WasmExportEntry* exports;        ///< エクスポートエントリ配列（プールから確保）。
    uint32_t export_count;

    WasmImportEntry* imports;        ///< インポートエントリ配列（プールから確保）。
    uint32_t import_count;

    WasmGlobal* globals;             ///< グローバル変数配列（プールから確保）。
    uint32_t global_count;

    uint8_t* linear_memory_ptr;       ///< 線形メモリの先頭ポインタ（インスタンス化後に確保）。
    uint32_t linear_memory_size;      ///< 現在のサイズ（min_pages * 65536）。
    uint32_t linear_memory_capacity;  ///< プールから確保した実際のバイト数。
    uint32_t max_linear_memory_pages;       ///< メモリセクションで指定された最大ページ数（0 = 制限なし）。
    uint32_t memory_min_pages;              ///< メモリ初期ページ数（インスタンス化前に有効）。
    uint8_t is_memory_shared;
    uint8_t memory_is_imported;            ///< メモリがインポート由来の場合 true。

    uint32_t** tables;
    uint32_t* table_sizes;
    uint32_t* table_max_sizes;
    WasmType* table_types;
    uint8_t* is_table_shared;
    const char** table_import_modules;      ///< テーブルインポートのモジュール名（バイナリを指す）。own テーブルは nullptr。
    uint32_t* table_import_module_lens;
    const char** table_import_fields;       ///< テーブルインポートのフィールド名（バイナリを指す）。own テーブルは nullptr。
    uint32_t* table_import_field_lens;
    uint32_t table_count;
    uint32_t table_capacity;

    const uint8_t** data_segments;
    uint32_t* data_segment_sizes;
    uint8_t* data_segment_dropped;
    uint32_t* data_segment_offsets;              ///< アクティブセグメントのメモリオフセット。
    uint32_t* data_segment_offset_global_refs;  ///< オフセット式が global.get のときのグローバルインデックス（0xFFFFFFFF = i32.const）。
    uint8_t* data_segment_is_active;             ///< true=アクティブ、false=パッシブ。
    uint32_t data_segment_count;
    uint32_t data_segment_capacity;

    uint32_t** elem_segments;
    uint32_t* elem_segment_sizes;
    uint8_t* elem_segment_dropped;
    uint32_t* elem_segment_table_indices; ///< アクティブセグメントのターゲットテーブル。
    uint32_t* elem_segment_offsets;              ///< アクティブセグメントのテーブルオフセット。
    uint32_t* elem_segment_offset_global_refs;  ///< オフセット式が global.get のときのグローバルインデックス（0xFFFFFFFF = i32.const）。
    uint8_t* elem_segment_is_active;             ///< true=アクティブ、false=パッシブ/宣言的。
    uint32_t elem_segment_count;
    uint32_t elem_segment_capacity;

    int32_t start_function_index; ///< Start セクションで指定された関数インデックス（-1 = なし）。
    uint32_t self_index;          ///< modules_[] 上の自身のインデックス。EncodeFuncRef で使用。

    /// @brief 線形メモリの先頭ポインタを返します。
    uint8_t *GetLinearMemory() const noexcept {
        return linear_memory_ptr;
    }

    /// @brief 線形メモリの現在サイズを返します。
    std::size_t GetLinearMemorySize() const noexcept {
        return linear_memory_size;
    }

    int32_t GetExportFunctionIndex(const char* func_name, std::size_t func_name_len) const noexcept;
};

struct WasmTrapInfo {
    WasmResult result;
    uint32_t pc;
};

class WasmEngine;

#if EMBWASM_ENABLE_MULTITHREADING

/// @brief スレッド間同期用イベント（セマフォ / フラグ相当）。
struct WasmEvent {
    uint32_t id;    ///< イベント ID（1-based）。
    uint8_t signaled;  ///< シグナル済みフラグ。

    /// @brief イベントを未シグナル状態にリセットします。
    void Reset() noexcept {
        id = 0;
        signaled = false;
    }
};

/// @brief 協調型マルチスレッドスケジューラ。
///
/// `WasmEngine` に内包されており、ユーザーが直接インスタンス化する必要はありません。
/// `engine.GetScheduler()` で参照を取得できます。
/// ラウンドロビン方式で `kReady` 状態のスレッドを順に実行します。
class WasmScheduler {
public:
    /// @brief メインスレッド専用スロットのインデックス（slot 0）。
    static constexpr uint32_t kMainThreadIndex = 0;

    /// @brief コンストラクタ。`WasmEngine` の初期化時に自動的に呼ばれます。
    /// @param engine  所属するエンジンインスタンスへの参照。
    WasmScheduler(WasmEngine& engine) noexcept;

    /// @brief スレッドプールを確保しメインスレッドを初期化します。`WasmEngine::Init()` から呼ばれます。
    void Init() noexcept;

    /// @brief スケジューラをクリーンアップします。`WasmEngine::Deinit()` から呼ばれます。
    void Deinit() noexcept;

    /// @brief ワーカースレッドを作成します（slot 1 以降を使用）。ホスト API から呼ばれます。
    /// @param func_index  実行する WASM 関数のインデックス。
    /// @return 作成されたスレッドの ID（1-based）。作成失敗時は 0 を返します。
    uint32_t CreateThread(uint32_t func_index) noexcept;

    /// @brief 新しいイベントを取得します。
    /// @return イベント ID（1-based）。取得失敗時は 0 を返します。
    uint32_t CreateEvent() noexcept;

    /// @brief 指定イベントをシグナルし、待機中のスレッドを `kReady` にします。
    /// @param event_id  シグナルするイベント ID。
    void SignalEvent(uint32_t event_id) noexcept;

    /// @brief 指定スレッドを指定イベントの待機状態（`kWaiting`）に移行します。
    ///        既にシグナル済みの場合は待機せずに `kReady` のままにします。
    /// @param thread_id  対象スレッドの ID（1-based）。
    /// @param event_id   待機するイベント ID。
    void WaitEvent(uint32_t thread_id, uint32_t event_id) noexcept;

    /// @brief 指定スレッドを `kNotify` 待機状態（`kWaiting`）に移行します。
    ///        `notify_pending` が立っていた場合は即 `kReady` に戻します。
    ///        非同期 I/O 待ちホスト関数から呼ばれます。
    void ThreadWait(uint32_t thread_id) noexcept;

    /// @brief 指定スレッドの `kNotify` 待機を解除して `kReady` にします。
    ///        完了ハンドラ（I/O マネージャスレッド）からスタックに結果を積んだ後に呼びます。
    ///        この関数は他のネイティブスレッドから実行可能です。
    void ThreadNotify(uint32_t thread_id) noexcept;

    /// @brief 指定スレッドを `duration_ms` ミリ秒スリープさせます。
    void ThreadSleep(uint32_t thread_id, uint32_t duration_ms) noexcept;

    /// @brief すべてのスレッドが終了するまでラウンドロビンで実行します。
    /// @return 正常終了時は kOk。いずれかのスレッドでエラーが発生した場合はそのエラーコード。
    WasmResult Run() noexcept;

    /// @brief メインスレッドにモジュールと関数を割り当て `kReady` 状態にします。
    ///        `WasmEngine::Execute()` および Start 関数実行時に内部から呼ばれます。
    /// @param mod         実行するモジュールインスタンス。
    /// @param func_index  実行する関数のインデックス。
    /// @return メインスレッドの ID。
    uint32_t SetupMainThread(WasmModuleInstance* mod, uint32_t func_index) noexcept;

    /// @brief メインスレッドのコンテキストを返します。未初期化時は `nullptr` を返します。
    WasmThreadContext* GetMainThreadContext() noexcept {
        return (threads_) ? threads_[kMainThreadIndex] : nullptr;
    }

    /// @brief 指定 ID のスレッドコンテキストを返します。
    /// @param thread_id  スレッド ID（`CreateThread` の戻り値、1-based）。
    /// @return 対応する WasmThreadContext へのポインタ。無効な ID の場合は `nullptr`。
    WasmThreadContext* GetThreadContext(uint32_t thread_id) noexcept {
        if (thread_id == 0 || thread_id > kMaxThreads || !threads_) return nullptr;
        return threads_[thread_id - 1];
    }

    /// @brief 所属するエンジンインスタンスへの参照を返します。
    WasmEngine& GetEngine() noexcept { return engine_; }

    /// @brief 現在実行中のスレッドコンテキストを返します。
    WasmThreadContext* GetCurrentThreadContext() noexcept {
        return (current_thread_index_ < kMaxThreads && threads_) ? threads_[current_thread_index_] : nullptr;
    }
    /// @brief 現在実行中のスレッドコンテキストを返します（const 版）。
    const WasmThreadContext* GetCurrentThreadContext() const noexcept {
        return (current_thread_index_ < kMaxThreads && threads_) ? threads_[current_thread_index_] : nullptr;
    }

private:
    /// @brief ラウンドロビンで kReady なスレッドを 1 つ選び 1 ステップ実行します。
    WasmResult Step() noexcept;

    /// @brief kReady 状態のスレッドが 1 つでも存在するか確認します。
    bool HasReadyThread() noexcept;

    /// @brief kSleep スレッドの次の起床時刻までの残時間（ms）を返します。
    ///        sleep スレッドが 1 つもない場合は UINT32_MAX を返します。
    uint32_t ComputeMinSleepTimeout() noexcept;

    /// @brief 起床時刻を過ぎた kSleep スレッドを kReady に移行します。
    void PollSleeps() noexcept;

    WasmEngine& engine_;
    WasmThreadContext** threads_;
    WasmEvent events_[kMaxEvents];
    uint32_t current_thread_index_;
};

#endif // EMBWASM_ENABLE_MULTITHREADING

/// @brief ベアメタル環境向け極小 WASM 実行エンジン。
///
/// STL・例外・RTTI・動的メモリを一切使用しません。
/// `Init()` で渡した `WasmMemoryPool` からのみメモリを確保します。
class WasmEngine {
public:
    WasmEngine() noexcept;
    ~WasmEngine() noexcept;

    /// @brief エンジンを初期化します。使用前に必ず呼んでください。
    /// @param pool    エンジンが使用するメモリプール。
    /// @param config  実行時設定（省略時はデフォルト値）。
    /// @return kOk（成功）またはエラーコード。失敗時はリソースを解放済み。
    WasmResult Init(WasmMemoryPool& pool, const WasmEngineConfig& config = WasmEngineConfig{}) noexcept;

    /// @brief エンジンを終了し、すべてのリソースを解放します。
    void Deinit() noexcept;

    /// @brief WASM バイナリをロードし、指定名のモジュールとして登録します。
    /// @param module_name      モジュール名文字列。
    /// @param module_name_len  モジュール名の長さ（バイト数）。
    /// @param binary           WASM バイナリデータの先頭ポインタ。
    /// @param size             バイナリのバイト数。
    /// @return 0 以上のインスタンス ID（成功）。負値のエラーコード（失敗(WasmResult型)）。
    int32_t LoadModule(const char* module_name, std::size_t module_name_len, const uint8_t* binary, std::size_t size) noexcept;

    /// @brief すべてのモジュールをアンロードし、プール上のメモリを解放します。
    void UnloadAllModules() noexcept;

    /// @brief WASM バイナリをロードする。
    /// @param binary  WASM バイナリデータの先頭ポインタ。
    /// @param size    バイナリのバイト数。
    /// @return 0 以上のインスタンス ID（成功）。負値のエラーコード（失敗(WasmResult型)）。
    int32_t LoadModule(const uint8_t* binary, std::size_t size) noexcept {
        return LoadModule(nullptr, 0, binary, size);
    }

    /// @brief ロード済みの全 WASM モジュールのインポートを解決（インスタンス化）します。
    /// すでにインスタンス化済みのモジュールはスキップします。
    /// @return kOk（成功）またはエラーコード。
    WasmResult InstantiateModules() noexcept;

    /// @brief 指定モジュールのエクスポート関数を実行します。
    /// @param module_name      モジュール名。
    /// @param module_name_len  モジュール名の長さ（バイト数）。
    /// @param func_name             エクスポート関数名。
    /// @param func_name_len         関数名の長さ（バイト数）。
    /// @param args             引数配列（引数なしの場合は `nullptr`）。
    /// @param arg_count        引数の個数。
    /// @param results          結果格納配列（戻り値なしの場合は `nullptr`）。
    /// @param result_count     戻り値の個数。
    /// @return 実行結果を示す WasmResult。
    WasmResult Execute(const char* module_name, std::size_t module_name_len, const char* func_name, std::size_t func_name_len, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;

    /// @brief 事前解決済みのインスタンス ID と関数インデックスで直接実行します。
    /// Execute() のモジュール名・関数名ルックアップと InstantiateModules() を省略するため、
    /// 同じ関数を繰り返し呼ぶホットパスで使用してください。
    /// @param instance_id   LoadModule() の戻り値。
    /// @param func_idx      GetExportFunctionIndex() の戻り値。
    /// @param args          引数配列（なければ nullptr）。
    /// @param arg_count     引数の個数。
    /// @param results       結果格納配列（なければ nullptr）。
    /// @param result_count  戻り値の個数。
    WasmResult ExecuteByIndex(int32_t instance_id, int32_t func_idx, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept;

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

    /// @brief プラットフォーム層が使用するデータポインタを返します。
    void* GetPlatformData() const noexcept { return platform_data_; }

    /// @brief プラットフォーム層が使用するデータポインタを設定します。
    void SetPlatformData(void* data) noexcept { platform_data_ = data; }

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

    /// @brief 指定モジュール・関数インデックスで実行ループを起動します（内部 API）。
    WasmResult ExecuteInternal(WasmModuleInstance* module, uint32_t func_index) noexcept;

    /// @brief ctx のコールスタックが空になるまでバイトコードを実行します（内部 API）。
    /// ExecuteInternal が初期フレームを積んだ後、またはスレッド再開時に直接呼ばれます。
    WasmResult RunLoop(WasmThreadContext* ctx) noexcept;

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

    /// @brief エンジンが使用するメモリプールへのポインタを返します。
    WasmMemoryPool* GetMemoryPool() const noexcept { return pool_; }

    /// @brief 現在の実行時設定を返します。
    const WasmEngineConfig& GetConfig() const noexcept { return config_; }

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

    /// @brief モジュールに別名を登録します（インポート解決で使用）。
    /// @param real_name      実モジュール名。
    /// @param real_name_len  実モジュール名の長さ（バイト数）。
    /// @param alias_name     別名。
    /// @param alias_name_len 別名の長さ（バイト数）。
    void RegisterAlias(const char* real_name, std::size_t real_name_len, const char* alias_name, std::size_t alias_name_len) noexcept;

private:
    friend class WasmScheduler;

    WasmResult ExecuteResolved(WasmModuleInstance* mod, uint32_t func_idx,
                               const WasmValue* args, uint32_t arg_count,
                               WasmValue* results, uint32_t result_count) noexcept;

    WasmResult ParseSections(WasmModuleInstance* mod, const uint8_t* binary, std::size_t size) noexcept;

    WasmResult Validate(WasmModuleInstance* mod) noexcept;
    WasmResult ValidateFunctionBody(WasmModuleInstance* mod, uint32_t func_idx) noexcept;

    WasmResult ResolveImports(WasmModuleInstance* mod) noexcept;
    void FreeModuleInstance(WasmModuleInstance* mod) noexcept;
    WasmResult OnTrap(WasmResult result) noexcept;

    struct NameAlias {
        ListNode node;
        uint32_t alias_len;
        WasmModuleInstance* module;
        char alias[1];
    };
    ListNode name_aliases_;
    uint32_t name_alias_count_;

    WasmEngineConfig config_;
    WasmMemoryPool* pool_;
    WasmModuleInstance* modules_[kMaxModules];

#if EMBWASM_ENABLE_MULTITHREADING
    WasmScheduler scheduler_;
#else
    WasmThreadContext* ctx_;
#endif

    int32_t last_loaded_id_;
    uint32_t max_call_stack_depth_;
    uint32_t max_stack_depth_;
    void* user_data_;
    void* platform_data_;
    void** module_user_datas_;
};

void GetLinearMemoryForHostApi(WasmEngine& engine, uint8_t *&mem_base, size_t &mem_size) noexcept;

} // namespace embwasm

#endif // EMBWASM_WASM_ENGINE_HPP_
