#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_engine.hpp"
#include "wasm_memory_pool.hpp"
#include "embwasm_hostmodule_thread.hpp"

#if EMBWASM_ENABLE_MULTITHREADING

namespace embwasm {

constexpr size_t kMemoryPoolSize = 1 << 20;
namespace {
alignas(16) uint8_t g_wasm_pool_buf[kMemoryPoolSize];
}

class WasmThreadTest : public ::testing::Test {
protected:
    WasmMemoryPool pool;
    WasmEngine engine;

    WasmThreadTest() : engine() {}

    void SetUp() override {
        pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
        engine.Init(pool); // Init() 内で scheduler_.SetAsInstance() が呼ばれる
    }

    void TearDown() override {
        engine.Deinit();
        pool.Deinit();
    }
};

TEST_F(WasmThreadTest, ThreadReset) {
    WasmThreadContext ctx;
    ctx.Reset();
    EXPECT_EQ(ctx.id, 0);
    EXPECT_EQ(ctx.state, ThreadState::kTerminated);
}

TEST_F(WasmThreadTest, EventReset) {
    WasmEvent ev;
    ev.Reset();
    EXPECT_EQ(ev.id, 0);
    EXPECT_FALSE(ev.signaled);
}

TEST_F(WasmThreadTest, CreateThread) {
    // ワーカースレッド（slot 1 以降）が生成されることを確認
    uint32_t tid = engine.CreateThread(0);
    EXPECT_NE(tid, 0);
    WasmThreadContext* ctx = engine.GetThreadContext(tid);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->id, tid);
}

TEST_F(WasmThreadTest, CreateEvent) {
    uint32_t eid = engine.CreateEvent();
    EXPECT_NE(eid, 0);
}

TEST_F(WasmThreadTest, SignalWaitEvent) {
    uint32_t tid = engine.CreateThread(0);
    uint32_t eid = engine.CreateEvent();
    WasmThreadContext* ctx = engine.GetThreadContext(tid);
    ASSERT_NE(ctx, nullptr);

    engine.WaitEvent(tid, eid);
    EXPECT_EQ(ctx->state, ThreadState::kWaiting);
    EXPECT_EQ(ctx->wait_param.event_id, eid);

    engine.SignalEvent(eid);
    EXPECT_EQ(ctx->state, ThreadState::kReady);
}

TEST_F(WasmThreadTest, WaitSignaledEvent) {
    uint32_t tid = engine.CreateThread(0);
    uint32_t eid = engine.CreateEvent();
    WasmThreadContext* ctx = engine.GetThreadContext(tid);
    ASSERT_NE(ctx, nullptr);

    engine.SignalEvent(eid);
    engine.WaitEvent(tid, eid);
    // シグナル済みなら即座にReadyに戻る
    EXPECT_EQ(ctx->state, ThreadState::kReady);
}

TEST_F(WasmThreadTest, ThreadSpawnApi) {
    int32_t out_result = 0;
    WasmResult res = hostmodules::sys::rt::threads::thread_spawn(engine, nullptr, 0, out_result);
    EXPECT_EQ(res, WasmResult::kOk);
    EXPECT_NE(out_result, 0);
}

TEST_F(WasmThreadTest, ThreadYieldApi) {
    WasmResult res = hostmodules::sys::rt::threads::thread_yield(engine);
    EXPECT_EQ(res, WasmResult::kYield);
}

TEST_F(WasmThreadTest, EventWaitSignalApi) {
    (void)engine.CreateThread(0);
    uint32_t eid = engine.CreateEvent();

    WasmResult res = hostmodules::sys::rt::threads::event_wait(engine, static_cast<int32_t>(eid));
    EXPECT_EQ(res, WasmResult::kYield);
    EXPECT_EQ(engine.GetCurrentThreadContext()->state, ThreadState::kWaiting);

    res = hostmodules::sys::rt::threads::event_signal(engine, static_cast<int32_t>(eid));
    EXPECT_EQ(res, WasmResult::kOk);
    EXPECT_EQ(engine.GetCurrentThreadContext()->state, ThreadState::kReady);
}

} // namespace embwasm

#endif // EMBWASM_ENABLE_MULTITHREADING
