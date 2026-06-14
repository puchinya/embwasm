#include <gtest/gtest.h>
#include "wasm_types.h"
#include "wasm_memory_pool.h"
#include "wasm_api.h"
#include "wasm_engine.h"

// モック側で定義されているテスト用のグローバル状態
namespace embwasm {
extern int32_t g_last_printed_value;
extern bool g_print_val_called;
}

// =============================================================================
// 基本実行およびエラー処理のテスト
// =============================================================================

TEST(WasmEngineTest, NormalExecution) {
    embwasm::WasmMemoryPool pool;
    
    embwasm::g_print_val_called = false;
    embwasm::g_last_printed_value = 0;

    // 静的登録モデルのため Register 呼び出しは不要

    embwasm::WasmEngine engine(pool);

    // 正常なWASMバイナリ (10 + 20 = 30 を計算し、print_val を呼び出す)
    constexpr uint8_t kWasmBinary[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x08, 0x02, 
          0x60, 0x01, 0x7f, 0x00,
          0x60, 0x00, 0x00,
        0x02, 0x11, 0x01, 
          0x03, 'e', 'n', 'v', 
          0x09, 'p', 'r', 'i', 'n', 't', '_', 'v', 'a', 'l', 
          0x00, 0x00,
        0x03, 0x02, 0x01, 0x01,
        0x07, 0x11, 0x01, 
          0x0d, 'a', 'd', 'd', '_', 'a', 'n', 'd', '_', 'p', 'r', 'i', 'n', 't', 
          0x00, 0x01,
        0x0a, 0x0b, 0x01, 
          0x09, 0x00, 0x41, 0x0a, 0x41, 0x14, 0x6a, 0x10, 0x00, 0x0b
    };

    EXPECT_EQ(engine.Load(kWasmBinary, sizeof(kWasmBinary)), embwasm::WasmResult::kOk);
    EXPECT_EQ(engine.Execute("add_and_print", nullptr, 0, nullptr, 0), embwasm::WasmResult::kOk);
    EXPECT_TRUE(embwasm::g_print_val_called);
    EXPECT_EQ(embwasm::g_last_printed_value, 30);
}

TEST(WasmEngineTest, LoadErrors) {
    embwasm::WasmMemoryPool pool;
    embwasm::WasmEngine engine(pool);

    // 1. サイズ不足
    constexpr uint8_t kTooShort[] = { 0x00, 0x61 };
    EXPECT_EQ(engine.Load(kTooShort, sizeof(kTooShort)), embwasm::WasmResult::kErrorInvalidMagic);

    // 2. マジックナンバーエラー
    constexpr uint8_t kBadMagic[] = { 0x11, 0x22, 0x33, 0x44, 0x01, 0x00, 0x00, 0x00 };
    EXPECT_EQ(engine.Load(kBadMagic, sizeof(kBadMagic)), embwasm::WasmResult::kErrorInvalidMagic);

    // 3. バージョンエラー
    constexpr uint8_t kBadVersion[] = { 0x00, 0x61, 0x73, 0x6d, 0x02, 0x00, 0x00, 0x00 };
    EXPECT_EQ(engine.Load(kBadVersion, sizeof(kBadVersion)), embwasm::WasmResult::kErrorInvalidVersion);
}

TEST(WasmEngineTest, ExecutionErrors) {
    embwasm::WasmMemoryPool pool;
    embwasm::WasmEngine engine(pool);

    // テスト用の単純なWASM
    constexpr uint8_t kWasmBinary[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 
        0x03, 0x02, 0x01, 0x00,             
        0x07, 0x0b, 0x01, 0x07, 'm', 'y', '_', 'f', 'u', 'n', 'c', 0x00, 0x00, 
        0x0a, 0x04, 0x01, 0x02, 0x00, 0x0b 
    };

    ASSERT_EQ(engine.Load(kWasmBinary, sizeof(kWasmBinary)), embwasm::WasmResult::kOk);

    // 1. 存在しない関数の実行
    EXPECT_EQ(engine.Execute("non_existent", nullptr, 0, nullptr, 0), embwasm::WasmResult::kErrorFunctionNotFound);

    // 2. 実行時スタック不足エラー検証
    embwasm::WasmValue result;
    EXPECT_EQ(engine.Execute("my_func", nullptr, 0, &result, 1), embwasm::WasmResult::kErrorRuntimeError);
}

