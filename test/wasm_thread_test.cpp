#include <gtest/gtest.h>
#include "wasm_thread.hpp"
#include "wasm_engine.hpp"
#include "wasm_memory_pool.hpp"
#include "embwasm_hostmodule_thread.hpp"

#if EMBWASM_ENABLE_MULTITHREADING

namespace embwasm {

class WasmThreadTest : public ::testing::Test {
protected:
    WasmMemoryPool pool;
    WasmEngine engine;
    WasmScheduler scheduler;

    WasmThreadTest() : engine(pool), scheduler(engine) {
        scheduler.SetAsInstance();
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
    uint32_t tid = scheduler.CreateThread(0);
    EXPECT_NE(tid, 0);
    EXPECT_EQ(scheduler.GetCurrentThread()->id, tid);
}

TEST_F(WasmThreadTest, CreateEvent) {
    uint32_t eid = scheduler.CreateEvent();
    EXPECT_NE(eid, 0);
}

TEST_F(WasmThreadTest, SignalWaitEvent) {
    uint32_t tid = scheduler.CreateThread(0);
    uint32_t eid = scheduler.CreateEvent();
    
    scheduler.WaitEvent(tid, eid);
    // ctx.stateは threads_ がプライベートなので直接見れないが、GetCurrentThread経由で確認
    EXPECT_EQ(scheduler.GetCurrentThread()->state, ThreadState::kWaiting);
    EXPECT_EQ(scheduler.GetCurrentThread()->wait_event_id, eid);

    scheduler.SignalEvent(eid);
    EXPECT_EQ(scheduler.GetCurrentThread()->state, ThreadState::kReady);
}

TEST_F(WasmThreadTest, WaitSignaledEvent) {
    uint32_t tid = scheduler.CreateThread(0);
    uint32_t eid = scheduler.CreateEvent();
    
    scheduler.SignalEvent(eid);
    scheduler.WaitEvent(tid, eid);
    
    // シグナル済みなら即座にReadyに戻る
    EXPECT_EQ(scheduler.GetCurrentThread()->state, ThreadState::kReady);
}

TEST_F(WasmThreadTest, ThreadSpawnApi) {
    WasmValue args[1] = {{WasmType::kI32, {10}}}; // func_idx = 10
    WasmValue results[1];
    WasmResult res = hostmodules::thread::ThreadSpawn(engine, args, 1, results, 1);
    
    EXPECT_EQ(res, WasmResult::kOk);
    EXPECT_NE(results[0].value.i32, 0);
}

TEST_F(WasmThreadTest, ThreadYieldApi) {
    WasmResult res = hostmodules::thread::ThreadYield(engine, nullptr, 0, nullptr, 0);
    EXPECT_EQ(res, WasmResult::kYield);
}

TEST_F(WasmThreadTest, EventWaitSignalApi) {
    (void)scheduler.CreateThread(0);
    uint32_t eid = scheduler.CreateEvent();
    
    WasmValue args[1] = {{WasmType::kI32, {static_cast<int32_t>(eid)}}};
    WasmResult res = hostmodules::thread::EventWait(engine, args, 1, nullptr, 0);
    
    EXPECT_EQ(res, WasmResult::kYield);
    EXPECT_EQ(scheduler.GetCurrentThread()->state, ThreadState::kWaiting);
    
    res = hostmodules::thread::EventSignal(engine, args, 1, nullptr, 0);
    EXPECT_EQ(res, WasmResult::kOk);
    EXPECT_EQ(scheduler.GetCurrentThread()->state, ThreadState::kReady);
}

} // namespace embwasm

#endif // EMBWASM_ENABLE_MULTITHREADING
