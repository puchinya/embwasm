#include "wasm_thread.hpp"
#include "wasm_engine.hpp"
#include "embwasm_hostmodule_thread.hpp"

namespace embwasm {
namespace hostmodules {
namespace thread {

#if EMBWASM_ENABLE_MULTITHREADING

// ホストAPI: スレッドの新規作成
// (import "env" "thread_spawn" (func (param i32) (result i32)))
WasmResult ThreadSpawn(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept 
{
    if (arg_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t val = static_cast<uint32_t>(args[0].value.i32);
    WasmScheduler* scheduler = engine.GetScheduler();
    if (!scheduler) return WasmResult::kErrorRuntimeError;

    // スケジューラからエンジンを取得してインデックスを解決
    int32_t resolved_idx = -1;

    // 呼び出し元のモジュールを取得
    WasmThreadContext* ctx = scheduler->GetCurrentThreadContext();
    WasmModuleInstance* calling_mod = nullptr;
    if (ctx && ctx->call_stack_top > 0 && ctx->call_stack[ctx->call_stack_top - 1].func) {
        calling_mod = const_cast<WasmModuleInstance*>(
            ctx->call_stack[ctx->call_stack_top - 1].func->module);
    }

    // 引数が文字列（ポインタ）として渡された場合を想定して解決を試みる
    uint8_t* mem = engine.GetLinearMemory();
    if (mem && val < engine.GetLinearMemorySize()) {
        const char* func_name = reinterpret_cast<const char*>(&mem[val]);

        // 文字列の終端を確認し、妥当な長さであることをチェック
        bool valid_string = false;
        uint32_t func_name_len = 0;
        for (uint32_t i = 0; val + i < engine.GetLinearMemorySize(); ++i) {
            if (mem[val + i] == '\0') {
                if (i > 0) { valid_string = true; func_name_len = i; }
                break;
            }
            // WASMの関数名として妥当でない文字が含まれている場合はスキップ
            if (mem[val + i] < 32 || mem[val + i] > 126) break;
        }

        if (valid_string && calling_mod) {
            resolved_idx = engine.GetExportFunctionIndex(calling_mod->name, calling_mod->name_len, func_name, func_name_len);
        }
    }

    // もし名前で見つからない場合は、エクスポートインデックスまたは直接の関数インデックスとして扱う（後方互換性）
    if (resolved_idx < 0) {
        // 呼び出し元モジュールのinstance_idを探す
        int32_t calling_instance_id = -1;
        if (calling_mod) {
            for (int32_t m = 0; m < static_cast<int32_t>(kMaxModules); ++m) {
                if (engine.GetModuleInstanceById(m) == calling_mod) {
                    calling_instance_id = m;
                    break;
                }
            }
        }
        if (calling_instance_id >= 0) {
            resolved_idx = engine.GetFunctionIndexByExportIndex(calling_instance_id, val);
        }
    }
    
    uint32_t func_idx = (resolved_idx >= 0) ? static_cast<uint32_t>(resolved_idx) : val;

    uint32_t thread_id = scheduler->CreateThread(func_idx);
    
    // WASM側のシグネチャに合わせて戻り値をセット（呼び出し側が値を期待している場合）
    if (result_count > 0) {
        results[0].value.i32 = static_cast<int32_t>(thread_id);
    }

    return WasmResult::kOk;
}

// ホストAPI: 実行権の譲渡
// (import "env" "thread_yield" (func))
WasmResult ThreadYield(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept 
{
    (void)engine; (void)args; (void)arg_count; (void)results; (void)result_count;
    return WasmResult::kYield;
}

// ホストAPI: イベント待ち
// (import "env" "event_wait" (func (param i32)))
WasmResult EventWait(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept 
{
    (void)results; (void)result_count;
    if (arg_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t event_id = static_cast<uint32_t>(args[0].value.i32);
    WasmScheduler* scheduler = engine.GetScheduler();
    if (!scheduler) return WasmResult::kErrorRuntimeError;

    WasmThreadContext* current = scheduler->GetCurrentThreadContext();
    if (!current) return WasmResult::kErrorRuntimeError;

    scheduler->WaitEvent(current->id, event_id);

    return WasmResult::kYield;
}

// ホストAPI: イベント通知
// (import "env" "event_signal" (func (param i32)))
WasmResult EventSignal(
    WasmEngine& engine,
    const WasmValue* args, 
    uint32_t arg_count, 
    WasmValue* results, 
    uint32_t result_count) noexcept 
{
    (void)results; (void)result_count;
    if (arg_count < 1) {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t event_id = static_cast<uint32_t>(args[0].value.i32);
    WasmScheduler* scheduler = engine.GetScheduler();
    if (!scheduler) return WasmResult::kErrorRuntimeError;

    scheduler->SignalEvent(event_id);

    return WasmResult::kOk;
}

#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace thread
} // namespace hostmodules
} // namespace embwasm
