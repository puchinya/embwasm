#include <cstddef>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "embwasm.hpp"
#include "main_wasm.hpp"

constexpr size_t kMemoryPoolSize = 128 << 10;

namespace {
alignas(16) uint8_t g_wasm_pool_buf[kMemoryPoolSize];

struct SyncData {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    embwasm::WasmResult result = embwasm::WasmResult::kOk;
};

void on_task_done(embwasm::WasmThreadContext*, void* ud, embwasm::WasmResult r) {
    auto* sync = static_cast<SyncData*>(ud);
    std::lock_guard<std::mutex> lock(sync->mtx);
    sync->result = r;
    sync->done   = true;
    sync->cv.notify_one();
}
} // namespace

int main() {
    std::cout << "=== Embedded WASM Engine Demo (Async Thread) ===" << std::endl;
    std::cout << "Memory Pool Size: " << kMemoryPoolSize << " bytes" << std::endl;

    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));

    embwasm::WasmEngine engine;
    embwasm::WasmEngineConfig config;
    config.stack_size       = 256;
    config.call_stack_size  = 16;
    config.labels_pool_size = 64;
    engine.Init(pool, config);

    std::cout << "\nLoading WASM Binary..." << std::endl;
    int32_t instance_id = engine.LoadModule("main", 4, kMainWasmBinary, kMainWasmBinarySize);
    if (instance_id < 0) {
        std::cerr << "Failed to load WASM. Error: " << instance_id << std::endl;
        return 1;
    }
    engine.InstantiateModules();
    std::cout << "WASM Loaded successfully." << std::endl;

    // Step 1: インタプリタループを専用OSスレッドで起動
    std::thread interp_thread([&engine]() {
        engine.RunInterpreterLoop();
    });

    // Steps 2-4: メインOSスレッドからWasmスレッドを非同期生成・起動
    embwasm::WasmModuleInstance* mod = engine.GetModuleInstanceById(instance_id);
    int32_t func_idx = engine.GetExportFunctionIndex("main", 4, "async_task", 10);
    if (!mod || func_idx < 0) {
        std::cerr << "Function 'async_task' not found." << std::endl;
        engine.StopInterpreterLoop();
        interp_thread.join();
        return 1;
    }

    SyncData sync;

    std::cout << "\nCreating async Wasm thread..." << std::endl;
    uint32_t tid = engine.CreateHostThread(mod, static_cast<uint32_t>(func_idx)); // Step 2
    engine.SetThreadCallback(tid, on_task_done, &sync);                            // Step 2
    engine.PushThreadArg(tid, embwasm::WasmValue::FromI32(3));                    // Step 3: n=3
    engine.StartThread(tid);                                                       // Step 4

    // Step 5: コールバックが呼ばれるまで待機
    std::cout << "Waiting for async task to complete..." << std::endl;
    {
        std::unique_lock<std::mutex> lock(sync.mtx);
        sync.cv.wait(lock, [&sync]{ return sync.done; });
    }

    std::cout << "Async task completed. Result: "
              << (sync.result == embwasm::WasmResult::kOk ? "OK" : "ERROR")
              << std::endl;

    engine.StopInterpreterLoop();
    interp_thread.join();

    std::cout << "\nMax call stack depth: " << engine.GetMaxCallStackDepth() << std::endl;
    std::cout << "Max VM stack depth:   " << engine.GetMaxStackDepth() << std::endl;
    std::cout << "Memory Used: " << pool.GetUsedBytes() << " / " << pool.GetTotalBytes() << " bytes" << std::endl;

    engine.Deinit();
    pool.Deinit();
    return 0;
}
