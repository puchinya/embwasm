#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_api_static.hpp"
#include "wasm_engine.hpp"

namespace embwasm {
extern int32_t g_last_printed_value;
extern bool g_print_val_called;
}

namespace {
alignas(16) uint8_t g_wasm_pool_buf[embwasm::kMemoryPoolSize];
}

TEST(WasmApiStaticTest, AllFunctions) {
    // 1. 静的登録テーブルからの検索検証 (正常系)
    embwasm::HostFunctionId func = embwasm::LookupStaticHostFunctionId("env", "dummy");
    EXPECT_EQ(func, embwasm::kEnvDummy);

    embwasm::HostFunctionId func_print = embwasm::LookupStaticHostFunctionId("env", "print_val");
    EXPECT_EQ(func_print, embwasm::kEnvPrintVal);

    // 2. 存在しないAPIの検索検証 (異常系)
    embwasm::HostFunctionId not_found = embwasm::LookupStaticHostFunctionId("env", "non_existent");
    EXPECT_EQ(not_found, embwasm::HostFunctionId::kInvalid);

    // 3. ディスパッチャの直接呼び出し検証
    embwasm::WasmValue args[1] = {{embwasm::WasmType::kI32, {99}}};
    embwasm::g_print_val_called = false;
    embwasm::g_last_printed_value = 0;
    
    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
    embwasm::WasmEngine engine;
    engine.Init(pool);
    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine,
        embwasm::kEnvPrintVal,
        args, 1,
        nullptr, 0
    );
    EXPECT_EQ(res, embwasm::WasmResult::kOk);
    EXPECT_TRUE(embwasm::g_print_val_called);
    EXPECT_EQ(embwasm::g_last_printed_value, 99);
    engine.Deinit();
    pool.Deinit();
}
