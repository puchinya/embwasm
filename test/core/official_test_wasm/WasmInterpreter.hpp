#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include "embwasm.hpp"

using WasmValue = embwasm::WasmValue;

class WasmInterpreter {
private:
    embwasm::WasmMemoryPool pool_;
    embwasm::WasmEngine engine_;

    char active_module_name_[64];
    char current_anonymous_name_[64];
    size_t load_count_;
    alignas(16) uint8_t pool_buf_[embwasm::kMemoryPoolSize];

    void reloadSpectest() {
        static const uint8_t kSpectestWasm[] = {
            // magic + version
            0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
            // Section 1: Type (30 bytes) - 7 function signatures
            0x01, 0x1E,
            0x07,
            0x60, 0x00, 0x00,                               // type 0: () → ()
            0x60, 0x01, 0x7F, 0x00,                         // type 1: (i32) → ()
            0x60, 0x01, 0x7E, 0x00,                         // type 2: (i64) → ()
            0x60, 0x01, 0x7D, 0x00,                         // type 3: (f32) → ()
            0x60, 0x01, 0x7C, 0x00,                         // type 4: (f64) → ()
            0x60, 0x02, 0x7F, 0x7D, 0x00,                   // type 5: (i32,f32) → ()
            0x60, 0x02, 0x7C, 0x7C, 0x00,                   // type 6: (f64,f64) → ()
            // Section 3: Function (8 bytes) - 7 no-op functions
            0x03, 0x08,
            0x07, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
            // Section 4: Table (5 bytes) - funcref, min=10, max=20
            0x04, 0x05,
            0x01, 0x70, 0x01, 0x0A, 0x14,
            // Section 5: Memory (4 bytes) - min=1, max=2
            0x05, 0x04,
            0x01, 0x01, 0x01, 0x02,
            // Section 6: Global (33 bytes) - 4 immutable globals
            0x06, 0x21,
            0x04,
            0x7F, 0x00, 0x41, 0x9A, 0x05, 0x0B,            // i32 const 666
            0x7E, 0x00, 0x42, 0x9A, 0x05, 0x0B,            // i64 const 666
            0x7D, 0x00, 0x43, 0x00, 0x00, 0x00, 0x00, 0x0B,// f32 const 0.0
            0x7C, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x0B,       // f64 const 0.0
            // Section 7: Export (158 bytes, LEB128=0x9E,0x01) - 13 exports
            0x07, 0x9E, 0x01,
            0x0D,
            0x05, 0x70, 0x72, 0x69, 0x6E, 0x74,             // "print"
              0x00, 0x00,
            0x09, 0x70, 0x72, 0x69, 0x6E, 0x74, 0x5F,       // "print_i32"
              0x69, 0x33, 0x32, 0x00, 0x01,
            0x09, 0x70, 0x72, 0x69, 0x6E, 0x74, 0x5F,       // "print_i64"
              0x69, 0x36, 0x34, 0x00, 0x02,
            0x09, 0x70, 0x72, 0x69, 0x6E, 0x74, 0x5F,       // "print_f32"
              0x66, 0x33, 0x32, 0x00, 0x03,
            0x09, 0x70, 0x72, 0x69, 0x6E, 0x74, 0x5F,       // "print_f64"
              0x66, 0x36, 0x34, 0x00, 0x04,
            0x0D, 0x70, 0x72, 0x69, 0x6E, 0x74, 0x5F,       // "print_i32_f32"
              0x69, 0x33, 0x32, 0x5F, 0x66, 0x33, 0x32, 0x00, 0x05,
            0x0D, 0x70, 0x72, 0x69, 0x6E, 0x74, 0x5F,       // "print_f64_f64"
              0x66, 0x36, 0x34, 0x5F, 0x66, 0x36, 0x34, 0x00, 0x06,
            0x05, 0x74, 0x61, 0x62, 0x6C, 0x65, 0x01, 0x00, // "table"
            0x06, 0x6D, 0x65, 0x6D, 0x6F, 0x72, 0x79,       // "memory"
              0x02, 0x00,
            0x0A, 0x67, 0x6C, 0x6F, 0x62, 0x61, 0x6C, 0x5F, // "global_i32"
              0x69, 0x33, 0x32, 0x03, 0x00,
            0x0A, 0x67, 0x6C, 0x6F, 0x62, 0x61, 0x6C, 0x5F, // "global_i64"
              0x69, 0x36, 0x34, 0x03, 0x01,
            0x0A, 0x67, 0x6C, 0x6F, 0x62, 0x61, 0x6C, 0x5F, // "global_f32"
              0x66, 0x33, 0x32, 0x03, 0x02,
            0x0A, 0x67, 0x6C, 0x6F, 0x62, 0x61, 0x6C, 0x5F, // "global_f64"
              0x66, 0x36, 0x34, 0x03, 0x03,
            // Section 10: Code (22 bytes) - 7 empty bodies
            0x0A, 0x16,
            0x07,
            0x02, 0x00, 0x0B,   // print
            0x02, 0x00, 0x0B,   // print_i32
            0x02, 0x00, 0x0B,   // print_i64
            0x02, 0x00, 0x0B,   // print_f32
            0x02, 0x00, 0x0B,   // print_f64
            0x02, 0x00, 0x0B,   // print_i32_f32
            0x02, 0x00, 0x0B,   // print_f64_f64
        };
        engine_.Load("spectest", 8, kSpectestWasm, sizeof(kSpectestWasm));
    }

public:
    WasmInterpreter() : load_count_(0) {
        pool_.Init(pool_buf_, sizeof(pool_buf_));
        engine_.Init(pool_);
        active_module_name_[0] = '\0';
        current_anonymous_name_[0] = '\0';
        reloadSpectest();
    }
    ~WasmInterpreter() {
        engine_.Deinit();
        pool_.Deinit();
    }

