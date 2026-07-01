// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This WebAssembly execution library has been designed and implemented entirely 
// from scratch based on the official WebAssembly Core Specification. No source 
// code from existing open-source WASM engines (such as wasm3, WAMR, or wabt) 
// has been copied, referenced, or adapted.
// =============================================================================

#include "wasm_engine.hpp"
#include "wasm_limits.hpp"
#include "wasm_platform.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace embwasm {

    bool WasmTypeSignature::Equals(const WasmTypeSignature *x, const WasmTypeSignature *y) noexcept {
        if (x->param_count != y->param_count || x->result_count != y->result_count) return false;
        uint32_t words = WordCount(x->param_count + x->result_count);
        for (uint32_t i = 0; i < words; ++i)
            if (x->data.u32[i] != y->data.u32[i]) return false;
        return true;
    }

    // =============================================================================
    // Helper Functions for Bitwise and Math operations
    // =============================================================================

    static inline uint32_t CountTrailingZeros32(uint32_t v) noexcept {
        return CountTrailingZeros(v);
    }

    static inline uint32_t CountTrailingZeros64(uint64_t v) noexcept {
        if (v == 0) return 64;
        uint32_t low = static_cast<uint32_t>(v);
        if (low != 0) return CountTrailingZeros(low);
        return 32 + CountTrailingZeros(static_cast<uint32_t>(v >> 32));
    }

    static inline uint32_t CountLeadingZeros32(uint32_t v) noexcept {
        return CountLeadingZeros(v);
    }

    static inline uint32_t CountLeadingZeros64(uint64_t v) noexcept {
        if (v == 0) return 64;
        uint32_t high = static_cast<uint32_t>(v >> 32);
        if (high != 0) return CountLeadingZeros(high);
        return 32 + CountLeadingZeros(static_cast<uint32_t>(v));
    }

#if defined(__arm__) || defined(__thumb__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
    // ARM Thumb-2 / Cortex-M: ROR 命令を使用（ROTL は符号反転シフトで実現）
    static inline uint32_t Rotl32(uint32_t x, uint32_t y) noexcept {
        uint32_t result;
        uint32_t shift = (-y) & 31u;
        __asm__ volatile ("ror %0, %1, %2" : "=r"(result) : "r"(x), "r"(shift));
        return result;
    }
    static inline uint32_t Rotr32(uint32_t x, uint32_t y) noexcept {
        uint32_t result;
        __asm__ volatile ("ror %0, %1, %2" : "=r"(result) : "r"(x), "r"(y));
        return result;
    }
#elif defined(__aarch64__)
    // AArch64: 32 ビット ROR 命令（%w 修飾子で Wn レジスタを指定）
    static inline uint32_t Rotl32(uint32_t x, uint32_t y) noexcept {
        uint32_t result;
        uint32_t shift = (-y) & 31u;
        __asm__ volatile ("ror %w0, %w1, %w2" : "=r"(result) : "r"(x), "r"(shift));
        return result;
    }

    static inline uint32_t Rotr32(uint32_t x, uint32_t y) noexcept {
        uint32_t result;
        __asm__ volatile ("ror %w0, %w1, %w2" : "=r"(result) : "r"(x), "r"(y));
        return result;
    }
#elif defined(__i386__) || defined(__x86_64__)
    // x86/x86_64: ROL / ROR 命令（シフト量は CL レジスタ経由）
    static inline uint32_t Rotl32(uint32_t x, uint32_t y) noexcept {
        __asm__ volatile ("roll %%cl, %0" : "+r"(x) : "c"(y));
        return x;
    }
    static inline uint32_t Rotr32(uint32_t x, uint32_t y) noexcept {
        __asm__ volatile ("rorl %%cl, %0" : "+r"(x) : "c"(y));
        return x;
    }
#elif defined(_MSC_VER)
    // MSVC: _rotl / _rotr 組み込み関数
    static inline uint32_t Rotl32(uint32_t x, uint32_t y) noexcept {
        return _rotl(x, static_cast<int>(y));
    }
    static inline uint32_t Rotr32(uint32_t x, uint32_t y) noexcept {
        return _rotr(x, static_cast<int>(y));
    }
#else
    // ソフトウェアフォールバック（GCC/Clang はローテートイディオムを最適化）
    static inline uint32_t Rotl32(uint32_t x, uint32_t y) noexcept {
        y &= 31u;
        return (x << y) | (x >> ((-y) & 31u));
    }
    static inline uint32_t Rotr32(uint32_t x, uint32_t y) noexcept {
        y &= 31u;
        return (x >> y) | (x << ((-y) & 31u));
    }
#endif

#if defined(__aarch64__)
    static inline uint64_t Rotr64(uint64_t x, uint64_t y) noexcept {
        uint64_t result;
        __asm__ volatile ("ror %0, %1, %2" : "=r"(result) : "r"(x), "r"(y));
        return result;
    }
    static inline uint64_t Rotl64(uint64_t x, uint64_t y) noexcept {
        return Rotr64(x, (-y) & 63u);
    }
#else
    static inline uint64_t Rotl64(uint64_t x, uint64_t y) noexcept {
        y &= 63;
        if (y == 0) return x;
        return (x << y) | (x >> (64 - y));
    }

    static inline uint64_t Rotr64(uint64_t x, uint64_t y) noexcept {
        y &= 63;
        if (y == 0) return x;
        return (x >> y) | (x << (64 - y));
    }
#endif

    static inline float NearestF32(float x) noexcept {
        float r = std::round(x);
        float diff = r - x;
        if (std::abs(diff) == 0.5f) {
            if (std::fmod(r, 2.0f) != 0.0f) {
                r = (r < x) ? r + 1.0f : r - 1.0f;
            }
        }
        return std::copysign(r, x);
    }

    static inline double NearestF64(double x) noexcept {
        double r = std::round(x);
        double diff = r - x;
        if (std::abs(diff) == 0.5) {
            if (std::fmod(r, 2.0) != 0.0) {
                r = (r < x) ? r + 1.0 : r - 1.0;
            }
        }
        return std::copysign(r, x);
    }

    // =============================================================================
    // LEB128 (Little Endian Base 128) Decoders
    // Designed cleanly from the W3C WebAssembly Specification.
    // =============================================================================

    // Decodes a variable-length unsigned 32-bit integer.
    static inline uint32_t DecodeVarUint32(const uint8_t *&cursor, const uint8_t *limit) noexcept {
        if (cursor < limit && *cursor < 0x80) return static_cast<uint32_t>(*cursor++);
        uint32_t decoded_value = 0;
        uint32_t shift_amount = 0;

        while (cursor < limit) {
            uint8_t raw_byte = *cursor++;
            if (shift_amount < 32) {
                decoded_value |= static_cast<uint32_t>(raw_byte & 0x7F) << shift_amount;
            }
            if ((raw_byte & 0x80) == 0) {
                break;
            }
            shift_amount += 7;
            if (shift_amount >= 35) {
                // 32-bit LEB128 is at most 5 bytes
                break;
            }
        }
        return decoded_value;
    }

    // Decodes a variable-length signed 32-bit integer.
    static inline int32_t DecodeVarInt32(const uint8_t *&cursor, const uint8_t *limit) noexcept {
        if (cursor < limit && *cursor < 0x80) {
            uint8_t b = *cursor++;
            return static_cast<int32_t>(b) | (b & 0x40 ? ~0x7F : 0);
        }
        int32_t decoded_value = 0;
        uint32_t shift_amount = 0;
        uint8_t raw_byte = 0;

        while (cursor < limit) {
            raw_byte = *cursor++;
            if (shift_amount < 32) {
                decoded_value |= static_cast<int32_t>(raw_byte & 0x7F) << shift_amount;
            }
            shift_amount += 7;
            if ((raw_byte & 0x80) == 0) {
                break;
            }
            if (shift_amount >= 35) {
                // 32-bit LEB128 is at most 5 bytes
                break;
            }
        }

        // Sign extension for negative LEB128 numbers
        if ((shift_amount < 32) && (raw_byte & 0x40)) {
            decoded_value |= static_cast<int32_t>(~0U << shift_amount);
        }
        return decoded_value;
    }

    // Decodes a variable-length signed 64-bit integer.
    static inline int64_t DecodeVarInt64(const uint8_t *&cursor, const uint8_t *limit) noexcept {
        if (cursor < limit && *cursor < 0x80) {
            uint8_t b = *cursor++;
            return static_cast<int64_t>(b) | (b & 0x40 ? ~static_cast<int64_t>(0x7F) : 0LL);
        }
        int64_t decoded_value = 0;
        uint32_t shift_amount = 0;
        uint8_t raw_byte = 0;

        while (cursor < limit) {
            raw_byte = *cursor++;
            if (shift_amount < 64) {
                decoded_value |= static_cast<int64_t>(raw_byte & 0x7F) << shift_amount;
            }
            shift_amount += 7;
            if ((raw_byte & 0x80) == 0) {
                break;
            }
            if (shift_amount >= 70) {
                // 64-bit LEB128 is at most 10 bytes
                break;
            }
        }

        // Sign extension for negative LEB128 numbers
        if ((shift_amount < 64) && (raw_byte & 0x40)) {
            decoded_value |= (static_cast<int64_t>(~0ULL) << shift_amount);
        }
        return decoded_value;
    }

    // Fast LEB128 decoders for validated bytecode — no bounds check, loop unrolled.
    // Call only from the interpreter hot path where bytecode is already validated.

    static inline uint32_t DecodeVarUint32Fast(const uint8_t*& p) noexcept {
        uint32_t b = *p++;
        if (!(b & 0x80)) return b;
        uint32_t r = b & 0x7F;
        b = *p++; r |= (b & 0x7F) << 7;
        if (!(b & 0x80)) return r;
        b = *p++; r |= (b & 0x7F) << 14;
        if (!(b & 0x80)) return r;
        b = *p++; r |= (b & 0x7F) << 21;
        if (!(b & 0x80)) return r;
        b = *p++; r |= (b & 0x0F) << 28;
        return r;
    }

    static inline int32_t DecodeVarInt32Fast(const uint8_t*& p) noexcept {
        uint32_t b = *p++;
        if (!(b & 0x80))
            return static_cast<int32_t>(b) | (b & 0x40 ? ~static_cast<int32_t>(0x7F) : 0);
        uint32_t r = b & 0x7F;
        b = *p++; r |= (b & 0x7F) << 7;
        if (!(b & 0x80))
            return static_cast<int32_t>(r) | (b & 0x40 ? static_cast<int32_t>(~0U << 14) : 0);
        b = *p++; r |= (b & 0x7F) << 14;
        if (!(b & 0x80))
            return static_cast<int32_t>(r) | (b & 0x40 ? static_cast<int32_t>(~0U << 21) : 0);
        b = *p++; r |= (b & 0x7F) << 21;
        if (!(b & 0x80))
            return static_cast<int32_t>(r) | (b & 0x40 ? static_cast<int32_t>(~0U << 28) : 0);
        b = *p++; r |= (b & 0x0F) << 28;
        return static_cast<int32_t>(r);
    }

    static inline int64_t DecodeVarInt64Fast(const uint8_t*& p) noexcept {
        uint64_t b = *p++;
        if (!(b & 0x80))
            return static_cast<int64_t>(b) | (b & 0x40 ? ~static_cast<int64_t>(0x7F) : 0LL);
        uint64_t r = b & 0x7F;
        b = *p++; r |= (b & 0x7F) << 7;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 14) : 0LL);
        b = *p++; r |= (b & 0x7F) << 14;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 21) : 0LL);
        b = *p++; r |= (b & 0x7F) << 21;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 28) : 0LL);
        b = *p++; r |= (b & 0x7F) << 28;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 35) : 0LL);
        b = *p++; r |= (b & 0x7F) << 35;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 42) : 0LL);
        b = *p++; r |= (b & 0x7F) << 42;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 49) : 0LL);
        b = *p++; r |= (b & 0x7F) << 49;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 56) : 0LL);
        b = *p++; r |= (b & 0x7F) << 56;
        if (!(b & 0x80))
            return static_cast<int64_t>(r) | (b & 0x40 ? static_cast<int64_t>(~0ULL << 63) : 0LL);
        b = *p++; r |= (b & 0x01ULL) << 63;
        return static_cast<int64_t>(r);
    }

    static uint32_t EncodeFuncRef(WasmEngine * /*current_engine*/, WasmModuleInstance *current_mod,
                                  uint32_t func_idx) noexcept {
        if (func_idx == 0xFFFFFFFF) return 0xFFFFFFFF;
        if ((func_idx >> 16) != 0) return func_idx; // Already encoded

        uint32_t mod_idx = current_mod->self_index + 1;
        return (mod_idx << 16) | (func_idx & 0xFFFF);
    }

    static void DecodeFuncRef(uint32_t ref_val, WasmEngine *current_engine, WasmModuleInstance *current_mod,
                              WasmModuleInstance *&out_module, uint32_t &out_func_idx) noexcept {
        if (ref_val == 0xFFFFFFFF) {
            out_module = nullptr;
            out_func_idx = 0xFFFFFFFF;
            return;
        }
        uint32_t mod_idx = ref_val >> 16;
        uint32_t func_idx = ref_val & 0xFFFF;

        if (mod_idx == 0) {
            out_module = current_mod;
            out_func_idx = func_idx;
        } else {
            std::size_t idx = static_cast<std::size_t>(mod_idx - 1);
            if (idx < kMaxModules && current_engine->GetModuleInstanceById(static_cast<int32_t>(idx)) != nullptr) {
                out_module = current_engine->GetModuleInstanceById(static_cast<int32_t>(idx));
                out_func_idx = func_idx;
            } else {
                out_module = nullptr;
                out_func_idx = 0xFFFFFFFF;
            }
        }
    }

    // WASMモジュール間インポートチェーンを辿り、最終的な内部関数を返す。
    // 成功時（内部関数に到達）は true を返し、out_mod / out_func をセット。
    // ホスト関数・未解決・ループ深度超過の場合は false を返す。
    static bool ResolveWasmImportChain(WasmEngine * /*engine*/, const WasmFunction *func,
                                       WasmModuleInstance *&out_mod,
                                       const WasmFunction *&out_func) noexcept {
        for (int depth = 0; depth < 64; ++depth) {
            if (func->kind != WasmFunctionKind::kImport || func->import.resolved_func == nullptr) {
                return false;
            }
            const WasmFunction *next_func = func->import.resolved_func;
            if (next_func->kind != WasmFunctionKind::kImport) {
                out_mod = next_func->module;
                out_func = next_func;
                return true;
            }
            func = next_func;
        }
        return false;
    }

    // =============================================================================
    // WasmEngine 実装
    // =============================================================================

    static void ClearModuleInstance(WasmModuleInstance &m) noexcept {
        m.is_active = false;
        m.imports_resolved = false;
        m.is_instantiated = false;
        m.has_memory = false;
        m.name[0] = '\0';
        m.name_len = 0;
        m.signatures = nullptr;
        m.signature_count = 0;
        m.functions = nullptr;
        m.function_count = 0;
        m.exports = nullptr;
        m.export_count = 0;
        m.globals = nullptr;
        m.global_count = 0;
        m.linear_memory_ptr = nullptr;
        m.linear_memory_size = 0;
        m.linear_memory_capacity = 0;
        m.max_linear_memory_pages = 0;
        m.memory_min_pages = 0;
        m.is_memory_shared = false;
        m.memory_is_imported = false;
        m.tables = nullptr;
        m.table_sizes = nullptr;
        m.table_max_sizes = nullptr;
        m.table_types = nullptr;
        m.is_table_shared = nullptr;
        m.table_import_modules = nullptr;
        m.table_import_module_lens = nullptr;
        m.table_import_fields = nullptr;
        m.table_import_field_lens = nullptr;
        m.table_count = 0;
        m.table_capacity = 0;
        m.data_segments = nullptr;
        m.data_segment_sizes = nullptr;
        m.data_segment_dropped = nullptr;
        m.data_segment_offsets = nullptr;
        m.data_segment_offset_global_refs = nullptr;
        m.data_segment_is_active = nullptr;
        m.data_segment_count = 0;
        m.data_segment_capacity = 0;
        m.elem_segments = nullptr;
        m.elem_segment_sizes = nullptr;
        m.elem_segment_dropped = nullptr;
        m.elem_segment_table_indices = nullptr;
        m.elem_segment_offsets = nullptr;
        m.elem_segment_offset_global_refs = nullptr;
        m.elem_segment_is_active = nullptr;
        m.elem_segment_count = 0;
        m.elem_segment_capacity = 0;
        m.start_function_index = -1;
        m.self_index = 0;
        m.stack_ptr_global_idx  = UINT32_MAX;
        m.cabi_realloc_func_idx = UINT32_MAX;
        m.data_end_global_idx   = UINT32_MAX;
        m.thread_stack_size     = 0;
    }

    static inline bool StrEq(const char *a, std::size_t a_len, const char *b, std::size_t b_len) noexcept {
        return a_len == b_len && std::memcmp(a, b, a_len) == 0;
    }

    int32_t WasmModuleInstance::GetExportFunctionIndex(const char *func_name,
                                                       std::size_t func_name_len) const noexcept {
        if (!is_active) return -1;
        for (std::size_t i = 0; i < export_count; ++i) {
            auto &exp = exports[i];
            if (exp.kind == 0 && StrEq(exp.name, exp.name_len, func_name, func_name_len)) {
                return static_cast<int32_t>(exp.index);
            }
        }
        return -1;
    }

    WasmEngine::WasmEngine() noexcept
        : name_alias_count_(0),
          pool_(nullptr),
#if !EMBWASM_ENABLE_MULTITHREADING
          ctx_(nullptr),
#endif
#if EMBWASM_ENABLE_MULTITHREADING
          threads_(nullptr),
          ready_list_({nullptr, nullptr}),
          timeout_list_({nullptr, nullptr}),
          stop_requested_(false),
#endif
          last_loaded_id_(-1),
          exit_code_(0),
          max_call_stack_depth_(0), max_stack_depth_(0),
          user_data_(nullptr),
          platform_data_(nullptr),
          module_user_datas_(nullptr) {
        InitListNode(&name_aliases_);
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            modules_[i] = nullptr;
        }
#if EMBWASM_ENABLE_MULTITHREADING
        InitListNode(&ready_list_);
        InitListNode(&timeout_list_);
        for (std::size_t i = 0; i < kMaxEvents; ++i) {
            events_[i].Reset();
            events_[i].id = static_cast<uint32_t>(i + 1);
        }
#endif
    }

    WasmEngine::~WasmEngine() noexcept {
        Deinit();
    }

    WasmResult WasmEngine::Init(WasmMemoryPool &pool, const WasmEngineConfig &config) noexcept {
        Deinit();

        config_ = config;
        pool_ = &pool;
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            modules_[i] = nullptr;
        }

#if !EMBWASM_ENABLE_MULTITHREADING
        ctx_ = static_cast<WasmThreadContext*>(pool_->Allocate(sizeof(WasmThreadContext)));
        if (ctx_) {
            ctx_->Init(*pool_, config_);
            ctx_->id = 1;
        }
#endif
        last_loaded_id_ = -1;
        exit_code_ = 0;
        max_call_stack_depth_ = 0;
        max_stack_depth_ = 0;

        if (kHostModuleCount > 0) {
            module_user_datas_ = static_cast<void **>(pool_->Allocate(kHostModuleCount * sizeof(void *)));
            if (module_user_datas_) {
                for (std::size_t i = 0; i < kHostModuleCount; ++i) {
                    module_user_datas_[i] = nullptr;
                }
            }
        }
        InitializeAllHostModules(*this);
#if EMBWASM_ENABLE_MULTITHREADING
        stop_requested_ = false;
        InitListNode(&ready_list_);
        InitListNode(&timeout_list_);
        {
            void* allocated = pool_->Allocate(sizeof(WasmThreadContext*) * kMaxThreads);
            if (allocated) {
                threads_ = static_cast<WasmThreadContext**>(allocated);
                for (std::size_t i = 0; i < kMaxThreads; ++i) {
                    threads_[i] = nullptr;
                }
                void* main_ctx = pool_->Allocate(sizeof(WasmThreadContext));
                if (main_ctx) {
                    WasmThreadContext* ctx = static_cast<WasmThreadContext*>(main_ctx);
                    threads_[kMainThreadIndex] = ctx;
                    ctx->Init(*pool_, config_);
                    ctx->id = static_cast<uint32_t>(kMainThreadIndex + 1);
                }
            }
        }
        for (std::size_t i = 0; i < kMaxEvents; ++i) {
            events_[i].Reset();
            events_[i].id = static_cast<uint32_t>(i + 1);
        }
