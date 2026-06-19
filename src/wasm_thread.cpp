#include "wasm_thread.hpp"
#include "wasm_engine.hpp"

namespace embwasm {

#if EMBWASM_ENABLE_MULTITHREADING

WasmScheduler::WasmScheduler(WasmEngine& engine) noexcept
    : engine_(engine), threads_(nullptr), current_thread_index_(0) {
    for (std::size_t i = 0; i < kMaxEvents; ++i) {
        events_[i].Reset();
        events_[i].id = static_cast<uint32_t>(i + 1);
    }
}

void WasmScheduler::Init() noexcept {
    if (!EnsureThreadsAllocated()) return;
}

void WasmScheduler::Deinit() noexcept {
    threads_ = nullptr;
    current_thread_index_ = 0;
    for (std::size_t i = 0; i < kMaxEvents; ++i) {
        events_[i].Reset();
        events_[i].id = static_cast<uint32_t>(i + 1);
    }
}

bool WasmScheduler::EnsureThreadsAllocated() noexcept {
    if (threads_) return true;
    WasmMemoryPool* pool = engine_.GetMemoryPool();
    if (!pool) return false;

    void* allocated = pool->Allocate(sizeof(WasmThreadContext) * kMaxThreads);
    if (!allocated) return false;

    threads_ = static_cast<WasmThreadContext*>(allocated);
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        threads_[i].Reset();
        threads_[i].id = static_cast<uint32_t>(i + 1);
    }
    return true;
}

uint32_t WasmScheduler::SetupMainThread(WasmModuleInstance* mod, uint32_t func_index) noexcept {
    if (!EnsureThreadsAllocated()) return 0;
    // メインスレッド（slot 0）を設定して返す
    WasmThreadContext& main = threads_[kMainThreadIndex];
    main.Reset();
    main.id = static_cast<uint32_t>(kMainThreadIndex + 1);
    main.state = ThreadState::kReady;
    main.stack_top = 0;
    main.call_stack_top = 0;
    main.locals_pool_top = 0;
    main.wait_event_id = func_index;
    main.start_module = mod;
    return main.id;
}

uint32_t WasmScheduler::CreateThread(uint32_t func_index) noexcept {
    if (!EnsureThreadsAllocated()) return 0;
    // ワーカースレッド: slot 1 以降を使用（slot 0 はメインスレッド専用）
    for (std::size_t i = kMainThreadIndex + 1; i < kMaxThreads; ++i) {
        if (threads_[i].state == ThreadState::kTerminated) {
            threads_[i].state = ThreadState::kReady;
            threads_[i].stack_top = 0;
            threads_[i].call_stack_top = 0;
            threads_[i].wait_event_id = func_index;

            // 呼び出し元のモジュールを記録（初回ExecuteInternal用）
            WasmThreadContext* active_ctx = engine_.GetContext();
            WasmModuleInstance* caller_mod = nullptr;
            if (active_ctx && active_ctx->call_stack_top > 0) {
                const WasmFrame& top = active_ctx->call_stack[active_ctx->call_stack_top - 1];
                if (top.func) caller_mod = const_cast<WasmModuleInstance*>(top.func->module);
            }
            threads_[i].start_module = caller_mod;

            return threads_[i].id;
        }
    }
    return 0;
}

uint32_t WasmScheduler::CreateEvent() noexcept {
    for (std::size_t i = 0; i < kMaxEvents; ++i) {
        if (events_[i].id != 0 && !events_[i].signaled) {
            // 未使用（またはリセット済み）のイベントを返す
            // 簡易実装のため、常に固定数を使い回す
            return events_[i].id;
        }
    }
    return 0;
}

void WasmScheduler::SignalEvent(uint32_t event_id) noexcept {
    if (!EnsureThreadsAllocated() || event_id == 0 || event_id > kMaxEvents) return;
    events_[event_id - 1].signaled = true;

    // このイベントを待っているスレッドをReadyにする
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        if (threads_[i].state == ThreadState::kWaiting && threads_[i].wait_event_id == event_id) {
            threads_[i].state = ThreadState::kReady;
        }
    }
}

void WasmScheduler::WaitEvent(uint32_t thread_id, uint32_t event_id) noexcept {
    if (!EnsureThreadsAllocated() || thread_id == 0 || thread_id > kMaxThreads) return;
    if (event_id == 0 || event_id > kMaxEvents) return;

    WasmThreadContext& ctx = threads_[thread_id - 1];
    if (events_[event_id - 1].signaled) {
        // 既にシグナルされている場合は待たない（消費するかは設計次第だが、ここではリセットする）
        events_[event_id - 1].signaled = false;
        ctx.state = ThreadState::kReady;
    } else {
        ctx.state = ThreadState::kWaiting;
        ctx.wait_event_id = event_id;
    }
}

WasmResult WasmScheduler::Run() noexcept {
    if (!EnsureThreadsAllocated()) return WasmResult::kErrorOutOfMemory;
    while (true) {
        bool any_active = false;
        for (std::size_t i = 0; i < kMaxThreads; ++i) {
            if (threads_[i].state != ThreadState::kTerminated) {
                any_active = true;
                break;
            }
        }
        if (!any_active) break;

        WasmResult res = Step();
        if (res != WasmResult::kOk && res != WasmResult::kYield) return res;
    }
    return WasmResult::kOk;
}

WasmResult WasmScheduler::Step() noexcept {
    if (!EnsureThreadsAllocated()) return WasmResult::kErrorOutOfMemory;
    // Round-robin で Ready なスレッドを探す
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        std::size_t idx = (current_thread_index_ + i) % kMaxThreads;
        if (threads_[idx].state == ThreadState::kReady) {
            current_thread_index_ = idx;
            WasmThreadContext& ctx = threads_[idx];
            
            engine_.SetContext(&ctx);
            ctx.state = ThreadState::kRunning;
            
            WasmResult res;
            if (ctx.call_stack_top == 0) {
                // 初回実行。ExecuteInternal を使う場合、引数はスタックに積まれている必要がある。
                // 現在の CreateThread 実装では引数をサポートしていないため、
                // 内部関数の開始として func_index を指定する。
                res = engine_.ExecuteInternal(ctx.start_module, ctx.wait_event_id);
            } else {
                // 再開（継続実行）。コールスタックは残っているため、モジュールはトップフレームから取得。
                WasmModuleInstance* resume_mod = const_cast<WasmModuleInstance*>(
                    ctx.call_stack[ctx.call_stack_top - 1].func->module);
                res = engine_.ExecuteInternal(resume_mod, 0);
            }

            if (res == WasmResult::kYield) {
                // 中断。状態はホストAPI側で kWaiting に変えられている可能性がある
                if (ctx.state == ThreadState::kRunning) {
                    ctx.state = ThreadState::kReady;
                }
            } else if (res == WasmResult::kOk) {
                ctx.state = ThreadState::kTerminated;
            } else {
                // 実行時エラー。
                ctx.state = ThreadState::kTerminated;
                current_thread_index_ = (current_thread_index_ + 1) % kMaxThreads;
                return res;
            }

            current_thread_index_ = (current_thread_index_ + 1) % kMaxThreads;
            return WasmResult::kOk;
        }
    }
    return WasmResult::kOk; // 実行可能なスレッドなし
}

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace embwasm
