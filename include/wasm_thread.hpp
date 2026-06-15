#ifndef EMBWASM_WASM_THREAD_HPP_
#define EMBWASM_WASM_THREAD_HPP_

#include "wasm_types.hpp"
#include "wasm_config.hpp"

namespace embwasm {

class WasmEngine;
struct WasmFunction;

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
    WasmValue locals[kMaxLocals];
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

    WasmEngine& GetEngine() noexcept { return engine_; }

    // ホストAPIから呼び出すための静的アクセサ（簡易実装のためグローバルまたはシングルトン）
    static WasmScheduler* GetInstance() noexcept { return instance_; }
    void SetAsInstance() noexcept { instance_ = this; }

    WasmThreadContext* GetCurrentThread() noexcept { 
        return (current_thread_index_ < kMaxThreads && threads_) ? &threads_[current_thread_index_] : nullptr; 
    }

private:
    bool EnsureThreadsAllocated() noexcept;

    WasmEngine& engine_;
    WasmThreadContext* threads_;
    WasmEvent events_[kMaxEvents];
    std::size_t current_thread_index_;
    static WasmScheduler* instance_;
};

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace embwasm

#endif // EMBWASM_WASM_THREAD_HPP_
