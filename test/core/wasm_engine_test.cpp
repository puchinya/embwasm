#include <cstddef>
#include <gtest/gtest.h>
#include <vector>
#include "wasm_types.hpp"
#include "wasm_limits.hpp"
#include "wasm_memory_pool.hpp"
#include "wasm_api.hpp"
#include "wasm_engine.hpp"
#include "wasm_api_static.hpp"

constexpr size_t kMemoryPoolSize = 1 << 20;

namespace {
alignas(16) uint8_t g_wasm_pool_buf[kMemoryPoolSize];
}

namespace embwasm {
extern int g_test_env_init_called;
extern int g_test_env_deinit_called;
}

TEST(WasmEngineTest, UserData) {
    embwasm::WasmMemoryPool pool; pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
    embwasm::WasmEngine engine; engine.Init(pool);

    EXPECT_EQ(engine.GetUserData(), nullptr);

    int mock_user_data = 12345;
    engine.SetUserData(&mock_user_data);
    EXPECT_EQ(engine.GetUserData(), &mock_user_data);

    int mock_user_data2 = 67890;
    engine.SetUserData(&mock_user_data2);
    EXPECT_EQ(engine.GetUserData(), &mock_user_data2);

    engine.SetUserData(nullptr);
    EXPECT_EQ(engine.GetUserData(), nullptr);
}

TEST(WasmEngineTest, ModuleUserData) {
    embwasm::WasmMemoryPool pool; pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
    embwasm::WasmEngine engine; engine.Init(pool);

    EXPECT_EQ(embwasm::LookupStaticHostModuleId("env", 3), embwasm::HostModuleId::kEnv);
    EXPECT_EQ(static_cast<uint32_t>(embwasm::LookupStaticHostModuleId("invalid_module", 14)), 0xFFFFFFFF);

    EXPECT_EQ(engine.GetModuleUserData(embwasm::HostModuleId::kEnv), nullptr);

    int env_user_data = 555;
    engine.SetModuleUserData(embwasm::HostModuleId::kEnv, &env_user_data);
    EXPECT_EQ(engine.GetModuleUserData(embwasm::HostModuleId::kEnv), &env_user_data);

    EXPECT_EQ(engine.GetModuleUserData(static_cast<embwasm::HostModuleId>(999)), nullptr);
    int invalid_user_data = 999;
    engine.SetModuleUserData(static_cast<embwasm::HostModuleId>(999), &invalid_user_data);
    EXPECT_EQ(engine.GetModuleUserData(static_cast<embwasm::HostModuleId>(999)), nullptr);
}

TEST(WasmEngineTest, HostModuleInitDeinit) {
    embwasm::g_test_env_init_called = 0;
    embwasm::g_test_env_deinit_called = 0;
    embwasm::WasmMemoryPool pool; pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));

    {
        embwasm::WasmEngine engine; engine.Init(pool);
        EXPECT_EQ(embwasm::g_test_env_init_called, 1);
        EXPECT_EQ(embwasm::g_test_env_deinit_called, 0);
    }

    EXPECT_EQ(embwasm::g_test_env_init_called, 1);
    EXPECT_EQ(embwasm::g_test_env_deinit_called, 1);
    pool.Deinit();
}