#endif
        WasmResult platform_result = PlatformEngineInit(*this);
        if (platform_result != WasmResult::kOk) {
            Deinit();
            return platform_result;
        }
        return WasmResult::kOk;
    }

    WasmResult WasmEngine::ResolveImports(WasmModuleInstance *mod) noexcept {
        if (mod->imports_resolved) return WasmResult::kOk;

        WasmResult result = WasmResult::kOk;

        for (std::size_t i = 0; i < mod->import_count; ++i) {
            const WasmImportEntry &entry = mod->imports[i];
            if (!entry.module_name || !entry.field_name) {
                result = WasmResult::kErrorLinking;
                continue;
            }

            WasmModuleInstance *src_mod = GetModuleInstance(entry.module_name, entry.module_name_len);

            switch (entry.kind) {
                case 0: {
                    // Function import: type_index と kind を設定し、ホスト API またはモジュールエクスポートへ解決。
                    WasmFunction &func = mod->functions[entry.index];
                    func.type_index = entry.desc.func.type_index;
                    {
                        HostFunctionId host_id = LookupStaticHostFunctionId(
                            entry.module_name, entry.module_name_len,
                            entry.field_name, entry.field_name_len);
                        if (host_id != HostFunctionId::kInvalid) {
                            if (entry.desc.func.type_index < mod->signature_count &&
                                !ValidateHostFunctionType(host_id, mod->signatures[entry.desc.func.type_index])) {
                                result = WasmResult::kErrorLinking;
                                break;
                            }
                            func.kind = WasmFunctionKind::kHost;
                            func.host.host_func_id = host_id;
                            break;
                        }
                    }
                    func.kind = WasmFunctionKind::kImport;
                    if (src_mod) {
                        for (std::size_t e = 0; e < src_mod->export_count; ++e) {
                            if (src_mod->exports[e].kind == 0 &&
                                StrEq(src_mod->exports[e].name, src_mod->exports[e].name_len,
                                      entry.field_name, entry.field_name_len)) {
                                WasmFunction* rfunc = &src_mod->functions[src_mod->exports[e].index];
                                uint32_t imp_tidx = entry.desc.func.type_index;
                                uint32_t src_tidx = rfunc->type_index;
                                bool type_ok = (imp_tidx < mod->signature_count &&
                                                src_tidx < src_mod->signature_count);
                                if (type_ok) {
                                    const WasmTypeSignature* isig = mod->signatures[imp_tidx];
                                    const WasmTypeSignature* ssig = src_mod->signatures[src_tidx];
                                    type_ok = WasmTypeSignature::Equals(isig, ssig);
                                }
                                if (type_ok) func.import.resolved_func = rfunc;
                                else result = WasmResult::kErrorLinking;
                                break;
                            }
                        }
                    }
                    if (func.import.resolved_func == nullptr && result == WasmResult::kOk) {
                        result = WasmResult::kErrorLinking;
                    }
                    break;
                }
                case 1: {
                    // Table import: テーブル型を desc から設定し、ソースモジュールから共有。
                    uint32_t tidx = entry.index;
                    if (tidx >= mod->table_count || mod->is_table_shared[tidx]) break;
                    mod->table_types[tidx] = static_cast<WasmType>(entry.desc.table.elem_type);
                    if (src_mod) {
                        for (std::size_t e = 0; e < src_mod->export_count; ++e) {
                            if (src_mod->exports[e].kind == 1 &&
                                StrEq(src_mod->exports[e].name, src_mod->exports[e].name_len,
                                      entry.field_name, entry.field_name_len)) {
                                uint32_t sidx = src_mod->exports[e].index;
                                if (sidx < src_mod->table_count && src_mod->tables[sidx] != nullptr) {
                                    if (src_mod->table_types[sidx] != static_cast<WasmType>(entry.desc.table.elem_type)) {
                                        result = WasmResult::kErrorLinking;
                                        break;
                                    }
                                    {
                                        uint32_t t_req_min = entry.desc.table.min_size;
                                        uint32_t t_req_max = entry.desc.table.max_size;
                                        uint32_t t_prov_min = src_mod->table_sizes[sidx];
                                        uint32_t t_prov_max = src_mod->table_max_sizes[sidx];
                                        bool t_ok = (t_prov_min >= t_req_min);
                                        if (t_ok && t_req_max != 0) {
                                            t_ok = (t_prov_max != 0 && t_prov_max <= t_req_max);
                                        }
                                        if (!t_ok) {
                                            result = WasmResult::kErrorLinking;
                                            break;
                                        }
                                    }
                                    mod->tables[tidx] = src_mod->tables[sidx];
                                    mod->table_types[tidx] = src_mod->table_types[sidx];
                                    mod->table_sizes[tidx] = src_mod->table_sizes[sidx];
                                    mod->table_max_sizes[tidx] = src_mod->table_max_sizes[sidx];
                                    mod->is_table_shared[tidx] = true;
                                }
                                break;
                            }
                        }
                    }
                    if (!mod->is_table_shared[tidx]) {
                        result = WasmResult::kErrorLinking;
                    }
                    break;
                }
                case 2: {
                    // Memory import: メモリメタデータを設定し、ソースモジュールから共有。
                    mod->has_memory = true;
                    mod->memory_is_imported = true;
                    if (mod->is_memory_shared || mod->linear_memory_ptr != nullptr) break;
                    if (src_mod) {
                        for (std::size_t e = 0; e < src_mod->export_count; ++e) {
                            if (src_mod->exports[e].kind == 2 &&
                                StrEq(src_mod->exports[e].name, src_mod->exports[e].name_len,
                                      entry.field_name, entry.field_name_len)) {
                                if (src_mod->linear_memory_ptr != nullptr) {
                                    // Validate memory limits: provided min >= required min;
                                    // if import declares a max, provided max must exist and be <= required max.
                                    uint32_t req_min = entry.desc.mem.min_pages;
                                    uint32_t req_max = entry.desc.mem.max_pages;
                                    uint32_t prov_min = src_mod->linear_memory_size / 65536;
                                    uint32_t prov_max = src_mod->max_linear_memory_pages;
                                    bool compat = (prov_min >= req_min);
                                    if (compat && req_max != 0) {
                                        compat = (prov_max != 0 && prov_max <= req_max);
                                    }
                                    if (!compat) {
                                        result = WasmResult::kErrorLinking;
                                        break;
                                    }
                                    mod->linear_memory_ptr = src_mod->linear_memory_ptr;
                                    mod->linear_memory_size = src_mod->linear_memory_size;
                                    mod->linear_memory_capacity = src_mod->linear_memory_capacity;
                                    mod->max_linear_memory_pages = src_mod->max_linear_memory_pages;
                                    mod->is_memory_shared = true;
                                }
                                break;
                            }
                        }
                    }
                    if (!mod->is_memory_shared && mod->linear_memory_ptr == nullptr) {
                        result = WasmResult::kErrorLinking;
                    }
                    break;
                }
                case 3: {
                    // Global import: desc から型・可変性を設定し、ソースモジュールから値をコピー。
                    uint32_t gidx = entry.index;
                    if (gidx >= mod->global_count) { result = WasmResult::kErrorLinking; break; }
                    mod->globals[gidx].type = static_cast<WasmType>(entry.desc.global.value_type);
                    mod->globals[gidx].is_mutable = entry.desc.global.is_mutable;
                    bool global_resolved = false;
                    if (src_mod) {
                        for (std::size_t e = 0; e < src_mod->export_count; ++e) {
                            if (src_mod->exports[e].kind == 3 &&
                                StrEq(src_mod->exports[e].name, src_mod->exports[e].name_len,
                                      entry.field_name, entry.field_name_len)) {
                                uint32_t sgidx = src_mod->exports[e].index;
                                if (sgidx < src_mod->global_count) {
                                    const WasmGlobal& src_g = src_mod->globals[sgidx];
                                    if (src_g.type != static_cast<WasmType>(entry.desc.global.value_type) ||
                                        src_g.is_mutable != entry.desc.global.is_mutable) {
                                        result = WasmResult::kErrorLinking;
                                        break;
                                    }
                                    mod->globals[gidx].value = src_g.value;
                                    global_resolved = true;
                                }
                                break;
                            }
                        }
                    }
                    if (!global_resolved) {
                        result = WasmResult::kErrorLinking;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        if (result != WasmResult::kOk) {
            return result;
        }

        mod->imports_resolved = true;

        return WasmResult::kOk;
    }

    WasmResult WasmEngine::InstantiateModules() noexcept {
        WasmResult last_error = WasmResult::kOk;
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            WasmModuleInstance *mod = modules_[i];
            if (!mod || !mod->is_active) continue;

            if (mod->is_instantiated) {
                // すでにインスタンス化済みなら、スキップ
                continue;
            }

            // --- 1. インポートの解決（メタデータ設定 + リンク） ---
            {
                WasmResult res = ResolveImports(mod);
                if (res != WasmResult::kOk) { last_error = res; continue; }
            }

            // --- 1a. バリデーション（ResolveImports でメタデータが揃った後に実施） ---
            {
                WasmResult res = Validate(mod);
                if (res != WasmResult::kOk) { last_error = res; continue; }
            }

            // --- 1b. global.get で初期化された own グローバルを再評価 ---
            // インポートグローバルの値はパース時点では 0 なので、ResolveImports() 後に再評価する。
            for (std::size_t g = 0; g < mod->global_count; ++g) {
                uint32_t ref = mod->globals[g].init_global_ref;
                if (ref != 0xFFFFFFFFu && ref < mod->global_count) {
                    mod->globals[g].value = mod->globals[ref].value;
                }
            }

            // --- 1c. global.get を参照するセグメントオフセットを再評価 ---
            // パース時点でインポートグローバルは 0 だったため、ResolveImports() 後に再計算する。
            if (mod->data_segment_offset_global_refs) {
                for (std::size_t d = 0; d < mod->data_segment_count; ++d) {
                    uint32_t gref = mod->data_segment_offset_global_refs[d];
                    if (gref != 0xFFFFFFFFu && gref < mod->global_count) {
                        mod->data_segment_offsets[d] = static_cast<uint32_t>(mod->globals[gref].value.value.i32);
                    }
                }
            }
            if (mod->elem_segment_offset_global_refs) {
                for (std::size_t e = 0; e < mod->elem_segment_count; ++e) {
                    uint32_t gref = mod->elem_segment_offset_global_refs[e];
                    if (gref != 0xFFFFFFFFu && gref < mod->global_count) {
                        mod->elem_segment_offsets[e] = static_cast<uint32_t>(mod->globals[gref].value.value.i32);
                    }
                }
            }

            // --- 2. 線形メモリのインスタンス化 ---
            // インポートメモリは ResolveImports() で解決済み。ここでは own メモリのみ確保する。
            if (mod->has_memory && !mod->is_memory_shared && mod->linear_memory_ptr == nullptr) {
                if (mod->memory_is_imported) {
                    // ResolveImports() で解決されなかった場合はリンクエラー
                    continue;
                }
                uint64_t initial_size = static_cast<uint64_t>(mod->memory_min_pages) * 65536;
                if (initial_size > kMaxLinearMemorySize) return WasmResult::kErrorExecuteTrapLinearMemoryLimitExceeded;
                std::size_t sentinel = (initial_size > 0) ? static_cast<std::size_t>(initial_size) : 1;
                mod->linear_memory_ptr = static_cast<uint8_t *>(pool_->Allocate(sentinel));
                if (!mod->linear_memory_ptr) return WasmResult::kErrorOutOfMemory;
                std::memset(mod->linear_memory_ptr, 0, sentinel);
                mod->linear_memory_size = static_cast<std::size_t>(initial_size);
                mod->linear_memory_capacity = sentinel;
            }

            // --- 3. テーブルのインスタンス化 ---
            // インポートテーブルは ResolveImports() で解決済み。ここでは own テーブルのみ確保する。
            for (std::size_t t = 0; t < mod->table_count; ++t) {
                if (mod->tables[t] != nullptr || mod->is_table_shared[t]) continue;
                // インポートテーブルが未解決の場合はスキップ（ResolveImports() でエラー済み）
                bool table_is_import = false;
                for (std::size_t ii = 0; ii < mod->import_count; ++ii) {
                    if (mod->imports[ii].kind == 1 && mod->imports[ii].index == static_cast<uint32_t>(t)) {
                        table_is_import = true;
                        break;
                    }
                }
                if (table_is_import) continue;

                uint32_t min_size = static_cast<uint32_t>(mod->table_sizes[t]);
                if (min_size > 0) {
                    uint32_t *t_ptr = static_cast<uint32_t *>(pool_->Allocate(min_size * sizeof(uint32_t)));
                    if (!t_ptr) return WasmResult::kErrorOutOfMemory;
                    for (uint32_t k = 0; k < min_size; ++k) t_ptr[k] = 0xFFFFFFFF;
                    mod->tables[t] = t_ptr;
                }
            }

            // --- 4. Element セグメントの適用 ---
            for (std::size_t e = 0; e < mod->elem_segment_count; ++e) {
                if (!mod->elem_segment_is_active[e] || mod->elem_segment_dropped[e]) continue;
                uint32_t tidx = mod->elem_segment_table_indices[e];
                uint32_t offset = mod->elem_segment_offsets[e];
                uint32_t nfuncs = mod->elem_segment_sizes[e];
                if (tidx >= mod->table_count) {
                    return WasmResult::kErrorInstantiate;
                }
                if (static_cast<uint64_t>(offset) + nfuncs > mod->table_sizes[tidx]) {
                    return WasmResult::kErrorInstantiate;
                }
                if (nfuncs == 0) {
                    mod->elem_segment_dropped[e] = true;
                    continue;
                }
                bool is_funcref = (mod->table_types[tidx] == WasmType::kFuncRef);
                for (uint32_t f = 0; f < nfuncs; ++f) {
                    uint32_t val = mod->elem_segments[e] ? mod->elem_segments[e][f] : 0xFFFFFFFF;
                    mod->tables[tidx][offset + f] = is_funcref ? EncodeFuncRef(this, mod, val) : val;
                }
                mod->elem_segment_dropped[e] = true;
                if (mod->elem_segments[e]) {
                    pool_->Free(mod->elem_segments[e]);
                    mod->elem_segments[e] = nullptr;
                }
                mod->elem_segment_sizes[e] = 0;
            }

            // --- 5. Data セグメントの適用 ---
            for (std::size_t d = 0; d < mod->data_segment_count; ++d) {
                if (!mod->data_segment_is_active[d] || mod->data_segment_dropped[d]) continue;
                uint32_t offset = mod->data_segment_offsets[d];
                uint32_t dsize = mod->data_segment_sizes[d];
                uint64_t end_off = static_cast<uint64_t>(offset) + dsize;
                if (end_off > mod->linear_memory_size) {
                    return WasmResult::kErrorInstantiate;
                }
                if (dsize == 0) {
                    mod->data_segment_dropped[d] = true;
                    continue;
                }
                if (!mod->linear_memory_ptr) {
                    return WasmResult::kErrorInstantiate;
                }
                std::memcpy(mod->linear_memory_ptr + offset, mod->data_segments[d], dsize);
                mod->data_segment_dropped[d] = true;
                mod->data_segments[d] = nullptr;
                mod->data_segment_sizes[d] = 0;
            }

            // --- 6. スタート関数の実行 ---
            if (mod->start_function_index != -1) {
                WasmResult res;
#if EMBWASM_ENABLE_MULTITHREADING
                uint32_t tid = SetupMainThread(mod, static_cast<uint32_t>(mod->start_function_index));
                if (tid == 0) return WasmResult::kErrorOutOfMemory;
                res = RunInternal(RunInternalFlags::kNone);
#else
                if (ctx_) {
                    ctx_->Reset();
                    ctx_->state = ThreadState::kRunning;
                    ctx_->stack_top = 0;
                    ctx_->call_stack_top = 0;
                    res = ExecuteInternal(mod, static_cast<uint32_t>(mod->start_function_index));
                    ctx_->state = ThreadState::kTerminated;
                } else {
                    res = WasmResult::kErrorOutOfMemory;
                }
#endif
                if (res != WasmResult::kOk) return res;
            }

            mod->is_instantiated = true;
        }
        return last_error;
    }

    void WasmEngine::FreeModuleInstance(WasmModuleInstance *mod) noexcept {
        if (!pool_ || !mod || !mod->is_active) return;

        // elem_segments 内のデータを解放
        for (std::size_t i = 0; i < mod->elem_segment_count; ++i) {
            if (mod->elem_segments && mod->elem_segments[i]) {
                pool_->Free(mod->elem_segments[i]);
                mod->elem_segments[i] = nullptr;
            }
        }

        // テーブルデータの解放
        if (mod->tables) {
            for (std::size_t i = 0; i < mod->table_count; ++i) {
                if (mod->tables[i] && !(mod->is_table_shared && mod->is_table_shared[i])) {
                    pool_->Free(mod->tables[i]);
                }
            }
        }

        // メタデータ配列の解放
        if (mod->tables) {
            pool_->Free(mod->tables);
            mod->tables = nullptr;
        }
        if (mod->table_sizes) {
            pool_->Free(mod->table_sizes);
            mod->table_sizes = nullptr;
        }
        if (mod->table_max_sizes) {
            pool_->Free(mod->table_max_sizes);
            mod->table_max_sizes = nullptr;
        }
        if (mod->table_types) {
            pool_->Free(mod->table_types);
            mod->table_types = nullptr;
        }
        if (mod->is_table_shared) {
            pool_->Free(mod->is_table_shared);
            mod->is_table_shared = nullptr;
        }
        if (mod->table_import_modules) {
            pool_->Free(mod->table_import_modules);
            mod->table_import_modules = nullptr;
        }
        if (mod->table_import_module_lens) {
            pool_->Free(mod->table_import_module_lens);
            mod->table_import_module_lens = nullptr;
        }
        if (mod->table_import_fields) {
            pool_->Free(mod->table_import_fields);
            mod->table_import_fields = nullptr;
        }
        if (mod->table_import_field_lens) {
            pool_->Free(mod->table_import_field_lens);
            mod->table_import_field_lens = nullptr;
        }
        mod->table_count = 0;

        if (mod->data_segments) {
            pool_->Free(mod->data_segments);
            mod->data_segments = nullptr;
        }
        if (mod->data_segment_sizes) {
            pool_->Free(mod->data_segment_sizes);
            mod->data_segment_sizes = nullptr;
        }
        if (mod->data_segment_dropped) {
            pool_->Free(mod->data_segment_dropped);
            mod->data_segment_dropped = nullptr;
        }
        if (mod->data_segment_offsets) {
            pool_->Free(mod->data_segment_offsets);
            mod->data_segment_offsets = nullptr;
        }
        if (mod->data_segment_offset_global_refs) {
            pool_->Free(mod->data_segment_offset_global_refs);
            mod->data_segment_offset_global_refs = nullptr;
        }
        if (mod->data_segment_is_active) {
            pool_->Free(mod->data_segment_is_active);
            mod->data_segment_is_active = nullptr;
        }
        mod->data_segment_count = 0;

        if (mod->elem_segments) {
            pool_->Free(mod->elem_segments);
            mod->elem_segments = nullptr;
        }
        if (mod->elem_segment_sizes) {
            pool_->Free(mod->elem_segment_sizes);
            mod->elem_segment_sizes = nullptr;
        }
        if (mod->elem_segment_dropped) {
            pool_->Free(mod->elem_segment_dropped);
            mod->elem_segment_dropped = nullptr;
        }
        if (mod->elem_segment_table_indices) {
            pool_->Free(mod->elem_segment_table_indices);
            mod->elem_segment_table_indices = nullptr;
        }
        if (mod->elem_segment_offsets) {
            pool_->Free(mod->elem_segment_offsets);
            mod->elem_segment_offsets = nullptr;
        }
        if (mod->elem_segment_offset_global_refs) {
            pool_->Free(mod->elem_segment_offset_global_refs);
            mod->elem_segment_offset_global_refs = nullptr;
        }
        if (mod->elem_segment_is_active) {
            pool_->Free(mod->elem_segment_is_active);
            mod->elem_segment_is_active = nullptr;
        }
        mod->elem_segment_count = 0;

        // signatures, functions, exports, globals の解放
        if (mod->signatures) {
            for (std::size_t i = 0; i < mod->signature_count; ++i) {
                if (mod->signatures[i]) pool_->Free(mod->signatures[i]);
            }
            pool_->Free(mod->signatures);
            mod->signatures = nullptr;
        }
        mod->signature_count = 0;
        if (mod->functions) {
            for (std::size_t i = 0; i < mod->function_count; ++i) {
                if (mod->functions[i].kind == WasmFunctionKind::kLocal &&
                    mod->functions[i].local.block_jump_table) {
                    pool_->Free(mod->functions[i].local.block_jump_table);
                }
            }
            pool_->Free(mod->functions);
            mod->functions = nullptr;
        }
        mod->function_count = 0;
        if (mod->exports) {
            pool_->Free(mod->exports);
            mod->exports = nullptr;
        }
        mod->export_count = 0;
        if (mod->imports) {
            pool_->Free(mod->imports);
            mod->imports = nullptr;
        }
        mod->import_count = 0;
        if (mod->globals) {
            pool_->Free(mod->globals);
            mod->globals = nullptr;
        }
        mod->global_count = 0;

        // 線形メモリの解放
        if (mod->linear_memory_ptr) {
            if (!mod->is_memory_shared) {
                pool_->Free(mod->linear_memory_ptr);
            }
            mod->linear_memory_ptr = nullptr;
        }

        pool_->Free(mod);
    }

    void WasmEngine::Deinit() noexcept {
        if (pool_) {
            UnloadAllModules();
            DeinitializeAllHostModules(*this);
            if (module_user_datas_) {
                pool_->Free(module_user_datas_);
            }
            PlatformEngineDeinit(*this);
#if !EMBWASM_ENABLE_MULTITHREADING
            if (ctx_) {
                ctx_->DeInit(*pool_);
                pool_->Free(ctx_);
                ctx_ = nullptr;
            }
#endif
#if EMBWASM_ENABLE_MULTITHREADING
            if (threads_) {
                for (std::size_t i = 0; i < kMaxThreads; ++i) {
                    if (threads_[i]) {
                        threads_[i]->DeInit(*pool_);
                        pool_->Free(threads_[i]);
                        threads_[i] = nullptr;
                    }
                }
                pool_->Free(threads_);
            }
#endif
        }
        module_user_datas_ = nullptr;
        user_data_ = nullptr;
        platform_data_ = nullptr;
        last_loaded_id_ = -1;
        pool_ = nullptr;
#if EMBWASM_ENABLE_MULTITHREADING
        threads_ = nullptr;
        InitListNode(&ready_list_);
        InitListNode(&timeout_list_);
        for (std::size_t i = 0; i < kMaxEvents; ++i) {
            events_[i].Reset();
            events_[i].id = static_cast<uint32_t>(i + 1);
        }
#endif
    }

    void WasmEngine::UnloadAllModules() noexcept {
        if (!pool_) return;
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (modules_[i]) {
                FreeModuleInstance(modules_[i]);
                modules_[i] = nullptr;
            }
        }
        for (ListNode* n = name_aliases_.next; n != &name_aliases_; ) {
            ListNode* next_n = n->next;
            pool_->Free(n);
            n = next_n;
        }
        name_alias_count_ = 0;
        InitListNode(&name_aliases_);
        last_loaded_id_ = -1;
    }

    void WasmEngine::RegisterAlias(const char *real_name, std::size_t real_name_len, const char *alias_name,
                                   std::size_t alias_name_len) noexcept {
        if (!alias_name) return;

        WasmModuleInstance *mod;
        if (real_name == nullptr) {
            mod = GetModuleInstanceById(last_loaded_id_);
        } else {
            mod = nullptr;
            for (std::size_t i = kMaxModules; i-- > 0;) {
                if (modules_[i] && modules_[i]->is_active &&
                    StrEq(modules_[i]->name, modules_[i]->name_len, real_name, real_name_len)) {
                    mod = modules_[i];
                    break;
                }
            }
        }
        if (!mod) return;

        for (ListNode* n = name_aliases_.next; n != &name_aliases_; n = n->next) {
            NameAlias* a = reinterpret_cast<NameAlias*>(n);
            if (StrEq(a->alias, a->alias_len, alias_name, alias_name_len)) {
                a->module = mod;
                return;
            }
        }
        std::size_t alen = alias_name_len;
        auto* entry = static_cast<NameAlias*>(pool_->Allocate(sizeof(NameAlias) + alen));
        if (!entry) return;
        std::memcpy(entry->alias, alias_name, alen);
        entry->alias[alen] = '\0';
        entry->alias_len = alen;
        entry->module = mod;
        AddLastListNode(&name_aliases_, &entry->node);
        ++name_alias_count_;
    }

    int32_t WasmEngine::LoadModule(const char *module_name, std::size_t module_name_len, const uint8_t *binary,
                                   std::size_t size) noexcept {
        if (!pool_) return static_cast<int32_t>(WasmResult::kErrorInvalidOperation);
        if (module_name != nullptr && module_name_len >= 64) {
            return static_cast<int32_t>(WasmResult::kErrorInvalidArgument);
        }

        if (size < 8) return static_cast<int32_t>(WasmResult::kErrorParseInvalidMagic);

        // マジックナンバー "\0asm" の検証
        if (binary[0] != 0x00 || binary[1] != 0x61 || binary[2] != 0x73 || binary[3] != 0x6d) {
            return static_cast<int32_t>(WasmResult::kErrorParseInvalidMagic);
        }
        // バージョン 1 の検証
        if (binary[4] != 0x01 || binary[5] != 0x00 || binary[6] != 0x00 || binary[7] != 0x00) {
            return static_cast<int32_t>(WasmResult::kErrorParseInvalidVersion);
        }

        // 空いているスロットを探す（既存の同名モジュールはそのまま保持）
        WasmModuleInstance *mod = nullptr;
        int32_t slot_idx = -1;
        for (std::size_t i = 0; i < kMaxModules; ++i) {
            if (!modules_[i]) {
                slot_idx = static_cast<int32_t>(i);
                break;
            }
        }

        if (slot_idx < 0) {
            return static_cast<int32_t>(WasmResult::kErrorTooManyModules);
        }

        // プールから新しいインスタンスを確保
        mod = static_cast<WasmModuleInstance *>(pool_->Allocate(sizeof(WasmModuleInstance)));
        if (!mod) {
            return static_cast<int32_t>(WasmResult::kErrorOutOfMemory);
        }
        ClearModuleInstance(*mod);
        modules_[slot_idx] = mod;

        // スロットの初期化
        // ParseSections中のEncodeFuncRefがこのモジュールのスロットを見つけられるよう先にtrueにする。
        // 失敗時はFreeModuleInstance()でmod本体ごと解放される。
        mod->is_active = true;
        mod->self_index = static_cast<uint32_t>(slot_idx);
        mod->imports_resolved = false;
        if (module_name && module_name_len > 0) {
            std::size_t nlen = module_name_len < sizeof(mod->name) - 1 ? module_name_len : sizeof(mod->name) - 1;
            std::memcpy(mod->name, module_name, nlen);
            mod->name[nlen] = '\0';
            mod->name_len = nlen;
        } else {
            mod->name[0] = '\0';
            mod->name_len = 0;
        }

        WasmResult res = ParseSections(mod, binary + 8, size - 8);
        if (res != WasmResult::kOk) {
            FreeModuleInstance(mod);
            modules_[slot_idx] = nullptr;
            return static_cast<int32_t>(res);
        }

        // 各ロードされた関数の module メンバに mod をセット
        for (std::size_t i = 0; i < mod->function_count; ++i) {
            mod->functions[i].module = mod;
        }

        mod->is_active = true;
        mod->is_instantiated = false;
        mod->imports_resolved = false;

        last_loaded_id_ = slot_idx;
        return slot_idx;
    }

    static void SkipLebBytes(const uint8_t*& p, const uint8_t* lim) noexcept {
        while (p < lim && (*p & 0x80)) ++p;
        if (p < lim) ++p;
    }

    // =============================================================================
    // 事前検査 (Validate / ValidateFunctionBody)
    // Load() から ParseSections() 完了後に呼ばれる。
    // 検査内容:
    //   1. 全関数の type_index が signature_count_ 内に収まること (型整合性)
    //   2. 各内部関数の total_locals (引数 + ローカル変数) が kMaxLocals 以下であること
    //   3. 各内部関数のバイトコードを線形スキャンし、
    //      最大ラベルネスト深度が kWasmValidationMaxLabelDepth 以下であること
    //      最大スタック深度が kWasmValidationMaxStack 以下であること
    //      local.get/set/tee のインデックスが total_locals 内に収まること
    //      global.get/set のインデックスが global_count_ 内に収まること
    //      call/call_indirect の関数・型インデックスが有効範囲内であること
    // 算出した max_label_depth / max_stack_depth は WasmFunction::InternalFunc に格納する。
    // =============================================================================

    WasmResult WasmEngine::ValidateFunctionBody(WasmModuleInstance *mod, uint32_t func_idx) noexcept {
        if (!mod) return WasmResult::kErrorInvalidArgument;
        WasmTypeSignature **signatures = mod->signatures;
        std::size_t signature_count = mod->signature_count;
        WasmFunction *functions = mod->functions;
        std::size_t function_count = mod->function_count;
        std::size_t global_count = mod->global_count;
        bool has_memory_ = mod->has_memory;
        WasmType *table_types = mod->table_types;
        std::size_t table_count = mod->table_count;

        WasmFunction &func = functions[func_idx];
        if (func.kind != WasmFunctionKind::kLocal) return WasmResult::kOk;

        if (func.type_index >= signature_count) return WasmResult::kErrorValidationFailed;
        const WasmTypeSignature *sig = signatures[func.type_index];

        uint32_t total_locals = sig->param_count + func.local.local_count;
        // total_locals の上限チェックはここでは行わない。
        // Engine全体で共有 Locals プールをフレームごとに切り出す設計へ移行予定のため。

        const uint8_t *ip = func.local.code_ptr;
        const uint8_t *limit = ip + func.local.code_size;

        // ラベルスタック (バリデーション専用)
        // [0] = 関数全体の暗黙ブロック、[1..] = ネストされたブロック
        struct ValLabel {
            int32_t  stack_at_entry;
            uint32_t result_count;
            uint32_t jump_idx;  // block/if: index into tmp_jumps; UINT32_MAX for loop/outer
            uint8_t  opcode;    // 0x02=block, 0x03=loop, 0x04=if, 0x00=outer
        };
        // 事前スキャン: 各オペコードのイミディエイトを正確にスキップしながら
        // 実際の最大ラベルネスト深度を計測し、プールから最低限確保する。
        uint32_t pre_top = 1, pre_max = 1; // pre_top = label_top 相当
        uint32_t block_if_count = 0;  // block(0x02) / if(0x04) のみカウント（ジャンプテーブルに必要）
        {
            const uint8_t* p = ip;
            while (p < limit) {
                uint8_t b = *p++;
                switch (b) {
                    case 0x02: case 0x03: case 0x04: // block/loop/if
                        SkipLebBytes(p, limit);           // block_type sleb128
                        if (pre_top >= kWasmValidationMaxLabelDepth)
                            return WasmResult::kErrorValidationFailed;
                        ++pre_top;
                        if (pre_top > pre_max) pre_max = pre_top;
                        if (b != 0x03) ++block_if_count;  // loop は除外
                        break;
                    case 0x0B: // end
                        if (pre_top > 1) --pre_top;
                        break;
                    case 0x0C: case 0x0D: case 0x10:
                    case 0x20: case 0x21: case 0x22:
                    case 0x23: case 0x24: case 0x25: case 0x26: case 0xD2:
                        SkipLebBytes(p, limit); break;    // 1 varuint32
                    case 0x0E: {                      // br_table
                        uint32_t n = 0, s = 0;
                        while (p < limit && (*p & 0x80)) { n |= uint32_t(*p++ & 0x7F) << s; s += 7; }
                        if (p < limit) n |= uint32_t(*p++) << s;
                        for (uint32_t i = 0; i <= n && p < limit; ++i) SkipLebBytes(p, limit);
                        break;
                    }
                    case 0x11:                        // call_indirect: 2 varuint32
                        SkipLebBytes(p, limit); SkipLebBytes(p, limit); break;
                    case 0x1C: {                      // select t*
                        uint32_t n = 0, s = 0;
                        while (p < limit && (*p & 0x80)) { n |= uint32_t(*p++ & 0x7F) << s; s += 7; }
                        if (p < limit) n |= uint32_t(*p++) << s;
                        if (n <= uint32_t(limit - p)) p += n; else p = limit;
                        break;
                    }
                    case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
                    case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x32: case 0x33:
                    case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
                    case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E:
                        SkipLebBytes(p, limit); SkipLebBytes(p, limit); break; // align + offset
                    case 0x3F: case 0x40:
                        if (p < limit) ++p; break;    // memory idx (1 byte)
                    case 0x41: case 0xD0:
                        SkipLebBytes(p, limit); break;    // varint32
                    case 0x42:
                        SkipLebBytes(p, limit); break;    // varint64
                    case 0x43:
                        if (uint32_t(limit - p) >= 4) p += 4; else p = limit; break; // f32
                    case 0x44:
                        if (uint32_t(limit - p) >= 8) p += 8; else p = limit; break; // f64
                    case 0xFC: {
                        uint32_t sub = 0, s = 0;
                        while (p < limit && (*p & 0x80)) { sub |= uint32_t(*p++ & 0x7F) << s; s += 7; }
                        if (p < limit) sub |= uint32_t(*p++) << s;
                        if (sub == 8 || sub == 10 || sub == 12 || sub == 14)
                            { SkipLebBytes(p, limit); SkipLebBytes(p, limit); }
                        else if (sub == 9 || sub == 11 || sub == 13 ||
                                 sub == 15 || sub == 16 || sub == 17)
                            SkipLebBytes(p, limit);
                        break;
                    }
                    default: break; // イミディエイトなし
                }
            }
        }
        ValLabel* val_labels = static_cast<ValLabel*>(
            pool_->Allocate(pre_max * sizeof(ValLabel)));
        if (!val_labels) return WasmResult::kErrorOutOfMemory;
        BlockJumpEntry* tmp_jumps = nullptr;
        if (block_if_count > 0) {
            tmp_jumps = static_cast<BlockJumpEntry*>(
                pool_->Allocate(block_if_count * sizeof(BlockJumpEntry)));
            if (!tmp_jumps) {
                pool_->Free(val_labels);
                return WasmResult::kErrorOutOfMemory;
            }
        }
        WasmResult result = WasmResult::kOk;
        uint32_t label_top = 1;
        uint32_t max_label_depth = 1;
        val_labels[0] = {0, sig->result_count, UINT32_MAX, 0x00};
        uint32_t next_jump_idx = 0;

        int32_t stack_depth = 0;
        int32_t max_stack_depth = 0;
        bool is_unreachable = false;

        while (ip < limit) {
            if (stack_depth > max_stack_depth) max_stack_depth = stack_depth;

            uint8_t op = *ip++;
            switch (op) {
                case 0x00: // unreachable
                    is_unreachable = true;
                    break;
                case 0x01: // nop
                    break;

                case 0x02: // block
                case 0x03: {
                    // loop
                    int32_t block_type = DecodeVarInt32(ip, limit);
                    uint32_t param_count = 0;
                    uint32_t result_count = 0;
                    if (block_type >= 0) {
                        uint32_t bt = static_cast<uint32_t>(block_type);
                        if (bt < signature_count) {
                            param_count = signatures[bt]->param_count;
                            result_count = signatures[bt]->result_count;
                        }
                    } else if (block_type >= -17 && block_type <= -1) {
                        result_count = 1;
                    }
                    if (label_top >= kWasmValidationMaxLabelDepth) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    int32_t entry;
                    if (stack_depth < static_cast<int32_t>(param_count)) {
                        if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                        entry = 0;
                    } else {
                        entry = stack_depth - static_cast<int32_t>(param_count);
                    }
                    if (op == 0x02 && tmp_jumps) {
                        tmp_jumps[next_jump_idx] = {
                            static_cast<uint32_t>(ip - func.local.code_ptr), 0, 0};
                        val_labels[label_top++] = {entry, result_count, next_jump_idx++, 0x02};
                    } else {
                        val_labels[label_top++] = {entry, result_count, UINT32_MAX, op};
                    }
                    if (label_top > max_label_depth) max_label_depth = label_top;
                    break;
                }

                case 0x04: {
                    // if
                    int32_t block_type = DecodeVarInt32(ip, limit);
                    uint32_t result_count = 0;
                    if (block_type >= 0) {
                        uint32_t bt = static_cast<uint32_t>(block_type);
                        if (bt < signature_count) {
                            result_count = signatures[bt]->result_count;
                        }
                    } else if (block_type >= -17 && block_type <= -1) {
                        result_count = 1;
                    }
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    } // 条件値をポップ
                    if (label_top >= kWasmValidationMaxLabelDepth) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (tmp_jumps) {
                        tmp_jumps[next_jump_idx] = {
                            static_cast<uint32_t>(ip - func.local.code_ptr), 0, 0};
                        val_labels[label_top++] = {stack_depth, result_count, next_jump_idx++, 0x04};
                    } else {
                        val_labels[label_top++] = {stack_depth, result_count, UINT32_MAX, 0x04};
                    }
                    if (label_top > max_label_depth) max_label_depth = label_top;
                    break;
                }

                case 0x05: // else: then-ブランチ終了 → スタックをif進入時の深度に戻す
                    if (label_top > 0) {
                        const ValLabel &elbl = val_labels[label_top - 1];
                        stack_depth = elbl.stack_at_entry;
                        if (tmp_jumps && elbl.jump_idx != UINT32_MAX)
                            tmp_jumps[elbl.jump_idx].else_offset =
                                static_cast<uint32_t>(ip - func.local.code_ptr);
                    }
                    is_unreachable = false;
                    break;

                case 0x0B: {
                    // end
                    if (label_top > 0) {
                        const ValLabel &lbl = val_labels[--label_top];
                        stack_depth = lbl.stack_at_entry + static_cast<int32_t>(lbl.result_count);
                        if (tmp_jumps && lbl.jump_idx != UINT32_MAX)
                            tmp_jumps[lbl.jump_idx].end_offset =
                                static_cast<uint32_t>(ip - func.local.code_ptr);
                    }
                    is_unreachable = false;
                    break;
                }

                case 0x0C: {
                    // br
                    uint32_t label_idx = DecodeVarUint32(ip, limit);
                    if (label_idx >= label_top) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    is_unreachable = true;
                    break;
                }

                case 0x0D: {
                    // br_if
                    uint32_t label_idx = DecodeVarUint32(ip, limit);
                    if (label_idx >= label_top) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    }
                    break;
                }

                case 0x0E: {
                    // br_table
                    uint32_t target_count = DecodeVarUint32(ip, limit);
                    for (uint32_t i = 0; i <= target_count; ++i) {
                        uint32_t target = DecodeVarUint32(ip, limit);
                        if (target >= label_top) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    }
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    }
                    is_unreachable = true;
                    break;
                }

                case 0x0F: // return: ポリモーフィック
                    is_unreachable = true;
                    break;

                case 0x10: {
                    // call
                    uint32_t fidx = DecodeVarUint32(ip, limit);
                    if (fidx >= function_count) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    uint32_t ti = functions[fidx].type_index;
                    if (ti >= signature_count) { result = WasmResult::kErrorValidationFailed; goto cleanup; } {
                        int32_t p = static_cast<int32_t>(signatures[ti]->param_count);
                        if (stack_depth < p) {
                            if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                            stack_depth = 0;
                        } else { stack_depth -= p; }
                    }
                    stack_depth += static_cast<int32_t>(signatures[ti]->result_count);
                    break;
                }

                case 0x11: {
                    // call_indirect
                    uint32_t type_idx = DecodeVarUint32(ip, limit);
                    uint32_t table_idx = 0;
                    if (ip < limit) table_idx = *ip++;
                    if (type_idx >= signature_count) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (table_count == 0 || table_idx >= table_count) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    // テーブルが必要
                    if (table_types[table_idx] != WasmType::kFuncRef) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    // funcrefテーブルが必要
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    } // 要素インデックスをポップ
                    {
                        int32_t p = static_cast<int32_t>(signatures[type_idx]->param_count);
                        if (stack_depth < p) {
                            if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                            stack_depth = 0;
                        } else { stack_depth -= p; }
                    }
                    stack_depth += static_cast<int32_t>(signatures[type_idx]->result_count);
                    break;
                }

                case 0x1A: // drop
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    }
                    break;

                case 0x1B: // select
                    if (stack_depth < 2) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth -= 2;
                    }
                    break;

                case 0x1C: {
                    // select t*
                    if (ip >= limit) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    uint32_t tc = DecodeVarUint32(ip, limit);
                    if (tc > static_cast<uint32_t>(limit - ip)) {
                        result = WasmResult::kErrorValidationFailed; goto cleanup;
                    }
                    ip += tc;
                    if (stack_depth < 2) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth -= 2;
                    }
                    break;
                }

                case 0x20: {
                    // local.get
                    uint32_t lidx = DecodeVarUint32(ip, limit);
                    if (lidx >= total_locals) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    stack_depth++;
                    break;
                }
                case 0x21: {
                    // local.set
                    uint32_t lidx = DecodeVarUint32(ip, limit);
                    if (lidx >= total_locals) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    }
                    break;
                }
                case 0x22: {
                    // local.tee
                    uint32_t lidx = DecodeVarUint32(ip, limit);
                    if (lidx >= total_locals) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    break;
                }
                case 0x23: {
                    // global.get
                    uint32_t gidx = DecodeVarUint32(ip, limit);
                    if (gidx >= global_count) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    stack_depth++;
                    break;
                }
                case 0x24: {
                    // global.set
                    uint32_t gidx = DecodeVarUint32(ip, limit);
                    if (gidx >= global_count) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    }
                    break;
                }

                case 0x25: // table.get: pop idx, push ref → net 0
                    DecodeVarUint32(ip, limit);
                    break;
                case 0x26: // table.set: pop idx + ref → -2
                    DecodeVarUint32(ip, limit);
                    if (stack_depth < 2) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth -= 2;
                    }
                    break;

                // Memory loads: align + offset immediates, pop addr push value → net 0
                case 0x28:
                case 0x29:
                case 0x2A:
                case 0x2B:
                case 0x2C:
                case 0x2D:
                case 0x2E:
                case 0x2F:
                case 0x30:
                case 0x31:
                case 0x32:
                case 0x33:
                case 0x34:
                case 0x35: {
                    if (!has_memory_) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    uint32_t align = DecodeVarUint32(ip, limit);
                    DecodeVarUint32(ip, limit); // offset
                    // アラインメントは log2(アクセスバイト数) 以下でなければならない
                    uint32_t max_align = (op == 0x29 || op == 0x2B)
                                             ? 3u
                                             : (op == 0x2E || op == 0x2F || op == 0x32 || op == 0x33)
                                                   ? 1u
                                                   : (op == 0x28 || op == 0x2A || op == 0x34 || op == 0x35)
                                                         ? 2u
                                                         : (op == 0x2C || op == 0x2D || op == 0x30 || op == 0x31)
                                                               ? 0u
                                                               : 0u;
                    if (align > max_align) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    break;
                }

                // Memory stores: align + offset immediates, pop addr + value → -2
                case 0x36:
                case 0x37:
                case 0x38:
                case 0x39:
                case 0x3A:
                case 0x3B:
                case 0x3C:
                case 0x3D:
                case 0x3E: {
                    if (!has_memory_) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    uint32_t align = DecodeVarUint32(ip, limit);
                    DecodeVarUint32(ip, limit);
                    uint32_t max_align = (op == 0x37 || op == 0x39)
                                             ? 3u
                                             : (op == 0x3B || op == 0x3D)
                                                   ? 1u
                                                   : (op == 0x36 || op == 0x38 || op == 0x3E)
                                                         ? 2u
                                                         : (op == 0x3A || op == 0x3C)
                                                               ? 0u
                                                               : 0u;
                    if (align > max_align) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (stack_depth < 2) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth -= 2;
                    }
                    break;
                }

                case 0x3F: // memory.size: push size → +1
                    if (!has_memory_) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (ip < limit) ip++;
                    stack_depth++;
                    break;
                case 0x40: // memory.grow: pop count, push old_size → net 0
                    if (!has_memory_) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (ip < limit) ip++;
                    break;

                case 0x41: DecodeVarInt32(ip, limit);
                    stack_depth++;
                    break; // i32.const
                case 0x42: DecodeVarInt64(ip, limit);
                    stack_depth++;
                    break; // i64.const
                case 0x43: // f32.const
                    if (static_cast<uint32_t>(limit - ip) >= 4) ip += 4;
                    stack_depth++;
                    break;
                case 0x44: // f64.const
                    if (static_cast<uint32_t>(limit - ip) >= 8) ip += 8;
                    stack_depth++;
                    break;

                // 単項演算・型変換: net 0
                case 0x45: // i32.eqz
                case 0x50: // i64.eqz
                case 0x67:
                case 0x68:
                case 0x69: // i32 clz/ctz/popcnt
                case 0x79:
                case 0x7A:
                case 0x7B: // i64 clz/ctz/popcnt
                case 0x8B:
                case 0x8C:
                case 0x8D:
                case 0x8E:
                case 0x8F:
                case 0x90:
                case 0x91: // f32 unary
                case 0x99:
                case 0x9A:
                case 0x9B:
                case 0x9C:
                case 0x9D:
                case 0x9E:
                case 0x9F: // f64 unary
                case 0xA7:
                case 0xA8:
                case 0xA9:
                case 0xAA:
                case 0xAB: // i32.trunc_f*
                case 0xAC:
                case 0xAD: // i64.extend_i32_s/u
                case 0xAE:
                case 0xAF:
                case 0xB0:
                case 0xB1: // i64.trunc_f*
                case 0xB2:
                case 0xB3:
                case 0xB4:
                case 0xB5: // f32.convert_i*
                case 0xB6: // f32.demote_f64
                case 0xB7:
                case 0xB8:
                case 0xB9:
                case 0xBA: // f64.convert_i*
                case 0xBB: // f64.promote_f32
                case 0xBC:
                case 0xBD:
                case 0xBE:
                case 0xBF: // reinterpret
                case 0xC0:
                case 0xC1:
                case 0xC2:
                case 0xC3:
                case 0xC4: // sign extension
                case 0xD1: // ref.is_null
                    break;

                // 二項演算・比較: net -1
                case 0x46:
                case 0x47:
                case 0x48:
                case 0x49:
                case 0x4A:
                case 0x4B:
                case 0x4C:
                case 0x4D:
                case 0x4E:
                case 0x4F: // i32 compare
                case 0x51:
                case 0x52:
                case 0x53:
                case 0x54:
                case 0x55:
                case 0x56:
                case 0x57:
                case 0x58:
                case 0x59:
                case 0x5A: // i64 compare
                case 0x5B:
                case 0x5C:
                case 0x5D:
                case 0x5E:
                case 0x5F:
                case 0x60: // f32 compare
                case 0x61:
                case 0x62:
                case 0x63:
                case 0x64:
                case 0x65:
                case 0x66: // f64 compare
                case 0x6A:
                case 0x6B:
                case 0x6C:
                case 0x6D:
                case 0x6E:
                case 0x6F:
                case 0x70:
                case 0x71:
                case 0x72:
                case 0x73:
                case 0x74:
                case 0x75:
                case 0x76:
                case 0x77:
                case 0x78: // i32 binary
                case 0x7C:
                case 0x7D:
                case 0x7E:
                case 0x7F:
                case 0x80:
                case 0x81:
                case 0x82:
                case 0x83:
                case 0x84:
                case 0x85:
                case 0x86:
                case 0x87:
                case 0x88:
                case 0x89:
                case 0x8A: // i64 binary
                case 0x92:
                case 0x93:
                case 0x94:
                case 0x95:
                case 0x96:
                case 0x97:
                case 0x98: // f32 binary
                case 0xA0:
                case 0xA1:
                case 0xA2:
                case 0xA3:
                case 0xA4:
                case 0xA5:
                case 0xA6: // f64 binary
                    if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                        stack_depth--;
                    }
                    break;

                case 0xD0: // ref.null
                    DecodeVarInt32(ip, limit); // heap type
                    stack_depth++;
                    break;
                case 0xD2: { // ref.func
                    uint32_t ref_func_idx = DecodeVarUint32(ip, limit);
                    if (ref_func_idx >= function_count) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    if (ref_func_idx >= kWasmMaxFuncRefIndex) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
                    stack_depth++;
                    break;
                }

                case 0xFC: {
                    // 拡張命令
                    uint32_t sub_op = DecodeVarUint32(ip, limit);
                    if (sub_op <= 7) {
                        // 飽和トランケーション (0-7): 単項変換 net 0
                    } else if (sub_op == 8 || sub_op == 12) {
                        // memory.init / table.init: 2 immediates, net -3
                        DecodeVarUint32(ip, limit);
                        DecodeVarUint32(ip, limit);
                        if (stack_depth < 3) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                            stack_depth -= 3;
                        }
                    } else if (sub_op == 9 || sub_op == 13) {
                        // data.drop / elem.drop: 1 immediate, net 0
                        DecodeVarUint32(ip, limit);
                    } else if (sub_op == 10) {
                        // memory.copy: 2 immediates (dst_mem, src_mem), net -3
                        DecodeVarUint32(ip, limit);
                        DecodeVarUint32(ip, limit);
                        if (stack_depth < 3) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                            stack_depth -= 3;
                        }
                    } else if (sub_op == 11) {
                        // memory.fill: 1 immediate (mem_idx), net -3
                        DecodeVarUint32(ip, limit);
                        if (stack_depth < 3) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                            stack_depth -= 3;
                        }
                    } else if (sub_op == 14) {
                        // table.copy: 2 immediates, net -3
                        DecodeVarUint32(ip, limit);
                        DecodeVarUint32(ip, limit);
                        if (stack_depth < 3) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                            stack_depth -= 3;
                        }
                    } else if (sub_op == 15) {
                        // table.grow: 1 immediate, pop ref + count push old_size → net -1
                        DecodeVarUint32(ip, limit);
                        if (stack_depth < 1) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                            stack_depth--;
                        }
                    } else if (sub_op == 16) {
                        // table.size: 1 immediate, push size → net +1
                        DecodeVarUint32(ip, limit);
                        stack_depth++;
                    } else if (sub_op == 17) {
                        // table.fill: 1 immediate, net -3
                        DecodeVarUint32(ip, limit);
                        if (stack_depth < 3) { if (!is_unreachable) { result = WasmResult::kErrorValidationFailed; goto cleanup; } } else {
                            stack_depth -= 3;
                        }
                    } else {
                        result = WasmResult::kErrorValidationFailed; goto cleanup;
                    }
                    break;
                }

                default:
                    result = WasmResult::kErrorValidationFailed; goto cleanup;
            }
        }

        if (stack_depth > max_stack_depth) max_stack_depth = stack_depth;

        // 制限超過チェック (スキャン中に既にチェック済みだが念のため)
        if (max_label_depth > kWasmValidationMaxLabelDepth) { result = WasmResult::kErrorValidationFailed; goto cleanup; }
        if (max_stack_depth > static_cast<int32_t>(kWasmValidationMaxStack))
            { result = WasmResult::kErrorValidationFailed; goto cleanup; }

        // 算出値を関数メンバーに記録
        func.local.max_label_depth = static_cast<uint16_t>(max_label_depth);
        func.local.max_stack_depth = static_cast<uint32_t>(max_stack_depth < 0 ? 0 : max_stack_depth);

        // ジャンプテーブルを永続領域にコピー（tmp_jumps は cleanup で解放する一時バッファ）
        if (result == WasmResult::kOk && block_if_count > 0 && tmp_jumps) {
            auto *tbl = static_cast<BlockJumpEntry*>(
                pool_->Allocate(block_if_count * sizeof(BlockJumpEntry)));
            if (!tbl) {
                result = WasmResult::kErrorOutOfMemory;
            } else {
                std::memcpy(tbl, tmp_jumps, block_if_count * sizeof(BlockJumpEntry));
                func.local.block_jump_table = tbl;
                func.local.block_count = block_if_count;
            }
        }

    cleanup:
        if (tmp_jumps) pool_->Free(tmp_jumps);
        pool_->Free(val_labels);
        return result;
    }

    WasmResult WasmEngine::Validate(WasmModuleInstance *mod) noexcept {
        if (!mod) return WasmResult::kErrorInvalidArgument;
        WasmTypeSignature **signatures = mod->signatures;
        std::size_t signature_count = mod->signature_count;
        WasmFunction *functions = mod->functions;
        std::size_t function_count = mod->function_count;
        WasmExportEntry *exports = mod->exports;
        std::size_t export_count = mod->export_count;
        int32_t start_function_index = mod->start_function_index;

        // 1. 型インデックスの整合性チェック (全関数)
        for (std::size_t i = 0; i < function_count; ++i) {
            if (functions[i].type_index >= signature_count) {
                return WasmResult::kErrorValidationFailed;
            }
        }

        // 3. メモリセクション: initial ≤ 65536 かつ initial ≤ maximum
        if (mod->has_memory) {
            uint32_t initial_pages = mod->memory_min_pages;
            if (initial_pages > 65536) {
                return WasmResult::kErrorValidationFailed;
            }
            if (mod->max_linear_memory_pages != 0 && initial_pages > mod->max_linear_memory_pages) {
                return WasmResult::kErrorValidationFailed;
            }
        }

        // 4. エクスポートセクション: 各インデックスが有効範囲内であること
        for (std::size_t i = 0; i < export_count; ++i) {
            if (exports[i].kind == 0 && exports[i].index >= function_count) {
                return WasmResult::kErrorValidationFailed;
            }
            if (exports[i].kind == 1 && exports[i].index >= mod->table_count) {
                return WasmResult::kErrorValidationFailed;
            }
            if (exports[i].kind == 2 && (!mod->has_memory || exports[i].index != 0)) {
                return WasmResult::kErrorValidationFailed;
            }
            if (exports[i].kind == 3 && exports[i].index >= mod->global_count) {
                return WasmResult::kErrorValidationFailed;
            }
        }

        // 4b. エレメントセグメント: テーブルインデックス・関数インデックスの検証
        for (std::size_t i = 0; i < mod->elem_segment_count; ++i) {
            if (mod->elem_segment_is_active[i] &&
                mod->elem_segment_table_indices[i] >= mod->table_count) {
                return WasmResult::kErrorValidationFailed;
            }
            uint32_t *elems = mod->elem_segments[i];
            uint32_t seg_size = mod->elem_segment_sizes[i];
            for (uint32_t f = 0; f < seg_size; ++f) {
                if (elems && elems[f] != 0xFFFFFFFF) {
                    if (elems[f] >= function_count) return WasmResult::kErrorValidationFailed;
                    if (elems[f] >= kWasmMaxFuncRefIndex) return WasmResult::kErrorValidationFailed;
                }
            }
        }

        // 5. スタート関数: インデックスが有効かつシグネチャが [] → []
        if (start_function_index != -1) {
            uint32_t si = static_cast<uint32_t>(start_function_index);
            if (si >= function_count) {
                return WasmResult::kErrorValidationFailed;
            }
            uint32_t ti = functions[si].type_index;
            if (ti >= signature_count) {
                return WasmResult::kErrorValidationFailed;
            }
            const WasmTypeSignature *sig = signatures[ti];
            if (sig->param_count != 0 || sig->result_count != 0) {
                return WasmResult::kErrorValidationFailed;
            }
        }

        // 6. 各内部関数のバイトコード検査
        for (std::size_t i = 0; i < function_count; ++i) {
            if (functions[i].kind == WasmFunctionKind::kLocal) {
                WasmResult r = ValidateFunctionBody(mod, static_cast<uint32_t>(i));
                if (r != WasmResult::kOk) return r;
            }
        }

        // 7. __stack_pointer / __data_end / cabi_realloc の検索
        mod->stack_ptr_global_idx  = UINT32_MAX;
        mod->data_end_global_idx   = UINT32_MAX;
        mod->cabi_realloc_func_idx = UINT32_MAX;
        mod->thread_stack_size     = 0;
        for (std::size_t i = 0; i < export_count; ++i) {
            const WasmExportEntry& e = exports[i];
            if (e.kind == 3 && StrEq(e.name, e.name_len, "__stack_pointer", 15))
                mod->stack_ptr_global_idx = e.index;
            else if (e.kind == 3 && StrEq(e.name, e.name_len, "__data_end", 10))
                mod->data_end_global_idx = e.index;
            else if (e.kind == 0 && StrEq(e.name, e.name_len, "cabi_realloc", 12))
                mod->cabi_realloc_func_idx = e.index;
        }
        if (mod->stack_ptr_global_idx != UINT32_MAX) {
            if (mod->cabi_realloc_func_idx == UINT32_MAX)
                return WasmResult::kErrorValidationFailed;
            uint32_t sp_init = static_cast<uint32_t>(
                mod->globals[mod->stack_ptr_global_idx].value.value.i32);
            if (mod->data_end_global_idx != UINT32_MAX) {
                uint32_t de = static_cast<uint32_t>(
                    mod->globals[mod->data_end_global_idx].value.value.i32);
                if (sp_init > de) {
                    // data-first layout: stack is placed after data
                    mod->thread_stack_size = sp_init - de;
                } else {
                    // stack-first layout (LLVM default): sp_init == stack_size
                    mod->thread_stack_size = sp_init;
                }
            } else {
                mod->thread_stack_size = config_.thread_wasm_stack_size;
            }
        }

        return WasmResult::kOk;
    }

    // =========================================================
    // Section parsing helpers
    // =========================================================

    struct SectionCounts {
        std::size_t type_count   = 0;
        std::size_t func_count   = 0;
        std::size_t export_count = 0;
        std::size_t import_count = 0;
        std::size_t global_count = 0;
        std::size_t table_count  = 0;
        std::size_t data_count   = 0;
        std::size_t elem_count   = 0;
    };

    struct ParseContext {
        WasmModuleInstance* mod;
        WasmMemoryPool*     pool;
        std::size_t         sig_idx;
        std::size_t         func_idx;
        std::size_t         exp_idx;
        std::size_t         imp_idx;
        std::size_t         glob_idx;
        uint32_t            code_index_offset;
    };

    static WasmResult ParseTypeSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t type_count = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < type_count; ++i) {
            if (ptr >= end) return WasmResult::kErrorParseOthers;
            uint8_t form = *ptr++;
            if (form != 0x60) return WasmResult::kErrorParseUnknownSection;
            uint32_t param_count = DecodeVarUint32(ptr, end);
            if (param_count > kWasmMaxParamCount) return WasmResult::kErrorValidationFailed;
            const uint8_t* params_start = ptr;
            for (uint32_t p = 0; p < param_count; ++p) {
                if (ptr >= end) return WasmResult::kErrorParseOthers;
                ++ptr;
            }
            uint32_t result_count = DecodeVarUint32(ptr, end);
            if (result_count > kWasmMaxResultCount) return WasmResult::kErrorValidationFailed;
            const uint8_t* results_start = ptr;
            for (uint32_t r = 0; r < result_count; ++r) {
                if (ptr >= end) return WasmResult::kErrorParseOthers;
                ++ptr;
            }
            if (ctx.sig_idx >= ctx.mod->signature_count) return WasmResult::kErrorOutOfMemory;
            std::size_t sig_size = WasmTypeSignature::ByteSize(param_count, result_count);
            auto* sig = static_cast<WasmTypeSignature*>(ctx.pool->Allocate(sig_size));
            if (!sig) return WasmResult::kErrorOutOfMemory;
            std::memset(sig, 0, sig_size);
            sig->param_count  = static_cast<uint16_t>(param_count);
            sig->result_count = static_cast<uint16_t>(result_count);
            for (uint32_t p = 0; p < param_count; ++p)
                sig->SetParam(p, static_cast<WasmType>(params_start[p]));
            for (uint32_t r = 0; r < result_count; ++r)
                sig->SetResult(r, static_cast<WasmType>(results_start[r]));
            ctx.mod->signatures[ctx.sig_idx++] = sig;
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseImportSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t import_count = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < import_count; ++i) {
            uint32_t mod_len = DecodeVarUint32(ptr, end);
            if (mod_len > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
            const char* mod_name = reinterpret_cast<const char*>(ptr);
            ptr += mod_len;
            uint32_t field_len = DecodeVarUint32(ptr, end);
            if (field_len > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
            const char* field_name = reinterpret_cast<const char*>(ptr);
            ptr += field_len;
            if (ptr >= end) return WasmResult::kErrorParseOthers;
            uint8_t kind = *ptr++;
            WasmImportEntry entry = {};
            entry.module_name     = mod_name;
            entry.module_name_len = mod_len;
            entry.field_name      = field_name;
            entry.field_name_len  = field_len;
            entry.kind            = kind;
            switch (kind) {
                case 0x00: {
                    uint32_t type_idx = DecodeVarUint32(ptr, end);
                    if (ctx.func_idx >= ctx.mod->function_count) return WasmResult::kErrorOutOfMemory;
                    entry.index = static_cast<uint32_t>(ctx.func_idx);
                    entry.desc.func.type_index = type_idx;
                    ctx.func_idx++;
                    ctx.code_index_offset++;
                    break;
                }
                case 0x01: {
                    uint8_t elem_type = *ptr++;
                    uint8_t flags     = *ptr++;
                    uint32_t min_size = DecodeVarUint32(ptr, end);
                    uint32_t max_size = 0xFFFFFFFF;
                    if (flags & 0x01) max_size = DecodeVarUint32(ptr, end);
                    entry.index = static_cast<uint32_t>(ctx.mod->table_count);
                    entry.desc.table.elem_type = elem_type;
                    entry.desc.table.min_size  = min_size;
                    entry.desc.table.max_size  = max_size;
                    if (ctx.mod->table_count < ctx.mod->table_capacity) ctx.mod->table_count++;
                    break;
                }
                case 0x02: {
                    uint8_t flags       = *ptr++;
                    uint32_t min_pages  = DecodeVarUint32(ptr, end);
                    uint32_t max_pages  = 0;
                    if (flags & 0x01) max_pages = DecodeVarUint32(ptr, end);
                    entry.index = 0;
                    entry.desc.mem.min_pages = min_pages;
                    entry.desc.mem.max_pages = max_pages;
                    break;
                }
                case 0x03: {
                    if (ptr + 2 > end) return WasmResult::kErrorParseOthers;
                    uint8_t value_type  = *ptr++;
                    bool    is_mutable  = (*ptr++ != 0);
                    entry.index = static_cast<uint32_t>(ctx.glob_idx);
                    entry.desc.global.value_type = value_type;
                    entry.desc.global.is_mutable = is_mutable;
                    if (ctx.glob_idx < ctx.mod->global_count) ctx.glob_idx++;
                    break;
                }
                default: break;
            }
            if (ctx.mod->imports && ctx.imp_idx < ctx.mod->import_count) {
                ctx.mod->imports[ctx.imp_idx++] = entry;
            }
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseFunctionSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t num_funcs = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < num_funcs; ++i) {
            uint32_t type_idx = DecodeVarUint32(ptr, end);
            if (ctx.func_idx >= ctx.mod->function_count) return WasmResult::kErrorOutOfMemory;
            ctx.mod->functions[ctx.func_idx].kind             = WasmFunctionKind::kLocal;
            ctx.mod->functions[ctx.func_idx].type_index       = type_idx;
            ctx.mod->functions[ctx.func_idx].local.code_ptr   = nullptr;
            ctx.mod->functions[ctx.func_idx].local.code_size  = 0;
            ctx.mod->functions[ctx.func_idx].local.local_count = 0;
            ctx.func_idx++;
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseTableSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t num_tables = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < num_tables; ++i) {
            uint8_t elem_type = *ptr++;
            if (elem_type != 0x70 && elem_type != 0x6F) return WasmResult::kErrorParseOthers;
            uint8_t  flags    = *ptr++;
            uint32_t min_size = DecodeVarUint32(ptr, end);
            uint32_t max_size = 0xFFFFFFFF;
            if (flags & 0x01) max_size = DecodeVarUint32(ptr, end);
            if (ctx.mod->table_count < ctx.mod->table_capacity) {
                ctx.mod->table_sizes[ctx.mod->table_count]          = min_size;
                ctx.mod->table_max_sizes[ctx.mod->table_count]      = max_size;
                ctx.mod->table_types[ctx.mod->table_count]          = static_cast<WasmType>(elem_type);
                ctx.mod->tables[ctx.mod->table_count]               = nullptr;
                ctx.mod->is_table_shared[ctx.mod->table_count]      = false;
                ctx.mod->table_import_modules[ctx.mod->table_count] = nullptr;
                ctx.mod->table_import_fields[ctx.mod->table_count]  = nullptr;
                ctx.mod->table_count++;
            } else {
                return WasmResult::kErrorOutOfMemory;
            }
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseMemorySection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t mem_count = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < mem_count; ++i) {
            uint8_t  flags         = *ptr++;
            uint32_t initial_pages = DecodeVarUint32(ptr, end);
            uint32_t maximum_pages = 0;
            if (flags & 0x01) maximum_pages = DecodeVarUint32(ptr, end);
            ctx.mod->has_memory              = true;
            ctx.mod->memory_is_imported      = false;
            ctx.mod->memory_min_pages        = initial_pages;
            ctx.mod->max_linear_memory_pages = maximum_pages;
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseGlobalSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t num_globals = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < num_globals; ++i) {
            if (ctx.glob_idx >= ctx.mod->global_count) return WasmResult::kErrorOutOfMemory;
            WasmType type       = static_cast<WasmType>(*ptr++);
            bool     is_mutable = (*ptr++ != 0);
            uint8_t  opcode     = *ptr++;
            WasmValue val;
            uint32_t  init_ref = 0xFFFFFFFFu;
            if (opcode == 0x41) {
                val.value.i32 = DecodeVarInt32(ptr, end);
            } else if (opcode == 0x42) {
                val.value.i64 = DecodeVarInt64(ptr, end);
            } else if (opcode == 0x43) {
                if (4 > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
                std::memcpy(&val.value.f32, ptr, 4); ptr += 4;
            } else if (opcode == 0x44) {
                if (8 > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
                std::memcpy(&val.value.f64, ptr, 8); ptr += 8;
            } else if (opcode == 0x23) {
                uint32_t idx = DecodeVarUint32(ptr, end);
                if (idx >= ctx.glob_idx) return WasmResult::kErrorParseOthers;
                val      = ctx.mod->globals[idx].value;
                init_ref = idx;
            } else if (opcode == 0xD0) {
                int32_t heap_type = DecodeVarInt32(ptr, end);
                (void)heap_type;
                val.value.i64 = -1;
            } else if (opcode == 0xD2) {
                uint32_t ref_func_idx = DecodeVarUint32(ptr, end);
                val.value.i64 = static_cast<int64_t>(ref_func_idx);
            } else {
                return WasmResult::kErrorParseOthers;
            }
            if (ptr >= end || *ptr++ != 0x0B) return WasmResult::kErrorParseOthers;
            ctx.mod->globals[ctx.glob_idx++] = {type, is_mutable, val, init_ref};
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseExportSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t num_exports = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < num_exports; ++i) {
            uint32_t name_len = DecodeVarUint32(ptr, end);
            if (name_len > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
            const char* name = reinterpret_cast<const char*>(ptr);
            ptr += name_len;
            if (ptr >= end) return WasmResult::kErrorParseOthers;
            uint8_t  kind = *ptr++;
            uint32_t idx  = DecodeVarUint32(ptr, end);
            if (ctx.exp_idx >= ctx.mod->export_count) return WasmResult::kErrorOutOfMemory;
            ctx.mod->exports[ctx.exp_idx++] = {name, name_len, kind, idx};
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseDataSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t data_count = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < data_count; ++i) {
            uint32_t seg_flags = DecodeVarUint32(ptr, end);
            bool     is_passive = (seg_flags == 1) || (seg_flags == 3);
            uint32_t offset = 0;
            if (!is_passive) {
                if (seg_flags == 2) DecodeVarUint32(ptr, end);
                if (ptr >= end) return WasmResult::kErrorParseOthers;
                uint8_t  opcode           = *ptr++;
                uint32_t offset_global_ref = 0xFFFFFFFFu;
                if (opcode == 0x41) {
                    offset = static_cast<uint32_t>(DecodeVarInt32(ptr, end));
                } else if (opcode == 0x23) {
                    uint32_t gidx = DecodeVarUint32(ptr, end);
                    offset_global_ref = gidx;
                    if (gidx < ctx.glob_idx)
                        offset = static_cast<uint32_t>(ctx.mod->globals[gidx].value.value.i32);
                } else {
                    return WasmResult::kErrorParseOthers;
                }
                if (ptr >= end || *ptr++ != 0x0B) return WasmResult::kErrorParseOthers;
                if (ctx.mod->data_segment_offset_global_refs)
                    ctx.mod->data_segment_offset_global_refs[ctx.mod->data_segment_count] = offset_global_ref;
            }
            uint32_t data_size = DecodeVarUint32(ptr, end);
            if (data_size > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
            if (ctx.mod->data_segment_count < ctx.mod->data_segment_capacity) {
                ctx.mod->data_segments[ctx.mod->data_segment_count]          = ptr;
                ctx.mod->data_segment_sizes[ctx.mod->data_segment_count]     = data_size;
                ctx.mod->data_segment_dropped[ctx.mod->data_segment_count]   = false;
                ctx.mod->data_segment_offsets[ctx.mod->data_segment_count]   = offset;
                ctx.mod->data_segment_is_active[ctx.mod->data_segment_count] = !is_passive;
                ctx.mod->data_segment_count++;
            } else {
                return WasmResult::kErrorOutOfMemory;
            }
            ptr += data_size;
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseElementSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t num_elems = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < num_elems; ++i) {
            if (ptr >= end) return WasmResult::kErrorParseOthers;
            uint32_t flags     = DecodeVarUint32(ptr, end);
            uint32_t table_idx = 0;
            bool     has_offset = false;
            if ((flags & 1) == 0) {
                has_offset = true;
                if ((flags & 2) == 2) table_idx = DecodeVarUint32(ptr, end);
            } else {
                if ((flags & 2) == 2) { uint8_t ref_type = *ptr++; (void)ref_type; }
                else                  { uint8_t kind     = *ptr++; (void)kind;     }
            }
            uint32_t offset            = 0;
            uint32_t offset_global_ref = 0xFFFFFFFFu;
            if (has_offset) {
                uint8_t opcode = *ptr++;
                if (opcode == 0x41) {
                    offset = static_cast<uint32_t>(DecodeVarInt32(ptr, end));
                } else if (opcode == 0x23) {
                    uint32_t global_idx = DecodeVarUint32(ptr, end);
                    offset_global_ref = global_idx;
                    if (global_idx < ctx.glob_idx)
                        offset = ctx.mod->globals[global_idx].value.value.i32;
                } else {
                    return WasmResult::kErrorParseOthers;
                }
                if (ptr >= end || *ptr++ != 0x0B) return WasmResult::kErrorParseOthers;
            }
            if (has_offset && (flags & 2) == 2) {
                if (ptr >= end) return WasmResult::kErrorParseOthers;
                uint8_t elemkind_or_reftype = *ptr++;
                (void)elemkind_or_reftype;
            }
            uint32_t  num_funcs = DecodeVarUint32(ptr, end);
            uint32_t* elem_arr  = nullptr;
            if (num_funcs > 0) {
                elem_arr = static_cast<uint32_t*>(ctx.pool->Allocate(num_funcs * sizeof(uint32_t)));
                if (!elem_arr) return WasmResult::kErrorOutOfMemory;
            }
            if ((flags & 4) == 4) {
                for (uint32_t f = 0; f < num_funcs; ++f) {
                    if (ptr >= end) { if (elem_arr) ctx.pool->Free(elem_arr); return WasmResult::kErrorParseOthers; }
                    uint8_t  op  = *ptr++;
                    uint32_t val = 0xFFFFFFFF;
                    if      (op == 0xD2) { val = DecodeVarUint32(ptr, end); }
                    else if (op == 0xD0) { uint8_t type = *ptr++; (void)type; }
                    if (ptr >= end || *ptr++ != 0x0B) { if (elem_arr) ctx.pool->Free(elem_arr); return WasmResult::kErrorParseOthers; }
                    if (elem_arr) elem_arr[f] = val;
                }
            } else {
                for (uint32_t f = 0; f < num_funcs; ++f) {
                    uint32_t fidx = DecodeVarUint32(ptr, end);
                    if (elem_arr) elem_arr[f] = fidx;
                }
            }
            if (ctx.mod->elem_segment_count < ctx.mod->elem_segment_capacity) {
                bool is_declarative = !has_offset && ((flags & 2) == 2);
                ctx.mod->elem_segments[ctx.mod->elem_segment_count]                   = elem_arr;
                ctx.mod->elem_segment_sizes[ctx.mod->elem_segment_count]              = num_funcs;
                ctx.mod->elem_segment_dropped[ctx.mod->elem_segment_count]            = is_declarative;
                ctx.mod->elem_segment_table_indices[ctx.mod->elem_segment_count]      = table_idx;
                ctx.mod->elem_segment_offsets[ctx.mod->elem_segment_count]            = offset;
                ctx.mod->elem_segment_offset_global_refs[ctx.mod->elem_segment_count] = offset_global_ref;
                ctx.mod->elem_segment_is_active[ctx.mod->elem_segment_count]          = has_offset;
                ctx.mod->elem_segment_count++;
            } else {
                if (elem_arr) ctx.pool->Free(elem_arr);
                return WasmResult::kErrorOutOfMemory;
            }
        }
        return WasmResult::kOk;
    }

    static WasmResult ParseCodeSection(ParseContext& ctx, const uint8_t*& ptr, const uint8_t* end) noexcept {
        uint32_t code_count = DecodeVarUint32(ptr, end);
        for (uint32_t i = 0; i < code_count; ++i) {
            uint32_t body_size = DecodeVarUint32(ptr, end);
            if (body_size > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
            const uint8_t* body_end    = ptr + body_size;
            uint32_t       local_decls = DecodeVarUint32(ptr, body_end);
            uint32_t       local_count = 0;
            for (uint32_t j = 0; j < local_decls; ++j) {
                uint32_t count = DecodeVarUint32(ptr, body_end);
                if (ptr >= body_end) return WasmResult::kErrorParseOthers;
                ++ptr; // type byte (値は不要、カウントのみ使用)
                if (count > kWasmValidationMaxLocals - local_count) return WasmResult::kErrorParseOthers;
                local_count += count;
            }
            uint32_t code_func_idx = ctx.code_index_offset + i;
            if (code_func_idx >= ctx.mod->function_count) return WasmResult::kErrorParseOthers;
            ctx.mod->functions[code_func_idx].local.code_ptr    = ptr;
            ctx.mod->functions[code_func_idx].local.code_size   = static_cast<uint32_t>(body_end - ptr);
            ctx.mod->functions[code_func_idx].local.local_count = static_cast<uint16_t>(local_count);
            ptr = body_end;
        }
        return WasmResult::kOk;
    }

    static WasmResult CountSectionEntries(const uint8_t* binary, std::size_t size, SectionCounts& counts) noexcept {
        uint16_t       seen = 0;
        const uint8_t* p   = binary;
        const uint8_t* e   = binary + size;
        while (p < e) {
            uint8_t  sid = *p++;
            if (p >= e) break;
            uint32_t ssz = DecodeVarUint32(p, e);
            if (ssz > static_cast<std::size_t>(e - p)) break;
            const uint8_t* se = p + ssz;
            if (sid != 0 && sid <= 15) {
                uint16_t bit = static_cast<uint16_t>(1u << sid);
                if (seen & bit) return WasmResult::kErrorParseOthers;
                seen |= bit;
            }
            switch (sid) {
                case 1: counts.type_count = DecodeVarUint32(p, se); break;
                case 2: {
                    uint32_t n = DecodeVarUint32(p, se);
                    counts.import_count = n;
                    for (uint32_t i = 0; i < n && p < se; ++i) {
                        uint32_t mlen = DecodeVarUint32(p, se);
                        if (mlen > static_cast<std::size_t>(se - p)) goto count_next;
                        p += mlen;
                        uint32_t flen = DecodeVarUint32(p, se);
                        if (flen > static_cast<std::size_t>(se - p)) goto count_next;
                        p += flen;
                        if (p >= se) goto count_next;
                        uint8_t k = *p++;
                        switch (k) {
                            case 0x00: DecodeVarUint32(p, se); counts.func_count++; break;
                            case 0x01: {
                                if (p >= se) goto count_next;
                                p++;
                                if (p >= se) goto count_next;
                                { uint8_t f = *p++; DecodeVarUint32(p, se); if (f & 1) DecodeVarUint32(p, se); }
                                counts.table_count++;
                                break;
                            }
                            case 0x02: {
                                if (p >= se) goto count_next;
                                { uint8_t f = *p++; DecodeVarUint32(p, se); if (f & 1) DecodeVarUint32(p, se); }
                                break;
                            }
                            case 0x03: {
                                if (p + 2 > se) goto count_next;
                                p++; p++;
                                counts.global_count++;
                                break;
                            }
                            default: goto count_next;
                        }
                    }
                    break;
                }
                case 3:  counts.func_count   += DecodeVarUint32(p, se); break;
                case 4:  counts.table_count  += DecodeVarUint32(p, se); break;
                case 6:  counts.global_count += DecodeVarUint32(p, se); break;
                case 7:  counts.export_count  = DecodeVarUint32(p, se); break;
                case 9:  counts.elem_count    = DecodeVarUint32(p, se); break;
                case 11: counts.data_count    = DecodeVarUint32(p, se); break;
                default: break;
            }
        count_next:
            p = se;
        }
        return WasmResult::kOk;
    }

    static WasmResult AllocateSectionBuffers(WasmModuleInstance* mod, WasmMemoryPool* pool,
                                              const SectionCounts& counts) noexcept {
        mod->signature_count = counts.type_count;
        mod->signatures = counts.type_count > 0
            ? static_cast<WasmTypeSignature**>(pool->Allocate(counts.type_count * sizeof(WasmTypeSignature*)))
            : nullptr;
        if (counts.type_count > 0 && !mod->signatures) return WasmResult::kErrorOutOfMemory;
        if (mod->signatures) std::memset(mod->signatures, 0, counts.type_count * sizeof(WasmTypeSignature*));

        mod->function_count = counts.func_count;
        mod->functions = counts.func_count > 0
            ? static_cast<WasmFunction*>(pool->Allocate(counts.func_count * sizeof(WasmFunction)))
            : nullptr;
        if (counts.func_count > 0 && !mod->functions) return WasmResult::kErrorOutOfMemory;
        if (mod->functions) std::memset(mod->functions, 0, counts.func_count * sizeof(WasmFunction));

        mod->export_count = counts.export_count;
        mod->exports = counts.export_count > 0
            ? static_cast<WasmExportEntry*>(pool->Allocate(counts.export_count * sizeof(WasmExportEntry)))
            : nullptr;
        if (counts.export_count > 0 && !mod->exports) return WasmResult::kErrorOutOfMemory;
        if (mod->exports) std::memset(mod->exports, 0, counts.export_count * sizeof(WasmExportEntry));

        mod->import_count = counts.import_count;
        mod->imports = counts.import_count > 0
            ? static_cast<WasmImportEntry*>(pool->Allocate(counts.import_count * sizeof(WasmImportEntry)))
            : nullptr;
        if (counts.import_count > 0 && !mod->imports) return WasmResult::kErrorOutOfMemory;
        if (mod->imports) std::memset(mod->imports, 0, counts.import_count * sizeof(WasmImportEntry));

        mod->global_count = counts.global_count;
        mod->globals = counts.global_count > 0
            ? static_cast<WasmGlobal*>(pool->Allocate(counts.global_count * sizeof(WasmGlobal)))
            : nullptr;
        if (counts.global_count > 0 && !mod->globals) return WasmResult::kErrorOutOfMemory;
        if (mod->globals) {
            std::memset(mod->globals, 0, counts.global_count * sizeof(WasmGlobal));
            for (std::size_t g = 0; g < counts.global_count; ++g) mod->globals[g].init_global_ref = 0xFFFFFFFFu;
        }

        mod->linear_memory_ptr       = nullptr;
        mod->linear_memory_size      = 0;
        mod->linear_memory_capacity  = 0;
        mod->max_linear_memory_pages = 0;
        mod->is_memory_shared        = false;

        mod->table_capacity = counts.table_count;
        mod->table_count    = 0;
        if (counts.table_count > 0) {
            mod->tables              = static_cast<uint32_t**>(pool->Allocate(counts.table_count * sizeof(uint32_t*)));
            mod->table_sizes         = static_cast<uint32_t*>(pool->Allocate(counts.table_count * sizeof(uint32_t)));
            mod->table_max_sizes     = static_cast<uint32_t*>(pool->Allocate(counts.table_count * sizeof(uint32_t)));
            mod->table_types         = static_cast<WasmType*>(pool->Allocate(counts.table_count * sizeof(WasmType)));
            mod->is_table_shared     = static_cast<uint8_t*>(pool->Allocate(counts.table_count * sizeof(uint8_t)));
            mod->table_import_modules     = static_cast<const char**>(pool->Allocate(counts.table_count * sizeof(const char*)));
            mod->table_import_module_lens = static_cast<uint32_t*>(pool->Allocate(counts.table_count * sizeof(uint32_t)));
            mod->table_import_fields      = static_cast<const char**>(pool->Allocate(counts.table_count * sizeof(const char*)));
            mod->table_import_field_lens  = static_cast<uint32_t*>(pool->Allocate(counts.table_count * sizeof(uint32_t)));
            if (!mod->tables || !mod->table_sizes || !mod->table_max_sizes || !mod->table_types ||
                !mod->is_table_shared || !mod->table_import_modules || !mod->table_import_module_lens ||
                !mod->table_import_fields || !mod->table_import_field_lens) {
                return WasmResult::kErrorOutOfMemory;
            }
            std::memset(mod->tables, 0, counts.table_count * sizeof(uint32_t*));
            std::memset(mod->table_sizes, 0, counts.table_count * sizeof(uint32_t));
            for (std::size_t i = 0; i < counts.table_count; ++i) mod->table_max_sizes[i] = 0xFFFFFFFF;
            std::memset(mod->table_types, 0, counts.table_count * sizeof(WasmType));
            std::memset(mod->is_table_shared, 0, counts.table_count * sizeof(uint8_t));
            std::memset(mod->table_import_modules, 0, counts.table_count * sizeof(const char*));
            std::memset(mod->table_import_module_lens, 0, counts.table_count * sizeof(uint32_t));
            std::memset(mod->table_import_fields, 0, counts.table_count * sizeof(const char*));
            std::memset(mod->table_import_field_lens, 0, counts.table_count * sizeof(uint32_t));
        } else {
            mod->tables = nullptr; mod->table_sizes = nullptr; mod->table_max_sizes = nullptr;
            mod->table_types = nullptr; mod->is_table_shared = nullptr;
            mod->table_import_modules = nullptr; mod->table_import_module_lens = nullptr;
            mod->table_import_fields = nullptr; mod->table_import_field_lens = nullptr;
        }

        mod->data_segment_capacity = counts.data_count;
        mod->data_segment_count    = 0;
        if (counts.data_count > 0) {
            mod->data_segments                 = static_cast<const uint8_t**>(pool->Allocate(counts.data_count * sizeof(const uint8_t*)));
            mod->data_segment_sizes            = static_cast<uint32_t*>(pool->Allocate(counts.data_count * sizeof(uint32_t)));
            mod->data_segment_dropped          = static_cast<uint8_t*>(pool->Allocate(counts.data_count * sizeof(uint8_t)));
            mod->data_segment_offsets          = static_cast<uint32_t*>(pool->Allocate(counts.data_count * sizeof(uint32_t)));
            mod->data_segment_offset_global_refs = static_cast<uint32_t*>(pool->Allocate(counts.data_count * sizeof(uint32_t)));
            mod->data_segment_is_active        = static_cast<uint8_t*>(pool->Allocate(counts.data_count * sizeof(uint8_t)));
            if (!mod->data_segments || !mod->data_segment_sizes || !mod->data_segment_dropped ||
                !mod->data_segment_offsets || !mod->data_segment_offset_global_refs || !mod->data_segment_is_active) {
                return WasmResult::kErrorOutOfMemory;
            }
            std::memset(mod->data_segments, 0, counts.data_count * sizeof(const uint8_t*));
            std::memset(mod->data_segment_sizes, 0, counts.data_count * sizeof(uint32_t));
            std::memset(mod->data_segment_dropped, 0, counts.data_count * sizeof(uint8_t));
            std::memset(mod->data_segment_offsets, 0, counts.data_count * sizeof(uint32_t));
            std::memset(mod->data_segment_offset_global_refs, 0xFF, counts.data_count * sizeof(uint32_t));
            std::memset(mod->data_segment_is_active, 0, counts.data_count * sizeof(uint8_t));
        } else {
            mod->data_segments = nullptr; mod->data_segment_sizes = nullptr;
            mod->data_segment_dropped = nullptr; mod->data_segment_offsets = nullptr;
            mod->data_segment_offset_global_refs = nullptr; mod->data_segment_is_active = nullptr;
        }

        mod->elem_segment_capacity = counts.elem_count;
        mod->elem_segment_count    = 0;
        if (counts.elem_count > 0) {
            mod->elem_segments              = static_cast<uint32_t**>(pool->Allocate(counts.elem_count * sizeof(uint32_t*)));
            mod->elem_segment_sizes         = static_cast<uint32_t*>(pool->Allocate(counts.elem_count * sizeof(uint32_t)));
            mod->elem_segment_dropped       = static_cast<uint8_t*>(pool->Allocate(counts.elem_count * sizeof(uint8_t)));
            mod->elem_segment_table_indices = static_cast<uint32_t*>(pool->Allocate(counts.elem_count * sizeof(uint32_t)));
            mod->elem_segment_offsets              = static_cast<uint32_t*>(pool->Allocate(counts.elem_count * sizeof(uint32_t)));
            mod->elem_segment_offset_global_refs   = static_cast<uint32_t*>(pool->Allocate(counts.elem_count * sizeof(uint32_t)));
            mod->elem_segment_is_active            = static_cast<uint8_t*>(pool->Allocate(counts.elem_count * sizeof(uint8_t)));
            if (!mod->elem_segments || !mod->elem_segment_sizes || !mod->elem_segment_dropped ||
                !mod->elem_segment_table_indices || !mod->elem_segment_offsets ||
                !mod->elem_segment_offset_global_refs || !mod->elem_segment_is_active) {
                return WasmResult::kErrorOutOfMemory;
            }
            std::memset(mod->elem_segments, 0, counts.elem_count * sizeof(uint32_t*));
            std::memset(mod->elem_segment_sizes, 0, counts.elem_count * sizeof(uint32_t));
            std::memset(mod->elem_segment_dropped, 0, counts.elem_count * sizeof(uint8_t));
            std::memset(mod->elem_segment_table_indices, 0, counts.elem_count * sizeof(uint32_t));
            std::memset(mod->elem_segment_offsets, 0, counts.elem_count * sizeof(uint32_t));
            std::memset(mod->elem_segment_offset_global_refs, 0xFF, counts.elem_count * sizeof(uint32_t));
            std::memset(mod->elem_segment_is_active, 0, counts.elem_count * sizeof(uint8_t));
        } else {
            mod->elem_segments = nullptr; mod->elem_segment_sizes = nullptr;
            mod->elem_segment_dropped = nullptr; mod->elem_segment_table_indices = nullptr;
            mod->elem_segment_offsets = nullptr; mod->elem_segment_offset_global_refs = nullptr;
            mod->elem_segment_is_active = nullptr;
        }
        return WasmResult::kOk;
    }

    static WasmResult FillSections(WasmModuleInstance* mod, WasmMemoryPool* pool,
                                    const uint8_t* binary, std::size_t size) noexcept {
        ParseContext ctx = {};
        ctx.mod  = mod;
        ctx.pool = pool;

        const uint8_t* ptr = binary;
        const uint8_t* end = binary + size;

        while (ptr < end) {
            uint8_t  section_id   = *ptr++;
            uint32_t section_size = DecodeVarUint32(ptr, end);
            if (section_size > static_cast<std::size_t>(end - ptr)) return WasmResult::kErrorParseOthers;
            const uint8_t* section_end = ptr + section_size;

            WasmResult r = WasmResult::kOk;
            switch (section_id) {
                case 1:  r = ParseTypeSection(ctx, ptr, section_end);     break;
                case 2:  r = ParseImportSection(ctx, ptr, section_end);   break;
                case 3:  r = ParseFunctionSection(ctx, ptr, section_end); break;
                case 4:  r = ParseTableSection(ctx, ptr, section_end);    break;
                case 5:  r = ParseMemorySection(ctx, ptr, section_end);   break;
                case 6:  r = ParseGlobalSection(ctx, ptr, section_end);   break;
                case 7:  r = ParseExportSection(ctx, ptr, section_end);   break;
                case 8: {
                    uint32_t start_func = DecodeVarUint32(ptr, section_end);
                    mod->start_function_index = static_cast<int32_t>(start_func);
                    break;
                }
                case 9:  r = ParseElementSection(ctx, ptr, section_end);  break;
                case 10: r = ParseCodeSection(ctx, ptr, section_end);     break;
                case 11: r = ParseDataSection(ctx, ptr, section_end);     break;
                default: ptr = section_end; break;
            }
            if (r != WasmResult::kOk) return r;
            ptr = section_end;
        }
        return WasmResult::kOk;
    }

    WasmResult WasmEngine::ParseSections(WasmModuleInstance *mod, const uint8_t *binary, std::size_t size) noexcept {
        if (!mod) return WasmResult::kErrorInvalidArgument;
        SectionCounts counts = {};
        WasmResult r = CountSectionEntries(binary, size, counts);
        if (r != WasmResult::kOk) return r;
        r = AllocateSectionBuffers(mod, pool_, counts);
        if (r != WasmResult::kOk) return r;
        return FillSections(mod, pool_, binary, size);
    }

    WasmResult WasmEngine::Execute(const char *module_name, std::size_t module_name_len, const char *func_name,
                                   std::size_t func_name_len, const WasmValue *args, uint32_t arg_count,
                                   WasmValue *results, uint32_t result_count) noexcept {
        WasmModuleInstance *mod = GetModuleInstance(module_name, module_name_len);
        if (!mod || !mod->is_active) {
            return WasmResult::kErrorModuleNotFound;
        }

        InstantiateModules();

        int32_t func_idx = mod->GetExportFunctionIndex(func_name, func_name_len);
        if (func_idx < 0) {
            return WasmResult::kErrorFunctionNotFound;
        }

        return ExecuteResolved(mod, static_cast<uint32_t>(func_idx), args, arg_count, results, result_count);
    }

    WasmResult WasmEngine::ExecuteByIndex(int32_t instance_id, int32_t func_idx,
                                          const WasmValue *args, uint32_t arg_count,
                                          WasmValue *results, uint32_t result_count) noexcept {
        WasmModuleInstance *mod = GetModuleInstanceById(instance_id);
        if (!mod) return WasmResult::kErrorModuleNotFound;

        // 未インスタンス化の場合のみ InstantiateModules() を実行する。
        // 2回目以降は is_instantiated == true のため、このブランチは通らない。
        if (!mod->is_instantiated) {
            WasmResult inst_res = InstantiateModules();
            if (inst_res != WasmResult::kOk) return inst_res;
        }

        if (func_idx < 0 || static_cast<uint32_t>(func_idx) >= mod->function_count) {
            return WasmResult::kErrorFunctionNotFound;
        }

        return ExecuteResolved(mod, static_cast<uint32_t>(func_idx), args, arg_count, results, result_count);
    }

    WasmResult WasmEngine::ExecuteResolved(WasmModuleInstance* mod, uint32_t func_idx,
                                           const WasmValue* args, uint32_t arg_count,
                                           WasmValue* results, uint32_t result_count) noexcept {
#if EMBWASM_ENABLE_MULTITHREADING
        uint32_t thread_id = SetupMainThread(mod, func_idx);
        if (thread_id == 0) return WasmResult::kErrorOutOfMemory;

        WasmThreadContext* exec_ctx = GetMainThreadContext();
        if (!exec_ctx) return WasmResult::kErrorOutOfMemory;

        exec_ctx->stack_top = 0;
        for (uint32_t i = 0; i < arg_count; ++i) {
            if (exec_ctx->stack_top >= exec_ctx->stack_size) {
                exec_ctx->state = ThreadState::kTerminated;
                return WasmResult::kErrorExecuteTrapStackOverflow;
            }
            exec_ctx->stack[exec_ctx->stack_top++] = args[i];
        }

        WasmResult res = RunInternal(RunInternalFlags::kNone);

        if (res == WasmResult::kOk) {
            const WasmFunction& func = mod->functions[func_idx];
            uint32_t actual_result_count = mod->signatures[func.type_index]->result_count;

            if (result_count > actual_result_count) return WasmResult::kErrorExecuteRuntimeError;
            if (exec_ctx->stack_top < actual_result_count) return WasmResult::kErrorExecuteRuntimeError;

            for (uint32_t i = 0; i < actual_result_count; ++i) {
                WasmValue val = exec_ctx->stack[--exec_ctx->stack_top];
                uint32_t dest = actual_result_count - 1 - i;
                if (dest < result_count) {
                    results[dest] = val;
                }
            }
        }

        return res;
#else
        if (!ctx_) return WasmResult::kErrorOutOfMemory;
        ctx_->Reset();
        ctx_->state = ThreadState::kRunning;
        ctx_->stack_top = 0;
        ctx_->call_stack_top = 0;

        for (uint32_t i = 0; i < arg_count; ++i) {
            if (ctx_->stack_top >= ctx_->stack_size) {
                return WasmResult::kErrorExecuteTrapStackOverflow;
            }
            ctx_->stack[ctx_->stack_top++] = args[i];
        }

        WasmResult res = ExecuteInternal(mod, func_idx);

        if (res == WasmResult::kOk) {
            const WasmFunction& func = mod->functions[func_idx];
            uint32_t actual_result_count = mod->signatures[func.type_index]->result_count;

            if (result_count > actual_result_count) return WasmResult::kErrorExecuteRuntimeError;
            if (ctx_->stack_top < actual_result_count) return WasmResult::kErrorExecuteRuntimeError;

            for (uint32_t i = 0; i < actual_result_count; ++i) {
                WasmValue val = ctx_->stack[--ctx_->stack_top];
                uint32_t dest = actual_result_count - 1 - i;
                if (dest < result_count) {
                    results[dest] = val;
                }
            }
        }

        ctx_->state = ThreadState::kTerminated;
        return res;
#endif
    }

    WasmModuleInstance *WasmEngine::GetModuleInstance(const char *name, std::size_t name_len) noexcept {
        if (!name) return GetModuleInstanceById(last_loaded_id_);
        for (ListNode* n = name_aliases_.next; n != &name_aliases_; n = n->next) {
            const NameAlias* a = reinterpret_cast<const NameAlias*>(n);
            if (StrEq(a->alias, a->alias_len, name, name_len)) return a->module;
        }
        for (std::size_t i = kMaxModules; i-- > 0;) {
            if (modules_[i] && modules_[i]->is_active &&
                StrEq(modules_[i]->name, modules_[i]->name_len, name, name_len)) {
                return modules_[i];
            }
        }
        return nullptr;
    }

    const WasmModuleInstance *WasmEngine::GetModuleInstance(const char *name, std::size_t name_len) const noexcept {
        if (!name) return GetModuleInstanceById(last_loaded_id_);
        for (const ListNode* n = name_aliases_.next; n != &name_aliases_; n = n->next) {
            const NameAlias* a = reinterpret_cast<const NameAlias*>(n);
            if (StrEq(a->alias, a->alias_len, name, name_len)) return a->module;
        }
        for (std::size_t i = kMaxModules; i-- > 0;) {
            if (modules_[i] && modules_[i]->is_active &&
                StrEq(modules_[i]->name, modules_[i]->name_len, name, name_len)) {
                return modules_[i];
            }
        }
        return nullptr;
    }

    int32_t WasmEngine::GetExportFunctionIndex(const char *module_name, std::size_t module_name_len, const char *name,
                                               std::size_t name_len) const noexcept {
        const WasmModuleInstance *mod = GetModuleInstance(module_name, module_name_len);
        if (!mod || !mod->is_active) return -1;
        for (std::size_t i = 0; i < mod->export_count; ++i) {
            if (mod->exports[i].kind == 0 && StrEq(mod->exports[i].name, mod->exports[i].name_len, name, name_len)) {
                return static_cast<int32_t>(mod->exports[i].index);
            }
        }
        return -1;
    }

    uint32_t WasmEngine::GetExportFunctionResultCount(const char *module_name, std::size_t module_name_len,
                                                      const char *name, std::size_t name_len) const noexcept {
        const WasmModuleInstance *mod = GetModuleInstance(module_name, module_name_len);
        if (!mod || !mod->is_active) return 0;
        int32_t func_idx = GetExportFunctionIndex(module_name, module_name_len, name, name_len);
        if (func_idx == -1) return 0;
        const WasmFunction &func = mod->functions[func_idx];
        if (func.type_index < mod->signature_count) {
            return mod->signatures[func.type_index]->result_count;
        }
        return 0;
    }

    int32_t WasmEngine::GetFunctionIndexByExportIndex(int32_t instance_id, uint32_t export_idx) const noexcept {
        const WasmModuleInstance *mod = GetModuleInstanceById(instance_id);
        if (!mod || !mod->is_active) return -1;
        if (export_idx < mod->export_count && mod->exports[export_idx].kind == 0) {
            return static_cast<int32_t>(mod->exports[export_idx].index);
        }
        return -1;
    }

    WasmResult WasmEngine::OnTrap(WasmResult result) noexcept {
        return result;
    }

    // block/if のジャンプテーブルから body_offset に対応するエントリを O(log N) で返す。
    // 見つからない場合は nullptr を返す（正常バイナリでは起こらない）。
    static const BlockJumpEntry* FindBlockJump(
        const WasmLocalFunction &lf, uint32_t body_offset) noexcept
    {
        uint32_t lo = 0, hi = lf.block_count;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (lf.block_jump_table[mid].body_offset < body_offset) lo = mid + 1;
            else hi = mid;
        }
        if (lo < lf.block_count && lf.block_jump_table[lo].body_offset == body_offset)
            return &lf.block_jump_table[lo];
        return nullptr;
    }

    // br/end/return 共通のスタック巻き戻しヘルパー。
    // dst <= src が常に成立するためオーバーラップを考慮しない。
    static inline void StackUnwind(
        WasmValue* stack, std::size_t dst, std::size_t src, uint32_t arity) noexcept
    {
        if (arity == 1) {
            stack[dst] = stack[src];
        } else {
            for (uint32_t i = 0; i < arity; ++i)
                stack[dst + i] = stack[src + i];
        }
    }

    // call / call_indirect 共通のフレーム構築ヘルパー。
    // 成功時は新フレームを call_stack に積み kOk を返す。sp と max_call_stack_depth は更新される。
    // 失敗時は sp / ctx->stack_top をロールバックしてエラーコードを返す。
    static WasmResult PushCallFrame(
        WasmThreadContext *ctx,
        const WasmFunction *target_func,
        WasmModuleInstance *target_mod,
        std::size_t &sp,
        uint32_t &max_call_stack_depth,
        WasmResult call_stack_error
    ) noexcept {
        if (ctx->call_stack_top >= ctx->call_stack_size)
            return call_stack_error;

        const WasmTypeSignature *sig = target_mod->signatures[target_func->type_index];
        uint32_t total_locals = sig->param_count + target_func->local.local_count;

        WasmFrame &new_frame = ctx->call_stack[ctx->call_stack_top++];
        if (ctx->call_stack_top > max_call_stack_depth)
            max_call_stack_depth = ctx->call_stack_top;
        new_frame.func = target_func;
        new_frame.ip = target_func->local.code_ptr;
        new_frame.limit = target_func->local.code_ptr + target_func->local.code_size;
        new_frame.total_locals = total_locals;
        new_frame.label_stack_top = 0;

        const std::size_t locals_base = sp - sig->param_count;
        new_frame.locals = ctx->stack + locals_base;
        ctx->stack_top = locals_base + total_locals;
        sp = ctx->stack_top;
        for (uint32_t i = sig->param_count; i < total_locals; ++i)
            new_frame.locals[i] = WasmValue{};

        if (ctx->stack_top + target_func->local.max_stack_depth > ctx->stack_size) {
            ctx->stack_top = locals_base + sig->param_count;
            sp = ctx->stack_top;
            --ctx->call_stack_top;
            return WasmResult::kErrorExecuteTrapStackOverflow;
        }

        new_frame.label_capacity = target_func->local.max_label_depth;
        if (ctx->labels_pool_top + new_frame.label_capacity > ctx->labels_pool_size) {
            ctx->stack_top = locals_base + sig->param_count;
            sp = ctx->stack_top;
            --ctx->call_stack_top;
            return WasmResult::kErrorExecuteTrapLabelStackOverflow;
        }
        new_frame.labels = ctx->labels_pool + ctx->labels_pool_top;
        ctx->labels_pool_top += new_frame.label_capacity;

        WasmLabel &func_label = new_frame.labels[new_frame.label_stack_top++];
        func_label.opcode = 0x02; // block
        func_label.stack_top = locals_base;
        func_label.param_count = 0;
        func_label.result_count = sig->result_count;
        func_label.pc = new_frame.limit;

        return WasmResult::kOk;
    }

    WasmResult WasmEngine::
    ExecuteInternal(WasmModuleInstance *module, uint32_t func_index) noexcept {
#if EMBWASM_ENABLE_MULTITHREADING
        WasmThreadContext *ctx = GetCurrentThreadContext();
#else
        WasmThreadContext *ctx = ctx_;
#endif
        if (!ctx || !module) {
            return WasmResult::kErrorInvalidArgument;
        }

        // module のエイリアス（初期フレーム構築に使用）
        WasmTypeSignature **signatures = module->signatures;
        WasmFunction *functions = module->functions;
        std::size_t function_count = module->function_count;

        // 前回の続きからでない（新規呼び出し）の場合はスタックをクリア
        if (ctx->call_stack_top == 0) {
            if (func_index >= function_count) {
                return WasmResult::kErrorFunctionNotFound;
            }
            const WasmFunction *initial_func = &functions[func_index];

            // kImportのときはチェーンを辿って実際の関数を得る
            if (initial_func->kind == WasmFunctionKind::kImport) {
                WasmModuleInstance *rm = nullptr;
                const WasmFunction *rf = nullptr;
                if (!ResolveWasmImportChain(this, initial_func, rm, rf)) {
                    return WasmResult::kErrorFunctionNotFound;
                }
                module = rm;
                initial_func = rf;
                signatures = module->signatures;
                functions = module->functions;
                function_count = module->function_count;
            }

            if (initial_func->kind == WasmFunctionKind::kHost) {
                WasmResult res = DispatchHostFunction(*this, initial_func->host.host_func_id, ctx);
                if (res != WasmResult::kOk) return res;
                if (ctx->stack_top > max_stack_depth_) max_stack_depth_ = ctx->stack_top;
                return WasmResult::kOk;
            }

            // 内部関数の最初のフレームをコールスタックに積む
            {
                const WasmTypeSignature *sig = signatures[initial_func->type_index];
                uint32_t total_locals = sig->param_count + initial_func->local.local_count;

                WasmFrame &frame = ctx->call_stack[ctx->call_stack_top++];
                if (ctx->call_stack_top > max_call_stack_depth_) {
                    max_call_stack_depth_ = ctx->call_stack_top;
                }
                frame.func = initial_func;
                frame.ip = initial_func->local.code_ptr;
                frame.limit = initial_func->local.code_ptr + initial_func->local.code_size;
                frame.total_locals = total_locals;
                frame.label_stack_top = 0;

                // 統合スタックにローカル変数領域を確保（引数は既に正しい位置にある）
                if (ctx->stack_top < sig->param_count) {
                    return OnTrap(WasmResult::kErrorExecuteRuntimeError);
                }
                const std::size_t locals_base = ctx->stack_top - sig->param_count;
                frame.locals = ctx->stack + locals_base;
                ctx->stack_top = locals_base + total_locals;
                // 引数以外のローカル変数のみゼロ初期化
                for (uint32_t i = sig->param_count; i < total_locals; ++i) {
                    frame.locals[i] = WasmValue{};
                }

                // ラベルプールからスライスを切り出す
                frame.label_capacity = initial_func->local.max_label_depth;
                if (ctx->labels_pool_top + frame.label_capacity > ctx->labels_pool_size) {
                    ctx->stack_top = locals_base + sig->param_count;
                    --ctx->call_stack_top;
                    return WasmResult::kErrorExecuteTrapLabelStackOverflow;
                }
                frame.labels = ctx->labels_pool + ctx->labels_pool_top;
                ctx->labels_pool_top += frame.label_capacity;

                // 関数全体の暗黙の block ラベルを追加
                {
                    WasmLabel &func_label = frame.labels[frame.label_stack_top++];
                    func_label.opcode = 0x02; // block
                    func_label.stack_top = locals_base;
                    func_label.param_count = 0;
                    func_label.result_count = sig->result_count;
                    func_label.pc = frame.limit;
                }
            }
        }

        {
            WasmResult begin_res = PlatformEngineRunBegin(*this);
            if (begin_res != WasmResult::kOk) return begin_res;
            WasmResult run_res = RunLoop(ctx);
            PlatformEngineRunEnd(*this);
            return run_res;
        }
    }

    WasmResult WasmEngine::RunLoop(WasmThreadContext *ctx) noexcept {
        std::size_t sp = ctx->stack_top;
        WasmResult result = WasmResult::kOk;
        WasmValue *stack = ctx->stack;

        // [Opt 1] モジュール切り替え時のみエイリアスを更新するため外側で宣言
        // linear_memory_* は memory.grow で変化しうるため毎回リロードし除外
        WasmModuleInstance   *loaded_mod          = nullptr;
        WasmTypeSignature   **signatures           = nullptr;
        std::size_t           signature_count      = 0;
        WasmFunction         *functions            = nullptr;
        WasmGlobal           *globals              = nullptr;
        uint32_t            **tables               = nullptr;
        uint32_t             *table_sizes          = nullptr;
        uint32_t             *table_max_sizes      = nullptr;
        WasmType             *table_types          = nullptr;
        std::size_t           table_count          = 0;
        const uint8_t       **data_segments        = nullptr;
        uint32_t             *data_segment_sizes   = nullptr;
        uint8_t              *data_segment_dropped = nullptr;
        std::size_t           data_segment_count   = 0;
        uint32_t            **elem_segments        = nullptr;
        uint32_t             *elem_segment_sizes   = nullptr;
        uint8_t              *elem_segment_dropped = nullptr;
        std::size_t           elem_segment_count   = 0;

        // コールスタックが空になるまで実行ループを回す
        while (ctx->call_stack_top > 0) {
            WasmFrame &frame = ctx->call_stack[ctx->call_stack_top - 1];
            WasmModuleInstance *current_mod = const_cast<WasmModuleInstance *>(frame.func->module);

            if (current_mod != loaded_mod) {
                signatures           = current_mod->signatures;
                signature_count      = current_mod->signature_count;
                functions            = current_mod->functions;
                globals              = current_mod->globals;
                tables               = current_mod->tables;
                table_sizes          = current_mod->table_sizes;
                table_max_sizes      = current_mod->table_max_sizes;
                table_types          = current_mod->table_types;
                table_count          = current_mod->table_count;
                data_segments        = current_mod->data_segments;
                data_segment_sizes   = current_mod->data_segment_sizes;
                data_segment_dropped = current_mod->data_segment_dropped;
                data_segment_count   = current_mod->data_segment_count;
                elem_segments        = current_mod->elem_segments;
                elem_segment_sizes   = current_mod->elem_segment_sizes;
                elem_segment_dropped = current_mod->elem_segment_dropped;
                elem_segment_count   = current_mod->elem_segment_count;
                loaded_mod = current_mod;
            }
            // linear_memory_* は memory.grow で変化しうるため毎回リロード
            uint8_t    *linear_memory_ptr       = current_mod->linear_memory_ptr;
            std::size_t linear_memory_size       = current_mod->linear_memory_size;
            std::size_t linear_memory_capacity   = current_mod->linear_memory_capacity;
            uint32_t    max_linear_memory_pages  = current_mod->max_linear_memory_pages;

            // フレーム状態をローカル変数にキャッシュ
            const uint8_t *ip    = frame.ip;
            const uint8_t *limit = frame.limit;
            WasmValue     *locals = frame.locals;
            sp = ctx->stack_top;
            // [Opt 4] label_stack_top キャッシュ（フレーム遷移前に frame へ同期する）
            std::size_t label_top = frame.label_stack_top;
            // [Opt 6] block/if ジャンプテーブル参照用に code_ptr をキャッシュ
            const uint8_t *code_base = frame.func->local.code_ptr;

            for (;;) {
                uint8_t op = *ip++;
                switch (op) {
                    case 0x00: // unreachable
                        result = WasmResult::kErrorExecuteTrapUnreachable;
                        goto done;

                    case 0x01: // nop
                        break;

                    case 0x02: // block
                    case 0x03: {
                        // loop
                        int32_t block_type = DecodeVarInt32Fast(ip);
                        uint32_t param_count = 0;
                        uint32_t result_count = 0;
                        if (block_type >= 0) {
                            if (static_cast<uint32_t>(block_type) < signature_count) {
                                param_count = signatures[block_type]->param_count;
                                result_count = signatures[block_type]->result_count;
                            }
                        } else if (block_type >= -17 && block_type <= -1) {
                            // -1=i32, -2=i64, -3=f32, -4=f64, -16=funcref, -17=externref, etc.
                            param_count = 0;
                            result_count = 1;
                        } else {
                            param_count = 0;
                            result_count = 0;
                        }

                        WasmLabel &label = frame.labels[label_top++];
                        label.opcode = op;
                        label.stack_top = sp - param_count;
                        label.param_count = param_count;
                        label.result_count = result_count;

                        if (op == 0x02) {
                            // block: 事前計算済みジャンプテーブルから end 位置を O(log N) で取得
                            const BlockJumpEntry *e = FindBlockJump(
                                frame.func->local,
                                static_cast<uint32_t>(ip - code_base));
                            label.pc = e ? code_base + e->end_offset : limit;
                        } else {
                            // loop: br 0 でループ先頭に戻る。ip は block_type の次を指す。
                            label.pc = ip;
                        }
                        break;
                    }

                    case 0x04: {
                        // if
                        int32_t block_type = DecodeVarInt32Fast(ip);
                        uint32_t result_count = 0;
                        if (block_type >= 0) {
                            if (static_cast<uint32_t>(block_type) < signature_count) {
                                result_count = signatures[block_type]->result_count;
                            }
                        } else if (block_type >= -17 && block_type <= -1) {
                            result_count = 1;
                        }

                        int32_t cond = stack[--sp].value.i32;

                        WasmLabel &label = frame.labels[label_top++];
                        label.opcode = 0x04;
                        label.stack_top = sp;
                        label.param_count = 0;
                        label.result_count = result_count;

                        // 事前計算済みジャンプテーブルから else/end 位置を O(log N) で取得
                        const BlockJumpEntry *e = FindBlockJump(
                            frame.func->local,
                            static_cast<uint32_t>(ip - code_base));
                        const uint8_t *else_ptr = nullptr;
                        if (e) {
                            label.pc = code_base + e->end_offset;
                            if (e->else_offset != 0)
                                else_ptr = code_base + e->else_offset;
                        } else {
                            label.pc = limit;
                        }

                        if (cond == 0) {
                            // 条件不成立: else があればそこへ、なければ end の次（label.pc）へ
                            if (else_ptr) {
                                ip = else_ptr;
                            } else {
                                ip = label.pc;
                                label_top--;
                            }
                        }
                        break;
                    }

                    case 0x05: {
                        // else
                        // if ブロックの実行が終わって else に到達した場合は end までジャンプ
                        if (label_top == 0) {
                            result = OnTrap(WasmResult::kErrorExecuteRuntimeError);
                            goto done;
                        }
                        ip = frame.labels[label_top - 1].pc;
                        label_top--; // if ブロックのラベルをポップする
                        break;
                    }

                    case 0x0C: // br <label_idx>
                    case 0x0D: {
                        // br_if <label_idx>
                        uint32_t label_idx = DecodeVarUint32Fast(ip);
                        bool jump = true;
                        if (op == 0x0D) {
                            jump = (stack[--sp].value.i32 != 0);
                        }

                        if (jump) {
                            WasmLabel &target_label = frame.labels[label_top - 1 - label_idx];

                            // データスタックの巻き戻し (Unwind)
                            uint32_t arity = (target_label.opcode == 0x03)
                                                 ? target_label.param_count
                                                 : target_label.result_count;
                            const std::size_t src = sp - arity;
                            sp = target_label.stack_top;
                            if (arity > 0)
                                StackUnwind(stack, sp, src, arity);
                            sp += arity;

                            ip = target_label.pc;
                            frame.ip = ip;

                            if (target_label.opcode == 0x03) {
                                label_top -= label_idx;
                            } else {
                                label_top -= (label_idx + 1);
                            }

                            ctx->stack_top = sp;
                            frame.label_stack_top = label_top;  // [Opt 4] 同期
                            goto frame_changed;
                        }
                        break;
                    }

                    case 0x0E: {
                        // br_table
                        uint32_t target_count = DecodeVarUint32Fast(ip);
                        uint32_t idx = static_cast<uint32_t>(stack[--sp].value.i32);

                        uint32_t chosen_label_idx = 0;
                        bool found = false;
                        for (uint32_t i = 0; i < target_count; ++i) {
                            uint32_t target = DecodeVarUint32Fast(ip);
                            if (i == idx) {
                                chosen_label_idx = target;
                                found = true;
                                break; // 常に goto frame_changed するため ip は不要
                            }
                        }
                        uint32_t default_target = DecodeVarUint32Fast(ip);
                        if (!found) {
                            chosen_label_idx = default_target;
                        }

                        WasmLabel &target_label = frame.labels[label_top - 1 - chosen_label_idx];

                        // データスタックの巻き戻し (Unwind)
                        uint32_t arity = (target_label.opcode == 0x03)
                                             ? target_label.param_count
                                             : target_label.result_count;
                        const std::size_t src = sp - arity;
                        sp = target_label.stack_top;
                        if (arity > 0)
                            StackUnwind(stack, sp, src, arity);
                        sp += arity;

                        ip = target_label.pc;
                        frame.ip = ip;

                        if (target_label.opcode == 0x03) {
                            label_top -= chosen_label_idx;
                        } else {
                            label_top -= (chosen_label_idx + 1);
                        }
                        ctx->stack_top = sp;
                        frame.label_stack_top = label_top;  // [Opt 4] 同期
                        goto frame_changed;
                    }

                    case 0x0F: // return
                    case 0x0B: // end
                        if (op == 0x0B && label_top > 0) {
                            WasmLabel &label = frame.labels[label_top - 1];
                            uint32_t arity = label.result_count;

                            const std::size_t src = sp - arity;
                            sp = label.stack_top;
                            if (arity > 0)
                                StackUnwind(stack, sp, src, arity);
                            sp += arity;

                            label_top--;
                            if (label_top > 0) break;
                            // label_top == 0: func_label を pop した = 関数終端 → フォールスルー
                        }
                        // return (0x0F): 関数外側ブロックラベルでスタックを巻き戻す
                        if (op == 0x0F) {
                            WasmLabel &func_label = frame.labels[0];
                            uint32_t arity = func_label.result_count;
                            const std::size_t src = sp - arity;
                            sp = func_label.stack_top;
                            if (arity > 0)
                                StackUnwind(stack, sp, src, arity);
                            sp += arity;
                        }
                        // 関数の終了 (end で func_label pop、または return)
                        ctx->labels_pool_top -= frame.label_capacity;
                        ctx->stack_top = sp;
                        if (ctx->call_stack_top > 0) {
                            --ctx->call_stack_top;
                            goto frame_changed;
                        } else {
                            result = WasmResult::kOk;
                            goto done;
                        }

                    case 0x10: {
                        // call <func_index>
                        uint32_t target_idx = DecodeVarUint32Fast(ip);

                        // kImportのときはチェーンを辿って実際の関数を得る
                        WasmModuleInstance *call_mod = current_mod;
                        const WasmFunction *target_func = &functions[target_idx];
                        if (target_func->kind == WasmFunctionKind::kImport) {
                            WasmModuleInstance *rm = nullptr;
                            const WasmFunction *rf = nullptr;
                            if (!ResolveWasmImportChain(this, target_func, rm, rf)) {
                                result = WasmResult::kErrorFunctionNotFound;
                                goto done;
                            }
                            call_mod = rm;
                            target_func = rf;
                        }

                        if (target_func->kind == WasmFunctionKind::kHost) {
                            ctx->stack_top = sp;
                            WasmResult res = DispatchHostFunction(*this, target_func->host.host_func_id, ctx);
                            sp = ctx->stack_top;
                            if (res == WasmResult::kYield) {
                                frame.ip = ip;
                                frame.label_stack_top = label_top;  // [Opt 4] 同期
                                result = WasmResult::kYield;
                                goto done;
                            }
                            if (res != WasmResult::kOk) {
                                result = res;
                                goto done;
                            }
                            if (sp > max_stack_depth_) max_stack_depth_ = sp;
                        } else {
                            frame.ip = ip;
                            frame.label_stack_top = label_top;  // [Opt 4] 同期
                            result = PushCallFrame(ctx, target_func, call_mod, sp, max_call_stack_depth_,
                                                   WasmResult::kErrorExecuteTrapStackOverflow);
                            if (result != WasmResult::kOk) goto done;
                            goto frame_changed;
                        }
                        break;
                    }
                    case 0x11: {
                        // call_indirect
                        uint32_t type_idx = DecodeVarUint32Fast(ip);
                        uint32_t table_idx = *ip++;

                        uint32_t elem_idx = static_cast<uint32_t>(stack[--sp].value.i32);

                        if (table_idx >= table_count || !tables[table_idx] || elem_idx >= table_sizes[table_idx]) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                            goto done;
                        }
                        uint32_t ref_val = tables[table_idx][elem_idx];
                        if (ref_val == 0xFFFFFFFF) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapTableUninitializedElement);
                            goto done;
                        }
                        WasmModuleInstance *target_module = nullptr;
                        uint32_t target_idx = 0xFFFFFFFF;
                        DecodeFuncRef(ref_val, this, current_mod, target_module, target_idx);
                        if (!target_module || target_idx >= target_module->function_count) {
                            result = OnTrap(WasmResult::kErrorExecuteRuntimeError);
                            goto done;
                        }

                        const WasmFunction *target_func = &target_module->functions[target_idx];
                        // 型シグネチャ検証: モジュールが異なる場合は同一インデックスでも構造比較が必要
                        if (target_func->type_index != type_idx || target_module != current_mod) {
                            const WasmTypeSignature *sa = target_module->signatures[target_func->type_index];
                            const WasmTypeSignature *sb = signatures[type_idx];
                            if (!WasmTypeSignature::Equals(sa, sb)) {
                                result = OnTrap(WasmResult::kErrorExecuteTrapIndirectCallSignatureMismatch);
                                goto done;
                            }
                        }

                        // kImportのときはチェーンを辿って実際の関数を得る
                        if (target_func->kind == WasmFunctionKind::kImport) {
                            WasmModuleInstance *rm = nullptr;
                            const WasmFunction *rf = nullptr;
                            if (!ResolveWasmImportChain(this, target_func, rm, rf)) {
                                result = OnTrap(WasmResult::kErrorFunctionNotFound);
                                goto done;
                            }
                            target_module = rm;
                            target_func = rf;
                        }

                        if (target_func->kind == WasmFunctionKind::kHost) {
                            ctx->stack_top = sp;
                            WasmResult res = DispatchHostFunction(*this, target_func->host.host_func_id, ctx);
                            sp = ctx->stack_top;
                            if (res == WasmResult::kYield) {
                                frame.ip = ip;
                                frame.label_stack_top = label_top;  // [Opt 4] 同期
                                result = WasmResult::kYield;
                                goto done;
                            }
                            if (res != WasmResult::kOk) {
                                result = res;
                                goto done;
                            }
                            if (sp > max_stack_depth_) max_stack_depth_ = sp;
                        } else {
                            frame.ip = ip;
                            frame.label_stack_top = label_top;  // [Opt 4] 同期
                            result = PushCallFrame(ctx, target_func, target_module, sp, max_call_stack_depth_,
                                                   WasmResult::kErrorExecuteTrapCallStackOverflow);
                            if (result != WasmResult::kOk) goto done;
                            goto frame_changed;
                        }
                        break;
                    }

                    case 0x1A: {
                        // drop
                        --sp;
                        break;
                    }

                    case 0x1B: {
                        // select
                        int32_t cond = stack[--sp].value.i32;
                        WasmValue val2 = stack[--sp];
                        WasmValue val1 = stack[--sp];
                        stack[sp++] = (cond != 0) ? val1 : val2;
                        break;
                    }

                    case 0x1C: {
                        // select (t*)
                        uint32_t type_count = DecodeVarUint32Fast(ip);
                        ip += type_count;
                        int32_t cond = stack[--sp].value.i32;
                        WasmValue val2 = stack[--sp];
                        WasmValue val1 = stack[--sp];
                        stack[sp++] = (cond != 0) ? val1 : val2;
                        break;
                    }

                    case 0x20: {
                        // local.get <local_idx>
                        uint32_t local_idx = DecodeVarUint32Fast(ip);
                        stack[sp++] = locals[local_idx];
                        break;
                    }

                    case 0x21: {
                        // local.set <local_idx>
                        uint32_t local_idx = DecodeVarUint32Fast(ip);
                        locals[local_idx] = stack[--sp];
                        break;
                    }

                    case 0x22: {
                        // local.tee <local_idx>
                        uint32_t local_idx = DecodeVarUint32Fast(ip);
                        locals[local_idx] = stack[sp - 1]; // ポップせずにコピー
                        break;
                    }

                    case 0x23: {
                        // global.get <global_idx>
                        uint32_t idx = DecodeVarUint32Fast(ip);
                        stack[sp++] = globals[idx].value;
                        break;
                    }

                    case 0x24: {
                        // global.set <global_idx>
                        // ValidateFunctionBody でイミュータブルへの書き込みは検証済み
                        uint32_t idx = DecodeVarUint32Fast(ip);
                        globals[idx].value = stack[--sp];
                        break;
                    }

                    case 0x25: {
                        // table.get
                        uint32_t table_idx = DecodeVarUint32Fast(ip);
                        uint32_t elem_idx = static_cast<uint32_t>(stack[--sp].value.i32);
                        if (table_idx >= table_count || !tables[table_idx] || elem_idx >= table_sizes[table_idx]) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                            goto done;
                        }
                        uint32_t target_idx = tables[table_idx][elem_idx];

                        WasmValue ref_val = {};
                        if (target_idx == 0xFFFFFFFF) {
                            ref_val.value.i64 = -1; // null
                        } else {
                            ref_val.value.i64 = static_cast<int64_t>(target_idx);
                        }
                        stack[sp++] = ref_val;
                        break;
                    }

                    case 0x26: {
                        // table.set
                        uint32_t table_idx = DecodeVarUint32Fast(ip);
                        WasmValue val = stack[--sp];
                        uint32_t elem_idx = static_cast<uint32_t>(stack[--sp].value.i32);
                        if (table_idx >= table_count || !tables[table_idx] || elem_idx >= table_sizes[table_idx]) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                            goto done;
                        }
                        uint32_t target_idx = 0xFFFFFFFF;
                        if (val.value.i64 != -1) {
                            target_idx = static_cast<uint32_t>(val.value.i64);
                        }
                        bool is_funcref = (table_types[table_idx] == WasmType::kFuncRef);
                        tables[table_idx][elem_idx] = is_funcref
                                                          ? EncodeFuncRef(this, current_mod, target_idx)
                                                          : target_idx;
                        break;
                    }

                    case 0x28: // i32.load
                    case 0x29: // i64.load
                    case 0x2A: // f32.load
                    case 0x2B: // f64.load
                    case 0x2C: // i32.load8_s
                    case 0x2D: // i32.load8_u
                    case 0x2E: // i32.load16_s
                    case 0x2F: // i32.load16_u
                    case 0x30: // i64.load8_s
                    case 0x31: // i64.load8_u
                    case 0x32: // i64.load16_s
                    case 0x33: // i64.load16_u
                    case 0x34: // i64.load32_s
                    case 0x35: {
                        // i64.load32_u
                        /* uint32_t align = */
                        DecodeVarUint32Fast(ip);
                        uint32_t offset = DecodeVarUint32Fast(ip);
                        uint32_t base = static_cast<uint32_t>(stack[--sp].value.i32);
                        uint64_t addr = static_cast<uint64_t>(base) + offset;

                        std::size_t size = kLoadSize[op - 0x28];

                        if (!linear_memory_ptr || addr + size > linear_memory_size) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                            goto done;
                        }

                        WasmValue result_val;
                        result_val.value.i64 = 0;
                        switch (op) {
                            case 0x28: std::memcpy(&result_val.value.i32, &linear_memory_ptr[addr], 4); break;
                            case 0x29: std::memcpy(&result_val.value.i64, &linear_memory_ptr[addr], 8); break;
                            case 0x2A: std::memcpy(&result_val.value.f32, &linear_memory_ptr[addr], 4); break;
                            case 0x2B: std::memcpy(&result_val.value.f64, &linear_memory_ptr[addr], 8); break;
                            case 0x2C: result_val.value.i32 = static_cast<int32_t>(static_cast<int8_t>(linear_memory_ptr[addr])); break;
                            case 0x2D: result_val.value.i32 = static_cast<int32_t>(linear_memory_ptr[addr]); break;
                            case 0x2E: { int16_t v; std::memcpy(&v, &linear_memory_ptr[addr], 2); result_val.value.i32 = static_cast<int32_t>(v); break; }
                            case 0x2F: { uint16_t v; std::memcpy(&v, &linear_memory_ptr[addr], 2); result_val.value.i32 = static_cast<int32_t>(v); break; }
                            case 0x30: result_val.value.i64 = static_cast<int64_t>(static_cast<int8_t>(linear_memory_ptr[addr])); break;
                            case 0x31: result_val.value.i64 = static_cast<int64_t>(linear_memory_ptr[addr]); break;
                            case 0x32: { int16_t v; std::memcpy(&v, &linear_memory_ptr[addr], 2); result_val.value.i64 = static_cast<int64_t>(v); break; }
                            case 0x33: { uint16_t v; std::memcpy(&v, &linear_memory_ptr[addr], 2); result_val.value.i64 = static_cast<int64_t>(v); break; }
                            case 0x34: { int32_t v; std::memcpy(&v, &linear_memory_ptr[addr], 4); result_val.value.i64 = static_cast<int64_t>(v); break; }
                            case 0x35: { uint32_t v; std::memcpy(&v, &linear_memory_ptr[addr], 4); result_val.value.i64 = static_cast<int64_t>(v); break; }
                        }
                        stack[sp++] = result_val;
                        break;
                    }

                    case 0x36: // i32.store
                    case 0x37: // i64.store
                    case 0x38: // f32.store
                    case 0x39: // f64.store
                    case 0x3A: // i32.store8
                    case 0x3B: // i32.store16
                    case 0x3C: // i64.store8
                    case 0x3D: // i64.store16
                    case 0x3E: {
                        // i64.store32
                        /* uint32_t align = */
                        DecodeVarUint32Fast(ip);
                        uint32_t offset = DecodeVarUint32Fast(ip);
                        WasmValue val = stack[--sp];
                        uint32_t base = static_cast<uint32_t>(stack[--sp].value.i32);
                        uint64_t addr = static_cast<uint64_t>(base) + offset;

                        std::size_t size = kStoreSize[op - 0x36];
                        if (!linear_memory_ptr || addr + size > linear_memory_size) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                            goto done;
                        }
                        switch (op) {
                            case 0x36: std::memcpy(&linear_memory_ptr[addr], &val.value.i32, 4); break;
                            case 0x37: std::memcpy(&linear_memory_ptr[addr], &val.value.i64, 8); break;
                            case 0x38: std::memcpy(&linear_memory_ptr[addr], &val.value.f32, 4); break;
                            case 0x39: std::memcpy(&linear_memory_ptr[addr], &val.value.f64, 8); break;
                            case 0x3A: linear_memory_ptr[addr] = static_cast<uint8_t>(val.value.i32 & 0xFF); break;
                            case 0x3B: { uint16_t v = static_cast<uint16_t>(val.value.i32 & 0xFFFF); std::memcpy(&linear_memory_ptr[addr], &v, 2); break; }
                            case 0x3C: linear_memory_ptr[addr] = static_cast<uint8_t>(val.value.i64 & 0xFF); break;
                            case 0x3D: { uint16_t v = static_cast<uint16_t>(val.value.i64 & 0xFFFF); std::memcpy(&linear_memory_ptr[addr], &v, 2); break; }
                            case 0x3E: { uint32_t v = static_cast<uint32_t>(val.value.i64 & 0xFFFFFFFFULL); std::memcpy(&linear_memory_ptr[addr], &v, 4); break; }
                        }
                        break;
                    }

                    case 0x41: {
                        // i32.const <value>
                        int32_t val = DecodeVarInt32Fast(ip);
                        stack[sp++].value.i32 = val;
                        break;
                    }

                    case 0x42: {
                        // i64.const <value>
                        int64_t val = DecodeVarInt64Fast(ip);
                        stack[sp++].value.i64 = val;
                        break;
                    }

                    case 0x43: {
                        // f32.const <value>
                        std::memcpy(&stack[sp++].value.f32, ip, 4);
                        ip += 4;
                        break;
                    }

                    case 0x44: {
                        // f64.const <value>
                        std::memcpy(&stack[sp++].value.f64, ip, 8);
                        ip += 8;
                        break;
                    }

                    case 0x45: {
                        // i32.eqz
                        stack[sp - 1].value.i32 = (stack[sp - 1].value.i32 == 0) ? 1 : 0;
                        break;
                    }

                    // i32 比較演算子
                    case 0x46: // i32.eq
                    case 0x47: // i32.ne
                    case 0x48: // i32.lt_s
                    case 0x49: // i32.lt_u
                    case 0x4A: // i32.gt_s
                    case 0x4B: // i32.gt_u
                    case 0x4C: // i32.le_s
                    case 0x4D: // i32.le_u
                    case 0x4E: // i32.ge_s
                    case 0x4F: {
                        // i32.ge_u
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];

                        int32_t res = 0;
                        switch (op) {
                            case 0x46: res = (a.value.i32 == b.value.i32) ? 1 : 0;
                                break;
                            case 0x47: res = (a.value.i32 != b.value.i32) ? 1 : 0;
                                break;
                            case 0x48: res = (a.value.i32 < b.value.i32) ? 1 : 0;
                                break;
                            case 0x49: res = (static_cast<uint32_t>(a.value.i32) < static_cast<uint32_t>(b.value.i32))
                                                 ? 1
                                                 : 0;
                                break;
                            case 0x4A: res = (a.value.i32 > b.value.i32) ? 1 : 0;
                                break;
                            case 0x4B: res = (static_cast<uint32_t>(a.value.i32) > static_cast<uint32_t>(b.value.i32))
                                                 ? 1
                                                 : 0;
                                break;
                            case 0x4C: res = (a.value.i32 <= b.value.i32) ? 1 : 0;
                                break;
                            case 0x4D: res = (static_cast<uint32_t>(a.value.i32) <= static_cast<uint32_t>(b.value.i32))
                                                 ? 1
                                                 : 0;
                                break;
                            case 0x4E: res = (a.value.i32 >= b.value.i32) ? 1 : 0;
                                break;
                            case 0x4F: res = (static_cast<uint32_t>(a.value.i32) >= static_cast<uint32_t>(b.value.i32))
                                                 ? 1
                                                 : 0;
                                break;
                        }
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x50: {
                        // i64.eqz
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.i32 = (val.value.i64 == 0) ? 1 : 0;
                        break;
                    }

                    case 0x51: // i64.eq
                    case 0x52: // i64.ne
                    case 0x53: // i64.lt_s
                    case 0x54: // i64.lt_u
                    case 0x55: // i64.gt_s
                    case 0x56: // i64.gt_u
                    case 0x57: // i64.le_s
                    case 0x58: // i64.le_u
                    case 0x59: // i64.ge_s
                    case 0x5A: {
                        // i64.ge_u
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];

                        int32_t res = 0;
                        switch (op) {
                            case 0x51: res = (a.value.i64 == b.value.i64) ? 1 : 0;
                                break;
                            case 0x52: res = (a.value.i64 != b.value.i64) ? 1 : 0;
                                break;
                            case 0x53: res = (a.value.i64 < b.value.i64) ? 1 : 0;
                                break;
                            case 0x54: res = (static_cast<uint64_t>(a.value.i64) < static_cast<uint64_t>(b.value.i64))
                                                 ? 1
                                                 : 0;
                                break;
                            case 0x55: res = (a.value.i64 > b.value.i64) ? 1 : 0;
                                break;
                            case 0x56: res = (static_cast<uint64_t>(a.value.i64) > static_cast<uint64_t>(b.value.i64))
                                                 ? 1
                                                 : 0;
                                break;
                            case 0x57: res = (a.value.i64 <= b.value.i64) ? 1 : 0;
                                break;
                            case 0x58: res = (static_cast<uint64_t>(a.value.i64) <= static_cast<uint64_t>(b.value.i64))
                                                 ? 1
                                                 : 0;
                                break;
                            case 0x59: res = (a.value.i64 >= b.value.i64) ? 1 : 0;
                                break;
                            case 0x5A: res = (static_cast<uint64_t>(a.value.i64) >= static_cast<uint64_t>(b.value.i64))
                                                 ? 1
                                                 : 0;
                                break;
                        }
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x5B: // f32.eq
                    case 0x5C: // f32.ne
                    case 0x5D: // f32.lt
                    case 0x5E: // f32.gt
                    case 0x5F: // f32.le
                    case 0x60: {
                        // f32.ge
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];

                        int32_t res = 0;
                        switch (op) {
                            case 0x5B: res = (a.value.f32 == b.value.f32) ? 1 : 0;
                                break;
                            case 0x5C: res = (a.value.f32 != b.value.f32) ? 1 : 0;
                                break;
                            case 0x5D: res = (a.value.f32 < b.value.f32) ? 1 : 0;
                                break;
                            case 0x5E: res = (a.value.f32 > b.value.f32) ? 1 : 0;
                                break;
                            case 0x5F: res = (a.value.f32 <= b.value.f32) ? 1 : 0;
                                break;
                            case 0x60: res = (a.value.f32 >= b.value.f32) ? 1 : 0;
                                break;
                        }
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x61: // f64.eq
                    case 0x62: // f64.ne
                    case 0x63: // f64.lt
                    case 0x64: // f64.gt
                    case 0x65: // f64.le
                    case 0x66: {
                        // f64.ge
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];

                        int32_t res = 0;
                        switch (op) {
                            case 0x61: res = (a.value.f64 == b.value.f64) ? 1 : 0;
                                break;
                            case 0x62: res = (a.value.f64 != b.value.f64) ? 1 : 0;
                                break;
                            case 0x63: res = (a.value.f64 < b.value.f64) ? 1 : 0;
                                break;
                            case 0x64: res = (a.value.f64 > b.value.f64) ? 1 : 0;
                                break;
                            case 0x65: res = (a.value.f64 <= b.value.f64) ? 1 : 0;
                                break;
                            case 0x66: res = (a.value.f64 >= b.value.f64) ? 1 : 0;
                                break;
                        }
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x67: {
                        // i32.clz
                        WasmValue val = stack[sp - 1];

                        stack[sp - 1].value.i32 = static_cast<int32_t>(CountLeadingZeros32(
                            static_cast<uint32_t>(val.value.i32)));
                        break;
                    }

                    // i32 算術・論理演算子
                    case 0x6A: // i32.add
                    case 0x6B: // i32.sub
                    case 0x6C: // i32.mul
                    case 0x6D: // i32.div_s
                    case 0x6E: // i32.div_u
                    case 0x71: // i32.and
                    case 0x72: // i32.or
                    case 0x73: // i32.xor
                    case 0x74: // i32.shl
                    case 0x75: // i32.shr_s
                    case 0x76: {
                        // i32.shr_u
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];

                        int32_t res = 0;
                        switch (op) {
                            case 0x6A: res = a.value.i32 + b.value.i32;
                                break;
                            case 0x6B: res = a.value.i32 - b.value.i32;
                                break;
                            case 0x6C: res = a.value.i32 * b.value.i32;
                                break;
                            case 0x6D:
                                if (b.value.i32 == 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                                    goto done;
                                }
                                if (a.value.i32 == static_cast<int32_t>(0x80000000) && b.value.i32 == -1) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapIntegerOverflow);
                                    goto done;
                                }
                                res = a.value.i32 / b.value.i32;
                                break;
                            case 0x6E:
                                if (b.value.i32 == 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                                    goto done;
                                }
                                res = static_cast<int32_t>(
                                    static_cast<uint32_t>(a.value.i32) / static_cast<uint32_t>(b.value.i32));
                                break;
                            case 0x71: res = a.value.i32 & b.value.i32;
                                break;
                            case 0x72: res = a.value.i32 | b.value.i32;
                                break;
                            case 0x73: res = a.value.i32 ^ b.value.i32;
                                break;
                            case 0x74: res = a.value.i32 << (b.value.i32 & 31);
                                break;
                            case 0x75: res = a.value.i32 >> (b.value.i32 & 31);
                                break;
                            case 0x76: res = static_cast<int32_t>(
                                           static_cast<uint32_t>(a.value.i32) >> (b.value.i32 & 31));
                                break;
                        }
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    // i64 算術・論理演算子
                    case 0x7C: // i64.add
                    case 0x7D: // i64.sub
                    case 0x7E: // i64.mul
                    case 0x83: // i64.and
                    case 0x84: // i64.or
                    case 0x85: // i64.xor
                    case 0x86: // i64.shl
                    case 0x87: // i64.shr_s
                    case 0x88: {
                        // i64.shr_u
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];

                        int64_t res = 0;
                        switch (op) {
                            case 0x7C: res = a.value.i64 + b.value.i64;
                                break;
                            case 0x7D: res = a.value.i64 - b.value.i64;
                                break;
                            case 0x7E: res = a.value.i64 * b.value.i64;
                                break;
                            case 0x83: res = a.value.i64 & b.value.i64;
                                break;
                            case 0x84: res = a.value.i64 | b.value.i64;
                                break;
                            case 0x85: res = a.value.i64 ^ b.value.i64;
                                break;
                            case 0x86: res = a.value.i64 << (b.value.i64 & 63);
                                break;
                            case 0x87: res = a.value.i64 >> (b.value.i64 & 63);
                                break;
                            case 0x88: res = static_cast<int64_t>(
                                           static_cast<uint64_t>(a.value.i64) >> (b.value.i64 & 63));
                                break;
                        }
                        stack[sp++].value.i64 = res;
                        break;
                    }

                    // f32 / f64 基本演算 (簡易対応)
                    case 0x92: // f32.add
                    case 0x93: // f32.sub
                    case 0x94: // f32.mul
                    case 0x95: {
                        // f32.div
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        float res = 0;
                        switch (op) {
                            case 0x92: res = a.value.f32 + b.value.f32;
                                break;
                            case 0x93: res = a.value.f32 - b.value.f32;
                                break;
                            case 0x94: res = a.value.f32 * b.value.f32;
                                break;
                            case 0x95: res = a.value.f32 / b.value.f32;
                                break;
                        }
                        WasmValue result_val;
                        result_val.value.f32 = res;
                        stack[sp++] = result_val;
                        break;
                    }

                    case 0xA0: // f64.add
                    case 0xA1: // f64.sub
                    case 0xA2: // f64.mul
                    case 0xA3: {
                        // f64.div
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        double res = 0;
                        switch (op) {
                            case 0xA0: res = a.value.f64 + b.value.f64;
                                break;
                            case 0xA1: res = a.value.f64 - b.value.f64;
                                break;
                            case 0xA2: res = a.value.f64 * b.value.f64;
                                break;
                            case 0xA3: res = a.value.f64 / b.value.f64;
                                break;
                        }
                        WasmValue result_val;
                        result_val.value.f64 = res;
                        stack[sp++] = result_val;
                        break;
                    }

                    case 0xA7: {
                        // i32.wrap_i64
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.i32 = static_cast<int32_t>(val.value.i64 & 0xFFFFFFFFULL);
                        break;
                    }

                    case 0xA8: { // i32.trunc_f32_s
                        float f = stack[sp - 1].value.f32;
                        if (std::isnan(f) || std::isinf(f) || f < -2147483648.0f || f >= 2147483648.0f)
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i32 = static_cast<int32_t>(f);
                        break;
                    }
                    case 0xA9: { // i32.trunc_f32_u
                        float f = stack[sp - 1].value.f32;
                        if (std::isnan(f) || std::isinf(f) || f <= -1.0f || f >= 4294967296.0f)
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i32 = static_cast<int32_t>(static_cast<uint32_t>(f));
                        break;
                    }
                    case 0xAA: { // i32.trunc_f64_s
                        double d = stack[sp - 1].value.f64;
                        if (!(d > -2147483649.0 && d < 2147483648.0))
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i32 = static_cast<int32_t>(d);
                        break;
                    }
                    case 0xAB: { // i32.trunc_f64_u
                        double d = stack[sp - 1].value.f64;
                        if (!(d > -1.0 && d < 4294967296.0))
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i32 = static_cast<int32_t>(static_cast<uint32_t>(d));
                        break;
                    }

                    case 0xAC: {
                        // i64.extend_i32_s
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.i64 = 0;
                        stack[sp - 1].value.i64 = static_cast<int64_t>(val.value.i32);
                        break;
                    }

                    case 0xAD: {
                        // i64.extend_i32_u
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.i64 = 0;
                        stack[sp - 1].value.i64 = static_cast<uint64_t>(static_cast<uint32_t>(val.value.i32));
                        break;
                    }

                    case 0xAE: { // i64.trunc_f32_s
                        float f = stack[sp - 1].value.f32;
                        if (std::isnan(f) || std::isinf(f) || f < -9223372036854775808.0f || f >= 9223372036854775808.0f)
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i64 = static_cast<int64_t>(f);
                        break;
                    }
                    case 0xAF: { // i64.trunc_f32_u
                        float f = stack[sp - 1].value.f32;
                        if (std::isnan(f) || std::isinf(f) || f <= -1.0f || f >= 18446744073709551616.0f)
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i64 = static_cast<int64_t>(static_cast<uint64_t>(f));
                        break;
                    }
                    case 0xB0: { // i64.trunc_f64_s
                        double d = stack[sp - 1].value.f64;
                        if (std::isnan(d) || std::isinf(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0)
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i64 = static_cast<int64_t>(d);
                        break;
                    }
                    case 0xB1: { // i64.trunc_f64_u
                        double d = stack[sp - 1].value.f64;
                        if (!(d > -1.0 && d < 18446744073709551616.0))
                        {
                            result = OnTrap(WasmResult::kErrorExecuteTrapInvalidConversionToInteger);
                            goto done;
                        }
                        stack[sp - 1].value.i64 = static_cast<int64_t>(static_cast<uint64_t>(d));
                        break;
                    }

                    case 0xB2: // f32.convert_i32_s
                    case 0xB3: // f32.convert_i32_u
                    case 0xB4: // f32.convert_i64_s
                    case 0xB5: {
                        // f32.convert_i64_u
                        WasmValue val = stack[sp - 1];
                        WasmValue res_val;
                        if (op == 0xB2) {
                            res_val.value.f32 = static_cast<float>(val.value.i32);
                        } else if (op == 0xB3) {
                            res_val.value.f32 = static_cast<float>(static_cast<uint32_t>(val.value.i32));
                        } else if (op == 0xB4) {
                            res_val.value.f32 = static_cast<float>(val.value.i64);
                        } else {
                            res_val.value.f32 = static_cast<float>(static_cast<uint64_t>(val.value.i64));
                        }
                        stack[sp - 1] = res_val;
                        break;
                    }

                    case 0xB6: {
                        // f32.demote_f64
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.f32 = 0;
                        stack[sp - 1].value.f32 = static_cast<float>(val.value.f64);
                        break;
                    }

                    case 0xB7: // f64.convert_i32_s
                    case 0xB8: // f64.convert_i32_u
                    case 0xB9: // f64.convert_i64_s
                    case 0xBA: {
                        // f64.convert_i64_u
                        WasmValue val = stack[sp - 1];
                        WasmValue res_val;
                        if (op == 0xB7) {
                            res_val.value.f64 = static_cast<double>(val.value.i32);
                        } else if (op == 0xB8) {
                            res_val.value.f64 = static_cast<double>(static_cast<uint32_t>(val.value.i32));
                        } else if (op == 0xB9) {
                            res_val.value.f64 = static_cast<double>(val.value.i64);
                        } else {
                            res_val.value.f64 = static_cast<double>(static_cast<uint64_t>(val.value.i64));
                        }
                        stack[sp - 1] = res_val;
                        break;
                    }

                    case 0xBB: {
                        // f64.promote_f32
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.f64 = 0;
                        stack[sp - 1].value.f64 = static_cast<double>(val.value.f32);
                        break;
                    }

                    case 0x3F: {
                        // memory.size
                        uint8_t reserved = *ip++;
                        (void) reserved;
                        int32_t pages = static_cast<int32_t>((linear_memory_size + 65535) / 65536);
                        stack[sp++].value.i32 = pages;
                        break;
                    }

                    case 0x40: {
                        // memory.grow
                        uint8_t reserved = *ip++;
                        (void) reserved;
                        uint32_t delta_pages = static_cast<uint32_t>(stack[sp - 1].value.i32);
                        int32_t prev_pages = static_cast<int32_t>((linear_memory_size + 65535) / 65536);
                        if (delta_pages == 0) {
                            stack[sp - 1].value.i32 = prev_pages;
                            break;
                        }
                        uint64_t new_pages = static_cast<uint64_t>(prev_pages) + delta_pages;
                        bool exceeds_module_max = (max_linear_memory_pages != 0) &&
                                                  (new_pages > max_linear_memory_pages);
                        uint64_t new_size_bytes = new_pages * 65536;
                        if (new_pages > 65536 || exceeds_module_max || new_size_bytes > kMaxLinearMemorySize) {
                            stack[sp - 1].value.i32 = -1;
                        } else if (new_size_bytes <= linear_memory_capacity) {
                            // 既存バッファ内に収まる場合
                            std::memset(linear_memory_ptr + linear_memory_size, 0,
                                        static_cast<std::size_t>(new_size_bytes) - linear_memory_size);
                            linear_memory_size = static_cast<std::size_t>(new_size_bytes);
                            // 同バッファを共有する全モジュールにサイズを伝播
                            for (std::size_t _m = 0; _m < kMaxModules; ++_m) {
                                if (!modules_[_m] || !modules_[_m]->is_active) continue;
                                if (modules_[_m]->linear_memory_ptr == linear_memory_ptr) {
                                    modules_[_m]->linear_memory_size = static_cast<std::size_t>(new_size_bytes);
                                }
                            }
                            stack[sp - 1].value.i32 = prev_pages;
                        } else {
                            // 容量不足: ダブリング戦略で再確保してデータをコピー
                            std::size_t new_cap = (linear_memory_capacity > 0)
                                                      ? linear_memory_capacity * 2
                                                      : static_cast<std::size_t>(new_size_bytes);
                            if (new_cap < static_cast<std::size_t>(new_size_bytes))
                                new_cap = static_cast<std::size_t>(new_size_bytes);
                            if (new_cap > kMaxLinearMemorySize) new_cap = kMaxLinearMemorySize;
                            uint8_t *new_mem = static_cast<uint8_t *>(pool_->Allocate(new_cap));
                            if (!new_mem) {
                                stack[sp - 1].value.i32 = -1;
                            } else {
                                if (linear_memory_ptr && linear_memory_size > 0) {
                                    std::memcpy(new_mem, linear_memory_ptr, linear_memory_size);
                                }
                                std::memset(new_mem + linear_memory_size, 0,
                                            new_cap - linear_memory_size);
                                uint8_t *old_mem = linear_memory_ptr;
                                std::size_t new_size = static_cast<std::size_t>(new_size_bytes);

                                linear_memory_ptr = new_mem;
                                linear_memory_size = new_size;
                                linear_memory_capacity = new_cap;

                                // 共有モジュールを含む全参照を新バッファに更新
                                for (std::size_t _m = 0; _m < kMaxModules; ++_m) {
                                    if (!modules_[_m] || !modules_[_m]->is_active) continue;
                                    if (modules_[_m]->linear_memory_ptr == old_mem) {
                                        modules_[_m]->linear_memory_ptr = new_mem;
                                        modules_[_m]->linear_memory_size = new_size;
                                        modules_[_m]->linear_memory_capacity = new_cap;
                                    }
                                }
                                if (old_mem) {
                                    pool_->Free(old_mem);
                                }
                                stack[sp - 1].value.i32 = prev_pages;
                            }
                        }
                        break;
                    }

                    case 0x68: {
                        // i32.ctz
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.i32 = static_cast<int32_t>(CountTrailingZeros32(
                            static_cast<uint32_t>(val.value.i32)));
                        break;
                    }

                    case 0x69: {
                        // i32.popcnt
                        WasmValue val = stack[sp - 1];
                        stack[sp - 1].value.i32 = static_cast<int32_t>(PopCount32(
                            static_cast<uint32_t>(val.value.i32)));
                        break;
                    }

                    case 0x6F: {
                        // i32.rem_s
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        if (b.value.i32 == 0) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                            goto done;
                        }
                        int32_t res = 0;
                        if (a.value.i32 == static_cast<int32_t>(0x80000000) && b.value.i32 == -1) {
                            res = 0;
                        } else {
                            res = a.value.i32 % b.value.i32;
                        }
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x70: {
                        // i32.rem_u
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        if (b.value.i32 == 0) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                            goto done;
                        }
                        uint32_t ua = static_cast<uint32_t>(a.value.i32);
                        uint32_t ub = static_cast<uint32_t>(b.value.i32);
                        int32_t res = static_cast<int32_t>(ua % ub);
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x77: {
                        // i32.rotl
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        int32_t res = static_cast<int32_t>(Rotl32(static_cast<uint32_t>(a.value.i32),
                                                                  static_cast<uint32_t>(b.value.i32)));
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x78: {
                        // i32.rotr
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        int32_t res = static_cast<int32_t>(Rotr32(static_cast<uint32_t>(a.value.i32),
                                                                  static_cast<uint32_t>(b.value.i32)));
                        stack[sp++].value.i32 = res;
                        break;
                    }

                    case 0x79: {
                        // i64.clz
                        WasmValue val = stack[sp - 1];
                        int64_t res = static_cast<int64_t>(CountLeadingZeros64(static_cast<uint64_t>(val.value.i64)));
                        stack[sp - 1].value.i64 = res;
                        break;
                    }

                    case 0x7A: {
                        // i64.ctz
                        WasmValue val = stack[sp - 1];
                        int64_t res = static_cast<int64_t>(CountTrailingZeros64(static_cast<uint64_t>(val.value.i64)));
                        stack[sp - 1].value.i64 = res;
                        break;
                    }

                    case 0x7B: {
                        // i64.popcnt
                        WasmValue val = stack[sp - 1];
                        int64_t res = static_cast<int64_t>(PopCount64(static_cast<uint64_t>(val.value.i64)));
                        stack[sp - 1].value.i64 = res;
                        break;
                    }

                    case 0x7F: {
                        // i64.div_s
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        if (b.value.i64 == 0) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                            goto done;
                        }
                        if (a.value.i64 == static_cast<int64_t>(0x8000000000000000ULL) && b.value.i64 == -1) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapIntegerOverflow);
                            goto done;
                        }
                        int64_t res = a.value.i64 / b.value.i64;
                        stack[sp++].value.i64 = res;
                        break;
                    }

                    case 0x80: {
                        // i64.div_u
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        if (b.value.i64 == 0) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                            goto done;
                        }
                        uint64_t ua = static_cast<uint64_t>(a.value.i64);
                        uint64_t ub = static_cast<uint64_t>(b.value.i64);
                        int64_t res = static_cast<int64_t>(ua / ub);
                        stack[sp++].value.i64 = res;
                        break;
                    }

                    case 0x81: {
                        // i64.rem_s
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        if (b.value.i64 == 0) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                            goto done;
                        }
                        int64_t res = 0;
                        if (a.value.i64 == static_cast<int64_t>(0x8000000000000000ULL) && b.value.i64 == -1) {
                            res = 0;
                        } else {
                            res = a.value.i64 % b.value.i64;
                        }
                        stack[sp++].value.i64 = res;
                        break;
                    }

                    case 0x82: {
                        // i64.rem_u
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        if (b.value.i64 == 0) {
                            result = OnTrap(WasmResult::kErrorExecuteTrapIntegerDivideByZero);
                            goto done;
                        }
                        uint64_t ua = static_cast<uint64_t>(a.value.i64);
                        uint64_t ub = static_cast<uint64_t>(b.value.i64);
                        int64_t res = static_cast<int64_t>(ua % ub);
                        stack[sp++].value.i64 = res;
                        break;
                    }

                    case 0x89: {
                        // i64.rotl
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        int64_t res = static_cast<int64_t>(Rotl64(static_cast<uint64_t>(a.value.i64),
                                                                  static_cast<uint64_t>(b.value.i64)));
                        stack[sp++].value.i64 = res;
                        break;
                    }

                    case 0x8A: {
                        // i64.rotr
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        int64_t res = static_cast<int64_t>(Rotr64(static_cast<uint64_t>(a.value.i64),
                                                                  static_cast<uint64_t>(b.value.i64)));
                        stack[sp++].value.i64 = res;
                        break;
                    }

                    case 0x8B: // f32.abs
                    case 0x8C: // f32.neg
                    case 0x8D: // f32.ceil
                    case 0x8E: // f32.floor
                    case 0x8F: // f32.trunc
                    case 0x90: // f32.nearest
                    case 0x91: {
                        // f32.sqrt
                        WasmValue val = stack[sp - 1];
                        float res = 0.0f;
                        switch (op) {
                            case 0x8B: res = std::fabs(val.value.f32);
                                break;
                            case 0x8C: res = -val.value.f32;
                                break;
                            case 0x8D: res = std::ceil(val.value.f32);
                                break;
                            case 0x8E: res = std::floor(val.value.f32);
                                break;
                            case 0x8F: res = std::trunc(val.value.f32);
                                break;
                            case 0x90: res = NearestF32(val.value.f32);
                                break;
                            case 0x91: res = std::sqrt(val.value.f32);
                                break;
                        }
                        stack[sp - 1].value.f32 = 0;
                        stack[sp - 1].value.f32 = res;
                        break;
                    }

                    case 0x96: // f32.min
                    case 0x97: // f32.max
                    case 0x98: {
                        // f32.copysign
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        float res = 0.0f;
                        if (op == 0x96) {
                            // min
                            if (std::isnan(a.value.f32) || std::isnan(b.value.f32)) {
                                res = std::nanf("");
                            } else if (a.value.f32 == 0.0f && b.value.f32 == 0.0f) {
                                res = (std::signbit(a.value.f32) || std::signbit(b.value.f32)) ? -0.0f : 0.0f;
                            } else {
                                res = std::fmin(a.value.f32, b.value.f32);
                            }
                        } else if (op == 0x97) {
                            // max
                            if (std::isnan(a.value.f32) || std::isnan(b.value.f32)) {
                                res = std::nanf("");
                            } else if (a.value.f32 == 0.0f && b.value.f32 == 0.0f) {
                                res = (std::signbit(a.value.f32) && std::signbit(b.value.f32)) ? -0.0f : 0.0f;
                            } else {
                                res = std::fmax(a.value.f32, b.value.f32);
                            }
                        } else {
                            // copysign
                            res = std::copysign(a.value.f32, b.value.f32);
                        }
                        WasmValue result_val;
                        result_val.value.f32 = res;
                        stack[sp++] = result_val;
                        break;
                    }

                    case 0x99: // f64.abs
                    case 0x9A: // f64.neg
                    case 0x9B: // f64.ceil
                    case 0x9C: // f64.floor
                    case 0x9D: // f64.trunc
                    case 0x9E: // f64.nearest
                    case 0x9F: {
                        // f64.sqrt
                        WasmValue val = stack[sp - 1];
                        double res = 0.0;
                        switch (op) {
                            case 0x99: res = std::fabs(val.value.f64);
                                break;
                            case 0x9A: res = -val.value.f64;
                                break;
                            case 0x9B: res = std::ceil(val.value.f64);
                                break;
                            case 0x9C: res = std::floor(val.value.f64);
                                break;
                            case 0x9D: res = std::trunc(val.value.f64);
                                break;
                            case 0x9E: res = NearestF64(val.value.f64);
                                break;
                            case 0x9F: res = std::sqrt(val.value.f64);
                                break;
                        }
                        stack[sp - 1].value.f64 = 0;
                        stack[sp - 1].value.f64 = res;
                        break;
                    }

                    case 0xA4: // f64.min
                    case 0xA5: // f64.max
                    case 0xA6: {
                        // f64.copysign
                        WasmValue b = stack[--sp];
                        WasmValue a = stack[--sp];
                        double res = 0.0;
                        if (op == 0xA4) {
                            // min
                            if (std::isnan(a.value.f64) || std::isnan(b.value.f64)) {
                                res = std::nan("");
                            } else if (a.value.f64 == 0.0 && b.value.f64 == 0.0) {
                                res = (std::signbit(a.value.f64) || std::signbit(b.value.f64)) ? -0.0 : 0.0;
                            } else {
                                res = std::fmin(a.value.f64, b.value.f64);
                            }
                        } else if (op == 0xA5) {
                            // max
                            if (std::isnan(a.value.f64) || std::isnan(b.value.f64)) {
                                res = std::nan("");
                            } else if (a.value.f64 == 0.0 && b.value.f64 == 0.0) {
                                res = (std::signbit(a.value.f64) && std::signbit(b.value.f64)) ? -0.0 : 0.0;
                            } else {
                                res = std::fmax(a.value.f64, b.value.f64);
                            }
                        } else {
                            // copysign
                            res = std::copysign(a.value.f64, b.value.f64);
                        }
                        WasmValue result_val;
                        result_val.value.f64 = res;
                        stack[sp++] = result_val;
                        break;
                    }

                    case 0xBC: {
                        // i32.reinterpret_f32
                        WasmValue val = stack[sp - 1];
                        int32_t bits;
                        std::memcpy(&bits, &val.value.f32, 4);
                        stack[sp - 1].value.i32 = bits;
                        break;
                    }

                    case 0xBD: {
                        // i64.reinterpret_f64
                        WasmValue val = stack[sp - 1];
                        int64_t bits;
                        std::memcpy(&bits, &val.value.f64, 8);
                        stack[sp - 1].value.i64 = bits;
                        break;
                    }

                    case 0xBE: {
                        // f32.reinterpret_i32
                        WasmValue val = stack[sp - 1];
                        float bits;
                        std::memcpy(&bits, &val.value.i32, 4);
                        stack[sp - 1].value.f32 = 0;
                        stack[sp - 1].value.f32 = bits;
                        break;
                    }

                    case 0xBF: {
                        // f64.reinterpret_i64
                        WasmValue val = stack[sp - 1];
                        double bits;
                        std::memcpy(&bits, &val.value.i64, 8);
                        stack[sp - 1].value.f64 = 0;
                        stack[sp - 1].value.f64 = bits;
                        break;
                    }

                    // Sign extension opcodes (sign extension proposal)
                    case 0xC0: {
                        // i32.extend8_s
                        int32_t v = stack[sp - 1].value.i32;
                        stack[sp - 1].value.i32 = static_cast<int32_t>(static_cast<int8_t>(v & 0xFF));
                        break;
                    }
                    case 0xC1: {
                        // i32.extend16_s
                        int32_t v = stack[sp - 1].value.i32;
                        stack[sp - 1].value.i32 = static_cast<int32_t>(static_cast<int16_t>(v & 0xFFFF));
                        break;
                    }
                    case 0xC2: {
                        // i64.extend8_s
                        int64_t v = stack[sp - 1].value.i64;
                        int64_t res = static_cast<int64_t>(static_cast<int8_t>(v & 0xFF));
                        stack[sp - 1].value.i64 = res;
                        break;
                    }
                    case 0xC3: {
                        // i64.extend16_s
                        int64_t v = stack[sp - 1].value.i64;
                        int64_t res = static_cast<int64_t>(static_cast<int16_t>(v & 0xFFFF));
                        stack[sp - 1].value.i64 = res;
                        break;
                    }
                    case 0xC4: {
                        // i64.extend32_s
                        int64_t v = stack[sp - 1].value.i64;
                        int64_t res = static_cast<int64_t>(static_cast<int32_t>(v & 0xFFFFFFFFULL));
                        stack[sp - 1].value.i64 = res;
                        break;
                    }

                    // ref.null (0xD0): push null reference (stored as i64=-1)
                    case 0xD0: {
                        int32_t heap_type = DecodeVarInt32Fast(ip);
                        (void) heap_type;
                        WasmValue ref_val = {};
                        ref_val.value.i64 = -1;
                        stack[sp++] = ref_val;
                        break;
                    }

                    // ref.is_null (0xD1)
                    case 0xD1: {
                        int64_t ptr_val = stack[--sp].value.i64;
                        stack[sp++].value.i32 = ptr_val == -1 ? 1 : 0;
                        break;
                    }

                    // ref.func (0xD2): push funcref
                    case 0xD2: {
                        uint32_t func_idx = DecodeVarUint32Fast(ip);
                        WasmValue ref_val = {};
                        ref_val.value.i64 = static_cast<int64_t>(func_idx);
                        stack[sp++] = ref_val;
                        break;
                    }

                    case 0xFC: {
                        // saturating truncation and other extended instructions
                        uint32_t sub_op = DecodeVarUint32Fast(ip);
                        switch (sub_op) {
                            case 0: {
                                // i32.trunc_sat_f32_s
                                float fv = stack[sp - 1].value.f32;
                                int32_t res;
                                if (std::isnan(fv)) { res = 0; } else if (fv >= 2147483648.0f) {
                                    res = static_cast<int32_t>(2147483647);
                                } else if (fv < -2147483648.0f) { res = static_cast<int32_t>(0x80000000U); } else {
                                    res = static_cast<int32_t>(fv);
                                }
                                stack[sp - 1].value.i32 = res;
                                break;
                            }
                            case 1: {
                                // i32.trunc_sat_f32_u
                                float fv = stack[sp - 1].value.f32;
                                uint32_t res;
                                if (std::isnan(fv) || fv < 0.0f) { res = 0; } else if (fv >= 4294967296.0f) {
                                    res = 0xFFFFFFFFU;
                                } else { res = static_cast<uint32_t>(fv); }
                                stack[sp - 1].value.i32 = static_cast<int32_t>(res);
                                break;
                            }
                            case 2: {
                                // i32.trunc_sat_f64_s
                                double dv = stack[sp - 1].value.f64;
                                int32_t res;
                                if (std::isnan(dv)) { res = 0; } else if (dv >= 2147483648.0) {
                                    res = static_cast<int32_t>(2147483647);
                                } else if (dv < -2147483648.0) { res = static_cast<int32_t>(0x80000000U); } else {
                                    res = static_cast<int32_t>(dv);
                                }
                                stack[sp - 1].value.i32 = res;
                                break;
                            }
                            case 3: {
                                // i32.trunc_sat_f64_u
                                double dv = stack[sp - 1].value.f64;
                                uint32_t res;
                                if (std::isnan(dv) || dv < 0.0) { res = 0; } else if (dv >= 4294967296.0) {
                                    res = 0xFFFFFFFFU;
                                } else { res = static_cast<uint32_t>(dv); }
                                stack[sp - 1].value.i32 = static_cast<int32_t>(res);
                                break;
                            }
                            case 4: {
                                // i64.trunc_sat_f32_s
                                float fv = stack[sp - 1].value.f32;
                                int64_t res;
                                if (std::isnan(fv)) { res = 0; } else if (fv >= 9223372036854775808.0f) {
                                    res = static_cast<int64_t>(0x7FFFFFFFFFFFFFFFLL);
                                } else if (fv < -9223372036854775808.0f) {
                                    res = static_cast<int64_t>(0x8000000000000000ULL);
                                } else { res = static_cast<int64_t>(fv); }
                                stack[sp - 1].value.i64 = res;
                                break;
                            }
                            case 5: {
                                // i64.trunc_sat_f32_u
                                float fv = stack[sp - 1].value.f32;
                                int64_t res;
                                if (std::isnan(fv) || fv < 0.0f) { res = 0; } else if (fv >= 18446744073709551616.0f) {
                                    res = static_cast<int64_t>(0xFFFFFFFFFFFFFFFFULL);
                                } else { res = static_cast<int64_t>(static_cast<uint64_t>(fv)); }
                                stack[sp - 1].value.i64 = res;
                                break;
                            }
                            case 6: {
                                // i64.trunc_sat_f64_s
                                double dv = stack[sp - 1].value.f64;
                                int64_t res;
                                if (std::isnan(dv)) { res = 0; } else if (dv >= 9223372036854775808.0) {
                                    res = static_cast<int64_t>(0x7FFFFFFFFFFFFFFFLL);
                                } else if (dv < -9223372036854775808.0) {
                                    res = static_cast<int64_t>(0x8000000000000000ULL);
                                } else { res = static_cast<int64_t>(dv); }
                                stack[sp - 1].value.i64 = res;
                                break;
                            }
                            case 7: {
                                // i64.trunc_sat_f64_u
                                double dv = stack[sp - 1].value.f64;
                                int64_t res;
                                if (std::isnan(dv) || dv < 0.0) { res = 0; } else if (dv >= 18446744073709551616.0) {
                                    res = static_cast<int64_t>(0xFFFFFFFFFFFFFFFFULL);
                                } else { res = static_cast<int64_t>(static_cast<uint64_t>(dv)); }
                                stack[sp - 1].value.i64 = res;
                                break;
                            }
                            case 8: {
                                // memory.init
                                uint32_t data_idx = DecodeVarUint32Fast(ip);
                                ip++; // memory index (0)
                                int32_t n = stack[--sp].value.i32;
                                int32_t s = stack[--sp].value.i32;
                                int32_t d = stack[--sp].value.i32;

                                if (n < 0 || s < 0 || d < 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                                    goto done;
                                }
                                if (data_idx >= data_segment_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapDataOutOfBounds);
                                    goto done;
                                }
                                uint32_t dseg_size = data_segment_dropped[data_idx] ? 0 : data_segment_sizes[data_idx];
                                if (static_cast<uint64_t>(s) + n > dseg_size) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapDataOutOfBounds);
                                    goto done;
                                }
                                if (static_cast<uint64_t>(d) + n > linear_memory_size) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                                    goto done;
                                }
                                if (n == 0) break;
                                if (linear_memory_ptr && data_segments[data_idx]) {
                                    std::memcpy(linear_memory_ptr + d, data_segments[data_idx] + s, n);
                                }
                                break;
                            }
                            case 9: {
                                // data.drop
                                uint32_t data_idx = DecodeVarUint32Fast(ip);
                                if (data_idx >= data_segment_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapDataOutOfBounds);
                                    goto done;
                                }
                                data_segment_dropped[data_idx] = true;
                                data_segments[data_idx] = nullptr;
                                data_segment_sizes[data_idx] = 0;
                                break;
                            }
                            case 10: {
                                // memory.copy
                                ip++; // dst memory (0)
                                ip++; // src memory (0)
                                int32_t n = stack[--sp].value.i32;
                                int32_t s = stack[--sp].value.i32;
                                int32_t d = stack[--sp].value.i32;

                                if (n < 0 || s < 0 || d < 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                                    goto done;
                                }
                                if (static_cast<uint64_t>(s) + n > linear_memory_size ||
                                    static_cast<uint64_t>(d) + n > linear_memory_size) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                                    goto done;
                                }
                                if (n == 0) break;
                                if (linear_memory_ptr) {
                                    std::memmove(linear_memory_ptr + d, linear_memory_ptr + s, n);
                                }
                                break;
                            }
                            case 11: {
                                // memory.fill
                                ip++; // memory (0)
                                int32_t n = stack[--sp].value.i32;
                                int32_t val = stack[--sp].value.i32;
                                int32_t d = stack[--sp].value.i32;

                                if (n < 0 || d < 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                                    goto done;
                                }
                                if (static_cast<uint64_t>(d) + n > linear_memory_size) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapMemoryOutOfBounds);
                                    goto done;
                                }
                                if (n == 0) break;
                                if (linear_memory_ptr) {
                                    std::memset(linear_memory_ptr + d, val, n);
                                }
                                break;
                            }
                            case 12: {
                                // table.init
                                uint32_t elem_idx = DecodeVarUint32Fast(ip);
                                uint32_t table_idx = DecodeVarUint32Fast(ip);
                                int32_t n = stack[--sp].value.i32;
                                int32_t s = stack[--sp].value.i32;
                                int32_t d = stack[--sp].value.i32;

                                if (n < 0 || s < 0 || d < 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (elem_idx >= elem_segment_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapDataOutOfBounds);
                                    goto done;
                                }
                                if (table_idx >= table_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                uint32_t eseg_size = elem_segment_dropped[elem_idx] ? 0 : elem_segment_sizes[elem_idx];
                                if (static_cast<uint64_t>(s) + n > eseg_size) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapDataOutOfBounds);
                                    goto done;
                                }

                                if (static_cast<uint64_t>(d) + n > table_sizes[table_idx]) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (n == 0) break;
                                uint32_t *tbl = tables[table_idx];
                                uint32_t *elms = elem_segments[elem_idx];
                                if (tbl && elms) {
                                    bool is_funcref = (table_types[table_idx] == WasmType::kFuncRef);
                                    for (int32_t i = 0; i < n; ++i) {
                                        tbl[d + i] = is_funcref
                                                         ? EncodeFuncRef(this, current_mod, elms[s + i])
                                                         : elms[s + i];
                                    }
                                }
                                break;
                            }
                            case 13: {
                                // elem.drop
                                uint32_t elem_idx = DecodeVarUint32Fast(ip);
                                if (elem_idx >= elem_segment_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapDataOutOfBounds);
                                    goto done;
                                }
                                elem_segment_dropped[elem_idx] = true;
                                if (elem_segments[elem_idx]) {
                                    pool_->Free(elem_segments[elem_idx]);
                                    elem_segments[elem_idx] = nullptr;
                                }
                                elem_segment_sizes[elem_idx] = 0;
                                break;
                            }
                            case 14: {
                                // table.copy
                                uint32_t dst_table = DecodeVarUint32Fast(ip);
                                uint32_t src_table = DecodeVarUint32Fast(ip);
                                int32_t n = stack[--sp].value.i32;
                                int32_t s = stack[--sp].value.i32;
                                int32_t d = stack[--sp].value.i32;

                                if (n < 0 || s < 0 || d < 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (dst_table >= table_count || src_table >= table_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (static_cast<uint64_t>(s) + n > table_sizes[src_table] ||
                                    static_cast<uint64_t>(d) + n > table_sizes[dst_table]) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (n == 0) break;
                                uint32_t *tbl_dst = tables[dst_table];
                                uint32_t *tbl_src = tables[src_table];
                                if (tbl_dst && tbl_src) {
                                    std::memmove(tbl_dst + d, tbl_src + s, n * sizeof(uint32_t));
                                }
                                break;
                            }
                            case 15: {
                                // table.grow
                                uint32_t table_idx = DecodeVarUint32Fast(ip);
                                int32_t n = stack[--sp].value.i32;
                                WasmValue init_val = stack[--sp];

                                if (table_idx >= table_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (n < 0) {
                                    stack[sp++].value.i32 = -1;
                                    break;
                                }
                                if (n == 0) {
                                    stack[sp++].value.i32 = static_cast<int32_t>(table_sizes[table_idx]);
                                    break;
                                }

                                uint32_t old_size = table_sizes[table_idx];
                                uint64_t new_size = static_cast<uint64_t>(old_size) + n;
                                if (new_size > table_max_sizes[table_idx]) {
                                    stack[sp++].value.i32 = -1;
                                    break;
                                }
                                uint32_t *new_tbl = static_cast<uint32_t *>(pool_->
                                    Allocate(new_size * sizeof(uint32_t)));
                                if (!new_tbl) {
                                    stack[sp++].value.i32 = -1;
                                    break;
                                }

                                uint32_t *old_tbl_ptr = tables[table_idx];
                                if (old_tbl_ptr) {
                                    std::memcpy(new_tbl, old_tbl_ptr, old_size * sizeof(uint32_t));
                                    pool_->Free(old_tbl_ptr);
                                }
                                bool is_funcref = (table_types[table_idx] == WasmType::kFuncRef);
                                uint32_t fill_val = (init_val.value.i64 < 0)
                                                        ? 0xFFFFFFFF
                                                        : (is_funcref
                                                               ? EncodeFuncRef(
                                                                   this, current_mod,
                                                                   static_cast<uint32_t>(init_val.value.i64))
                                                               : static_cast<uint32_t>(init_val.value.i64));
                                for (uint32_t i = old_size; i < new_size; ++i) {
                                    new_tbl[i] = fill_val;
                                }

                                tables[table_idx] = new_tbl;
                                table_sizes[table_idx] = static_cast<uint32_t>(new_size);
                                if (old_tbl_ptr) {
                                    for (std::size_t _m = 0; _m < kMaxModules; ++_m) {
                                        if (!modules_[_m] || !modules_[_m]->is_active) continue;
                                        for (std::size_t _t = 0; _t < modules_[_m]->table_count; ++_t) {
                                            if (modules_[_m]->tables[_t] == old_tbl_ptr) {
                                                modules_[_m]->tables[_t] = new_tbl;
                                                modules_[_m]->table_sizes[_t] = static_cast<uint32_t>(new_size);
                                            }
                                        }
                                    }
                                }

                                stack[sp++].value.i32 = static_cast<int32_t>(old_size);
                                break;
                            }
                            case 16: {
                                // table.size
                                uint32_t table_idx = DecodeVarUint32Fast(ip);
                                if (table_idx >= table_count) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                stack[sp++].value.i32 = static_cast<int32_t>(table_sizes[table_idx]);
                                break;
                            }
                            case 17: {
                                // table.fill
                                uint32_t table_idx = DecodeVarUint32Fast(ip);
                                int32_t n = stack[--sp].value.i32;
                                WasmValue val = stack[--sp];
                                int32_t idx = stack[--sp].value.i32;

                                if (n < 0 || idx < 0) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (table_idx >= table_count || static_cast<uint64_t>(idx) + n > table_sizes[
                                        table_idx]) {
                                    result = OnTrap(WasmResult::kErrorExecuteTrapTableOutOfBounds);
                                    goto done;
                                }
                                if (n == 0) break;
                                uint32_t *tbl = tables[table_idx];
                                if (tbl) {
                                    bool is_funcref = (table_types[table_idx] == WasmType::kFuncRef);
                                    uint32_t fill_val = (val.value.i64 < 0)
                                                            ? 0xFFFFFFFF
                                                            : (is_funcref
                                                                   ? EncodeFuncRef(
                                                                       this, current_mod,
                                                                       static_cast<uint32_t>(val.value.i64))
                                                                   : static_cast<uint32_t>(val.value.i64));
                                    for (int32_t i = 0; i < n; ++i) {
                                        tbl[idx + i] = fill_val;
                                    }
                                }
                                break;
                            }
                            default:
                                result = OnTrap(WasmResult::kErrorExecuteRuntimeError);
                                goto done;
                        }
                        break;
                    }

                    default:
                        // 未対応のオペコード
                        result = OnTrap(WasmResult::kErrorExecuteRuntimeError);
                        goto done;
                }
            }

        frame_changed:
            if (ctx->call_stack_top == 0) {
                result = WasmResult::kOk;
                goto done;
            }
            continue;
        }

        result = WasmResult::kOk;

    done:
        ctx->stack_top = sp;
        return result;
    }

    void *WasmEngine::GetModuleUserData(HostModuleId module_id) const noexcept {
        uint32_t idx = static_cast<uint32_t>(module_id);
        if (module_user_datas_ && idx < kHostModuleCount) {
            return module_user_datas_[idx];
        }
        return nullptr;
    }

    void WasmEngine::SetModuleUserData(HostModuleId module_id, void *user_data) noexcept {
        uint32_t idx = static_cast<uint32_t>(module_id);
        if (module_user_datas_ && idx < kHostModuleCount) {
            module_user_datas_[idx] = user_data;
        }
    }

    void GetLinearMemoryForHostApi(WasmEngine& engine, uint8_t *&mem_base, size_t &mem_size) noexcept {
        auto module = engine.GetCurrentThreadContext()->GetCurrentModule();
        mem_base = module->GetLinearMemory();
        mem_size = module->GetLinearMemorySize();
    }

