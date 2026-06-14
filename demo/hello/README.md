# embwasm - Hello Demo

このデモは、ベアメタルマイコン向け極小WebAssembly実行エンジン（`embwasm`）を用いて、C++（ホスト側）からWebAssemblyバイナリをロードし、エクスポートされた関数を実行するハローワールドデモです。

---

## 1. デモの構成要素

* **[main.cpp](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo/hello/main.cpp)**:
  C++側のメインロジック。メモリプール（`WasmMemoryPool`）と実行エンジン（`WasmEngine`）を初期化し、埋め込まれたWASMバイナリを実行します。また、最大ネスト数やスタック深度の統計を表示します。
* **[host_apis.cpp](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo/hello/host_apis.cpp) / [host_apis.hpp](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo/hello/host_apis.hpp)**:
  WASM側からインポートされるC++のホストAPIの実体（`PrintChar`, `PrintVal`）。
* **[hostapi.wit](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo/hello/hostapi.wit)**:
  WASMがインポートする環境関数と、C++側のホスト関数のマッピング、およびインターフェースを定義するファイル。
* **[wasm/main.c](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo/hello/wasm/main.c)**:
  WebAssembly側（C言語）のソースコード。ホストAPIを通じて "Hello" の文字と、数値 "100" を出力する処理が記述されています。

---

## 2. 自動ビルドプロセスと仕組み

CMakeビルド時に、以下のコード生成（コードジェネレータ）がバックグラウンドで自動実行されます：

1. **WASMのコンパイルと埋め込み**:
   * `clang` を用いて [wasm/main.c](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo/hello/wasm/main.c) を `main.wasm` バイナリへコンパイルします。
   * [wasm_to_cpp.py](file:///Users/nabeshimamasataka/CLionProjects/embwasm/tools/codegen/wasm_to_cpp.py) により、`main.wasm` からC++形式の静的バイト配列（`main_wasm.cpp` / `main_wasm.hpp`）を生成し、メモリ配置します。
2. **関数ポインタを排除した直接ディスパッチャの生成**:
   * [gen_api.py](file:///Users/nabeshimamasataka/CLionProjects/embwasm/tools/codegen/gen_api.py) が [hostapi.wit](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo/hello/hostapi.wit) を解析します。
   * インポート探索コード（`LookupStaticHostFunctionId`）と、**関数ポインタ（間接呼び出し）を排除して `switch` 文による直接呼び出しを行うディスパッチャ（`DispatchHostFunction`）**が自動生成されます。これにより、スタック消費量を静的に正確に見積もることが可能になります。

---

## 3. ビルドおよび実行手順

プロジェクトのルートディレクトリで以下のコマンドを実行してビルドおよびデモを実行します：

```bash
# ビルドディレクトリの作成と構成
cmake -B build

# ビルドの実行
cmake --build build

# デモプログラムの実行
./build/demo/hello/embwasm_demo
```

---

## 4. 期待される実行出力

デモを実行すると、以下のようにWASMの読み込み状況、WASM内からC++ホストAPIを直接呼び出した結果のコンソール出力、およびスタック使用統計情報が表示されます。

```text
=== Embedded WASM Engine Demo ===
Memory Pool Size Configured: 65536 bytes

Loading WASM Binary...
WASM Loaded successfully.
Memory Used: 54 / 65536 bytes

Executing Exported Function 'main'...
Hello
[Host API env.print] Output: 100
Execution finished successfully.
Max function nesting depth reached: 1
Max VM stack depth reached: 1
```

* **Max function nesting depth reached**: 関数の最大ネスト数（コールスタックの最大使用深度）。
* **Max VM stack depth reached**: 仮想マシン（VM）データスタックの最大使用深度。
