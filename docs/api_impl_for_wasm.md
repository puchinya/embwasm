# WASM向け公開API (Host API) の実装方法

このドキュメントでは、本プロジェクトのC++実行環境から、WASMモジュール内に提供する公開API（インポート関数/ホスト関数）の実装および登録方法について解説します。

---

## 1. ホストAPIの基本設計

ベアメタル環境の制約（**STL禁止、例外処理禁止、RTTI禁止**）に従い、ホストAPIは `include/wasm_types.hpp` で定義された `HostFunction` 型に適合する静的C++関数として定義します。

```cpp
// include/wasm_types.hpp の typedef
typedef WasmResult (*HostFunction)(
    const WasmValue* args,      // WASM側から渡された引数の配列
    uint32_t arg_count,         // 引数の個数
    WasmValue* results,         // WASM側へ返却する戻り値の配列
    uint32_t result_count,      // 戻り値の個数
    void* user_data             // エンジンに設定したユーザーデータ
) noexcept;
```

> **注意**: `WasmEngine&` は引数に含まれません。エンジンへのアクセスが必要な場合は `SetUserData()` / `GetUserData()` で任意のポインタを渡してください。

### WasmValue の構造
`WasmValue` は型タグ（`WasmType`）を**持ちません**。型はバイトコード上の命令が静的に決定するため、実行時に型を添付する必要がありません。値の格納・取り出しには `value.i32` / `value.i64` / `value.f32` / `value.f64` フィールドを直接使用します。

```cpp
// 値の生成ヘルパー
WasmValue v = WasmValue::FromI32(42);
WasmValue v = WasmValue::FromI64(100LL);
WasmValue v = WasmValue::FromF32(3.14f);
WasmValue v = WasmValue::FromF64(2.718);

// 値の参照
int32_t  x = v.value.i32;
int64_t  y = v.value.i64;
float    z = v.value.f32;
double   w = v.value.f64;
```

### 例外処理とエラーハンドリング
例外が利用できないため、関数自体の実行の成否は `WasmResult` のステータスコードを返します。
正常に値が計算できた場合は `WasmResult::kOk` を返し、値は `results` 配列に格納します。

---

## 2. APIの実装手順

### 手順 1: C++関数の定義
例えば、WASMモジュールに対して「センサーから現在値を取得するAPI（`get_sensor_value`）」と「値をLEDに書き込むAPI（`write_led_value`）」を提供したい場合のコード例です。

```cpp
#include "wasm_types.hpp"

namespace embwasm {

// センサー値を読み込むホストAPI
// WASMシグネチャ: (import "env" "get_sensor_value" (func (result i32)))
WasmResult GetSensorValue(
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count,
    void* user_data) noexcept
{
    (void)args;
    (void)arg_count;
    (void)user_data;

    if (result_count < 1) {
        return WasmResult::kErrorExecuteRuntimeError;
    }

    // 擬似的なセンサー値の読み出し
    results[0] = WasmValue::FromI32(42);

    return WasmResult::kOk;
}

// LEDに出力するホストAPI
// WASMシグネチャ: (import "env" "write_led_value" (func (param i32)))
WasmResult WriteLedValue(
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count,
    void* user_data) noexcept
{
    (void)results;
    (void)result_count;
    (void)user_data;

    if (arg_count < 1) {
        return WasmResult::kErrorExecuteRuntimeError;
    }

    int32_t led_state = args[0].value.i32;

    // ベアメタル環境ではここでハードウェアレジスタを操作
    // (例: GPIO_WriteBit(LED_PORT, LED_PIN, led_state);)

    return WasmResult::kOk;
}

} // namespace embwasm
```

### 手順 2: WITファイルへの記述

定義したホストAPIをエンジンに認識させるため、WIT (WebAssembly Interface Type) ファイルにマッピング情報を記述します。

```wit
package embwasm:sensor;

/// @cpp-header: "sensor_apis.hpp"  // 手順1で定義した関数の宣言を含むヘッダー
world sensor-api {
    /// @cpp-func: embwasm::GetSensorValue
    import get-sensor-value: func() -> i32;

    /// @cpp-func: embwasm::WriteLedValue
    import write-led-value: func(val: i32);
}
```

### 手順 3: コード生成の実行

`tools/codegen/gen_api.py` を実行して、静的なルックアップテーブルとディスパッチャを自動生成します。

```bash
python3 tools/codegen/gen_api.py sensor.wit src/wasm_api_static.cpp include/wasm_api_static.hpp
```

これにより、ビルド時に $O(\log N)$ の二分探索でホスト関数が解決されるようになります。詳細は [docs/tool_usage.md](tool_usage.md) を参照してください。

---

## 3. WASMモジュール側の記述 (WAT)

WASM（WebAssembly Text Format）側で上記で定義した API をインポートして呼び出す記述例です。

```wat
(module
  ;; ホストAPIのインポート定義
  (import "env" "get_sensor_value" (func $get_sensor (result i32)))
  (import "env" "write_led_value" (func $write_led (param i32)))

  ;; メインの処理
  (func (export "run_control_loop")
    ;; センサー値を取得
    call $get_sensor
    
    ;; センサー値が30より大きければ LED を ON(1) に、以下なら OFF(0) にする
    (if (i32.gt_s (i32.const 30))
      (then
        (i32.const 1)
        call $write_led)
      (else
        (i32.const 0)
        call $write_led))
  )
)
```

---

## 4. 設定値の変更

ホストAPIの数や実行時のスタック制限などは、`include/wasm_config.hpp` の設定値を書き換えてビルドすることで調整可能です。

* **[include/wasm_config.hpp](include/wasm_config.hpp)**:
  - `kWasmStackSize`: WASM実行スタックの最大深度。