    // WASMバイナリをロード
    bool loadModule(const char *module_name, const uint8_t* bytes, size_t size) {
        if (bytes == nullptr || size < 8) return false;

        char name[64];
        if (module_name != nullptr) {
            std::strncpy(name, module_name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            // 同名モジュールが既にあれば置き換える
            engine_.Unload(name, std::strlen(name));
        } else {
            std::snprintf(name, sizeof(name), "mod%zu", load_count_++);
            // 前回の匿名モジュールをアンロード（registerされていなければ）
            if (current_anonymous_name_[0] != '\0') {
                engine_.Unload(current_anonymous_name_, std::strlen(current_anonymous_name_));
            }
            std::strncpy(current_anonymous_name_, name, sizeof(current_anonymous_name_) - 1);
            current_anonymous_name_[sizeof(current_anonymous_name_) - 1] = '\0';
        }
        int32_t id = engine_.Load(name, std::strlen(name), bytes, size);
        if (id < 0) {
            std::printf("loadModule failed with error code: %d\n", -id);
            return false;
        }
        std::strncpy(active_module_name_, name, sizeof(active_module_name_) - 1);
        active_module_name_[sizeof(active_module_name_) - 1] = '\0';
        return true;
    }

    // アクティブモジュールにエイリアス名を登録
    void registerModule(const char* alias_name) {
        if (alias_name == nullptr || active_module_name_[0] == '\0') return;
        size_t active_len = std::strlen(active_module_name_);
        engine_.RegisterAlias(active_module_name_, active_len, alias_name, std::strlen(alias_name));
        // registerされたので匿名モジュールとして追跡しない
        size_t anon_len = std::strlen(current_anonymous_name_);
        if (anon_len == active_len && std::memcmp(current_anonymous_name_, active_module_name_, active_len) == 0) {
            current_anonymous_name_[0] = '\0';
        }
    }

    // 指定モジュールにエイリアス名を登録（register "$name" as "alias" の形式用）
    void registerModule(const char* real_name, const char* alias_name) {
        if (real_name == nullptr || alias_name == nullptr) return;
        size_t real_len = std::strlen(real_name);
        engine_.RegisterAlias(real_name, real_len, alias_name, std::strlen(alias_name));
        // registerされたので匿名モジュールとして追跡しない
        size_t anon_len = std::strlen(current_anonymous_name_);
        if (anon_len == real_len && std::memcmp(current_anonymous_name_, real_name, real_len) == 0) {
            current_anonymous_name_[0] = '\0';
        }
    }

    WasmValue create_i32(int32_t val) {
        WasmValue v; v.value.i32 = val; return v;
    }
    WasmValue create_i64(int64_t val) {
        WasmValue v; v.value.i64 = val; return v;
    }
    WasmValue create_f32(float val) {
        WasmValue v; v.value.f32 = val; return v;
    }
    WasmValue create_f64(double val) {
        WasmValue v; v.value.f64 = val; return v;
    }
    WasmValue create_f32_nan(uint32_t bit_pattern) {
        WasmValue v = {};
        *reinterpret_cast<uint32_t*>(&v.value) = bit_pattern;
        return v;
    }
    WasmValue create_f64_nan(uint64_t bit_pattern) {
        WasmValue v = {};
        *reinterpret_cast<uint64_t*>(&v.value) = bit_pattern;
        return v;
    }
    WasmValue create_f32_bits(uint32_t bits) {
        WasmValue v = {};
        *reinterpret_cast<uint32_t*>(&v.value) = bits;
        return v;
    }
    WasmValue create_f64_bits(uint64_t bits) {
        WasmValue v = {};
        *reinterpret_cast<uint64_t*>(&v.value) = bits;
        return v;
    }
    WasmValue create_externref(std::nullptr_t) {
        WasmValue v = {}; v.value.i64 = -1; return v;
    }
    WasmValue create_externref(const void* val) {
        WasmValue v = {}; v.value.i64 = reinterpret_cast<int64_t>(val); return v;
    }
    WasmValue create_funcref(std::nullptr_t) {
        WasmValue v = {}; v.value.i64 = -1; return v;
    }
    WasmValue create_funcref(const void* val) {
        WasmValue v = {}; v.value.i64 = reinterpret_cast<int64_t>(val); return v;
    }

    WasmValue invoke_internal(const char* module_name, size_t module_name_len, const char* func_name, size_t func_name_len, const WasmValue* args, size_t args_count) {
        WasmValue res = {};

        // アクティブモジュールで検索、なければ全スロットを検索
        const char* target_module = module_name;
        size_t target_module_len = module_name_len;
        if (target_module == nullptr) {
            target_module = active_module_name_;
            target_module_len = std::strlen(active_module_name_);
        }

        if (!target_module || target_module_len == 0) {
            res.value.i64 = -1LL;
            return res;
        }

        uint32_t result_count = engine_.GetExportFunctionResultCount(target_module, target_module_len, func_name, func_name_len);
        if (result_count > 1) result_count = 1;
        embwasm::WasmResult run_res = engine_.Execute(
            target_module, target_module_len, func_name, func_name_len, args,
            static_cast<uint32_t>(args_count), &res, result_count);
        if (run_res != embwasm::WasmResult::kOk) {
            std::printf("invoke failed with error code: %d\n", static_cast<int>(run_res));
            res.value.i64 = -1LL;
        }
        return res;
    }

    template <size_t N>
    WasmValue invoke(const char (&func_name)[N], const WasmValue* args, size_t args_count) {
        return invoke_internal(nullptr, 0, func_name, N - 1, args, args_count);
    }

    template <size_t N, size_t M>
    WasmValue invoke(const char (&module_name)[N], const char (&func_name)[M], const WasmValue* args, size_t args_count) {
        return invoke_internal(module_name, N - 1, func_name, M - 1, args, args_count);
    }

    WasmValue invoke(const std::string& func_name, const WasmValue* args, size_t args_count) {
        return invoke_internal(nullptr, 0, func_name.data(), func_name.size(), args, args_count);
    }

    void unloadAll() {
        engine_.UnloadAll();
        active_module_name_[0] = '\0';
        current_anonymous_name_[0] = '\0';
        reloadSpectest();
    }

    int32_t to_i32(WasmValue val) { return val.value.i32; }
    int64_t to_i64(WasmValue val) { return val.value.i64; }
    float   to_f32(WasmValue val) { return val.value.f32; }
    double  to_f64(WasmValue val) { return val.value.f64; }

    bool is_nan_f32(WasmValue val) {
        uint32_t bits = *reinterpret_cast<uint32_t*>(&val.value);
        return ((bits & 0x7F800000) == 0x7F800000) && ((bits & 0x007FFFFF) != 0);
    }
    bool is_nan_f64(WasmValue val) {
        uint64_t bits = *reinterpret_cast<uint64_t*>(&val.value);
        return ((bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
               ((bits & 0x000FFFFFFFFFFFFFULL) != 0);
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

