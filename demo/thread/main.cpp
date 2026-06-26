#include <cstddef>
#include <iostream>
#include "embwasm.hpp"
#include "main_wasm.hpp"
#include "embwasm_hostmodule_thread.hpp"

constexpr size_t kMemoryPoolSize = 128 << 10;

namespace {
alignas(16) uint8_t g_wasm_pool_buf[kMemoryPoolSize];
}

int main() {
#if EMBWASM_ENABLE_MULTITHREADING
    std::cout << "=== Embedded WASM Engine Demo (Multithreading) ===" << std::endl;
#else
    std::cout << "=== Embedded WASM Engine Demo (Single Thread) ===" << std::endl;
#endif
    std::cout << "Memory Pool Size Configured: " << kMemoryPoolSize << " bytes" << std::endl;

    // 1. メモリプールの作成
    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));

    // 3. WASMエンジンの構築
    embwasm::WasmEngine engine;
    embwasm::WasmEngineConfig config;
    config.stack_size = 256;
    config.call_stack_size = 16;
    config.labels_pool_size = 64;
    engine.Init(pool, config);

    // 5. WASMバイナリのロード
    std::cout << "\nLoading WASM Binary..." << std::endl;
    int32_t instance_id = engine.LoadModule("main", 4, kMainWasmBinary, kMainWasmBinarySize);
    if (instance_id < 0) {
        std::cerr << "Failed to load WASM binary. Error code: " << instance_id << std::endl;
        return 1;
    }
    std::cout << "WASM Loaded successfully." << std::endl;

    std::cout << "\nExecuting WASM 'main' function..." << std::endl;
    embwasm::WasmResult run_res = engine.Execute("main", 4, "main", 4, nullptr, 0, nullptr, 0);
    if (run_res != embwasm::WasmResult::kOk) {
        std::cerr << "Execution failed. Error code: " << static_cast<int>(run_res) << std::endl;
        return 1;
    }

    std::cout << "\nExecution finished successfully." << std::endl;
    std::cout << "Max function nesting depth reached: " << engine.GetMaxCallStackDepth() << std::endl;
    std::cout << "Max VM stack depth reached: " << engine.GetMaxStackDepth() << std::endl;
    std::cout << "Memory Used: " << pool.GetUsedBytes() << " / " << pool.GetTotalBytes() << " bytes" << std::endl;
    engine.Deinit();
    std::cout << "Memory Used: " << pool.GetUsedBytes() << " / " << pool.GetTotalBytes() << " bytes" << std::endl;
    pool.Deinit();
    return 0;
}
