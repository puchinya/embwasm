#ifndef EMBWASM_WASM_THREAD_HPP_
#define EMBWASM_WASM_THREAD_HPP_

#include "wasm_types.hpp"
#include "wasm_config.hpp"

namespace embwasm {

class WasmEngine;
struct WasmFunction;
struct WasmModuleInstance;

/// @brief 制御ブロック（`block` / `loop` / `if`）のラベル情報。
///
/// 分岐（`br`）時のジャンプ先 IP とスタック巻き戻し位置を保持します。
struct WasmLabel {
    const uint8_t* pc;      ///< ジャンプ先 IP。`block`/`if` は `end` の次、`loop` はループ先頭。
    std::size_t stack_top;  ///< ブロック進入時のデータスタック高さ（`br` 時にここまで巻き戻す）。
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
    uint32_t total_locals;    ///< 引数＋ローカル変数の合計数。
    WasmLabel* labels;            ///< `WasmThreadContext::labels_pool` 内のスライスへのポインタ。
    uint32_t label_capacity;      ///< このフレームに割り当てたラベルスロット数。
    std::size_t label_stack_top;  ///< ラベルスタックの現在深さ。
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
/// データスタック・コールスタック・ローカル変数プールをすべて静的配列で保持します。
/// 動的メモリ確保を行いません。
struct WasmThreadContext {
    uint32_t id;        ///< スレッド ID（1-based）。
    ThreadState state;  ///< 現在の実行状態。

    /// @brief 統合スタック。演算値とフレームごとのローカル変数を一本の配列で管理します。
    /// レイアウト: [frame0 locals][frame0 operands][frame1 locals][frame1 operands]...
    WasmValue stack[kUnifiedStackSize];
    std::size_t stack_top; ///< スタックの現在深さ（ローカル変数領域を含む）。

    WasmFrame call_stack[kWasmCallStackSize]; ///< WASM コールスタック。
    std::size_t call_stack_top;               ///< コールスタックの現在深さ。

    /// @brief 全フレーム共有のラベルプール。フレームごとに必要数を切り出します。
    WasmLabel labels_pool[kLabelsPoolSize];
    std::size_t labels_pool_top; ///< 現在の使用済み先頭インデックス。

    WaitKind  wait_kind;      ///< 待機種別（state == kWaiting 時のみ有効）。
    WaitParam wait_param;     ///< 待機パラメータ（wait_kind に応じて使用するフィールドが異なる）。
    bool      notify_pending; ///< ThreadNotify が ThreadWait より先に届いた場合のフラグ。

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
};

#if EMBWASM_ENABLE_MULTITHREADING

/// @brief スレッド間同期用イベント（セマフォ / フラグ相当）。
struct WasmEvent {
    uint32_t id;    ///< イベント ID（1-based）。
    bool signaled;  ///< シグナル済みフラグ。

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
    static constexpr std::size_t kMainThreadIndex = 0;

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
    std::size_t current_thread_index_;
};

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace embwasm

#endif // EMBWASM_WASM_THREAD_HPP_
