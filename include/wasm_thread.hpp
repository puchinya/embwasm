#ifndef EMBWASM_WASM_THREAD_HPP_
#define EMBWASM_WASM_THREAD_HPP_

#include "wasm_types.hpp"
#include "wasm_config.hpp"

namespace embwasm {

class WasmEngine;
struct WasmFunction;
struct WasmModuleInstance;

// 制御ブロックのラベル情報
struct WasmLabel {
    const uint8_t* pc;         // ブロック終了（end）またはループ開始のIP
    std::size_t stack_top;     // 入場時のスタックトップ（br時にここまで戻す）
    uint8_t opcode;            // ブロックの種類（0x02: block, 0x03: loop, 0x04: if）
    uint32_t param_count;      // ブロックのパラメータ数
    uint32_t result_count;     // ブロックの結果数
};

// WASM関数呼び出しのフレーム情報
struct WasmFrame {
    const WasmFunction* func;
    const uint8_t* ip;
    const uint8_t* limit;
    WasmValue* locals;      // WasmThreadContext::locals_pool 内のスライスへのポインタ
    uint32_t total_locals;

    // ラベルスタック（制御フロー用）
    WasmLabel labels[kMaxLabels];
    std::size_t label_stack_top;
};

// スレッドの実行状態
enum class ThreadState : uint8_t {
    kReady,     // 実行可能
    kRunning,   // 実行中
    kWaiting,   // イベント待ち
    kTerminated // 終了
};

// スレッドごとの実行コンテキスト
struct WasmThreadContext {
    uint32_t id;
    ThreadState state;

    // データスタック
    WasmValue stack[kWasmStackSize];
    std::size_t stack_top;

    // コールスタック
    WasmFrame call_stack[kWasmCallStackSize];
    std::size_t call_stack_top;

    // ローカル変数プール（全フレームで共有。フレームごとに必要数を切り出す）
    WasmValue locals_pool[kLocalsPoolSize];
    std::size_t locals_pool_top; // 現在の使用済み先頭インデックス

    // 待ちイベントID（kWaiting時のみ有効）
    uint32_t wait_event_id;

    // スレッド開始モジュール（初回ExecuteInternal用）
    WasmModuleInstance* start_module;

    void Reset() noexcept {
        id = 0;
        state = ThreadState::kTerminated;
        locals_pool_top = 0;
        start_module = nullptr;
    }
};

#if EMBWASM_ENABLE_MULTITHREADING

// イベント（セマフォ/フラグ的な役割）
struct WasmEvent {
    uint32_t id;
    bool signaled;

    void Reset() noexcept {
        id = 0;
        signaled = false;
    }
};

// スケジューラ
class WasmScheduler {
public:
    // slot 0 をメインスレッド専用に予約（Execute / start関数はここで実行）
    static constexpr std::size_t kMainThreadIndex = 0;

    WasmScheduler(WasmEngine& engine) noexcept;

    // スレッドプール確保＆メインスレッド初期化（WasmEngine::Init() から呼ぶ）
    void Init() noexcept;

    // クリーンアップ（WasmEngine::Deinit() から呼ぶ）
    void Deinit() noexcept;

    // ワーカースレッドの作成（slot 1 以降を使用。ホスト API から呼ぶ）
    uint32_t CreateThread(uint32_t func_index) noexcept;

    // イベントの取得/シグナル
    uint32_t CreateEvent() noexcept;
    void SignalEvent(uint32_t event_id) noexcept;
    void WaitEvent(uint32_t thread_id, uint32_t event_id) noexcept;

    // スケジューリング実行（すべてのスレッドが終了するまで回す、または1ステップ実行）
    WasmResult Run() noexcept;
    WasmResult Step() noexcept;

    // メインスレッドにモジュールと関数を割り当て、kReady 状態にする（Execute / start関数 から呼ぶ）
    uint32_t SetupMainThread(WasmModuleInstance* mod, uint32_t func_index) noexcept;

    // メインスレッドコンテキストの取得
    WasmThreadContext* GetMainThreadContext() noexcept {
        return (threads_) ? &threads_[kMainThreadIndex] : nullptr;
    }

    // スレッドコンテキストの取得（thread_id は CreateThread の戻り値、1-based）
    WasmThreadContext* GetThreadContext(uint32_t thread_id) noexcept {
        if (thread_id == 0 || thread_id > kMaxThreads || !threads_) return nullptr;
        return &threads_[thread_id - 1];
    }

    WasmEngine& GetEngine() noexcept { return engine_; }

    WasmThreadContext* GetCurrentThreadContext() noexcept {
        return (current_thread_index_ < kMaxThreads && threads_) ? &threads_[current_thread_index_] : nullptr;
    }
    const WasmThreadContext* GetCurrentThreadContext() const noexcept {
        return (current_thread_index_ < kMaxThreads && threads_) ? &threads_[current_thread_index_] : nullptr;
    }

private:
    bool EnsureThreadsAllocated() noexcept;

    WasmEngine& engine_;
    WasmThreadContext* threads_;
    WasmEvent events_[kMaxEvents];
    std::size_t current_thread_index_;
};

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace embwasm

#endif // EMBWASM_WASM_THREAD_HPP_
