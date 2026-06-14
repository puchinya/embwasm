#include <iostream>
#include "embwasm.h"
#include "hello_wasm.h"

int main() {
    std::cout << "=== Embedded WASM Engine Demo ===" << std::endl;
    std::cout << "Memory Pool Size Configured: " << embwasm::kMemoryPoolSize << " bytes" << std::endl;

    // 1. メモリプールの作成
    embwasm::WasmMemoryPool pool;

    // 2. ホストAPIレジストリの作成 (静的登録に移行したため Register 呼び出しは不要)
    embwasm::WasmApiRegistry registry;

    // 3. WASMエンジンの構築
    embwasm::WasmEngine engine(pool, registry);

    // 5. WASMバイナリのロード
    std::cout << "\nLoading WASM Binary..." << std::endl;
    embwasm::WasmResult load_res = engine.Load(kHelloWasmBinary, kHelloWasmBinarySize);
    if (load_res != embwasm::WasmResult::kOk) {
        std::cerr << "Failed to load WASM binary. Error code: " << static_cast<int>(load_res) << std::endl;
        return 1;
    }
    std::cout << "WASM Loaded successfully." << std::endl;
    std::cout << "Memory Used: " << pool.GetUsedBytes() << " / " << pool.GetTotalBytes() << " bytes" << std::endl;

    // 6. エクスポートされた関数の実行
    std::cout << "\nExecuting Exported Function 'hello'..." << std::endl;
    
    // 文字出力関数によりコンソールへ "Hello\n" と出力されます
    embwasm::WasmResult exec_res = engine.Execute("hello", nullptr, 0, nullptr, 0);
    if (exec_res != embwasm::WasmResult::kOk) {
        std::cerr << "Failed to execute. Error code: " << static_cast<int>(exec_res) << std::endl;
        return 1;
    }

    std::cout << "Execution finished successfully." << std::endl;
    return 0;
}
