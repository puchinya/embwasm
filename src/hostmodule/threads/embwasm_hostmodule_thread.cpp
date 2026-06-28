#include "wasm_engine.hpp"
#include "embwasm_hostmodule_thread.hpp"

namespace embwasm {
namespace hostmodules {
namespace sys {
namespace rt {
namespace threads {

#if EMBWASM_ENABLE_MULTITHREADING

// [embwasm-proto:func:thread_spawn]
WasmResult thread_spawn(WasmEngine& engine, const char* name, uint32_t name_len, int32_t& out_result) noexcept {

    if (name_len == 0) {
        out_result = -1;
        return WasmResult::kOk;
    }

    uint8_t* mem_base;
    size_t mem_size;
    GetLinearMemoryForHostApi(engine, mem_base, mem_size);
    if (reinterpret_cast<const uint8_t *>(name) < mem_base ||
        reinterpret_cast<const uint8_t *>(name) + name_len > mem_base + mem_size) {
        return WasmResult::kErrorExecuteTrapMemoryOutOfBounds;
    }

    WasmThreadContext* ctx = engine.GetCurrentThreadContext();
    WasmModuleInstance* calling_mod = ctx->GetCurrentModule();
    int32_t func_idx = calling_mod->GetExportFunctionIndex(name, name_len);
    if (func_idx < 0) {
        out_result = -1;
        return WasmResult::kOk;
    }

    uint32_t thread_id = engine.CreateThread(func_idx);
    out_result = static_cast<int32_t>(thread_id);

    return WasmResult::kOk;
}

// [embwasm-proto:func:thread_yield]
WasmResult thread_yield(WasmEngine& engine) noexcept {
    (void)engine;
    return WasmResult::kYield;
}

// [embwasm-proto:func:event_wait]
WasmResult event_wait(WasmEngine& engine, int32_t event_id) noexcept {
    WasmThreadContext* current = engine.GetCurrentThreadContext();
    engine.WaitEvent(current->id, static_cast<uint32_t>(event_id));
    return WasmResult::kYield;
}

// [embwasm-proto:func:event_signal]
WasmResult event_signal(WasmEngine& engine, int32_t event_id) noexcept {
    engine.SignalEvent(static_cast<uint32_t>(event_id));
    return WasmResult::kOk;
}

// [embwasm-proto:funcs-end]
#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace threads
} // namespace rt
} // namespace sys
} // namespace hostmodules
} // namespace embwasm
