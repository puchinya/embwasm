#include <cstddef>
#include <iostream>
#include "embwasm.hpp"
#include "main_wasm.hpp"

constexpr size_t kMemoryPoolSize = 1 << 20;

namespace {
alignas(16) uint8_t g_wasm_pool_buf[kMemoryPoolSize];
}

int main() {
    std::cout << "=== Embedded WASM Engine Demo ===" << std::endl;
    std::cout << "Memory Pool Size Configured: " << kMemoryPoolSize << " bytes" << std::endl;

    // 1. メモリプールの作成
    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));

    // 3. WASMエンジンの構築
    embwasm::WasmEngine engine;
    engine.Init(pool);

    // 5. WASMバイナリのロード
    std::cout << "\nLoading WASM Binary..." << std::endl;
    int32_t instance_id = engine.LoadModule("main", 4, kMainWasmBinary, kMainWasmBinarySize);
    if (instance_id < 0) {
        std::cerr << "Failed to load WASM binary. Error code: " << instance_id << std::endl;
        return 1;
    }
    std::cout << "WASM Loaded successfully." << std::endl;
    std::cout << "Memory Used: " << pool.GetUsedBytes() << " / " << pool.GetTotalBytes() << " bytes" << std::endl;

    // 6. エクスポートされた関数の実行
    std::cout << "\nExecuting Exported Function 'main'..." << std::endl;

    // 文字出力関数によりコンソールへ "Hello\n" と出力されます
    embwasm::WasmResult exec_res = engine.Execute("main", 4, "main", 4, nullptr, 0, nullptr, 0);
    if (exec_res != embwasm::WasmResult::kOk) {
        std::cerr << "Failed to execute. Error code: " << static_cast<int>(exec_res) << std::endl;
        return 1;
    }

    std::cout << "Execution finished successfully." << std::endl;
    std::cout << "Max function nesting depth reached: " << engine.GetMaxCallStackDepth() << std::endl;
    std::cout << "Max VM stack depth reached: " << engine.GetMaxStackDepth() << std::endl;
    engine.Deinit();
    pool.Deinit();
    return 0;
}
