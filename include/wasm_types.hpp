#ifndef EMBWASM_WASM_TYPES_HPP_
#define EMBWASM_WASM_TYPES_HPP_

#include <cstdint>
#include <cstddef>

namespace embwasm {

/// @brief WASM の値型タグ（WebAssembly 1.0 仕様の valtype に対応）。
enum class WasmType : uint8_t {
    kI32     = 0x7F,
    kI64     = 0x7E,
    kF32     = 0x7D,
    kF64     = 0x7C,
    kFuncRef = 0x70,
    kExternRef = 0x6F,
    kVoid    = 0x40
};

/// @brief WASM の演算スタック上の値コンテナ。
///
/// 型タグを持たない生ビット列で値を保持します。型はバイトコード上の命令が静的に決定するため、
/// 実行時に型タグを添付する必要がなく、スタックサイズとコピーコストを削減できます。
struct WasmValue {
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
    } value;

    WasmValue() noexcept { value.i64 = 0; }

    /// @brief i32 値から WasmValue を生成します。
    static WasmValue FromI32(int32_t v) noexcept { WasmValue r; r.value.i32 = v; return r; }
    /// @brief i64 値から WasmValue を生成します。
    static WasmValue FromI64(int64_t v) noexcept { WasmValue r; r.value.i64 = v; return r; }
    /// @brief f32 値から WasmValue を生成します。
    static WasmValue FromF32(float v) noexcept { WasmValue r; r.value.f32 = v; return r; }
    /// @brief f64 値から WasmValue を生成します。
    static WasmValue FromF64(double v) noexcept { WasmValue r; r.value.f64 = v; return r; }
};

/// @brief WASM エンジンの実行結果ステータスコード。
///
/// 例外処理が無効（`-fno-exceptions`）なため、エラーは戻り値のこの enum で表現します。
enum class WasmResult : int32_t {
    kOk = 0,
    kYield = 1,                  ///< 実行を一時中断（協調スケジューラへ戻る）。
    kErrorInvalidMagic = -1,     ///< WASM マジックナンバーが不正。
    kErrorInvalidVersion = -2,   ///< WASM バージョンが非対応。
    kErrorUnknownSection = -3,   ///< 未知のセクションを検出。
    kErrorOutOfMemory = -4,      ///< メモリプールが枯渇。
    kErrorValidationFailed = -5, ///< 事前検査失敗（型不整合・制限超過）。
    kErrorModuleNotFound = -6, ///< 指定したモジュールが見つからない。
    kErrorFunctionNotFound = -7, ///< 指定した関数が見つからない。
    kErrorStackOverflow = -8,    ///< スタックオーバーフロー。
    kErrorCallStackOverflow = -9,    ///< コールスタックオーバーフロー。
    kErrorRuntimeError = -10,     ///< その他の実行時エラー。
    kErrorTooManyModules = -11,   ///< ロード済みモジュール数が kMaxModules を超過。
    kErrorLinearMemoryLimitExceeded = -12,    ///< エンジン設定の線形メモリサイズ制限値を超過。
    kErrorInvalidArgument = -13, //< 引数えラー
    kErrorParse = -14 //< WASM ファイルのパースエラー。
};

/// @brief ホスト関数のシグネチャ型。
///
/// 例外無効（`-fno-exceptions`）のため、実行結果を WasmResult で返し、値は results 引数に格納します。
/// @param args         WASM 側から渡された引数配列。
/// @param arg_count    引数の個数。
/// @param results      WASM 側へ返す結果を格納する配列。
/// @param result_count 結果の個数。
/// @param user_data    ホスト側が任意に設定できるユーザーデータポインタ。
typedef WasmResult (*HostFunction)(const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count, void* user_data);

/// @brief 静的ホスト API テーブルのエントリ。
struct HostApiEntry {
    const char* module_name; ///< インポートモジュール名（NULL 終端文字列）。
    const char* field_name;  ///< インポートフィールド名（NULL 終端文字列）。
    HostFunction function;   ///< 対応するホスト関数ポインタ。
};

/// @brief WASM 関数シグネチャ（引数型リスト＋戻り値型リスト）。
///
/// 静的配列で保持し、動的メモリを使用しません。
struct WasmTypeSignature {
    static constexpr std::size_t kMaxParams = 128;  ///< 引数の最大数。
    static constexpr std::size_t kMaxResults = 128; ///< 戻り値の最大数。

    uint32_t param_count;          ///< 実際の引数数。
    uint32_t result_count;         ///< 実際の戻り値数。
    WasmType params[kMaxParams];   ///< 引数の型リスト。
    WasmType results[kMaxResults]; ///< 戻り値の型リスト。
};

} // namespace embwasm

#endif // EMBWASM_WASM_TYPES_HPP_
