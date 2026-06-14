#ifndef EMBWASM_WASM_THREAD_HPP_
#define EMBWASM_WASM_THREAD_HPP_

#include "wasm_types.hpp"
#include "wasm_config.hpp"

namespace embwasm {

class WasmEngine;
struct WasmFunction;

// WASM関数呼び出しのフレーム情報
struct WasmFrame {
    const WasmFunction* func;
    const uint8_t* ip;
    const uint8_t* limit;
    WasmValue locals[kMaxLocals];
    uint32_t total_locals;
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

    // 待ちイベントID（kWaiting時のみ有効）
    uint32_t wait_event_id;

    void Reset() noexcept {
        id = 0;
        state = ThreadState::kTerminated;
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
    WasmScheduler(WasmEngine& engine) noexcept;

    // スレッドの作成
    uint32_t CreateThread(uint32_t func_index) noexcept;

    // イベントの取得/シグナル
    uint32_t CreateEvent() noexcept;
    void SignalEvent(uint32_t event_id) noexcept;
    void WaitEvent(uint32_t thread_id, uint32_t event_id) noexcept;

    // スケジューリング実行（すべてのスレッドが終了するまで回す、または1ステップ実行）
    WasmResult Run() noexcept;
    WasmResult Step() noexcept;

    // ホストAPIから呼び出すための静的アクセサ（簡易実装のためグローバルまたはシングルトン）
    static WasmScheduler* GetInstance() noexcept { return instance_; }
    void SetAsInstance() noexcept { instance_ = this; }

    WasmThreadContext* GetCurrentThread() noexcept { 
        return (current_thread_index_ < kMaxThreads) ? &threads_[current_thread_index_] : nullptr; 
    }

private:
    WasmEngine& engine_;
    WasmThreadContext threads_[kMaxThreads];
    WasmEvent events_[kMaxEvents];
    std::size_t current_thread_index_;
    static WasmScheduler* instance_;
};

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace embwasm

#endif // EMBWASM_WASM_THREAD_HPP_