// -----------------------------------------------------------------------------
// WasmScheduler implementation (merged from wasm_thread.cpp)
// -----------------------------------------------------------------------------

#if EMBWASM_ENABLE_MULTITHREADING

bool WasmThreadContext::Init(WasmMemoryPool& pool, const WasmEngineConfig& cfg) noexcept {
    stack = nullptr; call_stack = nullptr; labels_pool = nullptr;
    stack       = static_cast<WasmValue*>(pool.Allocate(cfg.stack_size * sizeof(WasmValue)));
    call_stack  = static_cast<WasmFrame*>(pool.Allocate(cfg.call_stack_size * sizeof(WasmFrame)));
    labels_pool = static_cast<WasmLabel*>(pool.Allocate(cfg.labels_pool_size * sizeof(WasmLabel)));
    if (!stack || !call_stack || !labels_pool) return false;
    stack_size       = cfg.stack_size;
    call_stack_size  = cfg.call_stack_size;
    labels_pool_size = cfg.labels_pool_size;
    Reset();
    return true;
}

void WasmThreadContext::DeInit(WasmMemoryPool& pool) noexcept {
    if (labels_pool) { pool.Free(labels_pool); labels_pool = nullptr; }
    if (call_stack)  { pool.Free(call_stack);  call_stack  = nullptr; }
    if (stack)       { pool.Free(stack);       stack       = nullptr; }
    stack_size = call_stack_size = labels_pool_size = 0;
    Reset();
}

