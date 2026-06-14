#ifndef EMBWASM_WASM_TYPES_H_
#define EMBWASM_WASM_TYPES_H_

#include <cstdint>
#include <cstddef>

namespace embwasm {

// WASM値の型定義
enum class WasmType : uint8_t {
    kI32 = 0x7F,
    kI64 = 0x7E,
    kF32 = 0x7D,
    kF64 = 0x7C,
    kVoid = 0x40
};

// WASM値のコンテナ（タグ付き共用体）
struct WasmValue {
    WasmType type;
    union ValueUnion {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;

        constexpr ValueUnion() noexcept : i32(0) {}
        constexpr ValueUnion(int32_t val) noexcept : i32(val) {}
        constexpr ValueUnion(int64_t val) noexcept : i64(val) {}
        constexpr ValueUnion(float val) noexcept : f32(val) {}
        constexpr ValueUnion(double val) noexcept : f64(val) {}
    } value;
};

// WASMエンジン実行結果のステータスコード
enum class WasmResult : uint8_t {
    kOk,
    kYield, // 実行を一時中断（スケジューラへ戻る）
    kErrorInvalidMagic,
    kErrorInvalidVersion,
    kErrorUnknownSection,
    kErrorOutOfMemory,
    kErrorFunctionNotFound,
    kErrorStackOverflow,
    kErrorRuntimeError
};

// ホスト関数のシグネチャ定義
// 例外無効（-fno-exceptions）のため、実行結果を WasmResult で返し、値は results 引数に格納します。
typedef WasmResult (*HostFunction)(const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count, void* user_data);

// ホストAPIの登録エントリ
struct HostApiEntry {
    const char* module_name;
    const char* field_name;
    HostFunction function;
};

// WASM関数シグネチャの定義（ベアメタル上の制限に準拠した上限付き静的配列）
struct WasmTypeSignature {
    static constexpr std::size_t kMaxParams = 8;
    static constexpr std::size_t kMaxResults = 4;
    
    uint32_t param_count;
    uint32_t result_count;
    WasmType params[kMaxParams];
    WasmType results[kMaxResults];
};

} // namespace embwasm

#endif // EMBWASM_WASM_TYPES_H_
