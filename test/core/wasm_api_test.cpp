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
    embwasm::HostFunctionId func = embwasm::LookupStaticHostFunctionId("env", 3, "dummy", 5);
    EXPECT_EQ(func, embwasm::kEnvDummy);

    embwasm::HostFunctionId func_print = embwasm::LookupStaticHostFunctionId("env", 3, "print_val", 9);
    EXPECT_EQ(func_print, embwasm::kEnvPrintVal);

    // 2. 存在しないAPIの検索検証 (異常系)
    embwasm::HostFunctionId not_found = embwasm::LookupStaticHostFunctionId("env", 3, "non_existent", 12);
    EXPECT_EQ(not_found, embwasm::HostFunctionId::kInvalid);

    // 3. ディスパッチャの直接呼び出し検証
    embwasm::WasmValue args[1] = {embwasm::WasmValue::FromI32(99)};
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

namespace {
// 確実にLoadできてメモリセクションを持つダミーWASMバイナリ
constexpr uint8_t kWasmMemBinaryForStdio[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x01, 0x08, 0x02, 0x60, 0x00, 0x01, 0x7f, 0x60,
    0x00, 0x00, 0x03, 0x03, 0x02, 0x00, 0x01, 0x05,
    0x03, 0x01, 0x00, 0x02, 0x06, 0x08, 0x01, 0x7f,
    0x01, 0x41, 0x90, 0x88, 0x04, 0x0b, 0x07, 0x19,
    0x03, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79,
    0x02, 0x00, 0x04, 0x6c, 0x6f, 0x61, 0x64, 0x00,
    0x00, 0x05, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x00,
    0x01, 0x0a, 0x1f, 0x02, 0x0b, 0x00, 0x41, 0x00,
    0x28, 0x02, 0x80, 0x88, 0x80, 0x80, 0x00, 0x0b,
    0x11, 0x00, 0x41, 0x00, 0x41, 0xf8, 0xac, 0xd1,
    0x91, 0x01, 0x36, 0x02, 0x84, 0x88, 0x80, 0x80,
    0x00, 0x0b, 0x0b, 0x0f, 0x01, 0x00, 0x41, 0x80,
    0x08, 0x0b, 0x08, 0x41, 0x42, 0x43, 0x44, 0x00,
    0x00, 0x00, 0x00, 0x00
};
}

TEST(WasmApiStaticTest, StdioPrintfAndPuts) {
    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
    embwasm::WasmEngine engine;
    engine.Init(pool);

    // WASMロードして線形メモリを有効化
    ASSERT_EQ(engine.Load(kWasmMemBinaryForStdio, sizeof(kWasmMemBinaryForStdio)), embwasm::WasmResult::kOk);

    uint8_t* mem = engine.GetLinearMemory();
    ASSERT_NE(mem, nullptr);

    // 1. Puts テスト
    const char* test_str = "Hello from Puts!";
    uint32_t str_len = static_cast<uint32_t>(std::strlen(test_str));
    uint32_t str_addr = 100;
    std::memcpy(mem + str_addr, test_str, str_len);

    embwasm::WasmValue puts_args[2] = {
        embwasm::WasmValue::FromI32(static_cast<int32_t>(str_addr)),
        embwasm::WasmValue::FromI32(static_cast<int32_t>(str_len))
    };
    embwasm::WasmValue puts_results[1] = {};

    embwasm::WasmResult res_puts = embwasm::DispatchHostFunction(
        engine,
        embwasm::kStdioPuts,
        puts_args, 2,
        puts_results, 1
    );
    EXPECT_EQ(res_puts, embwasm::WasmResult::kOk);
    EXPECT_GT(puts_results[0].value.i32, 0);

    // 2. Printf テスト
    // 書式: "Val: %03d, Str: %-5s, Hex: 0x%X"
    const char* fmt_str = "Val: %03d, Str: %-5s, Hex: 0x%X";
    uint32_t fmt_len = static_cast<uint32_t>(std::strlen(fmt_str));
    uint32_t fmt_addr = 200;
    std::memcpy(mem + fmt_addr, fmt_str, fmt_len);

    const char* arg_str = "Wasm";
    uint32_t arg_str_len = static_cast<uint32_t>(std::strlen(arg_str));
    uint32_t arg_str_addr = 300;
    std::memcpy(mem + arg_str_addr, arg_str, arg_str_len + 1); // null-terminate

    // 引数リスト [ 42, arg_str_addr, 255 ]
    int32_t printf_list_args[] = { 42, static_cast<int32_t>(arg_str_addr), 255 };
    uint32_t list_addr = 400;
    std::memcpy(mem + list_addr, printf_list_args, sizeof(printf_list_args));

    embwasm::WasmValue printf_args[4] = {
        embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_addr)),
        embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_len)),
        embwasm::WasmValue::FromI32(static_cast<int32_t>(list_addr)),
        embwasm::WasmValue::FromI32(static_cast<int32_t>(3)) // 要素数 3
    };

    embwasm::WasmResult res_printf = embwasm::DispatchHostFunction(
        engine,
        embwasm::kStdioPrintf,
        printf_args, 4,
        nullptr, 0
    );
    EXPECT_EQ(res_printf, embwasm::WasmResult::kOk);

    engine.Deinit();
    pool.Deinit();
}

