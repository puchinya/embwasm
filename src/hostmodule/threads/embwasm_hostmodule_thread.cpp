#include "wasm_thread.hpp"
#include "wasm_engine.hpp"
#include "embwasm_hostmodule_thread.hpp"

namespace embwasm {
namespace hostmodules {
namespace thread {

#if EMBWASM_ENABLE_MULTITHREADING

WasmResult ThreadSpawn(WasmEngine& engine, const char* name, uint32_t name_len, int32_t& out_result) noexcept {
    WasmScheduler* scheduler = engine.GetScheduler();
    if (!scheduler) return WasmResult::kErrorExecuteRuntimeError;

    int32_t resolved_idx = -1;
    if (name_len > 0) {
        uint8_t* mem = engine.GetLinearMemory();
        if (!mem) return WasmResult::kErrorExecuteRuntimeError;
        ptrdiff_t name_off = reinterpret_cast<const uint8_t*>(name) - mem;
        if (name_off < 0 || static_cast<size_t>(name_off) + name_len > engine.GetLinearMemorySize()) {
            return WasmResult::kErrorExecuteRuntimeError;
        }

        WasmThreadContext* ctx = scheduler->GetCurrentThreadContext();
        WasmModuleInstance* calling_mod = nullptr;
        if (ctx && ctx->call_stack_top > 0 && ctx->call_stack[ctx->call_stack_top - 1].func) {
            calling_mod = const_cast<WasmModuleInstance*>(
                ctx->call_stack[ctx->call_stack_top - 1].func->module);
        }
        if (calling_mod) {
            resolved_idx = engine.GetExportFunctionIndex(calling_mod->name, calling_mod->name_len, name, name_len);
        }
    }

    uint32_t func_idx = (resolved_idx >= 0) ? static_cast<uint32_t>(resolved_idx) : 0;
    uint32_t thread_id = scheduler->CreateThread(func_idx);
    out_result = static_cast<int32_t>(thread_id);

    return WasmResult::kOk;
}

WasmResult ThreadYield(WasmEngine& engine) noexcept {
    (void)engine;
    return WasmResult::kYield;
}

WasmResult EventWait(WasmEngine& engine, int32_t event_id) noexcept {
    WasmScheduler* scheduler = engine.GetScheduler();
    if (!scheduler) return WasmResult::kErrorExecuteRuntimeError;

    WasmThreadContext* current = scheduler->GetCurrentThreadContext();
    if (!current) return WasmResult::kErrorExecuteRuntimeError;

    scheduler->WaitEvent(current->id, static_cast<uint32_t>(event_id));

    return WasmResult::kYield;
}

WasmResult EventSignal(WasmEngine& engine, int32_t event_id) noexcept {
    WasmScheduler* scheduler = engine.GetScheduler();
    if (!scheduler) return WasmResult::kErrorExecuteRuntimeError;

    scheduler->SignalEvent(static_cast<uint32_t>(event_id));

    return WasmResult::kOk;
}

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace thread
} // namespace hostmodules
} // namespace embwasm
