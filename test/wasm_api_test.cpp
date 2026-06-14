#include <gtest/gtest.h>
#include "wasm_types.h"
#include "wasm_api.h"
#include "host_apis.h"

TEST(WasmApiStaticTest, AllFunctions) {
    // 1. 静的登録テーブルからの検索検証 (正常系)
    embwasm::HostFunction func = embwasm::LookupStaticHostFunction("env", "dummy");
    EXPECT_EQ(func, embwasm::DummyHostFunc);

    embwasm::HostFunction func_print = embwasm::LookupStaticHostFunction("env", "print_val");
    EXPECT_EQ(func_print, embwasm::PrintVal);

    // 2. 存在しないAPIの検索検証 (異常系)
    embwasm::HostFunction not_found = embwasm::LookupStaticHostFunction("env", "non_existent");
    EXPECT_EQ(not_found, nullptr);
}
