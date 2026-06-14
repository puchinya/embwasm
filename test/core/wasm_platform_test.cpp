#include <gtest/gtest.h>
#include "wasm_platform.hpp"

TEST(WasmPlatformTest, CountLeadingZeros) {
    // 0のリーディングゼロは32
    EXPECT_EQ(embwasm::CountLeadingZeros(0), 32U);
    // 最上位ビットが1なら0
    EXPECT_EQ(embwasm::CountLeadingZeros(0x80000000), 0U);
    // 最下位ビットが1なら31
    EXPECT_EQ(embwasm::CountLeadingZeros(1), 31U);
    
    EXPECT_EQ(embwasm::CountLeadingZeros(0x0000FFFF), 16U);
    EXPECT_EQ(embwasm::CountLeadingZeros(0x0F000000), 4U);
}

TEST(WasmPlatformTest, InterruptControl) {
    // 割り込み禁止・復元関数が正常に（ハングやクラッシュせずに）呼び出せることを検証
    uint32_t primask = embwasm::DisableInterrupts();
    
    // クリティカルセクションを模した処理
    int val = 42;
    val += 1;
    (void)val;
    
    embwasm::RestoreInterrupts(primask);
    SUCCEED();
}
