# Agent Instructions (AGENTS.md)

このドキュメントは、このリポジトリを編集・拡張するAIエージェント（Antigravity等）に対する指示書です。
コードの生成、リファクタリング、バグ修正などを行う際は、必ず以下の基本制約および詳細ルールを厳守してください。

---

## 1. プロジェクト概要

### 1.1 このリポジトリは何か？

`embwasm` は、**Cortex-M シリーズなどのリソース制限の厳しいマイコン（ベアメタル環境）で WebAssembly (WASM) バイナリを実行するための C++11 極小ランタイムエンジン**です。

- **動的メモリ（ヒープ）割り当てゼロ**: `malloc` / `new` を一切使わず、静的メモリプールのみで動作します。
- **STL・例外・RTTI 完全排除**: ベアメタルコンパイラの制約（`-fno-exceptions`, `-fno-rtti`）に完全準拠します。
- **高速ホストAPIディスパッチ**: YAML設定に基づいてビルド時に C++ ルックアップテーブルを自動生成し、`switch` 文による直接呼び出しで $O(1)$ に近いディスパッチを実現します。

### 1.2 基本情報

| 項目 | 内容 |
|---|---|
| **言語** | C++11 以上 |
| **コーディングスタイル** | Google C++ Style Guide 準拠 |
| **対応コンパイラ** | GCC, Clang |
| **対応アーキテクチャ** | Cortex-M シリーズ, ARM/ARM64, x86, x86_64 |
| **対応OS/RTOS** | macOS, Linux, Windows, FreeRTOS, uITRON |
| **ビルドシステム** | CMake 3.11 以上 |
| **名前空間** | `embwasm` |

---

## 2. ディレクトリ構成とファイルマップ

```
embwasm/
├── include/              # コアライブラリの公開ヘッダ群
│   ├── embwasm.h         # 全ヘッダをまとめた単一インクルードエントリポイント
│   ├── wasm_config.h     # constexpr による全エンジン設定定数（メモリプールサイズ等）
│   ├── wasm_types.h      # 基本型定義 (WasmValue, WasmResult, WasmType 等)
│   ├── wasm_api.h        # ホストAPI登録/ディスパッチのインタフェース宣言
│   ├── wasm_memory_pool.h# 静的メモリプールクラスの宣言
│   ├── wasm_engine.h     # WASMエンジン本体クラスの宣言
│   └── wasm_platform.h   # プラットフォーム依存処理の抽象化（割り込み制御、CLZ命令等）
│
├── src/                  # コアライブラリの実装
│   ├── wasm_engine.cpp   # WASMバイナリのパース・実行エンジン本体（最重要ファイル）
│   └── wasm_memory_pool.cpp # 静的メモリプールの実装
│
├── demo/                 # 動作確認用デモアプリケーション
│   └── hello/            # WASMからホストAPIを呼び出すサンプル
│
├── test/                 # GoogleTest による単体テスト
│
├── platform/             # プラットフォーム固有の実装
│   ├── macos/            # macOS 向け割り込み制御実装
│   ├── windows/          # Windows 向け割り込み制御実装
│   ├── freertos/         # FreeRTOS 向け割り込み制御実装
│   └── uitron/           # uITRON 向け割り込み制御実装
│
├── tools/
│   └── codegen/
│       ├── gen_api.py    # YAML設定からホストAPIルックアップテーブルを自動生成するツール
│       └── wasm_to_cpp.py# WASMバイナリをC++バイト配列に変換するユーティリティ
│
└── docs/
    ├── coding_style.md   # 命名規則・言語機能制限の詳細ガイド（必読）
    ├── getting_started.md# ビルド手順とクイックスタートガイド
    ├── api_impl_for_wasm.md # ホストAPIの実装・公開手順
    └── tool_usage.md     # コード生成ツールの使い方
```

---

## 3. コアコンポーネントの概要

### 3.1 主要クラス・型

| クラス / 型 | ファイル | 役割 |
|---|---|---|
| `WasmEngine` | `include/wasm_engine.h` | WASMバイナリのLoad・Execute を担うエンジン本体 |
| `WasmMemoryPool` | `include/wasm_memory_pool.h` | ヒープ不使用の静的バンプアロケータ |
| `WasmValue` | `include/wasm_types.h` | WASM値のタグ付き共用体（i32/i64/f32/f64） |
| `WasmResult` | `include/wasm_types.h` | エラーコード列挙型（例外の代替） |
| `WasmType` | `include/wasm_types.h` | WASM値型の列挙型 |
| `WasmTypeSignature` | `include/wasm_types.h` | WASM関数シグネチャ（上限付き静的配列） |
| `HostFunctionId` | `include/wasm_api.h` | ホスト関数の識別ID（Enum） |
| `WasmFunction` | `include/wasm_engine.h` | インポート関数・内部関数のユニオン構造体 |

### 3.2 主要な設定定数（`include/wasm_config.h`）

エンジンの各制限値はすべて `constexpr` 定数で管理されます。変更する場合はこのファイルのみを編集してください。