uint32_t WasmEngine::SetupMainThread(WasmModuleInstance* mod, uint32_t func_index) noexcept {
    if (!threads_ || !threads_[kMainThreadIndex]) return 0;
    WasmThreadContext& main = *threads_[kMainThreadIndex];
    main.Reset();
    main.id = static_cast<uint32_t>(kMainThreadIndex + 1);
    main.state = ThreadState::kReady;
    main.stack_top = 0;
    main.call_stack_top = 0;
    main.labels_pool_top = 0;
    main.start_func_index = func_index;
    main.start_module = mod;
    AddLastListNode(&ready_list_, &main.list_node);
    return main.id;
}

uint32_t WasmEngine::CreateThread(uint32_t func_index) noexcept {
    if (!threads_) return 0;
    for (std::size_t i = kMainThreadIndex + 1; i < kMaxThreads; ++i) {
        if (!threads_[i] || threads_[i]->state == ThreadState::kTerminated) {
            if (!threads_[i]) {
                WasmMemoryPool* pool = GetMemoryPool();
                if (!pool) return 0;
                void* allocated = pool->Allocate(sizeof(WasmThreadContext));
                if (!allocated) return 0;
                WasmThreadContext* tctx = static_cast<WasmThreadContext*>(allocated);
                threads_[i] = tctx;
                tctx->Init(*pool, config_);
                tctx->id = static_cast<uint32_t>(i + 1);
            }
            threads_[i]->state = ThreadState::kReady;
            threads_[i]->stack_top = 0;
            threads_[i]->call_stack_top = 0;
            threads_[i]->labels_pool_top = 0;
            threads_[i]->start_func_index = func_index;
            AddLastListNode(&ready_list_, &threads_[i]->list_node);

            WasmThreadContext* active_ctx = GetCurrentThreadContext();
            WasmModuleInstance* caller_mod = nullptr;
            if (active_ctx && active_ctx->call_stack_top > 0) {
                const WasmFrame& top = active_ctx->call_stack[active_ctx->call_stack_top - 1];
                if (top.func) caller_mod = const_cast<WasmModuleInstance*>(top.func->module);
            }
            threads_[i]->start_module = caller_mod;

            return threads_[i]->id;
        }
    }
    return 0;
}