// =============================================================================
// 全オペコード境界値網羅テスト
// =============================================================================

// i32 二項演算子の境界値テスト
TEST(WasmEngineTest, OpcodeI32ArithmeticBoundary) {
    // テンプレートWASM (二項演算 i32)
    uint8_t wasm_bin[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f,
        0x03, 0x02, 0x01, 0x00,
        0x07, 0x0d, 0x01, 0x09, 't', 'e', 's', 't', '_', 'c', 'a', 'l', 'c', 0x00, 0x00,
        0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 
        0x6a, // [45] オペコード位置 (デフォルト: i32.add)
        0x0b
    };

    auto run_test = [&](uint8_t op, int32_t a, int32_t b, embwasm::WasmResult expected_res, int32_t expected_val = 0) {
        embwasm::WasmMemoryPool pool;
        embwasm::WasmEngine engine(pool);

        wasm_bin[45] = op;
        ASSERT_EQ(engine.Load(wasm_bin, sizeof(wasm_bin)), embwasm::WasmResult::kOk);

        embwasm::WasmValue args[2] = {
            {embwasm::WasmType::kI32, {a}},
            {embwasm::WasmType::kI32, {b}}
        };
        embwasm::WasmValue result;
        embwasm::WasmResult res = engine.Execute("test_calc", args, 2, &result, 1);
        
        EXPECT_EQ(res, expected_res);
        if (expected_res == embwasm::WasmResult::kOk) {
            EXPECT_EQ(result.type, embwasm::WasmType::kI32);
            EXPECT_EQ(result.value.i32, expected_val);
        }
    };

    // 1. i32.add (0x6A)
    run_test(0x6A, 10, 20, embwasm::WasmResult::kOk, 30);
    run_test(0x6A, 0x7FFFFFFF, 1, embwasm::WasmResult::kOk, static_cast<int32_t>(0x80000000)); // オーバーフロー

    // 2. i32.sub (0x6B)
    run_test(0x6B, 10, 3, embwasm::WasmResult::kOk, 7);
    run_test(0x6B, static_cast<int32_t>(0x80000000), 1, embwasm::WasmResult::kOk, 0x7FFFFFFF); // アンダーフロー

    // 3. i32.mul (0x6C)
    run_test(0x6C, 6, 7, embwasm::WasmResult::kOk, 42);
    run_test(0x6C, 0x7FFFFFFF, 2, embwasm::WasmResult::kOk, -2); // オーバーフロー

    // 4. i32.div_s (0x6D)
    run_test(0x6D, 10, 2, embwasm::WasmResult::kOk, 5);
    run_test(0x6D, -10, 3, embwasm::WasmResult::kOk, -3);
    run_test(0x6D, 10, 0, embwasm::WasmResult::kErrorRuntimeError); // ゼロ除算保護の検証

    // 5. i32.div_u (0x6E)
    run_test(0x6E, 10, 2, embwasm::WasmResult::kOk, 5);
    run_test(0x6E, -10, 2, embwasm::WasmResult::kOk, 0x7FFFFFFB); // 符号なし除算 (-10 = 0xFFFFFFF6 / 2 = 0x7FFFFFFB)
    run_test(0x6E, 10, 0, embwasm::WasmResult::kErrorRuntimeError); // ゼロ除算保護の検証

    // 6. i32.and (0x71), i32.or (0x72), i32.xor (0x73)
    run_test(0x71, 0xF0F0F0F0, 0xFFFF0000, embwasm::WasmResult::kOk, static_cast<int32_t>(0xF0F00000));
    run_test(0x72, 0xF0F0F0F0, 0xFFFF0000, embwasm::WasmResult::kOk, static_cast<int32_t>(0xFFFFF0F0));
    run_test(0x73, 0xF0F0F0F0, 0xFFFF0000, embwasm::WasmResult::kOk, static_cast<int32_t>(0x0F0FF0F0));

    // 7. シフト演算: i32.shl (0x74), i32.shr_s (0x75), i32.shr_u (0x76)
    run_test(0x74, 1, 4, embwasm::WasmResult::kOk, 16);
    run_test(0x74, 1, 36, embwasm::WasmResult::kOk, 16); // 32以上は & 31 マスクされることの検証 (36 & 31 = 4)
    run_test(0x75, -16, 2, embwasm::WasmResult::kOk, -4); // 符号付き右シフト (符号維持)
    run_test(0x76, -16, 2, embwasm::WasmResult::kOk, 0x3FFFFFFC); // 符号なし右シフト (0埋め)
}

