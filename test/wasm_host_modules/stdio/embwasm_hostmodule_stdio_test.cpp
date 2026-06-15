#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_api_static.hpp"
#include "wasm_engine.hpp"

namespace {
alignas(16) uint8_t g_wasm_pool_buf[embwasm::kMemoryPoolSize];

// 確実にLoadできてメモリセクションを持つダミーWASMバイナリ
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
        ASSERT_EQ(engine_.Load(kWasmMemBinaryForStdioTest, sizeof(kWasmMemBinaryForStdioTest)), embwasm::WasmResult::kOk);
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

// 1. API 解決のテスト
TEST_F(WasmHostModuleStdioTest, ResolveStaticFunctions) {
    embwasm::HostFunctionId func_printf = embwasm::LookupStaticHostFunctionId("embwasm:stdio/stdio", 19, "printf", 6);
    EXPECT_EQ(func_printf, embwasm::kStdioPrintf);

    embwasm::HostFunctionId func_puts = embwasm::LookupStaticHostFunctionId("embwasm:stdio/stdio", 19, "puts", 4);
    EXPECT_EQ(func_puts, embwasm::kStdioPuts);
}

// 2. Puts のテスト（正常系と境界値エラー系）
TEST_F(WasmHostModuleStdioTest, PutsExecution) {
    // 正常系
    const char* msg = "Hello from Stdio Test!";
    uint32_t len = static_cast<uint32_t>(std::strlen(msg));
    uint32_t addr = 100;
    std::memcpy(mem_ + addr, msg, len);

    embwasm::WasmValue args[2] = {
        {embwasm::WasmType::kI32, {static_cast<int32_t>(addr)}},
        {embwasm::WasmType::kI32, {static_cast<int32_t>(len)}}
    };
    embwasm::WasmValue results[1] = {};

    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine_,
        embwasm::kStdioPuts,
        args, 2,
        results, 1
    );
    EXPECT_EQ(res, embwasm::WasmResult::kOk);
    EXPECT_EQ(results[0].type, embwasm::WasmType::kI32);
    EXPECT_EQ(results[0].value.i32, static_cast<int32_t>(len + 1)); // 成功時は出力文字数(改行含む)

    // 異常系: 線形メモリ範囲外のポインタ指定
    embwasm::WasmValue oob_args[2] = {
        {embwasm::WasmType::kI32, {static_cast<int32_t>(mem_size_ - 5)}},
        {embwasm::WasmType::kI32, {10}} // 5バイト分はみ出す
    };
    embwasm::WasmResult res_oob = embwasm::DispatchHostFunction(
        engine_,
        embwasm::kStdioPuts,
        oob_args, 2,
        results, 1
    );
    EXPECT_EQ(res_oob, embwasm::WasmResult::kErrorRuntimeError);
}

// 3. Printf のテスト
TEST_F(WasmHostModuleStdioTest, PrintfExecution) {
    // 書式文字列: "Int: %04d, Hex: 0x%02x, Char: %c, String: %s, Percent: %%"
    const char* fmt = "Int: %04d, Hex: 0x%02x, Char: %c, String: %s, Percent: %%";
    uint32_t fmt_len = static_cast<uint32_t>(std::strlen(fmt));
    uint32_t fmt_addr = 200;
    std::memcpy(mem_ + fmt_addr, fmt, fmt_len);

    // 引数の文字列
    const char* val_str = "Antigravity";
    uint32_t val_str_len = static_cast<uint32_t>(std::strlen(val_str));
    uint32_t val_str_addr = 300;
    std::memcpy(mem_ + val_str_addr, val_str, val_str_len + 1); // null-terminate

    // 引数リストデータ (Int: 99, Hex: 15, Char: 'A', String: val_str_addr)
    int32_t list_data[4] = { 99, 15, 'A', static_cast<int32_t>(val_str_addr) };
    uint32_t list_addr = 400;
    std::memcpy(mem_ + list_addr, list_data, sizeof(list_data));

    embwasm::WasmValue args[4] = {
        {embwasm::WasmType::kI32, {static_cast<int32_t>(fmt_addr)}},
        {embwasm::WasmType::kI32, {static_cast<int32_t>(fmt_len)}},
        {embwasm::WasmType::kI32, {static_cast<int32_t>(list_addr)}},
        {embwasm::WasmType::kI32, {4}} // 要素数 4
    };

    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine_,
        embwasm::kStdioPrintf,
        args, 4,
        nullptr, 0
    );
    EXPECT_EQ(res, embwasm::WasmResult::kOk);
}

// 4. Printf の浮動小数点数（非対応）と引数不足のフォールバックテスト
TEST_F(WasmHostModuleStdioTest, PrintfOmissionAndOOB) {
    // 浮動小数点数 %f はスキップされるべき
    const char* fmt = "Float: %f, Int: %d";
    uint32_t fmt_len = static_cast<uint32_t>(std::strlen(fmt));
    uint32_t fmt_addr = 500;
    std::memcpy(mem_ + fmt_addr, fmt, fmt_len);

    // 引数リスト: 1つだけ提供 (整数 123)
    int32_t list_data[1] = { 123 };
    uint32_t list_addr = 600;
    std::memcpy(mem_ + list_addr, list_data, sizeof(list_data));

    embwasm::WasmValue args[4] = {
        {embwasm::WasmType::kI32, {static_cast<int32_t>(fmt_addr)}},
        {embwasm::WasmType::kI32, {static_cast<int32_t>(fmt_len)}},
        {embwasm::WasmType::kI32, {static_cast<int32_t>(list_addr)}},
        {embwasm::WasmType::kI32, {1}} // 要素数 1
    };

    // %f は引数を消費しない（スキップされる）ため、%d が 123 を消費して正常終了するはず
    embwasm::WasmResult res = embwasm::DispatchHostFunction(
        engine_,
        embwasm::kStdioPrintf,
        args, 4,
        nullptr, 0
    );
    EXPECT_EQ(res, embwasm::WasmResult::kOk);

    // 異常系: 線形メモリ範囲外のフォーマット文字列を指定
    embwasm::WasmValue oob_args[4] = {
        {embwasm::WasmType::kI32, {static_cast<int32_t>(mem_size_ - 2)}},
        {embwasm::WasmType::kI32, {5}}, // はみ出し
        {embwasm::WasmType::kI32, {static_cast<int32_t>(list_addr)}},
        {embwasm::WasmType::kI32, {1}}
    };
    embwasm::WasmResult res_oob = embwasm::DispatchHostFunction(
        engine_,
        embwasm::kStdioPrintf,
        oob_args, 4,
        nullptr, 0
    );
    EXPECT_EQ(res_oob, embwasm::WasmResult::kErrorRuntimeError);
}