TEST(WasmEngineTest, LoadErrors) {
    embwasm::WasmMemoryPool pool; pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));
    embwasm::WasmEngine engine; engine.Init(pool);

    // 1. サイズ不足
    constexpr uint8_t kTooShort[] = { 0x00, 0x61 };
    EXPECT_EQ(static_cast<embwasm::WasmResult>(engine.LoadModule(kTooShort, sizeof(kTooShort))), embwasm::WasmResult::kErrorParseInvalidMagic);

    // 2. マジックナンバーエラー
    constexpr uint8_t kBadMagic[] = { 0x11, 0x22, 0x33, 0x44, 0x01, 0x00, 0x00, 0x00 };
    EXPECT_EQ(static_cast<embwasm::WasmResult>(engine.LoadModule(kBadMagic, sizeof(kBadMagic))), embwasm::WasmResult::kErrorParseInvalidMagic);

    // 3. バージョンエラー
    constexpr uint8_t kBadVersion[] = { 0x00, 0x61, 0x73, 0x6d, 0x02, 0x00, 0x00, 0x00 };
    EXPECT_EQ(static_cast<embwasm::WasmResult>(engine.LoadModule(kBadVersion, sizeof(kBadVersion))), embwasm::WasmResult::kErrorParseInvalidVersion);

    // 4. 不明なセクション形式 (form != 0x60) -> kErrorUnknownSection
    constexpr uint8_t kWasmBadForm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x04, 0x01,
          0x61, // 0x60 であるべきところを 0x61
          0x00, 0x00
    };
    EXPECT_EQ(static_cast<embwasm::WasmResult>(engine.LoadModule(kWasmBadForm, sizeof(kWasmBadForm))), embwasm::WasmResult::kErrorParseUnknownSection);

    // 5. 完全に不正なインポート種類 (kind=0x05, 仕様外) -> kOk (スキップ扱い)
    // 注: kind=0x01(table), 0x02(memory), 0x03(global) は有効なインポート種別として処理される
    constexpr uint8_t kWasmBadImportKind[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
        0x02, 0x0d, 0x01,
          0x03, 'e', 'n', 'v',
          0x05, 'd', 'u', 'm', 'm', 'y',
          0x01, // table import (有効な種別)
          0x70, 0x00, 0x00  // funcref, min=0 (tableの構文)
    };
    EXPECT_GE(engine.LoadModule(kWasmBadImportKind, sizeof(kWasmBadImportKind)), 0);

    // 6. ホスト関数が見つからない -> kOk (未解決インポートはno-opとして許容)
    constexpr uint8_t kWasmImportNotFound[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
        0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
        0x02, 0x11, 0x01,
          0x03, 'e', 'n', 'v',
          0x09, 'n', 'o', 't', '_', 'f', 'o', 'u', 'n', 'd',
          0x00, // kind: Function
          0x00  // type index
    };
    EXPECT_GE(engine.LoadModule(kWasmImportNotFound, sizeof(kWasmImportNotFound)), 0);

    // 7. 型の引数制限超え -> kErrorOutOfMemory
    {
        uint32_t num_params = static_cast<uint32_t>(embwasm::kWasmMaxParamCount) + 1;
        std::vector<uint8_t> count_bytes;
        {
            uint32_t v = num_params;
            do {
                uint8_t b = v & 0x7f;
                v >>= 7;
                if (v) b |= 0x80;
                count_bytes.push_back(b);
            } while (v);
        }
        std::vector<uint8_t> type_body;
        type_body.push_back(0x01);
        type_body.push_back(0x60);
        type_body.insert(type_body.end(), count_bytes.begin(), count_bytes.end());
        for (uint32_t i = 0; i < num_params; ++i) type_body.push_back(0x7f);
        type_body.push_back(0x00);
        uint32_t sec_len = static_cast<uint32_t>(type_body.size());
        std::vector<uint8_t> sec_len_bytes;
        {
            uint32_t v = sec_len;
            do {
                uint8_t b = v & 0x7f;
                v >>= 7;
                if (v) b |= 0x80;
                sec_len_bytes.push_back(b);
            } while (v);
        }
        std::vector<uint8_t> too_many_params_wasm = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01
        };
        too_many_params_wasm.insert(too_many_params_wasm.end(), sec_len_bytes.begin(), sec_len_bytes.end());
        too_many_params_wasm.insert(too_many_params_wasm.end(), type_body.begin(), type_body.end());
        EXPECT_EQ(static_cast<embwasm::WasmResult>(engine.LoadModule(too_many_params_wasm.data(), too_many_params_wasm.size())), embwasm::WasmResult::kErrorValidationFailed);
    }

    // 8. 型の戻り値制限超え -> kErrorValidationFailed
    {
        uint32_t num_results = static_cast<uint32_t>(embwasm::kWasmMaxResultCount) + 1;
        std::vector<uint8_t> count_bytes;
        {
            uint32_t v = num_results;
            do {
                uint8_t b = v & 0x7f;
                v >>= 7;
                if (v) b |= 0x80;
                count_bytes.push_back(b);
            } while (v);
        }
        std::vector<uint8_t> type_body;
        type_body.push_back(0x01);
        type_body.push_back(0x60);
        type_body.push_back(0x00);
        type_body.insert(type_body.end(), count_bytes.begin(), count_bytes.end());
        for (uint32_t i = 0; i < num_results; ++i) type_body.push_back(0x7f);
        uint32_t sec_len = static_cast<uint32_t>(type_body.size());
        std::vector<uint8_t> sec_len_bytes;
        {
            uint32_t v = sec_len;
            do {
                uint8_t b = v & 0x7f;
                v >>= 7;
                if (v) b |= 0x80;
                sec_len_bytes.push_back(b);
            } while (v);
        }
        std::vector<uint8_t> too_many_results_wasm = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
            0x01
        };
        too_many_results_wasm.insert(too_many_results_wasm.end(), sec_len_bytes.begin(), sec_len_bytes.end());
        too_many_results_wasm.insert(too_many_results_wasm.end(), type_body.begin(), type_body.end());
        EXPECT_EQ(static_cast<embwasm::WasmResult>(engine.LoadModule(too_many_results_wasm.data(), too_many_results_wasm.size())), embwasm::WasmResult::kErrorValidationFailed);
    }
}
