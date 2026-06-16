#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include "embwasm.hpp"

// テストコード側が使うエイリアス。値型として扱えるように実体をバッファにする

using WasmValue = embwasm::WasmValue;

class WasmInterpreter {
private:
    struct ModuleInstance {
        alignas(16) uint8_t pool_buf[embwasm::kMemoryPoolSize];
        embwasm::WasmMemoryPool pool;
        embwasm::WasmEngine engine;

        ModuleInstance() {
            pool.Init(pool_buf, sizeof(pool_buf));
            engine.Init(pool);
        }
    };

    static constexpr size_t kMaxModules = 512;
    ModuleInstance* modules_[kMaxModules] = {};
    size_t module_count_ = 0;
    ModuleInstance* active_module_ = nullptr;

public:
    WasmInterpreter() {
        // spectest モック用のダミーモジュールをロード
        static const uint8_t spectest_wasm[] = {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // magic & version
            0x04, 0x05, 0x01, 0x70, 0x01, 0x0a, 0x14,       // table section (funcref, min 10, max 20)
            0x05, 0x04, 0x01, 0x01, 0x01, 0x02,             // memory section (min 1, max 2)
            0x07, 0x12, 0x02,                               // export section (2 exports)
              0x06, 'm', 'e', 'm', 'o', 'r', 'y', 0x02, 0x00, // "memory" (kind memory, index 0)
              0x05, 't', 'a', 'b', 'l', 'e', 0x01, 0x00      // "table" (kind table, index 0)
        };
        loadModule(spectest_wasm, sizeof(spectest_wasm));

        // デフォルトのモジュールを1つ用意しておく
        ModuleInstance* inst = new ModuleInstance();
        active_module_ = inst;
        modules_[module_count_++] = inst;
    }
    ~WasmInterpreter() {
        for (size_t i = 0; i < module_count_; ++i) {
            delete modules_[i];
        }
    }

    // Wasmバイナリ（バイト列）をロード
    bool loadModule(const uint8_t* bytes, size_t size) {
        if (bytes == nullptr || size < 8) return false;
        if (module_count_ >= kMaxModules) return false;

        ModuleInstance* inst = new ModuleInstance();
        embwasm::WasmResult load_res = inst->engine.Load(bytes, size);
        if (load_res != embwasm::WasmResult::kOk) {
            std::printf("loadModule failed with error code: %d\n", static_cast<int>(load_res));
            delete inst;
            return false;
        }

        modules_[module_count_++] = inst;
        active_module_ = inst;
        return true;
    }

    // 値型を安全に生成する各種ファクトリ
    WasmValue create_i32(int32_t val) {
        WasmValue _v; _v.value.i32 = val; return _v;
    }
    WasmValue create_i64(int64_t val) {
        WasmValue _v; _v.value.i64 = val; return _v;
    }
    WasmValue create_f32(float val) {
        WasmValue _v; _v.value.f32 = val; return _v;
    }
    WasmValue create_f64(double val) {
        WasmValue _v; _v.value.f64 = val; return _v;
    }
    WasmValue create_f32_nan(uint32_t bit_pattern) {
        WasmValue val = {};

        *reinterpret_cast<uint32_t*>(&val.value) = bit_pattern;

        return val;
    }
    WasmValue create_f64_nan(uint64_t bit_pattern) {
        WasmValue val = {};

        *reinterpret_cast<uint64_t*>(&val.value) = bit_pattern;

        return val;
    }

    // 生ビットパターンから直接浮動小数点数（NaN等含む）を作るための拡張関数
    WasmValue create_f32_bits(uint32_t bits) {
        WasmValue val = {};

        *reinterpret_cast<uint32_t*>(&val.value) = bits;

        return val;
    }
    WasmValue create_f64_bits(uint64_t bits) {
        WasmValue val = {};

        *reinterpret_cast<uint64_t*>(&val.value) = bits;

        return val;
    }

    WasmValue create_externref(std::nullptr_t) {
        WasmValue v = {};
        v.value.i64 = -1;
        return v;
    }
    WasmValue create_externref(const void* val) {
        WasmValue v = {};
        v.value.i64 = reinterpret_cast<int64_t>(val);
        return v;
    }
    WasmValue create_funcref(std::nullptr_t) {
        WasmValue v = {};
        v.value.i64 = -1;
        return v;
    }
    WasmValue create_funcref(const void* val) {
        WasmValue v = {};
        v.value.i64 = reinterpret_cast<int64_t>(val);
        return v;
    }

    // 関数実行 (内部用)
    WasmValue invoke_internal(const char* func_name, size_t name_len, const WasmValue* args, size_t args_count) {
        WasmValue res = {};

        // 最新の（active_module_）から順に、指定された関数を持つモジュールを検索する
        ModuleInstance* target_inst = nullptr;
        if (active_module_ && active_module_->engine.GetExportFunctionIndex(func_name, name_len) != -1) {
            target_inst = active_module_;
        } else {
            // 後ろから順に検索
            for (size_t i = module_count_; i > 0; --i) {
                if (modules_[i - 1]->engine.GetExportFunctionIndex(func_name, name_len) != -1) {
                    target_inst = modules_[i - 1];
                    break;
                }
            }
        }

        if (!target_inst) {
            // 見つからなければ active_module_ で実行を試みる（エラー出力のため）
            target_inst = active_module_;
        }

        if (target_inst) {
            uint32_t result_count = target_inst->engine.GetExportFunctionResultCount(func_name, name_len);
            if (result_count > 1) result_count = 1;
            embwasm::WasmResult run_res = target_inst->engine.Execute(func_name, name_len, args, args_count, &res, result_count);
            if (run_res != embwasm::WasmResult::kOk) {
                std::printf("invoke failed with error code: %d\n", static_cast<int>(run_res));
                res.value.i64 = -1LL;
            }
        }
        return res;
    }

    // 文字列リテラル用テンプレート
    template <size_t N>
    WasmValue invoke(const char (&func_name)[N], const WasmValue* args, size_t args_count) {
        return invoke_internal(func_name, N - 1, args, args_count);
    }

    // 動的文字列/ポインタ用フォールバック
    WasmValue invoke(const std::string& func_name, const WasmValue* args, size_t args_count) {
        return invoke_internal(func_name.data(), func_name.size(), args, args_count);
    }

    // 値検証用の型取り出し/ステータスチェック
    int32_t to_i32(WasmValue val) {
        return val.value.i32;
    }
    int64_t  to_i64(WasmValue val) {
        return val.value.i64;
    }
    float to_f32(WasmValue val) {
        return val.value.f32;

    }
    double to_f64(WasmValue val) {
        return val.value.f64;
    }
    bool is_nan_f32(WasmValue val) {
        uint32_t bits = *reinterpret_cast<uint32_t*>(&val.value);
        return ((bits & 0x7F800000) == 0x7F800000) && ((bits & 0x007FFFFF) != 0);
    }

    bool is_nan_f64(WasmValue val) {
        uint64_t bits = *reinterpret_cast<uint64_t*>(&val.value);
        return ((bits & 0x7FF0000000000000) == 0x7FF0000000000000) && ((bits & 0x000FFFFFFFFFFFFF) != 0);
    }

    uint32_t to_f32_bits(WasmValue val) { return val.value.i32; }
    uint64_t to_f64_bits(WasmValue val) { return val.value.i64; }

    void* to_externref(WasmValue val) {
        if (val.value.i64 == -1) return nullptr;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(val.value.i64));
    }
    void* to_funcref(WasmValue val) {
        if (val.value.i64 == -1) return nullptr;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(val.value.i64));
    }
};