uint32_t WasmEngine::CreateEvent() noexcept {
    for (std::size_t i = 0; i < kMaxEvents; ++i) {
        if (events_[i].id != 0 && !events_[i].signaled) {
            return events_[i].id;
        }
    }
    return 0;
}

void WasmEngine::SignalEvent(uint32_t event_id) noexcept {
    if (!threads_ || event_id == 0 || event_id > kMaxEvents) return;
    events_[event_id - 1].signaled = true;

    ListNode* wq = &events_[event_id - 1].wait_list_;
    ListNode* node = wq->next;
    while (node != wq) {
        ListNode* next_node = node->next;
        RemoveListNode(node);
        WasmThreadContext* t = thread_from_node(node);
        t->state     = ThreadState::kReady;
        t->wait_kind = WaitKind::kNone;
        AddLastListNode(&ready_list_, node);
        node = next_node;
    }
    PlatformNotifyActivity(*this);
}

void WasmEngine::WaitEvent(uint32_t thread_id, uint32_t event_id) noexcept {
    if (!threads_ || thread_id == 0 || thread_id > kMaxThreads) return;
    if (event_id == 0 || event_id > kMaxEvents) return;

    WasmThreadContext* ctx = threads_[thread_id - 1];
    if (!ctx) return;
    if (events_[event_id - 1].signaled) {
        events_[event_id - 1].signaled = false;
        ctx->state    = ThreadState::kReady;
        ctx->wait_kind = WaitKind::kNone;
    } else {
        ctx->state    = ThreadState::kWaiting;
        ctx->wait_kind = WaitKind::kEvent;
        ctx->wait_param.event_id = event_id;
        AddLastListNode(&events_[event_id - 1].wait_list_, &ctx->list_node);
    }
}

