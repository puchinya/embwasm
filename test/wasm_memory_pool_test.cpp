#include <gtest/gtest.h>
#include "wasm_config.hpp"
#include "wasm_memory_pool.hpp"

TEST(WasmMemoryPoolTest, AllFunctions) {
    embwasm::WasmMemoryPool pool;
    
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

    // Alignment（メモリ境界）の検証
    void* ptr2 = pool.Allocate(1, 8);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 8, 0U);

    void* ptr3 = pool.Allocate(1, 16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr3) % 16, 0U);

    // OutOfMemory の検証
    void* ptr_overflow = pool.Allocate(embwasm::kMemoryPoolSize + 1);
    EXPECT_EQ(ptr_overflow, nullptr);
}
