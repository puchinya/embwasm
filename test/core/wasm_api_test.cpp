#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_api_static.hpp"
#include "wasm_engine.hpp"

namespace embwasm {
extern int32_t g_last_printed_value;
extern bool g_print_val_called;
}

constexpr size_t kMemoryPoolSize = 1 << 20;

namespace {
alignas(16) uint8_t g_wasm_pool_buf[kMemoryPoolSize];
}

TEST(WasmApiStaticTest, AllFunctions) {
    // 1. 静的登録テーブルからの検索検証 (正常系)
    embwasm::HostFunctionId func = embwasm::LookupStaticHostFunctionId("env", 3, "dummy", 5);
    EXPECT_EQ(func, embwasm::kWasmHostFuncIdEnvDummy);

    embwasm::HostFunctionId func_print = embwasm::LookupStaticHostFunctionId("env", 3, "print_val", 9);
    EXPECT_EQ(func_print, embwasm::kWasmHostFuncIdEnvPrintVal);

    // 2. 存在しないAPIの検索検証 (異常系)
    embwasm::HostFunctionId not_found = embwasm::LookupStaticHostFunctionId("env", 3, "non_existent", 12);
    EXPECT_EQ(not_found, embwasm::HostFunctionId::kInvalid);

    // 3. ディスパッチャの直接呼び出し検証（スタック経由）
    embwasm::g_print_val_called = false;
    embwasm::g_last_printed_value = 0;

    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
    embwasm::WasmEngine engine;
    engine.Init(pool);

    embwasm::WasmValue    local_stack[32]      = {};
    embwasm::WasmFrame    local_call_stack[16] = {};
    embwasm::WasmLabel    local_labels[16]     = {};
    embwasm::WasmThreadContext ctx;
    ctx.stack            = local_stack;       ctx.stack_size       = 32;
    ctx.call_stack       = local_call_stack;  ctx.call_stack_size  = 16;
    ctx.labels_pool      = local_labels;      ctx.labels_pool_size = 16;
    ctx.Reset();
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(99);

    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine,
        embwasm::kWasmHostFuncIdEnvPrintVal,
        &ctx
    );
    EXPECT_EQ(res, embwasm::WasmResult::kOk);
    EXPECT_TRUE(embwasm::g_print_val_called);
    EXPECT_EQ(embwasm::g_last_printed_value, 99);
    engine.Deinit();
    pool.Deinit();
}

namespace {
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

    ASSERT_GE(engine.LoadModule(kWasmMemBinaryForStdio, sizeof(kWasmMemBinaryForStdio)), 0);
    ASSERT_EQ(engine.InstantiateModules(), embwasm::WasmResult::kOk);

    uint8_t* mem = engine.GetLinearMemory();
    ASSERT_NE(mem, nullptr);

    // 1. Puts テスト
    const char* test_str = "Hello from Puts!";
    uint32_t str_len = static_cast<uint32_t>(std::strlen(test_str));
    uint32_t str_addr = 100;
    std::memcpy(mem + str_addr, test_str, str_len);

    embwasm::WasmValue    local_stack2[32]      = {};
    embwasm::WasmFrame    local_call_stack2[16] = {};
    embwasm::WasmLabel    local_labels2[16]     = {};
    embwasm::WasmThreadContext ctx;
    ctx.stack            = local_stack2;       ctx.stack_size       = 32;
    ctx.call_stack       = local_call_stack2;  ctx.call_stack_size  = 16;
    ctx.labels_pool      = local_labels2;      ctx.labels_pool_size = 16;
    ctx.Reset();
    // WASM pushes ptr then len; dispatch pops len first, then ptr
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(str_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(str_len));

    embwasm::WasmResult res_puts = embwasm::DispatchHostFunction(
        engine, embwasm::kWasmHostFuncIdStdioPuts, &ctx);
    EXPECT_EQ(res_puts, embwasm::WasmResult::kOk);
    ASSERT_EQ(ctx.stack_top, 1u);
    EXPECT_GT(ctx.stack[0].value.i32, 0);

    // 2. Printf テスト
    const char* fmt_str = "Val: %03d, Str: %-5s, Hex: 0x%X";
    uint32_t fmt_len = static_cast<uint32_t>(std::strlen(fmt_str));
    uint32_t fmt_addr = 200;
    std::memcpy(mem + fmt_addr, fmt_str, fmt_len);

    const char* arg_str = "Wasm";
    uint32_t arg_str_addr = 300;
    std::memcpy(mem + arg_str_addr, arg_str, std::strlen(arg_str) + 1);

    int32_t printf_list_args[] = { 42, static_cast<int32_t>(arg_str_addr), 255 };
    uint32_t list_addr = 400;
    std::memcpy(mem + list_addr, printf_list_args, sizeof(printf_list_args));

    ctx.Reset();
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_len));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(list_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(3);

    embwasm::WasmResult res_printf = embwasm::DispatchHostFunction(
        engine, embwasm::kWasmHostFuncIdStdioPrintf, &ctx);
    EXPECT_EQ(res_printf, embwasm::WasmResult::kOk);

    engine.Deinit();
    pool.Deinit();
}