void WasmEngine::ThreadWait(uint32_t thread_id) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return;
    if (ctx->notify_pending) {
        ctx->notify_pending = false;
        ctx->state    = ThreadState::kReady;
        ctx->wait_kind = WaitKind::kNone;
    } else {
        ctx->state    = ThreadState::kWaiting;
        ctx->wait_kind = WaitKind::kNotify;
    }
}

void WasmEngine::ThreadNotify(uint32_t thread_id) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return;
    if (ctx->state == ThreadState::kWaiting && ctx->wait_kind == WaitKind::kNotify) {
        ctx->state     = ThreadState::kReady;
        ctx->wait_kind = WaitKind::kNone;
        AddLastListNode(&ready_list_, &ctx->list_node);
        PlatformNotifyActivity(*this);
    } else {
        ctx->notify_pending = true;
    }
}

void WasmEngine::ThreadSleep(uint32_t thread_id, uint32_t duration_ms) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return;
    ctx->state    = ThreadState::kWaiting;
    ctx->wait_kind = WaitKind::kSleep;
    ctx->wait_param.wake_time_ms = PlatformGetTimeMs() + duration_ms;
    AddLastListNode(&timeout_list_, &ctx->list_node);
}

bool WasmEngine::HasReadyThread() noexcept {
    return !IsEmptyListNode(&ready_list_);
}

