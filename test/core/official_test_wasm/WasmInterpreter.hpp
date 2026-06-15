#pragma once
#include <cstdint>
#include <cstddef>
#include "embwasm.hpp"

// インタプリタ内部で使う真のデータ構造（テストコードには見せない）
struct MyInternalValue {
    int64_t raw_bits;
    uint8_t type_tag;
};

// テストコード側が使うエイリアス。値型として扱えるように実体をバッファにする
using WasmValue = embwasm::WasmValue;

alignas(16) uint8_t g_core_wasm_pool_buf[embwasm::kMemoryPoolSize];

class WasmInterpreter {
private:
    embwasm::WasmMemoryPool pool_;
    embwasm::WasmEngine engine_;
public:
    WasmInterpreter() {
        pool_.Init(g_core_wasm_pool_buf, sizeof(g_core_wasm_pool_buf));
        engine_.Init(pool_);
    }
    ~WasmInterpreter() {}

    // Wasmバイナリ（バイト列）をロード
    bool loadModule(const uint8_t* bytes, size_t size) {
        if (bytes == nullptr || size < 8) return false;

        embwasm::WasmResult load_res = engine_.Load(bytes, size);

        return load_res == embwasm::WasmResult::kOk;
    }

    // 値型を安全に生成する各種ファクトリ
    WasmValue create_i32(int32_t val) {
        return { embwasm::WasmType::kI32, val };
    }
    WasmValue create_i64(int64_t val) {
        return { embwasm::WasmType::kI64, val };
    }
    WasmValue create_f32(float val) {
        return { embwasm::WasmType::kF32, val };
    }
    WasmValue create_f64(double val) {
        return { embwasm::WasmType::kF64, val };
    }
    WasmValue create_f32_nan(uint32_t bit_pattern) {
        WasmValue val = {};
        val.type = embwasm::WasmType::kF32;

        *reinterpret_cast<uint32_t*>(&val.value) = bit_pattern;

        return val;
    }
    WasmValue create_f64_nan(uint64_t bit_pattern) {
        WasmValue val = {};
        val.type = embwasm::WasmType::kF64;

        *reinterpret_cast<uint64_t*>(&val.value) = bit_pattern;

        return val;
    }

    // 生ビットパターンから直接浮動小数点数（NaN等含む）を作るための拡張関数
    WasmValue create_f32_bits(uint32_t bits) {
        WasmValue val = {};
        val.type = embwasm::WasmType::kF32;

        *reinterpret_cast<uint32_t*>(&val.value) = bits;

        return val;
    }
    WasmValue create_f64_bits(uint64_t bits) {
        WasmValue val = {};
        val.type = embwasm::WasmType::kF64;

        *reinterpret_cast<uint64_t*>(&val.value) = bits;

        return val;
    }

    // ★修正: 参照型テスト用のファクトリ関数を追加してコンパイルエラーを回避
    WasmValue create_externref(const void* val) { (void)val; return WasmValue{}; }
    WasmValue create_funcref(const void* val)   { (void)val; return WasmValue{}; }

    // 関数実行
    WasmValue invoke(const char* func_name, const WasmValue* args, size_t args_count) {
        WasmValue res = {};
        engine_.Execute(func_name, args, args_count, &res, 1);
        return res;
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
    bool is_nan(WasmValue val) {
        if (val.type == embwasm::WasmType::kF32) {
            uint32_t bits = *reinterpret_cast<uint32_t*>(&val.value);

            // 指数部(8bit)がすべて1: 0x7f800000
            // 仮数部(23bit)が0以外: & 0x007fffff != 0
            return ((bits & 0x7F800000) == 0x7F800000) && ((bits & 0x007FFFFF) != 0);
        }
        if (val.type == embwasm::WasmType::kF64) {uint64_t bits = *reinterpret_cast<uint64_t*>(&val.value);

            // 指数部(11bit)がすべて1: 0x7ff0000000000000
            // 仮数部(52bit)が0以外: & 0x000fffffffffffff != 0
            return ((bits & 0x7FF0000000000000) == 0x7FF0000000000000) && ((bits & 0x000FFFFFFFFFFFFF) != 0);
        }

        return false;
    }

    // ★生ビットパターン検証用にテストコード側から安全に覗き込むためのブリッジ
    uint32_t to_f32_bits(WasmValue val) { return val.value.i32; }
    uint64_t to_f64_bits(WasmValue val) { return val.value.i64; }

    // 参照型・最新命令用の取り出しヘルパー（コンパイルエラー回避用）
    void* to_externref(WasmValue val) { (void)val; return nullptr; }
    void* to_funcref(WasmValue val)   { (void)val; return nullptr; }
};