// i32 比較演算子の境界値テスト
TEST(WasmEngineTest, OpcodeI32ComparisonBoundary) {
    uint8_t wasm_bin[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f,
        0x03, 0x02, 0x01, 0x00,
        0x07, 0x0d, 0x01, 0x09, 't', 'e', 's', 't', '_', 'c', 'a', 'l', 'c', 0x00, 0x00,
        0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 
        0x46, // [45] オペコード位置 (デフォルト: i32.eq)
        0x0b
    };

    auto run_test = [&](uint8_t op, int32_t a, int32_t b, int32_t expected_val) {
        embwasm::WasmMemoryPool pool;
        embwasm::WasmEngine engine(pool);

        wasm_bin[45] = op;
        ASSERT_EQ(engine.Load(wasm_bin, sizeof(wasm_bin)), embwasm::WasmResult::kOk);

        embwasm::WasmValue args[2] = {
            {embwasm::WasmType::kI32, {a}},
            {embwasm::WasmType::kI32, {b}}
        };
        embwasm::WasmValue result;
        ASSERT_EQ(engine.Execute("test_calc", args, 2, &result, 1), embwasm::WasmResult::kOk);
        EXPECT_EQ(result.value.i32, expected_val);
    };

    // 0x46: eq, 0x47: ne
    run_test(0x46, 5, 5, 1);
    run_test(0x46, 5, 6, 0);
    run_test(0x47, 5, 6, 1);

    // 0x48: lt_s, 0x49: lt_u
    run_test(0x48, -5, 3, 1); // 符号あり: -5 < 3 => 1
    run_test(0x49, -5, 3, 0); // 符号なし: 0xFFFFFFFB < 3 => 0

    // 0x4A: gt_s, 0x4B: gt_u
    run_test(0x4A, -5, 3, 0);
    run_test(0x4B, -5, 3, 1);

    // 0x4C: le_s, 0x4D: le_u
    run_test(0x4C, 5, 5, 1);
    run_test(0x4D, -5, -5, 1);

    // 0x4E: ge_s, 0x4F: ge_u
    run_test(0x4E, 5, 4, 1);
    run_test(0x4F, 4, -5, 0);
}

// i64 算術演算子の境界値テスト
TEST(WasmEngineTest, OpcodeI64ArithmeticBoundary) {
    uint8_t wasm_bin[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x07, 0x01, 0x60, 0x02, 0x7e, 0x7e, 0x01, 0x7e, // (i64, i64) -> i64
        0x03, 0x02, 0x01, 0x00,
        0x07, 0x0d, 0x01, 0x09, 't', 'e', 's', 't', '_', 'c', 'a', 'l', 'c', 0x00, 0x00,
        0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 
        0x7c, // [45] オペコード位置 (デフォルト: i64.add)
        0x0b
    };

    auto run_test = [&](uint8_t op, int64_t a, int64_t b, int64_t expected_val) {
        embwasm::WasmMemoryPool pool;
        embwasm::WasmEngine engine(pool);

        wasm_bin[45] = op;
        ASSERT_EQ(engine.Load(wasm_bin, sizeof(wasm_bin)), embwasm::WasmResult::kOk);

        embwasm::WasmValue args[2] = {
            {embwasm::WasmType::kI64, {a}},
            {embwasm::WasmType::kI64, {b}}
        };
        embwasm::WasmValue result;
        ASSERT_EQ(engine.Execute("test_calc", args, 2, &result, 1), embwasm::WasmResult::kOk);
        EXPECT_EQ(result.type, embwasm::WasmType::kI64);
        EXPECT_EQ(result.value.i64, expected_val);
    };

    // 0x7C: i64.add, 0x7D: i64.sub, 0x7E: i64.mul
    run_test(0x7C, 1000000000000LL, 2000000000000LL, 3000000000000LL);
    run_test(0x7D, 0LL, 1LL, -1LL);
    run_test(0x7E, 3000000000LL, 3LL, 9000000000LL);

    // 0x83: i64.and, 0x84: i64.or, 0x85: i64.xor
    run_test(0x83, 0x0F0F0F0F0F0F0F0FLL, 0xFFFFFFFFFFFFFFFFLL, 0x0F0F0F0F0F0F0F0FLL);
    run_test(0x84, 0x0F0F0F0F0F0F0F0FLL, 0xF0F0F0F0F0F0F0F0LL, 0xFFFFFFFFFFFFFFFFLL);
    run_test(0x85, 0x0F0F0F0F0F0F0F0FLL, 0xFFFFFFFFFFFFFFFFLL, 0xF0F0F0F0F0F0F0F0LL);
}

