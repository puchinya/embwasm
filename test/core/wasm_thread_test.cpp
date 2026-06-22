#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_thread.hpp"
#include "wasm_engine.hpp"
#include "wasm_memory_pool.hpp"
#include "embwasm_hostmodule_thread.hpp"

#if EMBWASM_ENABLE_MULTITHREADING

namespace embwasm {

namespace {
alignas(16) uint8_t g_wasm_pool_buf[embwasm::kMemoryPoolSize];
}

class WasmThreadTest : public ::testing::Test {
protected:
    WasmMemoryPool pool;
    WasmEngine engine;
    WasmScheduler& scheduler;

    WasmThreadTest() : engine(), scheduler(*engine.GetScheduler()) {}

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
    uint32_t tid = scheduler.CreateThread(0);
    EXPECT_NE(tid, 0);
    WasmThreadContext* ctx = scheduler.GetThreadContext(tid);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->id, tid);
}

TEST_F(WasmThreadTest, CreateEvent) {
    uint32_t eid = scheduler.CreateEvent();
    EXPECT_NE(eid, 0);
}

TEST_F(WasmThreadTest, SignalWaitEvent) {
    uint32_t tid = scheduler.CreateThread(0);
    uint32_t eid = scheduler.CreateEvent();
    WasmThreadContext* ctx = scheduler.GetThreadContext(tid);
    ASSERT_NE(ctx, nullptr);

    scheduler.WaitEvent(tid, eid);
    EXPECT_EQ(ctx->state, ThreadState::kWaiting);
    EXPECT_EQ(ctx->wait_param.event_id, eid);

    scheduler.SignalEvent(eid);
    EXPECT_EQ(ctx->state, ThreadState::kReady);
}

TEST_F(WasmThreadTest, WaitSignaledEvent) {
    uint32_t tid = scheduler.CreateThread(0);
    uint32_t eid = scheduler.CreateEvent();
    WasmThreadContext* ctx = scheduler.GetThreadContext(tid);
    ASSERT_NE(ctx, nullptr);

    scheduler.SignalEvent(eid);
    scheduler.WaitEvent(tid, eid);
    // シグナル済みなら即座にReadyに戻る
    EXPECT_EQ(ctx->state, ThreadState::kReady);
}

TEST_F(WasmThreadTest, ThreadSpawnApi) {
    int32_t out_result = 0;
    WasmResult res = hostmodules::thread::ThreadSpawn(engine, nullptr, 0, out_result);
    EXPECT_EQ(res, WasmResult::kOk);
    EXPECT_NE(out_result, 0);
}

TEST_F(WasmThreadTest, ThreadYieldApi) {
    WasmResult res = hostmodules::thread::ThreadYield(engine);
    EXPECT_EQ(res, WasmResult::kYield);
}

TEST_F(WasmThreadTest, EventWaitSignalApi) {
    (void)scheduler.CreateThread(0);
    uint32_t eid = scheduler.CreateEvent();

    WasmResult res = hostmodules::thread::EventWait(engine, static_cast<int32_t>(eid));
    EXPECT_EQ(res, WasmResult::kYield);
    EXPECT_EQ(scheduler.GetCurrentThreadContext()->state, ThreadState::kWaiting);

    res = hostmodules::thread::EventSignal(engine, static_cast<int32_t>(eid));
    EXPECT_EQ(res, WasmResult::kOk);
    EXPECT_EQ(scheduler.GetCurrentThreadContext()->state, ThreadState::kReady);
}

} // namespace embwasm

#endif // EMBWASM_ENABLE_MULTITHREADING
