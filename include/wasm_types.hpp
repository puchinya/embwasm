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
    kExit = 2,                   ///< sys:rt::system::exit により実行を終了。

    kErrorInvalidArgument = -1, ///< 引数エラー
    kErrorInvalidOperation = -2, ///< 不正な操作
    kErrorOutOfMemory = -3,      ///< メモリプールが枯渇。
    kErrorPlatformInit = -5,     ///< プラットフォームリソースの初期化失敗。

    kErrorModuleNotFound = -10, ///< 指定したモジュールが見つからない。
    kErrorFunctionNotFound = -11, ///< 指定した関数が見つからない。
    kErrorTooManyModules = -12,   ///< ロード済みモジュール数が kMaxModules を超過。

    kErrorParseInvalidMagic = -20,     ///< WASM マジックナンバーが不正。
    kErrorParseInvalidVersion = -21,   ///< WASM バージョンが非対応。
    kErrorParseUnknownSection = -22,   ///< 未知のセクションを検出。
    kErrorParseOthers = -23,    ///< WASM ファイルのパースエラー

    kErrorValidationFailed = -30, ///< 事前検査失敗（型不整合・制限超過）。

    kErrorLinking = -40, ///< Importsのリンクエラー。

    kErrorInstantiate = -50, ///< インスタンス生成エラー。

    kErrorExecuteTrapStackOverflow = -60,    ///< スタックオーバーフロー。
    kErrorExecuteTrapCallStackOverflow = -61,    ///< コールスタックオーバーフロー。
    kErrorExecuteRuntimeError = -62,     ///< その他の実行時エラー。
    kErrorExecuteTrapLinearMemoryLimitExceeded = -63,    ///< エンジン設定の線形メモリサイズ制限値を超過。
    kErrorExecuteTrapLabelStackOverflow = -64,           ///< ラベルスタックオーバーフロー。
    kErrorExecuteTrapTableOutOfBounds = -65,    ///< テーブルインデックスが範囲外。
    kErrorExecuteTrapIntegerDivideByZero = -66, ///< 整数除算でゼロ除算が発生。
    kErrorExecuteTrapIntegerOverflow = -67, ///< 整数オーバーフロー。
    kErrorExecuteTrapInvalidConversionToInteger = -68, ///< 整数への変換に失敗。
    kErrorExecuteTrapIndirectCallSignatureMismatch = -69, ///< 関数シグネチャが一致しない。
    kErrorExecuteTrapUnreachable = -70, ///< unreachable命令が実行された。
    kErrorExecuteTrapMemoryOutOfBounds = -71, ///< 線形メモリインデックスが範囲外。
    kErrorExecuteTrapTableUninitializedElement = -72, ///< テーブル要素が初期化されていない。
    kErrorExecuteTrapGlobalImmutable = -73, ///< グローバル変数が不変である。
    kErrorExecuteTrapDataOutOfBounds = -74, ///< データインデックスが範囲外。
};

static inline bool IsSuccess(WasmResult result) {
    return static_cast<int32_t>(result) >= 0;
}

static inline bool IsError(WasmResult result) {
    return static_cast<int32_t>(result) < 0;
}

/// @brief リンクリストの要素の基底型。
///
struct ListNode {
    ListNode *next;
    ListNode *prev;
};

static inline void InitListNode(ListNode *node) {
    node->next = node;
    node->prev = node;
}

static inline void AddLastListNode(ListNode *list, ListNode *node) {
    node->next = list;
    node->prev = list->prev;
    list->prev->next = node;
    list->prev = node;
}

static inline void AddFirstListNode(ListNode *list, ListNode *node) {
    node->next = list->next;
    node->prev = list;
    list->next->prev = node;
    list->next = node;
}

static inline void RemoveListNode(ListNode *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node->prev = nullptr;
}

static inline bool IsEmptyListNode(const ListNode *list) {
    return list->next == list;
}

static inline ListNode* PopFrontListNode(ListNode *list) {
    if (IsEmptyListNode(list)) return nullptr;
    ListNode* node = list->next;
    RemoveListNode(node);
    return node;
}

/// @brief ホスト関数のシグネチャ型。
///
/// 例外無効（`-fno-exceptions`）のため、実行結果を WasmResult で返し、値は results 引数に格納します。
/// @param args         WASM 側から渡された引数配列。
/// @param arg_count    引数の個数。
/// @param results      WASM 側へ返す結果を格納する配列。
/// @param result_count 結果の個数。
/// @param user_data    ホスト側が任意に設定できるユーザーデータポインタ。
typedef WasmResult (*HostFunction)(const WasmValue* args, uint32_t arg_count, WasmValue* results, uint32_t result_count, void* user_data);

/// @brief WASM 関数シグネチャ（引数型リスト＋戻り値型リスト）。
///
/// params→results の順に可変長配列として保持する。プールから ByteSize() 分確保して使用する。
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#endif
struct WasmTypeSignature {
    uint16_t param_count;  ///< 実際の引数数。
    uint16_t result_count; ///< 実際の戻り値数。
    union {
        uint32_t u32[0];   ///< 4 バイト単位のワード比較用。
        WasmType types[0]; ///< 個別型アクセス用（params→results の順）。
    } data;

    WasmType GetParam(uint32_t i)  const noexcept { return data.types[i]; }
    WasmType GetResult(uint32_t i) const noexcept { return data.types[param_count + i]; }
    void SetParam(uint32_t i, WasmType t) noexcept { data.types[i] = t; }
    void SetResult(uint32_t i, WasmType t) noexcept { data.types[param_count + i] = t; }

    static uint32_t    WordCount(uint32_t total) noexcept { return (total + 3) >> 2; }
    static std::size_t ByteSize(uint32_t pc, uint32_t rc) noexcept {
        return sizeof(WasmTypeSignature) + WordCount(pc + rc) * sizeof(uint32_t);
    }
    static bool Equals(const WasmTypeSignature *x, const WasmTypeSignature *y) noexcept;
};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/// @brief WASM インポートエントリ。
struct WasmImportEntry {
    const char* module_name;     ///< インポートモジュール名（ROM を指す）。
    std::size_t module_name_len; ///< モジュール名の長さ（バイト数）。
    const char* field_name;      ///< インポートフィールド名（ROM を指す）。
    std::size_t field_name_len;  ///< フィールド名の長さ（バイト数）。
    uint8_t kind;                ///< インポート種別（0=Func, 1=Table, 2=Mem, 3=Global）。
    uint32_t index;              ///< インポート対象のインデックス（関数/テーブル/グローバル）。
    /// WASM バイナリから得られる種別固有のデータ（リンク処理で参照）。
    union {
        struct { uint32_t type_index; } func;
        struct { uint8_t elem_type; uint32_t min_size; uint32_t max_size; } table;
        struct { uint32_t min_pages; uint32_t max_pages; } mem;
        struct { uint8_t value_type; bool is_mutable; } global;
    } desc;
};

} // namespace embwasm

#endif // EMBWASM_WASM_TYPES_HPP_
