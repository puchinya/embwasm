#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sys/types.h>

#include "embwasm.hpp"

using WasmValue = embwasm::WasmValue;
constexpr size_t kPoolBufSize = 256 << 20;

class WasmInterpreter {
private:
    embwasm::WasmMemoryPool pool_;
    embwasm::WasmEngine engine_;

    uint8_t *pool_buf_;

    void InstantiateAll() {
        embwasm::WasmResult res = engine_.InstantiateModules();
        if (res != embwasm::WasmResult::kOk)
            std::printf("InstantiateModules failed: %d\n", static_cast<int>(res));
    }

    void ReloadSpectest() {
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
        engine_.LoadModule("spectest", 8, kSpectestWasm, sizeof(kSpectestWasm));
    }

public:
    WasmInterpreter()  {
        pool_buf_ = new uint8_t[kPoolBufSize];
        pool_.Init(pool_buf_, kPoolBufSize);
        engine_.Init(pool_);
        ReloadSpectest();
    }
    ~WasmInterpreter() {
        engine_.Deinit();
        pool_.Deinit();
        if (pool_buf_ != nullptr) {
            delete[] pool_buf_;
            pool_buf_ = nullptr;
        }
    }

    int32_t LoadModule(const char* module_name, const uint8_t* bytes, size_t size) {
        size_t name_len = (module_name != nullptr) ? std::strlen(module_name) : 0;
        int32_t id = engine_.LoadModule(module_name, name_len, bytes, size);
        if (id < 0) {
            std::printf("LoadModule failed with error code: %d\n", -id);
            return id;
        }
        embwasm::WasmResult res = engine_.InstantiateModules();
        if (res != embwasm::WasmResult::kOk) {
            std::printf("InstantiateModules failed with error code: %d\n", static_cast<int>(res));
            return static_cast<int32_t>(res);
        }
        return id;
    }

    int32_t LoadModuleExpectFailure(const uint8_t* bytes, size_t size) {
        int32_t id = engine_.LoadModule(nullptr, 0, bytes, size);
        if (id < 0) return id;
        embwasm::WasmResult res = engine_.InstantiateModules();
        if (res == embwasm::WasmResult::kOk) return 0;
        // Keep is_active = true so any func refs written into shared tables
        // during partial instantiation remain callable (WASM MVP semantics).
        // Set is_instantiated = true to prevent retry.
        embwasm::WasmModuleInstance* mod = engine_.GetModuleInstanceById(id);
        if (mod) {
            mod->is_instantiated = true;
        }
        return static_cast<int32_t>(res);
    }

    void RegisterModule(const char* alias_name) {
        engine_.RegisterAlias(nullptr, 0, alias_name, std::strlen(alias_name));
    }

    void RegisterModule(const char* real_name, const char* alias_name) {
        if (real_name == nullptr || alias_name == nullptr) return;
        size_t real_len = std::strlen(real_name);
        engine_.RegisterAlias(real_name, real_len, alias_name, std::strlen(alias_name));
    }

    WasmValue CreateI32(int32_t val) {
        WasmValue v; v.value.i32 = val; return v;
    }
    WasmValue CreateI64(int64_t val) {
        WasmValue v; v.value.i64 = val; return v;
    }
    WasmValue CreateF32(float val) {
        WasmValue v; v.value.f32 = val; return v;
    }
    WasmValue CreateF64(double val) {
        WasmValue v; v.value.f64 = val; return v;
    }
    WasmValue CreateF32Nan(uint32_t bit_pattern) {
        WasmValue v = {};
        *reinterpret_cast<uint32_t*>(&v.value) = bit_pattern;
        return v;
    }
    WasmValue CreateF64Nan(uint64_t bit_pattern) {
        WasmValue v = {};
        *reinterpret_cast<uint64_t*>(&v.value) = bit_pattern;
        return v;
    }
    WasmValue CreateF32Bits(uint32_t bits) {
        WasmValue v = {};
        *reinterpret_cast<uint32_t*>(&v.value) = bits;
        return v;
    }
    WasmValue CreateF64Bits(uint64_t bits) {
        WasmValue v = {};
        *reinterpret_cast<uint64_t*>(&v.value) = bits;
        return v;
    }
    WasmValue CreateExternref(std::nullptr_t) {
        WasmValue v = {}; v.value.i64 = -1; return v;
    }
    WasmValue CreateExternref(const void* val) {
        WasmValue v = {}; v.value.i64 = reinterpret_cast<int64_t>(val); return v;
    }
    WasmValue CreateFuncref(std::nullptr_t) {
        WasmValue v = {}; v.value.i64 = -1; return v;
    }
    WasmValue CreateFuncref(const void* val) {
        WasmValue v = {}; v.value.i64 = reinterpret_cast<int64_t>(val); return v;
    }

