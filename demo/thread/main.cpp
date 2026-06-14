#include <cstddef>
#include <iostream>
#include "embwasm.hpp"
#include "main_wasm.hpp"
#include "wasm_thread.hpp"
#include "embwasm_hostmodule_thread.hpp"

namespace {
alignas(16) uint8_t g_wasm_pool_buf[embwasm::kMemoryPoolSize];
}

int main() {
#if EMBWASM_ENABLE_MULTITHREADING
    std::cout << "=== Embedded WASM Engine Demo (Multithreading) ===" << std::endl;
#else
    std::cout << "=== Embedded WASM Engine Demo (Single Thread) ===" << std::endl;
#endif
    std::cout << "Memory Pool Size Configured: " << embwasm::kMemoryPoolSize << " bytes" << std::endl;

    // 1. メモリプールの作成
    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));

    // 3. WASMエンジンの構築
    embwasm::WasmEngine engine;
    engine.Init(pool);

    // 5. WASMバイナリのロード
    std::cout << "\nLoading WASM Binary..." << std::endl;
    embwasm::WasmResult load_res = engine.Load(kMainWasmBinary, kMainWasmBinarySize);
    if (load_res != embwasm::WasmResult::kOk) {
        std::cerr << "Failed to load WASM binary. Error code: " << static_cast<int>(load_res) << std::endl;
        return 1;
    }
    std::cout << "WASM Loaded successfully." << std::endl;
    std::cout << "Memory Used: " << pool.GetUsedBytes() << " / " << pool.GetTotalBytes() << " bytes" << std::endl;

#if EMBWASM_ENABLE_MULTITHREADING
    // 6. スケジューラの初期化
    embwasm::WasmScheduler scheduler(engine);
    scheduler.SetAsInstance();

    // 7. メインスレッドの作成
    std::cout << "\nStarting Scheduler with Multithreaded WASM..." << std::endl;
    int32_t main_idx = engine.GetExportFunctionIndex("main");
    int32_t t2_idx = engine.GetExportFunctionIndex("thread2");
    
    if (main_idx < 0) {
        std::cerr << "Exported function 'main' not found." << std::endl;
        return 1;
    }
    std::cout << "Main func idx: " << main_idx << ", Thread2 func idx: " << t2_idx << std::endl;

    scheduler.CreateThread(static_cast<uint32_t>(main_idx));

    embwasm::WasmResult run_res = scheduler.Run();
    if (run_res != embwasm::WasmResult::kOk) {
        std::cerr << "Scheduler execution failed. Error code: " << static_cast<int>(run_res) << std::endl;
        return 1;
    }
#else
    // 単一スレッド実行
    std::cout << "\nExecuting WASM 'main' function..." << std::endl;
    embwasm::WasmValue results[1];
    embwasm::WasmResult run_res = engine.Execute("main", nullptr, 0, results, 0);
    if (run_res != embwasm::WasmResult::kOk) {
        std::cerr << "WASM execution failed. Error code: " << static_cast<int>(run_res) << std::endl;
        return 1;
    }
#endif

    std::cout << "\nExecution finished successfully." << std::endl;
    std::cout << "Max function nesting depth reached: " << engine.GetMaxCallStackDepth() << std::endl;
    std::cout << "Max VM stack depth reached: " << engine.GetMaxStackDepth() << std::endl;
    engine.Deinit();
    pool.Deinit();
    return 0;
}
