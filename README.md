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
  YAML設定ファイル（例: `api_config.yaml`）に基づいてビルド中に C++ ホスト関数のルックアップテーブルを自動生成し、二分探索で高速（$O(\log N)$）に解決します。
- **高い移植性**: GCC / Clang に対応し、Cortex-M シリーズやPC（ARM/ARM64, x86/x86_64）の各種OS（macOS, Linux, Windows）および RTOS（FreeRTOS, uITRON）で動作します。

## ディレクトリ構成
- [include/](file:///Users/nabeshimamasataka/CLionProjects/embwasm/include): コアライブラリのヘッダーファイル
- [src/](file:///Users/nabeshimamasataka/CLionProjects/embwasm/src): コアライブラリのソースコード
- [demo/](file:///Users/nabeshimamasataka/CLionProjects/embwasm/demo): デモアプリケーション（WASMからホストAPIを呼び出すサンプル）
- [test/](file:///Users/nabeshimamasataka/CLionProjects/embwasm/test): GoogleTest を使用した単体テストコード
- [tools/codegen/](file:///Users/nabeshimamasataka/CLionProjects/embwasm/tools/codegen): ホストAPI自動生成用 Python スクリプト (`gen_api.py`)
- [platform/](file:///Users/nabeshimamasataka/CLionProjects/embwasm/platform): プラットフォーム固有の実装（FreeRTOS, uITRON, 各OS等）

## ビルド方法 & クイックスタート
ビルド手順やデモの実行方法などの詳細については、[docs/getting_started.md](file:///Users/nabeshimamasataka/CLionProjects/embwasm/docs/getting_started.md) を参照してください。

簡易手順：
```bash
# ビルドディレクトリの作成とビルドの実行
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# デモの実行
./build/demo/embwasm_demo
```

## ホストAPIの追加方法 & ツールの使い方
新しいホストAPIの定義方法、WASMモジュールへの公開手順、および自動コード生成ツール（`gen_api.py`）の使い方などの詳細については、以下のドキュメントを参照してください。
- **ホストAPI実装ガイド**: [docs/api_impl_for_wasm.md](file:///Users/nabeshimamasataka/CLionProjects/embwasm/docs/api_impl_for_wasm.md)
- **コード生成ツールの使い方**: [docs/tool_usage.md](file:///Users/nabeshimamasataka/CLionProjects/embwasm/docs/tool_usage.md)

## コーディング規約
C++コードの命名規則や言語機能の制限（STL・例外・RTTI禁止など）などの詳細については、[docs/coding_style.md](file:///Users/nabeshimamasataka/CLionProjects/embwasm/docs/coding_style.md) を参照してください。

## ライセンス
Copyright (c) 2026 embwasm Project. All rights reserved.
