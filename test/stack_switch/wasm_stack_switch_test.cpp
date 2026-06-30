#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_engine.hpp"
#include "wasm_memory_pool.hpp"

#if SS_HAS_WASM
#include "stack_switch_wasm.hpp"
#endif

namespace embwasm {

constexpr size_t kMemPoolSize = 1 << 21; // 2MB
namespace {
alignas(16) uint8_t g_pool_buf[kMemPoolSize];
}

// __stack_pointer あり・cabi_realloc なし → Validate エラー用最小バイナリ
static constexpr uint8_t kNoReallocWasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x06, 0x08, 0x01, 0x7f, 0x01, 0x41, 0x80, 0x80,
    0x04, 0x0b, 0x07, 0x13, 0x01, 0x0f, 0x5f, 0x5f,
    0x73, 0x74, 0x61, 0x63, 0x6b, 0x5f, 0x70, 0x6f,
    0x69, 0x6e, 0x74, 0x65, 0x72, 0x03, 0x00
};

class StackSwitchTest : public ::testing::Test {
protected:
    WasmMemoryPool pool;
    WasmEngine engine;

    void SetUp() override {
        pool.Init(g_pool_buf, sizeof(g_pool_buf));
        engine.Init(pool);
    }
    void TearDown() override {
        engine.Deinit();
        pool.Deinit();
    }
};

// cabi_realloc がない WASM をロードすると Validate エラーになること
TEST_F(StackSwitchTest, ValidateErrorWhenNoCabiRealloc) {
    int32_t id = engine.LoadModule("bad", 3, kNoReallocWasm, sizeof(kNoReallocWasm));
    ASSERT_GE(id, 0);
    WasmResult res = engine.InstantiateModules();
    EXPECT_EQ(res, WasmResult::kErrorValidationFailed);
}

#if SS_HAS_WASM
// __stack_pointer / __data_end / cabi_realloc が正しく検出されること
TEST_F(StackSwitchTest, ValidateDetectsIndices) {
    int32_t id = engine.LoadModule("main", 4, kStack_switchWasmBinary, kStack_switchWasmBinarySize);
    ASSERT_GE(id, 0);
    WasmResult res = engine.InstantiateModules();
    ASSERT_EQ(res, WasmResult::kOk);

    WasmModuleInstance* mod = engine.GetModuleInstanceById(id);
    ASSERT_NE(mod, nullptr);
    EXPECT_NE(mod->stack_ptr_global_idx,  static_cast<uint32_t>(UINT32_MAX));
    EXPECT_NE(mod->data_end_global_idx,   static_cast<uint32_t>(UINT32_MAX));
    EXPECT_NE(mod->cabi_realloc_func_idx, static_cast<uint32_t>(UINT32_MAX));
    EXPECT_GT(mod->thread_stack_size, 0u);

    uint32_t sp_init = static_cast<uint32_t>(
        mod->globals[mod->stack_ptr_global_idx].value.value.i32);
    uint32_t de = static_cast<uint32_t>(
        mod->globals[mod->data_end_global_idx].value.value.i32);
    // data-first: sp_init > de  → size = sp_init - de
    // stack-first: sp_init <= de → size = sp_init (LLVM 22 default)
    uint32_t expected_size = (sp_init > de) ? (sp_init - de) : sp_init;
    EXPECT_EQ(mod->thread_stack_size, expected_size);
}

// 2 スレッドがそれぞれ正しい fibonacci 値を返すこと（スタック破壊がないこと）
TEST_F(StackSwitchTest, TwoThreadsCorrectResult) {
    int32_t id = engine.LoadModule("main", 4, kStack_switchWasmBinary, kStack_switchWasmBinarySize);
    ASSERT_GE(id, 0);
    ASSERT_EQ(engine.InstantiateModules(), WasmResult::kOk);

    int32_t func_idx = engine.GetExportFunctionIndex("main", 4, "thread_worker", 13);
    ASSERT_GE(func_idx, 0);

    WasmModuleInstance* mod = engine.GetModuleInstanceById(id);
    ASSERT_NE(mod, nullptr);

    WasmResult r1 = WasmResult::kErrorInvalidArgument;
    WasmResult r2 = WasmResult::kErrorInvalidArgument;
    WasmValue  v1{}, v2{};

    uint32_t t1 = engine.CreateHostThread(mod, static_cast<uint32_t>(func_idx));
    ASSERT_NE(t1, 0u);
    engine.PushThreadArg(t1, WasmValue::FromI32(10)); // fib(10) = 55
    engine.SetThreadCallback(t1, [](WasmThreadContext*, void* ud, WasmResult r) {
        *static_cast<WasmResult*>(ud) = r;
    }, &r1);
    engine.StartThread(t1);

    uint32_t t2 = engine.CreateHostThread(mod, static_cast<uint32_t>(func_idx));
    ASSERT_NE(t2, 0u);
    engine.PushThreadArg(t2, WasmValue::FromI32(12)); // fib(12) = 144
    engine.SetThreadCallback(t2, [](WasmThreadContext*, void* ud, WasmResult r) {
        *static_cast<WasmResult*>(ud) = r;
    }, &r2);
    engine.StartThread(t2);

    while (engine.HasReadyThread()) {
        engine.Step();
    }

    // スレッドが完了していること
    EXPECT_EQ(r1, WasmResult::kOk);
    EXPECT_EQ(r2, WasmResult::kOk);

    // スタック結果を確認: Step() 後 execution_result は kOk、スタックに戻り値が残る
    WasmThreadContext* ctx1 = engine.GetThreadContext(t1);
    WasmThreadContext* ctx2 = engine.GetThreadContext(t2);
    ASSERT_NE(ctx1, nullptr);
    ASSERT_NE(ctx2, nullptr);
    EXPECT_EQ(ctx1->state, ThreadState::kTerminated);
    EXPECT_EQ(ctx2->state, ThreadState::kTerminated);
    // thread_worker は i32 を 1 つ返す
    EXPECT_EQ(ctx1->stack[0].value.i32, 55);  // fib(10)
    EXPECT_EQ(ctx2->stack[0].value.i32, 144); // fib(12)
}
#endif // SS_HAS_WASM

} // namespace embwasm
