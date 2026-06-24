#include "wasm_thread.hpp"
#include "wasm_engine.hpp"
#include "wasm_platform.hpp"

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
    WasmMemoryPool* pool = engine_.GetMemoryPool();
    if (!pool) return;
    void* allocated = pool->Allocate(sizeof(WasmThreadContext*) * kMaxThreads);
    if (!allocated) return;
    threads_ = static_cast<WasmThreadContext**>(allocated);
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        threads_[i] = nullptr;
    }
    void* main_ctx = pool->Allocate(sizeof(WasmThreadContext));
    if (!main_ctx) return;
    threads_[kMainThreadIndex] = static_cast<WasmThreadContext*>(main_ctx);
    threads_[kMainThreadIndex]->Reset();
    threads_[kMainThreadIndex]->id = static_cast<uint32_t>(kMainThreadIndex + 1);
}

void WasmScheduler::Deinit() noexcept {
    threads_ = nullptr;
    current_thread_index_ = 0;
    for (std::size_t i = 0; i < kMaxEvents; ++i) {
        events_[i].Reset();
        events_[i].id = static_cast<uint32_t>(i + 1);
    }
}

uint32_t WasmScheduler::SetupMainThread(WasmModuleInstance* mod, uint32_t func_index) noexcept {
    if (!threads_ || !threads_[kMainThreadIndex]) return 0;
    WasmThreadContext& main = *threads_[kMainThreadIndex];
    main.Reset();
    main.id = static_cast<uint32_t>(kMainThreadIndex + 1);
    main.state = ThreadState::kReady;
    main.stack_top = 0;
    main.call_stack_top = 0;
    main.labels_pool_top = 0;
    main.start_func_index = func_index;
    main.start_module = mod;
    return main.id;
}

uint32_t WasmScheduler::CreateThread(uint32_t func_index) noexcept {
    if (!threads_) return 0;
    for (std::size_t i = kMainThreadIndex + 1; i < kMaxThreads; ++i) {
        if (!threads_[i] || threads_[i]->state == ThreadState::kTerminated) {
            if (!threads_[i]) {
                WasmMemoryPool* pool = engine_.GetMemoryPool();
                if (!pool) return 0;
                void* allocated = pool->Allocate(sizeof(WasmThreadContext));
                if (!allocated) return 0;
                threads_[i] = static_cast<WasmThreadContext*>(allocated);
                threads_[i]->Reset();
                threads_[i]->id = static_cast<uint32_t>(i + 1);
            }
            threads_[i]->state = ThreadState::kReady;
            threads_[i]->stack_top = 0;
            threads_[i]->call_stack_top = 0;
            threads_[i]->labels_pool_top = 0;
            threads_[i]->start_func_index = func_index;

            WasmThreadContext* active_ctx = GetCurrentThreadContext();
            WasmModuleInstance* caller_mod = nullptr;
            if (active_ctx && active_ctx->call_stack_top > 0) {
                const WasmFrame& top = active_ctx->call_stack[active_ctx->call_stack_top - 1];
                if (top.func) caller_mod = const_cast<WasmModuleInstance*>(top.func->module);
            }
            threads_[i]->start_module = caller_mod;

            return threads_[i]->id;
        }
    }
    return 0;
}

uint32_t WasmScheduler::CreateEvent() noexcept {
    for (std::size_t i = 0; i < kMaxEvents; ++i) {
        if (events_[i].id != 0 && !events_[i].signaled) {
            return events_[i].id;
        }
    }
    return 0;
}

void WasmScheduler::SignalEvent(uint32_t event_id) noexcept {
    if (!threads_ || event_id == 0 || event_id > kMaxEvents) return;
    events_[event_id - 1].signaled = true;

    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        WasmThreadContext* t = threads_[i];
        if (t && t->state == ThreadState::kWaiting
               && t->wait_kind == WaitKind::kEvent
               && t->wait_param.event_id == event_id) {
            t->state    = ThreadState::kReady;
            t->wait_kind = WaitKind::kNone;
        }
    }
    PlatformNotifyActivity();
}

void WasmScheduler::WaitEvent(uint32_t thread_id, uint32_t event_id) noexcept {
    if (!threads_ || thread_id == 0 || thread_id > kMaxThreads) return;
    if (event_id == 0 || event_id > kMaxEvents) return;

    WasmThreadContext* ctx = threads_[thread_id - 1];
    if (!ctx) return;
    if (events_[event_id - 1].signaled) {
        events_[event_id - 1].signaled = false;
        ctx->state    = ThreadState::kReady;
        ctx->wait_kind = WaitKind::kNone;
    } else {
        ctx->state    = ThreadState::kWaiting;
        ctx->wait_kind = WaitKind::kEvent;
        ctx->wait_param.event_id = event_id;
    }
}