// 単項演算子のテスト
TEST(WasmEngineTest, OpcodeUnaryBoundary) {
    uint8_t wasm_bin[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, // (i32) -> i32
        0x03, 0x02, 0x01, 0x00,
        0x07, 0x0d, 0x01, 0x09, 't', 'e', 's', 't', '_', 'c', 'a', 'l', 'c', 0x00, 0x00,
        0x0a, 0x07, 0x01, 0x05, 0x00, 0x20, 0x00, 
        0x45, // [42] オペコード位置 (デフォルト: i32.eqz)
        0x0b
    };

    auto run_test = [&](uint8_t op, int32_t a, int32_t expected_val) {
        embwasm::WasmMemoryPool pool;
        embwasm::WasmEngine engine(pool);

        wasm_bin[42] = op;
        ASSERT_EQ(engine.Load(wasm_bin, sizeof(wasm_bin)), embwasm::WasmResult::kOk);

        embwasm::WasmValue args[1] = {
            {embwasm::WasmType::kI32, {a}}
        };
        embwasm::WasmValue result;
        ASSERT_EQ(engine.Execute("test_calc", args, 1, &result, 1), embwasm::WasmResult::kOk);
        EXPECT_EQ(result.value.i32, expected_val);
    };

    // 0x45: i32.eqz
    run_test(0x45, 0, 1);
    run_test(0x45, 99, 0);

    // 0x67: i32.clz
    run_test(0x67, 0, 32);
    run_test(0x67, 0x80000000, 0);
    run_test(0x67, 0x0000FFFF, 16);
}

// 制御・変数・特殊命令のテスト
TEST(WasmEngineTest, VariableAndControlFlow) {
    embwasm::WasmMemoryPool pool;
    embwasm::WasmEngine engine(pool);

    // local.tee, drop の挙動テスト用WASM
    // local.get 0, local.tee 0, drop, local.get 0
    constexpr uint8_t kWasmTeeBinary[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f,
        0x03, 0x02, 0x01, 0x00,
        0x07, 0x0d, 0x01, 0x09, 't', 'e', 's', 't', '_', 'c', 'a', 'l', 'c', 0x00, 0x00,
        0x0a, 0x0b, 0x01, 0x09, 0x00, 0x20, 0x00, 0x22, 0x00, 0x1a, 0x20, 0x00, 0x0b
    };

    ASSERT_EQ(engine.Load(kWasmTeeBinary, sizeof(kWasmTeeBinary)), embwasm::WasmResult::kOk);
    embwasm::WasmValue args[1] = {{embwasm::WasmType::kI32, {42}}};
    embwasm::WasmValue result;
    ASSERT_EQ(engine.Execute("test_calc", args, 1, &result, 1), embwasm::WasmResult::kOk);
    EXPECT_EQ(result.value.i32, 42);
}

