# APIコード生成ツールの使い方 (docs/tool_usage.md)

このドキュメントでは、WASMモジュール向け公開API（ホスト関数）の静的テーブルと高速検索二分探索コードを自動生成するツール（[tools/codegen/gen_api.py](file:///Users/nabeshimamasataka/CLionProjects/embwasm/tools/codegen/gen_api.py)）の使い方について解説します。

---

## 1. 概要
本プロジェクトでは、組み込み環境における実行時メモリ消費の削減と高速検索（`O(log N)`）を両立するため、ホスト関数の登録に静的コードジェネレータを使用します。

Pythonスクリプトが [module_config.yaml](file:///Users/nabeshimamasataka/CLionProjects/embwasm/module_config.yaml) から設定情報を読み込み、探索キー（`module_name`, `field_name`）を辞書順でソートしたうえで、C++ソース・ヘッダーを自動生成します。

---

## 2. 前提環境
* **Python 3.x** がインストールされていること。
  - `PyYAML` パッケージが利用可能な場合はそれを使用しますが、インストールされていないクリーンな環境でも、Python標準ライブラリのみで動作する簡易フォールバックパーサーが自動的に有効になります。

---

## 3. 設定ファイル `module_config.yaml` の仕様

`module_config.yaml` に、インポートするAPIの対応関係をYAML形式で定義します。

### 3.1 基本フォーマット

```yaml
# WASM Host Module Configuration
# This file maps WASM import module and field names to C++ functions.

headers:
  - "host_apis.h"

modules:
  env:
    apis:
      - field: print_val
        function: embwasm::PrintVal

      - field: dummy
        function: embwasm::DummyHostFunc

  wasi:
    apis:
      - field: proc_exit
        function: embwasm::ProcExit
```

### 3.2 キー定義:
* `headers`: 生成される `.cpp` ファイルにインクルードするヘッダーファイルのリスト。
* `modules`: モジュール名をキーとするマップ。
  * `<module_name>`: WASM側でインポート宣言する際のモジュール名（例: `(import "env" "print_val" ...)` の `env`）。
    * `apis`: そのモジュールに属するAPIエントリのリスト。
      * `field`: WASM側でインポート宣言する際の関数名（例: `print_val`）。
      * `function`: C++側で実装する実関数のフルパス（名前空間修飾付き）。

### 3.3 複数ファイルのインポート

`imports:` キーを使って、別の `module_config.yaml` をインポートできます。パスはインポート元ファイルを基準とした **相対パス** で指定します。

```yaml
# module_config.yaml (エントリポイント)
imports:
  - "common/module_config.yaml"   # 共通 API 定義
  - "extra/module_config.yaml"    # 追加 API 定義

headers:
  - "my_host_apis.h"

modules:
  env:
    apis:
      - field: my_func
        function: embwasm::MyFunc
```

インポートされたファイル側にも `imports:` / `headers:` / `modules:` を記述できます（多段インポート対応）。

**マージルール:**
* `headers:` はすべてのファイルから収集され、**重複排除**（最初に現れたものを優先）されます。
* `modules.<name>.apis:` は `(module, field)` の組み合わせをキーとして**重複排除**されます（最初に定義されたものを優先）。
* **循環インポート**は自動検出してスキップし、標準エラーへ警告を出力します。

> [!WARNING]
> ここで指定するホスト関数は、必ず [include/wasm_host_apis.h](file:///Users/nabeshimamasataka/CLionProjects/embwasm/include/wasm_host_apis.h) にプロトタイプ宣言（前方宣言）を追加してください。宣言がない場合、自動生成された C++ コードのビルド時にコンパイルエラーとなります。

---

## 4. ツールの実行方法

ターミナルまたはビルドスクリプトから以下のように実行します。

```bash
# 基本実行 (デフォルトパスで生成)
python3 tools/codegen/gen_api.py

# 明示的にパスを指定して実行
# 第1引数: 設定ファイル(YAML)
# 第2引数: 出力先C++ソースファイル(.cpp)
# 第3引数: 出力先ヘッダーファイル(.h)
python3 tools/codegen/gen_api.py \
  module_config.yaml \
  src/wasm_api_static.cpp \
  include/wasm_api_static.h
```

---

## 5. 自動生成されるファイル

スクリプト実行後、以下のファイルが上書き（再生成）されます。

1. **[include/wasm_api_static.h](file:///Users/nabeshimamasataka/CLionProjects/embwasm/include/wasm_api_static.h)**:
   高速検索インターフェースである `LookupStaticHostFunction` の宣言。
2. **[src/wasm_api_static.cpp](file:///Users/nabeshimamasataka/CLionProjects/embwasm/src/wasm_api_static.cpp)**:
   ソート済みの静的APIテーブルの実体および二分探索の実装。
