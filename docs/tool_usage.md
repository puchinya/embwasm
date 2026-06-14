# APIコード生成ツールの使い方 (docs/tool_usage.md)

このドキュメントでは、WASMモジュール向け公開API（ホスト関数）の静的テーブルと高速検索二分探索コードを自動生成するツール（[tools/codegen/gen_api.py](file:///Users/nabeshimamasataka/CLionProjects/embwasm/tools/codegen/gen_api.py)）の使い方について解説します。

---

## 1. 概要
本プロジェクトでは、組み込み環境における実行時メモリ消費の削減と高速検索（`O(log N)`）を両立するため、ホスト関数の登録に静的コードジェネレータを使用します。

Pythonスクリプトが WIT (WebAssembly Interface Type) ファイルから設定情報を読み込み、探索キー（`module_name`, `field_name`）を辞書順でソートしたうえで、C++ソース・ヘッダーを自動生成します。

---

## 2. 前提環境
* **Python 3.x** がインストールされていること。

---

## 3. 設定ファイル (WIT) の仕様

WASM Interface Type (WIT) ファイルを直接インポートします。WIT ファイル内にメタデータとして C++ 関数のマッピング情報を記述することで、インターフェース定義と実装の紐付けを一箇所で管理できます。

### 3.1 基本フォーマット (`hostapi.wit`)

```wit
package embwasm:demo;

/// @cpp-header: "host_apis.hpp"
world hello {
    /// @cpp-func: embwasm::Print
    import print: func(val: i32);

    /// @cpp-func: embwasm::PrintChar
    import print-char: func(character: i32);
}
```

### 3.2 メタデータタグ
* `/// @cpp-func: <C++関数名>`: インポート関数に対応する C++ 関数のフルパスを指定します。
* `/// @cpp-header: <ヘッダー名>`: 生成コードに必要なインクルードヘッダーを指定します（`world` または各 `import` の直前に記述可能）。
* `/// @wit-import: <WITファイルパス>`: 別の WIT ファイルをインポートします（相対パス）。

WIT 内の `import` 名が `kebab-case`（例: `print-char`）の場合、WASM の慣習に従って自動的に `snake_case`（例: `print_char`）に変換されて登録されます。

### 3.3 複数ファイルのインポート

`/// @wit-import:` タグを使って、別の `.wit` ファイルをインポートできます。パスはインポート元ファイルを基準とした **相対パス** で指定します。

```wit
package embwasm:demo;

/// @wit-import: "common.wit"   # 共通 API 定義
/// @wit-import: "extra.wit"    # 追加 API 定義
world my-world {
    // ...
}
```

**マージルール:**
* `@cpp-header` はすべてのファイルから収集され、**重複排除**されます。
* インポート関数は `(module, field)` の組み合わせをキーとして**重複排除**されます。
* **循環インポート**は自動検出してスキップし、標準エラーへ警告を出力します。

> [!WARNING]
> ここで指定するホスト関数は、必ず C++ のヘッダーファイルでプロトタイプ宣言（前方宣言）を追加してください。宣言がない場合、自動生成された C++ コードのビルド時にコンパイルエラーとなります。

---

## 4. ツールの実行方法

ターミナルまたはビルドスクリプトから以下のように実行します。

```bash
# WIT ファイルを入力として実行
python3 tools/codegen/gen_api.py \
  hostapi.wit \
  src/wasm_api_static.cpp \
  include/wasm_api_static.hpp \
  wasm/wasm_api.h
```

### 4.1 引数の仕様
* **第1引数**: 設定ファイル (WIT)。
* **第2引数**: 出力先 C++ ソースファイル (`.cpp`)。
* **第3引数**: 出力先 C++ ヘッダーファイル (`.hpp`)。
* **第4引数 (オプション)**: 出力先 WASM クライアント用 C ヘッダーファイル (`.h`)。
  - 指定した場合、WASM 側（C/C++）でインポート宣言として使用できるヘッダーを自動生成します。

---

## 5. 自動生成されるファイル

スクリプト実行後、以下のファイルが上書き（再生成）されます。

1. **[include/wasm_api_static.hpp](file:///Users/nabeshimamasataka/CLionProjects/embwasm/include/wasm_api_static.hpp)**:
   高速検索インターフェースである `LookupStaticHostFunctionId` の宣言。
2. **[src/wasm_api_static.cpp](file:///Users/nabeshimamasataka/CLionProjects/embwasm/src/wasm_api_static.cpp)**:
   ソート済みの静的APIテーブルの実体および二分探索の実装。
3. **WASM クライアント用ヘッダー (オプション)**:
   WASM 側でホスト関数を呼び出すための `__attribute__((import_module(...)))` 付きの C 関数宣言。