void WasmScheduler::ThreadWait(uint32_t thread_id) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return;
    if (ctx->notify_pending) {
        ctx->notify_pending = false;
        ctx->state    = ThreadState::kReady;
        ctx->wait_kind = WaitKind::kNone;
    } else {
        ctx->state    = ThreadState::kWaiting;
        ctx->wait_kind = WaitKind::kNotify;
    }
}

void WasmScheduler::ThreadNotify(uint32_t thread_id) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return;
    if (ctx->state == ThreadState::kWaiting && ctx->wait_kind == WaitKind::kNotify) {
        ctx->state    = ThreadState::kReady;
        ctx->wait_kind = WaitKind::kNone;
        PlatformNotifyActivity();
    } else {
        ctx->notify_pending = true;
    }
}

void WasmScheduler::ThreadSleep(uint32_t thread_id, uint32_t duration_ms) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return;
    ctx->state    = ThreadState::kWaiting;
    ctx->wait_kind = WaitKind::kSleep;
    ctx->wait_param.wake_time_ms = PlatformGetTimeMs() + duration_ms;
}

bool WasmScheduler::HasReadyThread() noexcept {
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        if (threads_[i] && threads_[i]->state == ThreadState::kReady) return true;
    }
    return false;
}

uint32_t WasmScheduler::ComputeMinSleepTimeout() noexcept {
    uint32_t now = PlatformGetTimeMs();
    uint32_t min_timeout = UINT32_MAX;
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        WasmThreadContext* t = threads_[i];
        if (t && t->state == ThreadState::kWaiting && t->wait_kind == WaitKind::kSleep) {
            uint32_t rem = (t->wait_param.wake_time_ms > now)
                           ? t->wait_param.wake_time_ms - now : 0;
            if (rem < min_timeout) min_timeout = rem;
        }
    }
    return min_timeout;
}

void WasmScheduler::PollSleeps() noexcept {
    uint32_t now = PlatformGetTimeMs();
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        WasmThreadContext* t = threads_[i];
        if (t && t->state == ThreadState::kWaiting
               && t->wait_kind == WaitKind::kSleep
               && now >= t->wait_param.wake_time_ms) {
            t->state    = ThreadState::kReady;
            t->wait_kind = WaitKind::kNone;
        }
    }
}

WasmResult WasmScheduler::Run() noexcept {
    if (!threads_) return WasmResult::kErrorOutOfMemory;
    while (true) {
        bool any_active = false;
        bool any_ready  = false;
        for (std::size_t i = 0; i < kMaxThreads; ++i) {
            WasmThreadContext* t = threads_[i];
            if (!t || t->state == ThreadState::kTerminated) continue;
            any_active = true;
            if (t->state == ThreadState::kReady) any_ready = true;
        }
        if (!any_active) break;

        if (any_ready) {
            WasmResult res = Step();
            if (res != WasmResult::kOk && res != WasmResult::kYield) return res;
        } else {
            PollSleeps();
            if (!HasReadyThread()) {
                PlatformWaitForActivity(ComputeMinSleepTimeout());
                PollSleeps();
            }
        }
    }
    return WasmResult::kOk;
}

WasmResult WasmScheduler::Step() noexcept {
    for (std::size_t i = 0; i < kMaxThreads; ++i) {
        std::size_t idx = (current_thread_index_ + i) % kMaxThreads;
        if (threads_[idx] && threads_[idx]->state == ThreadState::kReady) {
            current_thread_index_ = idx;
            WasmThreadContext& ctx = *threads_[idx];
            ctx.state = ThreadState::kRunning;

            WasmResult res;
            if (ctx.call_stack_top == 0) {
                res = engine_.ExecuteInternal(ctx.start_module, ctx.start_func_index);
            } else {
                WasmModuleInstance* resume_mod = const_cast<WasmModuleInstance*>(
                    ctx.call_stack[ctx.call_stack_top - 1].func->module);
                res = engine_.ExecuteInternal(resume_mod, 0);
            }

            if (res == WasmResult::kYield) {
                if (ctx.state == ThreadState::kRunning) {
                    ctx.state = ThreadState::kReady;
                }
            } else if (res == WasmResult::kOk) {
                ctx.state = ThreadState::kTerminated;
            } else {
                ctx.state = ThreadState::kTerminated;
                current_thread_index_ = (current_thread_index_ + 1) % kMaxThreads;
                return res;
            }

            current_thread_index_ = (current_thread_index_ + 1) % kMaxThreads;
            return WasmResult::kOk;
        }
    }
    return WasmResult::kOk;
}

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace embwasm
