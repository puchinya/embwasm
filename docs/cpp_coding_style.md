# C++ Coding Style Guide for Bare-Metal Embedded Systems

このドキュメントは、マイコン組み込み（ベアメタル環境）向けC++開発におけるコーディングスタイルと設計原則を定義します。本プロジェクトでは、**Google C++ Style Guide** をベースとしつつ、リソース制限の厳しいベアメタル環境に適合するための制約を追加しています。

## 1. 基本制約（ベアメタル環境）

ベアメタル環境の特性（メモリ制限、リアルタイム性、非力なハードウェア）を考慮し、以下のC++機能を**原則使用禁止**とします。

### 1.1. STL (Standard Template Library) の使用禁止
* **理由**: 多くのSTLコンテナ（`std::vector`, `std::string`, `std::map`など）は動的メモリ（ヒープ）を暗黙的に割り当てます。これはメモリ断片化（フラグメンテーション）やメモリエラーを引き起こす原因となります。
* **代替案**:
  * 固定長配列（`std::array` はヒープを使わないため使用可能）。
  * 動的割り当てが必要な場合は、メモリプール、または固定容量（Fixed Capacity）のカスタムコンテナを使用する。
  * `std::string_view`（C++17）などのアロケーションを行わない読み取り専用ビューは積極的に使用する。

### 1.2. 例外処理 (Exceptions) の使用禁止
* **理由**: 例外処理はバイナリサイズの大幅な増加（オーバーヘッド）を引き起こし、かつ実行時間の予測不可能性（決定論的動作の阻害）をもたらします。コンパイラフラグ `-fno-exceptions` を有効にします。
* **代替案**:
  * 関数の戻り値としてエラーコード（`enum class`）またはエラーハンドリング用オブジェクト（`Result` や `Status` パターン）を使用する。
  * 致命的なエラーに対しては、アサート（`assert`）またはシステム固有のパニックハンドラを呼び出す。

### 1.3. RTTI (Run-Time Type Information) の使用禁止
* **理由**: `dynamic_cast` や `typeid` は実行時のメタデータを必要とし、バイナリサイズと実行速度に悪影響を与えます。コンパイラフラグ `-fno-rtti` を有効にします。
* **代替案**:
  * 静的キャスト（`static_cast`）を使用する。
  * 実行時に型を判別する必要がある場合は、基底クラスに明示的な型識別子（`enum class Type { kA, kB };`）を定義し、それをクエリする。

---

## 2. メモリ管理

* **動的メモリ割り当て（`malloc`, `free`, `new`, `delete`）の禁止**:
  * 起動プロセス中に1回だけメモリを割り当てる設計（静的初期化）は許容されますが、メインループ実行中の動的割り当ては禁止します。
* **スタックメモリの制限**:
  * マイコンのスタックサイズは非常に小さいため、大きなローカル変数（大きな配列や構造体）をスタック上に作成してはいけません。
  * 再帰呼び出しはスタックオーバーフローの原因となるため禁止します。

---

## 3. ネーミング規則（Google C++ Style Guide準拠）

基本的にはGoogleの命名規則に従います。

* **ファイル名**: `snake_case.cpp`, `snake_case.hpp`
* **型名（クラス、構造体、エイリアス、enum）**: `CamelCase` (例: `SensorManager`, `ErrorCode`)
* **変数名**: `snake_case` (例: `loop_count`, `sensor_value`)
* **クラスメンバ変数**: `snake_case_`（末尾にアンダースコア、例: `raw_value_`）
* **定数（`const`, `constexpr`）**: `kCamelCase`（先頭に `k`、例: `kMaxBufferSize`）
* **関数名**: `CamelCase` (例: `Initialize`, `ReadSensorData`)
* **マクロ名**: `UPPER_SNAKE_CASE` (例: `DISALLOW_COPY_AND_ASSIGN`)
  * ※ マクロの使用は極力避け、`constexpr` や `inline` 関数を使用してください。

---

## 4. C++言語機能の選択的利用

* **`constexpr` の積極的活用**:
  * 定数計算やテーブル生成などは、コンパイル時に計算させてROM（Flashメモリ）に配置するために、可能な限り `constexpr` を使用します。
* **`noexcept` の明示**:
  * 例外を使用しないことを明示し、最適化を促進するために、関数宣言に `noexcept` を付与します（特にインターフェース関数）。
* **仮想関数と多態性**:
  * 仮想関数（`virtual`）はインターフェース定義に有用ですが、仮想関数テーブル（vtable）へのポインタにより各インスタンスのメモリ消費が1ワード増えることに留意してください。
  * 可能な限り、静的多態性（CRTPパターンやテンプレート）の使用を検討してください。

* **メンバー変数を参照経由で書き換えない**:
  * 関数の引数や戻り値として受け取った参照を通じてメンバー変数を変更してはいけません。
  * 副作用が追いにくく、意図しない状態変更を引き起こしやすいためです。
  * メンバー変数を変更する場合は、`this->member_ = value` のように直接代入を使用してください。

---

## 5. コメントとドキュメント

### 5.1. 外部公開ヘッダーのDoxygenコメント

`include/` 配下の外部向けヘッダーファイル（`.h` / `.hpp`）に定義するクラス・構造体・関数・定数には、**Doxygenスタイルのコメント**を付与します。

```cpp
/// @brief センサーから現在値を読み取るホストAPI。
/// @param[in]  engine      エンジンインスタンスへの参照。
/// @param[in]  args        WASM側から渡された引数配列（本関数では使用しない）。
/// @param[in]  arg_count   引数の個数。
/// @param[out] results     戻り値を格納する配列。results[0].value.i32 に値を設定する。
/// @param[in]  result_count 戻り値の個数。
/// @return kOk: 正常終了。kErrorRuntimeError: result_count が不足している場合。
WasmResult GetSensorValue(
    WasmEngine& engine,
    const WasmValue* args, uint32_t arg_count,
    WasmValue* results, uint32_t result_count) noexcept;
```

**適用ルール**:
* 使用するタグは `@brief`、`@param[in]`、`@param[out]`、`@param[in,out]`、`@return` を基本とします。
* `@brief` は1行で関数・クラスの目的を端的に記述します。
* `src/` 配下の実装ファイルや内部ヘッダー（`private` メンバ等）への適用は任意です。
* 自明な getter/setter などへの冗長なコメントは不要です。

---

## 6. 静的解析とフォーマッタ

* すべてのコードは `clang-format` (Googleスタイル) で整形すること。
* コンパイラの警告レベルは高く設定し（`-Wall -Wextra -Wpedantic`）、警告をエラーとして処理（`-Werror`）することを推奨します。
