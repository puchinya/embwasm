#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_api_static.hpp"
#include "wasm_engine.hpp"
#include "wasm_thread.hpp"

namespace {
alignas(16) uint8_t g_wasm_pool_buf[embwasm::kMemoryPoolSize];

constexpr uint8_t kWasmMemBinaryForStdioTest[] = {
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

class WasmHostModuleStdioTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool_.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
        engine_.Init(pool_);
        ASSERT_GE(engine_.LoadModule(kWasmMemBinaryForStdioTest, sizeof(kWasmMemBinaryForStdioTest)), 0);
        ASSERT_EQ(engine_.InstantiateModules(), embwasm::WasmResult::kOk);
        mem_ = engine_.GetLinearMemory();
        mem_size_ = engine_.GetLinearMemorySize();
        ASSERT_NE(mem_, nullptr);
    }

    void TearDown() override {
        engine_.Deinit();
        pool_.Deinit();
    }

    embwasm::WasmMemoryPool pool_;
    embwasm::WasmEngine engine_;
    uint8_t* mem_;
    size_t mem_size_;
};

TEST_F(WasmHostModuleStdioTest, ResolveStaticFunctions) {
    embwasm::HostFunctionId func_printf = embwasm::LookupStaticHostFunctionId("embwasm:stdio/stdio", 19, "printf", 6);
    EXPECT_EQ(func_printf, embwasm::kWasmHostFuncIdStdioPrintf);

    embwasm::HostFunctionId func_puts = embwasm::LookupStaticHostFunctionId("embwasm:stdio/stdio", 19, "puts", 4);
    EXPECT_EQ(func_puts, embwasm::kWasmHostFuncIdStdioPuts);
}

TEST_F(WasmHostModuleStdioTest, PutsExecution) {
    const char* msg = "Hello from Stdio Test!";
    uint32_t len = static_cast<uint32_t>(std::strlen(msg));
    uint32_t addr = 100;
    std::memcpy(mem_ + addr, msg, len);

    embwasm::WasmThreadContext ctx;
    ctx.Reset();
    // WASM pushes ptr then len; dispatch pops len first, then ptr
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(len));

    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine_, embwasm::kWasmHostFuncIdStdioPuts, &ctx);
    EXPECT_EQ(res, embwasm::WasmResult::kOk);
    ASSERT_EQ(ctx.stack_top, 1u);
    EXPECT_EQ(ctx.stack[0].value.i32, static_cast<int32_t>(len + 1));

    // 異常系: 線形メモリ範囲外のポインタ指定
    ctx.Reset();
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(mem_size_ - 5));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(10);

    embwasm::WasmResult res_oob = embwasm::DispatchHostFunction(
        engine_, embwasm::kWasmHostFuncIdStdioPuts, &ctx);
    EXPECT_EQ(res_oob, embwasm::WasmResult::kErrorExecuteRuntimeError);
}

TEST_F(WasmHostModuleStdioTest, PrintfExecution) {
    const char* fmt = "Int: %04d, Hex: 0x%02x, Char: %c, String: %s, Percent: %%";
    uint32_t fmt_len = static_cast<uint32_t>(std::strlen(fmt));
    uint32_t fmt_addr = 200;
    std::memcpy(mem_ + fmt_addr, fmt, fmt_len);

    const char* val_str = "Antigravity";
    uint32_t val_str_addr = 300;
    std::memcpy(mem_ + val_str_addr, val_str, std::strlen(val_str) + 1);

    int32_t list_data[4] = { 99, 15, 'A', static_cast<int32_t>(val_str_addr) };
    uint32_t list_addr = 400;
    std::memcpy(mem_ + list_addr, list_data, sizeof(list_data));

    embwasm::WasmThreadContext ctx;
    ctx.Reset();
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_len));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(list_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(4);

    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine_, embwasm::kWasmHostFuncIdStdioPrintf, &ctx);
    EXPECT_EQ(res, embwasm::WasmResult::kOk);
}

TEST_F(WasmHostModuleStdioTest, PrintfOmissionAndOOB) {
    const char* fmt = "Float: %f, Int: %d";
    uint32_t fmt_len = static_cast<uint32_t>(std::strlen(fmt));
    uint32_t fmt_addr = 500;
    std::memcpy(mem_ + fmt_addr, fmt, fmt_len);

    int32_t list_data[1] = { 123 };
    uint32_t list_addr = 600;
    std::memcpy(mem_ + list_addr, list_data, sizeof(list_data));

    embwasm::WasmThreadContext ctx;
    ctx.Reset();
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(fmt_len));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(list_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(1);

    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine_, embwasm::kWasmHostFuncIdStdioPrintf, &ctx);
    EXPECT_EQ(res, embwasm::WasmResult::kOk);

    // 異常系: 線形メモリ範囲外のフォーマット文字列
    ctx.Reset();
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(mem_size_ - 2));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(5);
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(static_cast<int32_t>(list_addr));
    ctx.stack[ctx.stack_top++] = embwasm::WasmValue::FromI32(1);

    embwasm::WasmResult res_oob = embwasm::DispatchHostFunction(
        engine_, embwasm::kWasmHostFuncIdStdioPrintf, &ctx);
    EXPECT_EQ(res_oob, embwasm::WasmResult::kErrorExecuteRuntimeError);
}