uint32_t WasmEngine::ComputeMinSleepTimeout() noexcept {
    uint32_t now = PlatformGetTimeMs();
    uint32_t min_timeout = UINT32_MAX;
    for (ListNode* node = timeout_list_.next; node != &timeout_list_; node = node->next) {
        WasmThreadContext* t = thread_from_node(node);
        uint32_t rem = (t->wait_param.wake_time_ms > now)
                       ? t->wait_param.wake_time_ms - now : 0;
        if (rem < min_timeout) min_timeout = rem;
    }
    return min_timeout;
}

void WasmEngine::PollSleeps() noexcept {
    uint32_t now = PlatformGetTimeMs();
    ListNode* node = timeout_list_.next;
    while (node != &timeout_list_) {
        ListNode* next_node = node->next;
        WasmThreadContext* t = thread_from_node(node);
        if (now >= t->wait_param.wake_time_ms) {
            RemoveListNode(node);
            t->state     = ThreadState::kReady;
            t->wait_kind = WaitKind::kNone;
            AddLastListNode(&ready_list_, node);
        }
        node = next_node;
    }
}

WasmResult WasmEngine::Run() noexcept {
    return RunInternal(RunInternalFlags::kUseLock |
                       RunInternalFlags::kWithLifecycle |
                       RunInternalFlags::kAbortable |
                       RunInternalFlags::kLoopForever);
}

