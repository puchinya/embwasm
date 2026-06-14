// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
//
// [Clean-room Implementation Notice]
// This WebAssembly execution library has been designed and implemented entirely 
// from scratch based on the official WebAssembly Core Specification. No source 
// code from existing open-source WASM engines (such as wasm3, WAMR, or wabt) 
// has been copied, referenced, or adapted.
// =============================================================================

#include "wasm_engine.h"
#include "wasm_platform.h"
#include <cstring>

namespace embwasm {

// =============================================================================
// LEB128 (Little Endian Base 128) Decoders
// Designed cleanly from the W3C WebAssembly Specification.
// =============================================================================

// Decodes a variable-length unsigned 32-bit integer.
static inline uint32_t DecodeVarUint32(const uint8_t*& cursor, const uint8_t* limit) noexcept {
    uint32_t decoded_value = 0;
    uint32_t shift_amount = 0;
    
    while (cursor < limit) {
        uint8_t raw_byte = *cursor++;
        decoded_value |= static_cast<uint32_t>(raw_byte & 0x7F) << shift_amount;
        if ((raw_byte & 0x80) == 0) {
            break;
        }
        shift_amount += 7;
    }
    return decoded_value;
}

// Decodes a variable-length signed 32-bit integer.
static inline int32_t DecodeVarInt32(const uint8_t*& cursor, const uint8_t* limit) noexcept {
    int32_t decoded_value = 0;
    uint32_t shift_amount = 0;
    uint8_t raw_byte = 0;
    
    while (cursor < limit) {
        raw_byte = *cursor++;
        decoded_value |= static_cast<int32_t>(raw_byte & 0x7F) << shift_amount;
        shift_amount += 7;
        if ((raw_byte & 0x80) == 0) {
            break;
        }
    }
    
    // Sign extension for negative LEB128 numbers
    if ((shift_amount < 32) && (raw_byte & 0x40)) {
        decoded_value |= (~0U << shift_amount);
    }
    return decoded_value;
}

// Decodes a variable-length signed 64-bit integer.
static inline int64_t DecodeVarInt64(const uint8_t*& cursor, const uint8_t* limit) noexcept {
    int64_t decoded_value = 0;
    uint32_t shift_amount = 0;
    uint8_t raw_byte = 0;
    
    while (cursor < limit) {
        raw_byte = *cursor++;
        decoded_value |= static_cast<int64_t>(raw_byte & 0x7F) << shift_amount;
        shift_amount += 7;
        if ((raw_byte & 0x80) == 0) {
            break;
        }
    }
    
    // Sign extension for negative LEB128 numbers
    if ((shift_amount < 64) && (raw_byte & 0x40)) {
        decoded_value |= (static_cast<int64_t>(~0ULL) << shift_amount);
    }
    return decoded_value;
}

// =============================================================================
// WasmEngine 実装
// =============================================================================

WasmEngine::WasmEngine(WasmMemoryPool& pool) noexcept
    : pool_(pool), signature_count_(0), function_count_(0), export_count_(0), stack_top_(0),
      max_call_stack_depth_(0), max_stack_depth_(0) {
    for (std::size_t i = 0; i < kMaxWasmFunctions; ++i) {
        functions_[i] = {};
        exports_[i] = {};
    }
    for (std::size_t i = 0; i < kMaxWasmTypes; ++i) {
        signatures_[i] = {};
    }
}

WasmResult WasmEngine::Load(const uint8_t* binary, std::size_t size) noexcept {
    if (size < 8) return WasmResult::kErrorInvalidMagic;

    // マジックナンバー "\0asm" の検証
    if (binary[0] != 0x00 || binary[1] != 0x61 || binary[2] != 0x73 || binary[3] != 0x6d) {
        return WasmResult::kErrorInvalidMagic;
    }
    // バージョン 1 の検証
    if (binary[4] != 0x01 || binary[5] != 0x00 || binary[6] != 0x00 || binary[7] != 0x00) {
        return WasmResult::kErrorInvalidVersion;
    }

    return ParseSections(binary + 8, size - 8);
}

WasmResult WasmEngine::ParseSections(const uint8_t* binary, std::size_t size) noexcept {
    const uint8_t* ptr = binary;
    const uint8_t* end = binary + size;

    uint32_t code_index_offset = 0; // インポート関数の数。Code sectionの関数インデックスはインポート関数の後に続きます。

    while (ptr < end) {
        uint8_t section_id = *ptr++;
        uint32_t section_size = DecodeVarUint32(ptr, end);
        const uint8_t* section_end = ptr + section_size;
        if (section_end > end) return WasmResult::kErrorRuntimeError;

        switch (section_id) {
            case 1: { // Type Section (型定義)
                uint32_t type_count = DecodeVarUint32(ptr, section_end);
                for (uint32_t i = 0; i < type_count; ++i) {
                    if (ptr >= section_end) return WasmResult::kErrorRuntimeError;
                    uint8_t form = *ptr++;
                    if (form != 0x60) { // 0x60 = Function Type
                        return WasmResult::kErrorUnknownSection;
                    }

                    uint32_t param_count = DecodeVarUint32(ptr, section_end);
                    if (param_count > WasmTypeSignature::kMaxParams) {
                        return WasmResult::kErrorOutOfMemory;
                    }
                    
                    WasmTypeSignature sig = {};
                    sig.param_count = param_count;

                    for (uint32_t p = 0; p < param_count; ++p) {
                        if (ptr >= section_end) return WasmResult::kErrorRuntimeError;
                        sig.params[p] = static_cast<WasmType>(*ptr++);
                    }

                    uint32_t result_count = DecodeVarUint32(ptr, section_end);
                    if (result_count > WasmTypeSignature::kMaxResults) {
                        return WasmResult::kErrorOutOfMemory;
                    }
                    sig.result_count = result_count;

                    for (uint32_t r = 0; r < result_count; ++r) {
                        if (ptr >= section_end) return WasmResult::kErrorRuntimeError;
                        sig.results[r] = static_cast<WasmType>(*ptr++);
                    }

                    if (signature_count_ >= kMaxWasmTypes) {
                        return WasmResult::kErrorOutOfMemory;
                    }
                    signatures_[signature_count_++] = sig;
                }
                break;
            }

            case 2: { // Import Section (ホストAPIのリンク)
                uint32_t import_count = DecodeVarUint32(ptr, section_end);
                for (uint32_t i = 0; i < import_count; ++i) {
                    uint32_t mod_len = DecodeVarUint32(ptr, section_end);
                    const char* mod_name = CopyString(ptr, mod_len, section_end);
                    uint32_t field_len = DecodeVarUint32(ptr, section_end);
                    const char* field_name = CopyString(ptr, field_len, section_end);
                    if (!mod_name || !field_name) return WasmResult::kErrorOutOfMemory;

                    uint8_t kind = *ptr++;
                    if (kind != 0x00) { // 0x00 = Function import
                        return WasmResult::kErrorUnknownSection;
                    }
                    uint32_t type_idx = DecodeVarUint32(ptr, section_end);

                    // ホストAPIを自動生成された静的テーブルから二分探索で検索
                    HostFunction host_func = LookupStaticHostFunction(mod_name, field_name);
                    if (!host_func) {
                        return WasmResult::kErrorFunctionNotFound;
                    }

                    if (function_count_ >= kMaxWasmFunctions) {
                        return WasmResult::kErrorOutOfMemory;
                    }

                    functions_[function_count_].is_import = true;
                    functions_[function_count_].type_index = type_idx;
                    functions_[function_count_].host_func = host_func;
                    function_count_++;
                    code_index_offset++;
                }
                break;
            }

            case 3: { // Function Section (関数と型のマッピング)
                uint32_t num_funcs = DecodeVarUint32(ptr, section_end);
                for (uint32_t i = 0; i < num_funcs; ++i) {
                    uint32_t type_idx = DecodeVarUint32(ptr, section_end);

                    if (function_count_ >= kMaxWasmFunctions) {
                        return WasmResult::kErrorOutOfMemory;
                    }
                    functions_[function_count_].is_import = false;
                    functions_[function_count_].type_index = type_idx;
                    functions_[function_count_].internal_func.code_ptr = nullptr;
                    functions_[function_count_].internal_func.code_size = 0;
                    functions_[function_count_].internal_func.local_count = 0;
                    function_count_++;
                }
                break;
            }

            case 7: { // Export Section (公開関数)
                uint32_t export_count = DecodeVarUint32(ptr, section_end);
                for (uint32_t i = 0; i < export_count; ++i) {
                    uint32_t name_len = DecodeVarUint32(ptr, section_end);
                    const char* name = CopyString(ptr, name_len, section_end);
                    if (!name) return WasmResult::kErrorOutOfMemory;

                    uint8_t kind = *ptr++;
                    uint32_t idx = DecodeVarUint32(ptr, section_end);

                    if (kind == 0x00) { // 0x00 = Function export
                        if (export_count_ >= kMaxWasmFunctions) {
                            return WasmResult::kErrorOutOfMemory;
                        }
                        exports_[export_count_] = {name, idx};
                        export_count_++;
                    }
                }
                break;
            }

            case 10: { // Code Section (関数の実体)
                uint32_t code_count = DecodeVarUint32(ptr, section_end);
                for (uint32_t i = 0; i < code_count; ++i) {
                    uint32_t body_size = DecodeVarUint32(ptr, section_end);
                    const uint8_t* body_end = ptr + body_size;

                    // ローカル変数の宣言数をパース
                    uint32_t local_decls = DecodeVarUint32(ptr, body_end);
                    uint32_t local_count = 0;
                    for (uint32_t j = 0; j < local_decls; ++j) {
                        uint32_t count = DecodeVarUint32(ptr, body_end);
                        uint8_t type = *ptr++;
                        (void)type;
                        local_count += count;
                    }

                    uint32_t func_idx = code_index_offset + i;
                    if (func_idx >= function_count_) {
                        return WasmResult::kErrorRuntimeError;
                    }

                    functions_[func_idx].internal_func.code_ptr = ptr;
                    functions_[func_idx].internal_func.code_size = static_cast<uint32_t>(body_end - ptr);
                    functions_[func_idx].internal_func.local_count = local_count;

                    ptr = body_end;
                }
                break;
            }

            default:
                // 未知または不要なセクションはスキップ
                ptr = section_end;
                break;
        }
    }

    return WasmResult::kOk;
}

WasmResult WasmEngine::Execute(const char* name, const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count) noexcept {
    // 公開関数の検索
    int32_t func_idx = -1;
    for (std::size_t i = 0; i < export_count_; ++i) {
        if (std::strcmp(exports_[i].name, name) == 0) {
            func_idx = exports_[i].func_index;
            break;
        }
    }

    if (func_idx == -1) {
        return WasmResult::kErrorFunctionNotFound;
    }

    // 統計情報の初期化
    max_call_stack_depth_ = 0;
    max_stack_depth_ = 0;

    // 引数を仮想スタックにセット
    stack_top_ = 0;
    for (uint32_t i = 0; i < arg_count; ++i) {
        if (stack_top_ >= kWasmStackSize) return WasmResult::kErrorStackOverflow;
        stack_[stack_top_++] = args[i];
        if (stack_top_ > max_stack_depth_) {
            max_stack_depth_ = stack_top_;
        }
    }

    // 内部実行開始
    WasmResult res = ExecuteInternal(func_idx);
    if (res != WasmResult::kOk) return res;

    // 実行結果を戻り値配列に格納
    for (uint32_t i = 0; i < result_count; ++i) {
        if (stack_top_ == 0) return WasmResult::kErrorRuntimeError;
        results[result_count - 1 - i] = stack_[--stack_top_]; // LIFO順
    }

    return WasmResult::kOk;
}

WasmResult WasmEngine::ExecuteInternal(uint32_t func_index) noexcept {
    call_stack_top_ = 0;

    if (func_index >= function_count_) return WasmResult::kErrorFunctionNotFound;
    const WasmFunction* initial_func = &functions_[func_index];

    if (initial_func->is_import) {
        // ホストAPI (C++関数) の呼び出し（シグネチャに応じた完全なポップ・プッシュ実装）
        if (initial_func->type_index >= signature_count_) {
            return WasmResult::kErrorRuntimeError;
        }
        const WasmTypeSignature& sig = signatures_[initial_func->type_index];

        // 引数の数だけスタックからポップ
        if (stack_top_ < sig.param_count) {
            return WasmResult::kErrorRuntimeError;
        }

        WasmValue call_args[WasmTypeSignature::kMaxParams];
        // スタックはLIFOなので、ポップした引数は逆順に格納する
        for (uint32_t i = 0; i < sig.param_count; ++i) {
            call_args[sig.param_count - 1 - i] = stack_[--stack_top_];
        }

        // 戻り値用の一時バッファ
        WasmValue call_results[WasmTypeSignature::kMaxResults] = {};

        // ホスト関数の実行
        WasmResult res = initial_func->host_func(call_args, sig.param_count, call_results, sig.result_count, nullptr);
        if (res != WasmResult::kOk) return res;

        // 実行結果をスタックにプッシュ
        for (uint32_t i = 0; i < sig.result_count; ++i) {
            if (stack_top_ >= kWasmStackSize) {
                return WasmResult::kErrorStackOverflow;
            }
            stack_[stack_top_++] = call_results[i];
            if (stack_top_ > max_stack_depth_) {
                max_stack_depth_ = stack_top_;
            }
        }
        return WasmResult::kOk;
    }

    // 内部関数の最初のフレームをコールスタックに積む
    {
        if (initial_func->type_index >= signature_count_) {
            return WasmResult::kErrorRuntimeError;
        }
        const WasmTypeSignature& sig = signatures_[initial_func->type_index];
        uint32_t total_locals = sig.param_count + initial_func->internal_func.local_count;
        if (total_locals > kMaxLocals) {
            return WasmResult::kErrorOutOfMemory;
        }

        WasmFrame& frame = call_stack_[call_stack_top_++];
        if (call_stack_top_ > max_call_stack_depth_) {
            max_call_stack_depth_ = call_stack_top_;
        }
        frame.func = initial_func;
        frame.ip = initial_func->internal_func.code_ptr;
        frame.limit = initial_func->internal_func.code_ptr + initial_func->internal_func.code_size;
        frame.total_locals = total_locals;

        // 引数をスタックからポップし、ローカル変数の前半部分に格納 (LIFOのため逆順)
        if (stack_top_ < sig.param_count) {
            return WasmResult::kErrorRuntimeError;
        }
        for (uint32_t i = 0; i < sig.param_count; ++i) {
            frame.locals[sig.param_count - 1 - i] = stack_[--stack_top_];
        }
        
        // 残りのローカル変数の型と初期値(0)の設定
        for (uint32_t i = sig.param_count; i < total_locals; ++i) {
            frame.locals[i] = WasmValue{WasmType::kI32, {0}};
        }
    }

    // コールスタックが空になるまで実行ループを回す
    while (call_stack_top_ > 0) {
        WasmFrame& frame = call_stack_[call_stack_top_ - 1];

        // 高速化のため、現在実行中のフレーム状態（IP、LIMIT、ローカル変数）をローカル変数にキャッシュ
        const uint8_t* ip = frame.ip;
        const uint8_t* limit = frame.limit;
        WasmValue* locals = frame.locals;
        uint32_t total_locals = frame.total_locals;

        while (ip < limit) {
            uint8_t op = *ip++;
            switch (op) {
                case 0x00: // nop
                    break;

                case 0x0F: // return
                case 0x0B: // end
                    // 現在のフレームをポップ
                    --call_stack_top_;
                    goto frame_changed;

                case 0x10: { // call <func_index>
                    uint32_t target_idx = DecodeVarUint32(ip, limit);
                    if (target_idx >= function_count_) return WasmResult::kErrorFunctionNotFound;
                    const WasmFunction* target_func = &functions_[target_idx];

                    if (target_func->is_import) {
                        if (target_func->type_index >= signature_count_) {
                            return WasmResult::kErrorRuntimeError;
                        }
                        const WasmTypeSignature& sig = signatures_[target_func->type_index];

                        if (stack_top_ < sig.param_count) {
                            return WasmResult::kErrorRuntimeError;
                        }

                        WasmValue call_args[WasmTypeSignature::kMaxParams];
                        for (uint32_t i = 0; i < sig.param_count; ++i) {
                            call_args[sig.param_count - 1 - i] = stack_[--stack_top_];
                        }

                        WasmValue call_results[WasmTypeSignature::kMaxResults] = {};
                        WasmResult res = target_func->host_func(call_args, sig.param_count, call_results, sig.result_count, nullptr);
                        if (res != WasmResult::kOk) return res;

                        for (uint32_t i = 0; i < sig.result_count; ++i) {
                            if (stack_top_ >= kWasmStackSize) {
                                return WasmResult::kErrorStackOverflow;
                            }
                            stack_[stack_top_++] = call_results[i];
                            if (stack_top_ > max_stack_depth_) {
                                max_stack_depth_ = stack_top_;
                            }
                        }
                    } else {
                        // 内部関数の実行（新しいフレームをコールスタックに積む）
                        if (call_stack_top_ >= kWasmCallStackSize) {
                            return WasmResult::kErrorStackOverflow;
                        }
                        if (target_func->type_index >= signature_count_) {
                            return WasmResult::kErrorRuntimeError;
                        }
                        const WasmTypeSignature& sig = signatures_[target_func->type_index];
                        uint32_t target_total_locals = sig.param_count + target_func->internal_func.local_count;
                        if (target_total_locals > kMaxLocals) {
                            return WasmResult::kErrorOutOfMemory;
                        }

                        // 遷移前に現在のフレームのIPを書き戻す
                        frame.ip = ip;

                        WasmFrame& new_frame = call_stack_[call_stack_top_++];
                        if (call_stack_top_ > max_call_stack_depth_) {
                            max_call_stack_depth_ = call_stack_top_;
                        }
                        new_frame.func = target_func;
                        new_frame.ip = target_func->internal_func.code_ptr;
                        new_frame.limit = target_func->internal_func.code_ptr + target_func->internal_func.code_size;
                        new_frame.total_locals = target_total_locals;

                        if (stack_top_ < sig.param_count) {
                            return WasmResult::kErrorRuntimeError;
                        }
                        for (uint32_t i = 0; i < sig.param_count; ++i) {
                            new_frame.locals[sig.param_count - 1 - i] = stack_[--stack_top_];
                        }
                        
                        for (uint32_t i = sig.param_count; i < target_total_locals; ++i) {
                            new_frame.locals[i] = WasmValue{WasmType::kI32, {0}};
                        }
                        goto frame_changed;
                    }
                    break;
                }

                case 0x1A: { // drop
                    if (stack_top_ < 1) return WasmResult::kErrorRuntimeError;
                    --stack_top_;
                    break;
                }

                case 0x20: { // local.get <local_idx>
                    uint32_t local_idx = DecodeVarUint32(ip, limit);
                    if (local_idx >= total_locals) return WasmResult::kErrorRuntimeError;
                    if (stack_top_ >= kWasmStackSize) return WasmResult::kErrorStackOverflow;
                    stack_[stack_top_++] = locals[local_idx];
                    if (stack_top_ > max_stack_depth_) {
                        max_stack_depth_ = stack_top_;
                    }
                    break;
                }

                case 0x21: { // local.set <local_idx>
                    uint32_t local_idx = DecodeVarUint32(ip, limit);
                    if (local_idx >= total_locals) return WasmResult::kErrorRuntimeError;
                    if (stack_top_ < 1) return WasmResult::kErrorRuntimeError;
                    locals[local_idx] = stack_[--stack_top_];
                    break;
                }

                case 0x22: { // local.tee <local_idx>
                    uint32_t local_idx = DecodeVarUint32(ip, limit);
                    if (local_idx >= total_locals) return WasmResult::kErrorRuntimeError;
                    if (stack_top_ < 1) return WasmResult::kErrorRuntimeError;
                    locals[local_idx] = stack_[stack_top_ - 1]; // ポップせずにコピー
                    break;
                }

                case 0x41: { // i32.const <value>
                    int32_t val = DecodeVarInt32(ip, limit);
                    if (stack_top_ >= kWasmStackSize) return WasmResult::kErrorStackOverflow;
                    stack_[stack_top_++] = WasmValue{WasmType::kI32, {val}};
                    if (stack_top_ > max_stack_depth_) {
                        max_stack_depth_ = stack_top_;
                    }
                    break;
                }

                case 0x42: { // i64.const <value>
                    int64_t val = DecodeVarInt64(ip, limit);
                    if (stack_top_ >= kWasmStackSize) return WasmResult::kErrorStackOverflow;
                    stack_[stack_top_++] = WasmValue{WasmType::kI64, {val}};
                    if (stack_top_ > max_stack_depth_) {
                        max_stack_depth_ = stack_top_;
                    }
                    break;
                }

                case 0x45: { // i32.eqz
                    if (stack_top_ < 1) return WasmResult::kErrorRuntimeError;
                    WasmValue val = stack_[stack_top_ - 1];
                    if (val.type != WasmType::kI32) return WasmResult::kErrorRuntimeError;
                    stack_[stack_top_ - 1] = WasmValue{WasmType::kI32, {(val.value.i32 == 0) ? 1 : 0}};
                    break;
                }

                // i32 比較演算子
                case 0x46:   // i32.eq
                case 0x47:   // i32.ne
                case 0x48:   // i32.lt_s
                case 0x49:   // i32.lt_u
                case 0x4A:   // i32.gt_s
                case 0x4B:   // i32.gt_u
                case 0x4C:   // i32.le_s
                case 0x4D:   // i32.le_u
                case 0x4E:   // i32.ge_s
                case 0x4F: { // i32.ge_u
                    if (stack_top_ < 2) return WasmResult::kErrorRuntimeError;
                    WasmValue b = stack_[--stack_top_];
                    WasmValue a = stack_[--stack_top_];
                    if (a.type != WasmType::kI32 || b.type != WasmType::kI32) return WasmResult::kErrorRuntimeError;
                    
                    int32_t res = 0;
                    switch (op) {
                        case 0x46: res = (a.value.i32 == b.value.i32) ? 1 : 0; break;
                        case 0x47: res = (a.value.i32 != b.value.i32) ? 1 : 0; break;
                        case 0x48: res = (a.value.i32 < b.value.i32) ? 1 : 0; break;
                        case 0x49: res = (static_cast<uint32_t>(a.value.i32) < static_cast<uint32_t>(b.value.i32)) ? 1 : 0; break;
                        case 0x4A: res = (a.value.i32 > b.value.i32) ? 1 : 0; break;
                        case 0x4B: res = (static_cast<uint32_t>(a.value.i32) > static_cast<uint32_t>(b.value.i32)) ? 1 : 0; break;
                        case 0x4C: res = (a.value.i32 <= b.value.i32) ? 1 : 0; break;
                        case 0x4D: res = (static_cast<uint32_t>(a.value.i32) <= static_cast<uint32_t>(b.value.i32)) ? 1 : 0; break;
                        case 0x4E: res = (a.value.i32 >= b.value.i32) ? 1 : 0; break;
                        case 0x4F: res = (static_cast<uint32_t>(a.value.i32) >= static_cast<uint32_t>(b.value.i32)) ? 1 : 0; break;
                    }
                    stack_[stack_top_++] = WasmValue{WasmType::kI32, {res}};
                    break;
                }

                case 0x67: { // i32.clz
                    if (stack_top_ < 1) return WasmResult::kErrorRuntimeError;
                    WasmValue val = stack_[stack_top_ - 1];
                    if (val.type != WasmType::kI32) return WasmResult::kErrorRuntimeError;
                    
                    stack_[stack_top_ - 1] = WasmValue{WasmType::kI32, {static_cast<int32_t>(CountLeadingZeros(static_cast<uint32_t>(val.value.i32)))}};
                    break;
                }

                // i32 算術・論理演算子
                case 0x6A:   // i32.add
                case 0x6B:   // i32.sub
                case 0x6C:   // i32.mul
                case 0x6D:   // i32.div_s
                case 0x6E:   // i32.div_u
                case 0x71:   // i32.and
                case 0x72:   // i32.or
                case 0x73:   // i32.xor
                case 0x74:   // i32.shl
                case 0x75:   // i32.shr_s
                case 0x76: { // i32.shr_u
                    if (stack_top_ < 2) return WasmResult::kErrorRuntimeError;
                    WasmValue b = stack_[--stack_top_];
                    WasmValue a = stack_[--stack_top_];
                    if (a.type != WasmType::kI32 || b.type != WasmType::kI32) return WasmResult::kErrorRuntimeError;

                    int32_t res = 0;
                    switch (op) {
                        case 0x6A: res = a.value.i32 + b.value.i32; break;
                        case 0x6B: res = a.value.i32 - b.value.i32; break;
                        case 0x6C: res = a.value.i32 * b.value.i32; break;
                        case 0x6D: 
                            if (b.value.i32 == 0) return WasmResult::kErrorRuntimeError;
                            res = a.value.i32 / b.value.i32; 
                            break;
                        case 0x6E: 
                            if (b.value.i32 == 0) return WasmResult::kErrorRuntimeError;
                            res = static_cast<int32_t>(static_cast<uint32_t>(a.value.i32) / static_cast<uint32_t>(b.value.i32)); 
                            break;
                        case 0x71: res = a.value.i32 & b.value.i32; break;
                        case 0x72: res = a.value.i32 | b.value.i32; break;
                        case 0x73: res = a.value.i32 ^ b.value.i32; break;
                        case 0x74: res = a.value.i32 << (b.value.i32 & 31); break;
                        case 0x75: res = a.value.i32 >> (b.value.i32 & 31); break;
                        case 0x76: res = static_cast<int32_t>(static_cast<uint32_t>(a.value.i32) >> (b.value.i32 & 31)); break;
                    }
                    stack_[stack_top_++] = WasmValue{WasmType::kI32, {res}};
                    break;
                }

                // i64 算術・論理演算子
                case 0x7C:   // i64.add
                case 0x7D:   // i64.sub
                case 0x7E:   // i64.mul
                case 0x83:   // i64.and
                case 0x84:   // i64.or
                case 0x85: { // i64.xor
                    if (stack_top_ < 2) return WasmResult::kErrorRuntimeError;
                    WasmValue b = stack_[--stack_top_];
                    WasmValue a = stack_[--stack_top_];
                    if (a.type != WasmType::kI64 || b.type != WasmType::kI64) return WasmResult::kErrorRuntimeError;

                    int64_t res = 0;
                    switch (op) {
                        case 0x7C: res = a.value.i64 + b.value.i64; break;
                        case 0x7D: res = a.value.i64 - b.value.i64; break;
                        case 0x7E: res = a.value.i64 * b.value.i64; break;
                        case 0x83: res = a.value.i64 & b.value.i64; break;
                        case 0x84: res = a.value.i64 | b.value.i64; break;
                        case 0x85: res = a.value.i64 ^ b.value.i64; break;
                    }
                    stack_[stack_top_++] = WasmValue{WasmType::kI64, {res}};
                    break;
                }

                default:
                    // 未対応のオペコード
                    return WasmResult::kErrorRuntimeError;
            }
        }

        // 関数の末尾に達した場合は暗黙のリターン
        frame.ip = ip;
        --call_stack_top_;

    frame_changed:
        continue;
    }

    return WasmResult::kOk;
}

const char* WasmEngine::CopyString(const uint8_t*& ptr, uint32_t len, const uint8_t* end) noexcept {
    if (ptr + len > end) return nullptr;
    char* str = static_cast<char*>(pool_.Allocate(len + 1));
    if (!str) return nullptr;
    std::memcpy(str, ptr, len);
    str[len] = '\0';
    ptr += len;
    return str;
}

} // namespace embwasm
