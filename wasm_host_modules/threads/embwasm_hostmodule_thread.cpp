#include "wasm_thread.h"
#include "wasm_engine.h"
#include "embwasm_hostmodule_thread.h"

namespace embwasm {
namespace hostmodules {
namespace thread {

#if EMBWASM_ENABLE_MULTITHREADING

// ホストAPI: スレッドの新規作成
// (import "env" "thread_spawn" (func (param i32) (result i32)))
WasmResult ThreadSpawn(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)user_data;
    if (arg_count < 1 || args[0].type != WasmType::kI32 || result_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t func_idx = static_cast<uint32_t>(args[0].value.i32);
    WasmScheduler* scheduler = WasmScheduler::GetInstance();
    if (!scheduler) return WasmResult::kErrorRuntimeError;

    // TODO: ここで func_idx はエクスポートインデックスから関数インデックスへの変換が必要。
    // 今回のデモではWASM側で thread_spawn(1) と指定されている（thread2のインデックス）。
    // 実運用ではシンボル解決が必要だが、簡易実装のためそのまま渡す。
    uint32_t thread_id = scheduler->CreateThread(func_idx);
    
    results[0].type = WasmType::kI32;
    results[0].value.i32 = static_cast<int32_t>(thread_id);

    return WasmResult::kOk;
}

// ホストAPI: 実行権の譲渡
// (import "env" "thread_yield" (func))
WasmResult ThreadYield(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)args; (void)arg_count; (void)results; (void)result_count; (void)user_data;
    return WasmResult::kYield;
}

// ホストAPI: イベント待ち
// (import "env" "event_wait" (func (param i32)))
WasmResult EventWait(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results; (void)result_count; (void)user_data;
    if (arg_count < 1 || args[0].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t event_id = static_cast<uint32_t>(args[0].value.i32);
    WasmScheduler* scheduler = WasmScheduler::GetInstance();
    if (!scheduler) return WasmResult::kErrorRuntimeError;

    WasmThreadContext* current = scheduler->GetCurrentThread();
    if (!current) return WasmResult::kErrorRuntimeError;

    scheduler->WaitEvent(current->id, event_id);

    return WasmResult::kYield;
}

// ホストAPI: イベント通知
// (import "env" "event_signal" (func (param i32)))
WasmResult EventSignal(
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count, 
    void* user_data) noexcept 
{
    (void)results; (void)result_count; (void)user_data;
    if (arg_count < 1 || args[0].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t event_id = static_cast<uint32_t>(args[0].value.i32);
    WasmScheduler* scheduler = WasmScheduler::GetInstance();
    if (!scheduler) return WasmResult::kErrorRuntimeError;

    scheduler->SignalEvent(event_id);

    return WasmResult::kOk;
}

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace thread
} // namespace hostmodules
} // namespace embwasm