WasmResult WasmEngine::RunInternal(RunInternalFlags flags) noexcept {
    if (!threads_) return WasmResult::kErrorOutOfMemory;

    const bool use_lock       = (static_cast<uint32_t>(flags) & static_cast<uint32_t>(RunInternalFlags::kUseLock)) != 0;
    const bool with_lifecycle = (static_cast<uint32_t>(flags) & static_cast<uint32_t>(RunInternalFlags::kWithLifecycle)) != 0;
    const bool abortable      = (static_cast<uint32_t>(flags) & static_cast<uint32_t>(RunInternalFlags::kAbortable)) != 0;
    const bool loop_forever   = (static_cast<uint32_t>(flags) & static_cast<uint32_t>(RunInternalFlags::kLoopForever)) != 0;

    if (with_lifecycle) {
        WasmResult res = PlatformEngineRunBegin(*this);
        if (IsError(res)) return res;
    }

    WasmResult final_result = WasmResult::kOk;

    if (use_lock) {
        while (true) {
            PlatformLock(*this);

            if (abortable && stop_requested_) {
                PlatformUnlock(*this);
                break;
            }

            PollSleeps();

            bool any_active = false;
            for (std::size_t i = 0; i < kMaxThreads; ++i) {
                WasmThreadContext* t = threads_[i];
                if (t && t->state != ThreadState::kTerminated && t->state != ThreadState::kCreated) {
                    any_active = true;
                    break;
                }
            }
            if (!any_active) {
                if (!loop_forever) {
                    PlatformUnlock(*this);
                    break;
                }
                PlatformUnlock(*this);
                PlatformWaitForActivity(*this, UINT32_MAX);
                continue;
            }

            WasmThreadContext* ctx = nullptr;
            {
                ListNode* node = PopFrontListNode(&ready_list_);
                if (node) {
                    ctx = thread_from_node(node);
                    ctx->state = ThreadState::kRunning;
                }
            }

            uint32_t sleep_timeout = ctx ? 0 : ComputeMinSleepTimeout();
            PlatformUnlock(*this);

            if (ctx) {
                if (ctx->call_stack_top == 0 &&
                    ctx->id != static_cast<uint32_t>(kMainThreadIndex + 1) &&
                    ctx->wasm_stack_base == 0) {
                    WasmResult alloc_res = AllocThreadWasmStack(ctx);
                    if (alloc_res != WasmResult::kOk) {
                        PlatformLock(*this);
                        ctx->execution_result = alloc_res;
                        ctx->state = ThreadState::kTerminated;
                        PlatformUnlock(*this);
                        if (ctx->completion_callback)
                            ctx->completion_callback(ctx, ctx->thread_user_data, alloc_res);
                        final_result = alloc_res;
                        break;
                    }
                }
                RestoreThreadStackPointer(ctx);

                WasmResult res = (ctx->call_stack_top == 0)
                    ? ExecuteInternal(ctx->start_module, ctx->start_func_index)
                    : RunLoop(ctx);

                SaveThreadStackPointer(ctx);
                if (res != WasmResult::kYield) {
                    FreeThreadWasmStack(ctx);
                }

                PlatformLock(*this);
                if (res == WasmResult::kYield) {
                    if (ctx->state == ThreadState::kRunning) {
                        ctx->state = ThreadState::kReady;
                    }
                    if (ctx->state == ThreadState::kReady) {
                        AddLastListNode(&ready_list_, &ctx->list_node);
                    }
                } else {
                    ctx->execution_result = res;
                    ctx->state = ThreadState::kTerminated;
                }
                WasmThreadCompletionCallback cb = ctx->completion_callback;
                void* ud = ctx->thread_user_data;
                WasmResult result_for_cb = ctx->execution_result;
                PlatformUnlock(*this);

                if (res != WasmResult::kYield && cb) {
                    cb(ctx, ud, result_for_cb);
                }

                if (IsError(res)) {
                    final_result = res;
                    break;
                }
            } else {
                PlatformWaitForActivity(*this, sleep_timeout);
            }
        }
    } else {
        while (true) {
            bool any_active = false;
            for (std::size_t i = 0; i < kMaxThreads; ++i) {
                WasmThreadContext* t = threads_[i];
                if (!t || t->state == ThreadState::kTerminated || t->state == ThreadState::kCreated) continue;
                any_active = true;
                break;
            }
            if (!any_active) break;

            if (!IsEmptyListNode(&ready_list_)) {
                WasmResult res = Step();
                if (res != WasmResult::kOk && res != WasmResult::kYield) {
                    final_result = res;
                    break;
                }
            } else {
                PollSleeps();
                if (!HasReadyThread()) {
                    PlatformWaitForActivity(*this, ComputeMinSleepTimeout());
                    PollSleeps();
                }
            }
        }
    }

    if (with_lifecycle) {
        PlatformEngineRunEnd(*this);
    }

    return final_result;
}

WasmResult WasmEngine::Step() noexcept {
    ListNode* node = PopFrontListNode(&ready_list_);
    if (!node) return WasmResult::kOk;

    WasmThreadContext& ctx = *thread_from_node(node);
    ctx.state = ThreadState::kRunning;

    if (ctx.call_stack_top == 0 &&
        ctx.id != static_cast<uint32_t>(kMainThreadIndex + 1) &&
        ctx.wasm_stack_base == 0) {
        WasmResult alloc_res = AllocThreadWasmStack(&ctx);
        if (alloc_res != WasmResult::kOk) {
            ctx.execution_result = alloc_res;
            ctx.state = ThreadState::kTerminated;
            if (ctx.completion_callback)
                ctx.completion_callback(&ctx, ctx.thread_user_data, alloc_res);
            return alloc_res;
        }
    }
    RestoreThreadStackPointer(&ctx);

    WasmResult res;
    if (ctx.call_stack_top == 0) {
        res = ExecuteInternal(ctx.start_module, ctx.start_func_index);
    } else {
        res = RunLoop(&ctx);
    }

    SaveThreadStackPointer(&ctx);
    if (res != WasmResult::kYield) {
        FreeThreadWasmStack(&ctx);
    }

    if (res == WasmResult::kYield) {
        if (ctx.state == ThreadState::kRunning) {
            ctx.state = ThreadState::kReady;
        }
        if (ctx.state == ThreadState::kReady) {
            AddLastListNode(&ready_list_, &ctx.list_node);
        }
    } else {
        ctx.execution_result = res;
        ctx.state = ThreadState::kTerminated;
        if (ctx.completion_callback) {
            ctx.completion_callback(&ctx, ctx.thread_user_data, res);
        }
        if (res != WasmResult::kOk) return res;
    }

    return WasmResult::kOk;
}

WasmResult WasmEngine::AllocThreadWasmStack(WasmThreadContext* ctx) noexcept {
    WasmModuleInstance* mod = ctx->start_module;
    if (!mod || mod->stack_ptr_global_idx == UINT32_MAX) return WasmResult::kOk;

    const uint32_t size = mod->thread_stack_size
                          ? mod->thread_stack_size
                          : config_.thread_wasm_stack_size;
    // ユーザー引数の上に cabi_realloc(0, 0, 16, size) の 4 引数を積む
    ctx->stack[ctx->stack_top++].value.i32 = 0;
    ctx->stack[ctx->stack_top++].value.i32 = 0;
    ctx->stack[ctx->stack_top++].value.i32 = 16;
    ctx->stack[ctx->stack_top++].value.i32 = static_cast<int32_t>(size);
    WasmResult res = ExecuteInternal(mod, mod->cabi_realloc_func_idx);
    uint32_t base_ptr = (res == WasmResult::kOk && ctx->stack_top > 0)
        ? static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32) : 0;

    if (res != WasmResult::kOk || base_ptr == 0) return WasmResult::kErrorOutOfMemory;

    ctx->wasm_stack_base = base_ptr;
    ctx->wasm_stack_size = size;
    ctx->wasm_saved_sp   = base_ptr + size;
    return WasmResult::kOk;
}

void WasmEngine::FreeThreadWasmStack(WasmThreadContext* ctx) noexcept {
    if (!ctx->wasm_stack_base) return;
    WasmModuleInstance* mod = ctx->start_module;
    if (!mod || mod->cabi_realloc_func_idx == UINT32_MAX) return;

    ctx->stack[ctx->stack_top++].value.i32 = static_cast<int32_t>(ctx->wasm_stack_base);
    ctx->stack[ctx->stack_top++].value.i32 = static_cast<int32_t>(ctx->wasm_stack_size);
    ctx->stack[ctx->stack_top++].value.i32 = 16;
    ctx->stack[ctx->stack_top++].value.i32 = 0;
    ExecuteInternal(mod, mod->cabi_realloc_func_idx);
    ctx->stack_top = ctx->call_stack_top = 0;
    ctx->wasm_stack_base = ctx->wasm_stack_size = ctx->wasm_saved_sp = 0;
}

void WasmEngine::SaveThreadStackPointer(WasmThreadContext* ctx) noexcept {
    WasmModuleInstance* mod = ctx->start_module;
    if (!mod || mod->stack_ptr_global_idx == UINT32_MAX) return;
    ctx->wasm_saved_sp = static_cast<uint32_t>(
        mod->globals[mod->stack_ptr_global_idx].value.value.i32);
}

void WasmEngine::RestoreThreadStackPointer(WasmThreadContext* ctx) noexcept {
    if (!ctx->wasm_saved_sp) return;
    WasmModuleInstance* mod = ctx->start_module;
    if (!mod || mod->stack_ptr_global_idx == UINT32_MAX) return;
    mod->globals[mod->stack_ptr_global_idx].value.value.i32 =
        static_cast<int32_t>(ctx->wasm_saved_sp);
}

uint32_t WasmEngine::CreateHostThread(WasmModuleInstance* module, uint32_t func_index) noexcept {
    if (!threads_ || !module) return 0;
    PlatformLock(*this);
    uint32_t result_id = 0;
    for (std::size_t i = kMainThreadIndex + 1; i < kMaxThreads; ++i) {
        if (!threads_[i] || (threads_[i]->state == ThreadState::kTerminated && !threads_[i]->requires_destroy)) {
            if (!threads_[i]) {
                WasmMemoryPool* pool = GetMemoryPool();
                if (!pool) break;
                void* allocated = pool->Allocate(sizeof(WasmThreadContext));
                if (!allocated) break;
                WasmThreadContext* tctx = static_cast<WasmThreadContext*>(allocated);
                threads_[i] = tctx;
                tctx->Init(*pool, config_);
            }
            WasmThreadContext* ctx = threads_[i];
            ctx->Reset();
            ctx->id               = static_cast<uint32_t>(i + 1);
            ctx->state            = ThreadState::kCreated;
            ctx->start_module     = module;
            ctx->start_func_index = func_index;
            ctx->requires_destroy = true;
            result_id = ctx->id;
            break;
        }
    }
    PlatformUnlock(*this);
    if (result_id != 0) PlatformNotifyActivity(*this);
    return result_id;
}

bool WasmEngine::DestroyHostThread(uint32_t thread_id) noexcept {
    if (!threads_ || thread_id == 0) return false;
    const auto idx = static_cast<std::size_t>(thread_id - 1);
    if (idx < kMainThreadIndex + 1 || idx >= kMaxThreads) return false;
    PlatformLock(*this);
    WasmThreadContext* ctx = threads_[idx];
    bool ok = false;
    if (ctx && ctx->requires_destroy &&
        (ctx->state == ThreadState::kTerminated || ctx->state == ThreadState::kCreated)) {
        ctx->Reset();
        ok = true;
    }
    PlatformUnlock(*this);
    return ok;
}

void WasmEngine::SetThreadCallback(uint32_t thread_id,
                                      WasmThreadCompletionCallback callback,
                                      void* user_data) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return;
    ctx->completion_callback = callback;
    ctx->thread_user_data    = user_data;
}

WasmResult WasmEngine::PushThreadArg(uint32_t thread_id, WasmValue value) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx) return WasmResult::kErrorInvalidArgument;
    if (ctx->stack_top >= ctx->stack_size) return WasmResult::kErrorExecuteTrapStackOverflow;
    ctx->stack[ctx->stack_top++] = value;
    return WasmResult::kOk;
}

void WasmEngine::StartThread(uint32_t thread_id) noexcept {
    WasmThreadContext* ctx = GetThreadContext(thread_id);
    if (!ctx || ctx->state != ThreadState::kCreated) return;
    PlatformLock(*this);
    ctx->state = ThreadState::kReady;
    AddLastListNode(&ready_list_, &ctx->list_node);
    PlatformUnlock(*this);
    PlatformNotifyActivity(*this);
}

WasmResult WasmEngine::GetThreadResult(uint32_t thread_id) const noexcept {
    if (thread_id == 0 || thread_id > kMaxThreads || !threads_) return WasmResult::kErrorInvalidArgument;
    const WasmThreadContext* ctx = threads_[thread_id - 1];
    if (!ctx) return WasmResult::kErrorInvalidArgument;
    return ctx->execution_result;
}

void WasmEngine::Stop() noexcept {
    PlatformLock(*this);
    stop_requested_ = true;
    PlatformUnlock(*this);
    PlatformNotifyActivity(*this);
}


#endif // EMBWASM_ENABLE_MULTITHREADING

} // namespace embwasm
