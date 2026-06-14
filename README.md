# embwasm

組み込み（ベアメタル環境）向け極小 WebAssembly (WASM) 実行エンジン

## 概要
`embwasm` は、Cortex-M シリーズなどのリソース制限の厳しいマイコン（ベアメタル環境）で WebAssembly バイナリを効率的に解析・実行するための C++11 極小 WASM 実行エンジンです。

## 特徴
- **依存ゼロ & アロケータフリー**: `std::vector` や `std::string` などの STL コンテナを一切排除し、動的なヒープ割り当てを行いません。
- **ベアメタル環境向けの厳しい制約に準拠**:
  - 例外処理の禁止 (`-fno-exceptions`)
  - RTTI の禁止 (`-fno-rtti`)
  - STL コンテナの使用禁止
- **高速な静的ホストAPIルックアップ**: 
  WIT (WebAssembly Interface Type) 設定に基づいてビルド中に C++ ホスト関数のルックアップテーブルを自動生成し、二分探索で高速（$O(\log N)$）に解決します。
- **標準的なホストモジュール**:
  - **Threads**: 協調型マルチスレッド (Green Threads) サポート。
  - **Sockets**: WASI 互換 o ネットワークソケット API サポート。
- **高い移植性**: GCC / Clang に対応し、Cortex-M シリーズやPC（ARM/ARM64, x86/x86_64）の各種OS（macOS, Linux, Windows）および RTOS（FreeRTOS, uITRON）で動作します。

## ディレクトリ構成
- [include/](include/): コアライブラリのヘッダーファイル
- [src/](src/): コアライブラリのソースコード
- [wasm_host_modules/](wasm_host_modules/): 標準ホストモジュール（Threads, Sockets 等）の実装
- [demo/](demo/): デモアプリケーション
- [test/](test/): 単体テストコード
- [tools/codegen/](tools/codegen/): ホストAPI自動生成用 Python スクリプト (`gen_api.py`)
- [platform/](platform/): プラットフォーム固有の実装（FreeRTOS, uITRON, 各OS等）

## ビルド環境 & 必要ツール
本プロジェクトのビルドおよびデモの構築には、以下のツールが必要です。

- **C++ コンパイラ**: C++11 以降に対応した GCC または Clang。
- **CMake**: バージョン 3.10 以上。
- **Python 3**: ホストAPI自動生成スクリプト（`gen_api.py` 等）の実行に必要。
- **LLVM / Clang**: WASM バイナリのコンパイルに必要（`wasm32` ターゲットおよび `wasm-ld` が含まれていること）。
- **wit-bindgen-cli**: WASM ゲスト（C言語）側のバインディング生成に必要。
  - インストール方法: `cargo install wit-bindgen-cli --version 0.36.0`

## ビルド方法 & クイックスタート
ビルド手順やデモの実行方法などの詳細については、[docs/getting_started.md](docs/getting_started.md) を参照してください。

簡易手順：
```bash
# ビルドディレクトリの作成とビルドの実行
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# デモの実行
./build/demo/hello/embwasm_demo_hello
```

## ホストAPIの追加方法 & ツールの使い方
新しいホストAPIの定義方法、WASMモジュールへの公開手順、および自動コード生成ツール（`gen_api.py`）の使い方などの詳細については、以下のドキュメントを参照してください。
- **ホストAPI実装ガイド**: [docs/api_impl_for_wasm.md](docs/api_impl_for_wasm.md)
- **コード生成ツールの使い方**: [docs/tool_usage.md](docs/tool_usage.md)
- **マルチスレッド機能**: [docs/multithreading.md](docs/multithreading.md)
- **ネットワークソケット機能**: [docs/sockets.md](docs/sockets.md)

## コーディング規約
C++コードの命名規則や言語機能の制限（STL・例外・RTTI禁止など）などの詳細については、[docs/coding_style.md](docs/coding_style.md) を参照してください。

## ライセンス
Copyright (c) 2026 embwasm Project. All rights reserved.
