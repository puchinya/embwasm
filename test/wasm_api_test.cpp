#include <gtest/gtest.h>
#include "wasm_config.h"
#include "wasm_types.h"
#include "wasm_api.h"
#include "wasm_api_static.h"
#include "host_apis.h"

TEST(WasmApiRegistryTest, AllFunctions) {
    embwasm::WasmApiRegistry registry;
    
    // 1. 静的登録テーブルからの検索検証 (正常系)
    embwasm::HostFunction func = registry.Lookup("env", "dummy");
    EXPECT_EQ(func, embwasm::DummyHostFunc);

    embwasm::HostFunction func_print = registry.Lookup("env", "print_val");
    EXPECT_EQ(func_print, embwasm::PrintVal);

    // 2. 存在しないAPIの検索検証 (異常系)
    embwasm::HostFunction not_found = registry.Lookup("env", "non_existent");
    EXPECT_EQ(not_found, nullptr);

    // 3. Register が常に kOk を返すダミー動作であることの検証
    embwasm::WasmResult res = registry.Register("env", "new_api", embwasm::DummyHostFunc);
    EXPECT_EQ(res, embwasm::WasmResult::kOk);
}
