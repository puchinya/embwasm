#include <cstddef>
#include <gtest/gtest.h>
#include "wasm_config.hpp"
#include "wasm_memory_pool.hpp"

namespace {
alignas(16) uint8_t g_wasm_pool_buf[embwasm::kMemoryPoolSize];
}

TEST(WasmMemoryPoolTest, AllFunctions) {
    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
    
    // GetTotalBytes の検証
    EXPECT_EQ(pool.GetTotalBytes(), embwasm::kMemoryPoolSize);
    
    // 初期状態の確認 (GetUsedBytes, GetFreeBytes)
    EXPECT_EQ(pool.GetUsedBytes(), 0U);
    EXPECT_EQ(pool.GetFreeBytes(), embwasm::kMemoryPoolSize);

    // Allocate & Reset の検証
    void* ptr1 = pool.Allocate(100);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_GE(pool.GetUsedBytes(), 100U);
    EXPECT_EQ(pool.GetFreeBytes(), embwasm::kMemoryPoolSize - pool.GetUsedBytes());

    pool.Reset();
    EXPECT_EQ(pool.GetUsedBytes(), 0U);
    EXPECT_EQ(pool.GetFreeBytes(), embwasm::kMemoryPoolSize);

    // Alignment（kMemoryPoolAlignment 境界に揃うことの検証）
    void* ptr2 = pool.Allocate(1);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % embwasm::kMemoryPoolAlignment, 0U);

    void* ptr3 = pool.Allocate(1);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % embwasm::kMemoryPoolAlignment, 0U);

    // OutOfMemory の検証
    void* ptr_overflow = pool.Allocate(embwasm::kMemoryPoolSize + 1);
    EXPECT_EQ(ptr_overflow, nullptr);
    pool.Deinit();
}