TEST(WasmEngineTest, NopAndReturn) {
    embwasm::WasmMemoryPool pool;
    embwasm::WasmEngine engine(pool);

    // nop, return の挙動テスト用WASM
    // nop, local.get 0, return, i32.const 99
    constexpr uint8_t kWasmRetBinary[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f,
        0x03, 0x02, 0x01, 0x00,
        0x07, 0x0d, 0x01, 0x09, 't', 'e', 's', 't', '_', 'c', 'a', 'l', 'c', 0x00, 0x00,
        0x0a, 0x0a, 0x01, 0x08, 0x00, 0x00, 0x20, 0x00, 0x0f, 0x41, 0x63, 0x0b
    };

    ASSERT_EQ(engine.Load(kWasmRetBinary, sizeof(kWasmRetBinary)), embwasm::WasmResult::kOk);
    embwasm::WasmValue args[1] = {{embwasm::WasmType::kI32, {77}}};
    embwasm::WasmValue result;
    ASSERT_EQ(engine.Execute("test_calc", args, 1, &result, 1), embwasm::WasmResult::kOk);
    // return があるため、i32.const 99 は実行されず 77 が返るべき
    EXPECT_EQ(result.value.i32, 77);
}

TEST(WasmEngineTest, InternalFunctionCall) {
    embwasm::WasmMemoryPool pool;
    embwasm::WasmEngine engine(pool);

    // quadruple(x) = double(double(x))
    // double(x) = x + x
    constexpr uint8_t kWasmCallBinary[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, // (i32) -> i32
        0x03, 0x03, 0x02, 0x00, 0x00,                   // 2 funcs of type index 0
        0x07, 0x0d, 0x01, 0x09, 'q', 'u', 'a', 'd', 'r', 'u', 'p', 'l', 'e', 0x00, 0x01, // export "quadruple" at index 1 (size: 0x0d)
        0x0a, 0x12, 0x02,                               // Code section (size: 0x12)
          0x07, 0x00, 0x20, 0x00, 0x20, 0x00, 0x6a, 0x0b, // body 0 ($double): size 0x07, local decls 0, local.get 0, local.get 0, i32.add, end
          0x08, 0x00, 0x20, 0x00, 0x10, 0x00, 0x10, 0x00, 0x0b  // body 1 ($quadruple): size 0x08, local decls 0, local.get 0, call 0, call 0, end
    };

    ASSERT_EQ(engine.Load(kWasmCallBinary, sizeof(kWasmCallBinary)), embwasm::WasmResult::kOk);
    embwasm::WasmValue args[1] = {{embwasm::WasmType::kI32, {5}}};
    embwasm::WasmValue result;
    ASSERT_EQ(engine.Execute("quadruple", args, 1, &result, 1), embwasm::WasmResult::kOk);
    // quadruple(5) -> double(5) = 10 -> double(10) = 20
    EXPECT_EQ(result.value.i32, 20);
}

TEST(WasmEngineTest, CallStackOverflow) {
    embwasm::WasmMemoryPool pool;
    embwasm::WasmEngine engine(pool);

    // infinite_call(x) = infinite_call(x)
    constexpr uint8_t kWasmOverflowBinary[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x06, 0x01, 0x60, 0x01, 0x7f, 0x01, 0x7f, // (i32) -> i32
        0x03, 0x02, 0x01, 0x00,                         // 1 func of type index 0
        0x07, 0x11, 0x01, 0x0d, 'i', 'n', 'f', 'i', 'n', 'i', 't', 'e', '_', 'c', 'a', 'l', 'l', 0x00, 0x00, // export "infinite_call" (size: 0x11)
        0x0a, 0x08, 0x01, 0x06, 0x00, 0x20, 0x00, 0x10, 0x00, 0x0b // body: size 0x06, local.get 0, call 0, end
    };

    ASSERT_EQ(engine.Load(kWasmOverflowBinary, sizeof(kWasmOverflowBinary)), embwasm::WasmResult::kOk);
    embwasm::WasmValue args[1] = {{embwasm::WasmType::kI32, {1}}};
    embwasm::WasmValue result;
    // C++の再帰呼び出しを排除しているため、プロセスがクラッシュせずに安全にkErrorStackOverflowを返す
    EXPECT_EQ(engine.Execute("infinite_call", args, 1, &result, 1), embwasm::WasmResult::kErrorStackOverflow);
}
