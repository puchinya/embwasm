#include <cstddef>
#include <iostream>
#include "embwasm.hpp"
#include "main_wasm.hpp"
#include "embwasm_hostmodule_socket.hpp"

constexpr size_t kMWasmMemoryPoolSize = 1 << 20;

namespace {
alignas(16) uint8_t g_wasm_pool_buf[kMWasmMemoryPoolSize];
}

int main() {
    std::cout << "=== embwasm HTTP Server Demo ===" << std::endl;
    std::cout << "Listening on http://127.0.0.1:50050/" << std::endl;

    // メモリプール初期化
    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));

    // エンジン初期化
    embwasm::WasmEngine engine;
    engine.Init(pool);

    // WASM バイナリのロード
    int32_t instance_id = engine.LoadModule("main", 4, kMainWasmBinary, kMainWasmBinarySize);
    if (instance_id < 0) {
        std::cerr << "Failed to load WASM binary. Error: " << instance_id << std::endl;
        return 1;
    }

    // 実行
    embwasm::WasmResult res = engine.Execute("main", 4, "main", 4, nullptr, 0, nullptr, 0);
    if (res != embwasm::WasmResult::kOk) {
        std::cerr << "Execution failed. Error: " << static_cast<int>(res) << std::endl;
    }

    engine.Deinit();
    pool.Deinit();
    return 0;
}