    /// @brief WASM関数を実行する内部実装。
    /// @param[in]  module_name      モジュール名（nullptrの場合はアクティブモジュールを使用）。
    /// @param[in]  module_name_len  モジュール名の長さ。
    /// @param[in]  func_name        関数名。
    /// @param[in]  func_name_len    関数名の長さ。
    /// @param[in]  args             引数配列。
    /// @param[in]  args_count       引数の個数。
    /// @param[out] out_result       戻り値の格納先（不要な場合はnullptr）。
    /// @return 0: 正常終了。非0: エラーコード（embwasm::WasmResultのキャスト値）。
    int32_t InvokeInternal(const char* module_name, size_t module_name_len,
                           const char* func_name, size_t func_name_len,
                           const WasmValue* args, size_t args_count,
                           WasmValue* out_result) {

        WasmValue res = {};

        uint32_t result_count = engine_.GetExportFunctionResultCount(
            module_name, module_name_len, func_name, func_name_len);

        if (result_count > 1) result_count = 1;

        embwasm::WasmResult run_res = engine_.Execute(
            module_name, module_name_len, func_name, func_name_len, args,
            static_cast<uint32_t>(args_count), &res, result_count);

        if (out_result != nullptr) *out_result = res;
        return static_cast<int32_t>(run_res);
    }

    template <size_t N>
    int32_t Invoke(const char (&func_name)[N], const WasmValue* args, size_t args_count,
                   WasmValue* out_result) {
        return InvokeInternal(nullptr, 0, func_name, N - 1, args, args_count, out_result);
    }

    template <size_t N, size_t M>
    int32_t Invoke(const char (&module_name)[N], const char (&func_name)[M],
                   const WasmValue* args, size_t args_count, WasmValue* out_result) {
        return InvokeInternal(module_name, N - 1, func_name, M - 1, args, args_count, out_result);
    }

    WasmValue GetGlobalInternal(const char* module_name, size_t module_name_len,
                                const char* field_name, size_t field_name_len,
                                int depth = 0) {
        WasmValue v = {};
        if (depth > 8) return v;
        const embwasm::WasmModuleInstance* mod =
            engine_.GetModuleInstance(module_name, module_name_len);
        if (!mod) return v;
        for (size_t i = 0; i < mod->export_count; ++i) {
            const auto& exp = mod->exports[i];
            if (exp.kind == 3 && exp.name_len == field_name_len &&
                std::memcmp(exp.name, field_name, field_name_len) == 0) {
                if (exp.index >= mod->global_count) return v;
                // If this global index corresponds to an imported global,
                // follow the import chain to get the live value from the source.
                for (size_t j = 0; j < mod->import_count; ++j) {
                    const auto& imp = mod->imports[j];
                    if (imp.kind == 3 && imp.index == exp.index) {
                        return GetGlobalInternal(imp.module_name, imp.module_name_len,
                                                 imp.field_name, imp.field_name_len,
                                                 depth + 1);
                    }
                }
                return mod->globals[exp.index].value;
            }
        }
        return v;
    }

    template <size_t N, size_t M>
    WasmValue GetGlobal(const char (&module_name)[N], const char (&field_name)[M]) {
        return GetGlobalInternal(module_name, N - 1, field_name, M - 1);
    }

    template <size_t M>
    WasmValue GetGlobal(std::nullptr_t, const char (&field_name)[M]) {
        return GetGlobalInternal(nullptr, 0, field_name, M - 1);
    }

    void UnloadAll() {
        engine_.UnloadAllModules();
        ReloadSpectest();
    }

    int32_t ToI32(WasmValue val) { return val.value.i32; }
    int64_t ToI64(WasmValue val) { return val.value.i64; }
    float   ToF32(WasmValue val) { return val.value.f32; }
    double  ToF64(WasmValue val) { return val.value.f64; }

    bool IsNanF32(WasmValue val) {
        uint32_t bits = *reinterpret_cast<uint32_t*>(&val.value);
        return ((bits & 0x7F800000) == 0x7F800000) && ((bits & 0x007FFFFF) != 0);
    }
    bool IsNanF64(WasmValue val) {
        uint64_t bits = *reinterpret_cast<uint64_t*>(&val.value);
        return ((bits & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
               ((bits & 0x000FFFFFFFFFFFFFULL) != 0);
    }

    uint32_t ToF32Bits(WasmValue val) { return val.value.i32; }
    uint64_t ToF64Bits(WasmValue val) { return val.value.i64; }

    void* ToExternref(WasmValue val) {
        if (val.value.i64 == -1) return nullptr;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(val.value.i64));
    }
    void* ToFuncref(WasmValue val) {
        if (val.value.i64 == -1) return nullptr;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(val.value.i64));
    }
};