| 定数 | デフォルト値 | 意味 |
|---|---|---|
| `kMemoryPoolSize` | 65536 (64 KB) | 静的メモリプールの総サイズ |
| `kMaxWasmFunctions` | 32 | サポートするWASM関数数の上限 |
| `kMaxWasmTypes` | 16 | サポートするWASM型シグネチャ数の上限 |
| `kWasmStackSize` | 64 | WASM実行スタックの最大深度 |
| `kWasmCallStackSize` | 16 | コールスタックの最大深度 |
| `kMaxLocals` | 32 | 1関数あたりの最大ローカル変数数 |

### 3.3 ホストAPIの仕組み

WASMからホスト（C++）側の関数を呼び出す仕組みは以下のとおりです。

1. **定義**: YAML設定ファイル（`module_config.yaml`）にモジュール名・フィールド名・引数型を記述します。
2. **自動生成**: `tools/codegen/gen_api.py` が YAML を読み込み、`HostFunctionId` 列挙体とルックアップテーブルを C++ コードとして自動生成します。
3. **ルックアップ**: `LookupStaticHostFunctionId()` がバイナリ探索（$O(\log N)$）でIDを解決します。
4. **ディスパッチ**: `DispatchHostFunction()` が `switch` 文による直接呼び出しで関数を実行します（関数ポインタ禁止ルール準拠）。

---

4. **開発上の重要制約（厳守）**

ベアメタル環境向けにビルドされるため、以下の機能はコンパイラレベルで無効化されています。これらを使用したコードはビルドエラーになるか、実行時エラーを引き起こします。

1. **STL (Standard Template Library) の使用禁止**
   * 暗黙的な動的メモリ（ヒープ）割り当てを行うコンテナ（`std::vector`, `std::string`, `std::map` など）は使用できません。
   * 固定長（`std::array`）や、スタックまたは静的メモリ領域を使用するアロケータフリーな設計にしてください。
2. **ベアメタル非互換ライブラリの使用禁止**
   * `include/` および `src/` ディレクトリには、デバッグ目的を除き、ベアメタル環境で使用できないライブラリを導入してはいけません。
3. **例外処理 (Exceptions) の禁止**
   * `throw`, `try`, `catch` は使用できません（`-fno-exceptions`）。
   * エラーハンドリングは、エラーコードや `Result` / `Status` オブジェクトの返却で行ってください。
4. **RTTI (Run-Time Type Information) の禁止**
   * `dynamic_cast` や `typeid` は使用できません（`-fno-rtti`）。
   * 多態性のキャストが必要な場合は、`static_cast` または型判別用のメンバ変数（Tag/Enum）を介した安全なダウンキャストを使用してください。
5. **再帰呼び出し (Recursion) の禁止**
   * マイコンの非常に小さなスタック領域を保護し、予期せぬスタックオーバーフローを防ぐため、再帰呼び出しは使用できません。
   * ループ（反復処理）や、明示的なコールスタック / キューなどのデータ構造を用いた反復アルゴリズム（Iterative algorithm）で実装してください。
6. **関数ポインタおよびメンバー関数ポインタの使用禁止**
   * 間接呼び出し（indirect call）によるスタックの最大消費量が静的解析ツールで算出不能になるのを避けるため、関数ポインタおよびメンバー関数ポインタは原則使用禁止とします。
   * ディスパッチ処理や動的な切り替えが必要な場合は、ID（Enum等）と `switch` 文などの直接呼び出し（Direct Call）を用いて実装してください。

---

## 5. コーディングスタイルの詳細

命名規則やC++機能の制限に関する詳細なガイドラインは、以下のドキュメントに定義されています。コードを変更する前に必ず参照してください。

* **詳細コーディングスタイル**: [docs/coding_style.md](docs/coding_style.md)

---

## 6. エージェントへの具体的アクション要求

* 新しいファイルを提案または作成する場合、[docs/coding_style.md](docs/coding_style.md) に記載されている命名規則（クラス名: `CamelCase`、メンバ変数: `snake_case_` など）を必ず適用してください。
* 動的メモリ割り当てを排除するため、配列サイズなどは `constexpr` による静的定数として決定してください。
* 未定義動作（UB）を避けるため、ポインタ演算や型キャストは最小限にし、必要であれば `reinterpret_cast` ではなく明示的なインタフェースを使用してください。
* ホストスタックの保護のため、関数のネストや再帰呼び出しを行う場合は、必ず明示的なコールスタック（データ構造）を用いた反復処理を実装してください。
* 静的解析ツールによる最悪スタック消費量の算出を可能にするため、関数ポインタやメンバー関数ポインタによる間接呼び出しは行わず、IDと `switch` 分岐等を用いた直接呼び出しによる静的ディスパッチを実装してください。
* エンジンの容量制限値を変更する必要がある場合は `include/wasm_config.h` の `constexpr` 定数のみを変更し、コード内にマジックナンバーを直接記述しないでください。
* プラットフォーム固有の処理（割り込み制御など）を追加する場合は `platform/` 配下に対応するディレクトリを作成し、`wasm_platform.h` のインタフェースに従って実装してください。
* 新しいホストAPIを追加する場合は、コードを手書きせず `tools/codegen/gen_api.py` を用いた自動生成フローに従ってください（詳細: [docs/api_impl_for_wasm.md](docs/api_impl_for_wasm.md)）。
* テストコードは必ず `test/` ディレクトリ配下に作成し、`include/`, `src/`, `wasm_host_modules/` 内のすべての関数に対してテストを実装してください。
