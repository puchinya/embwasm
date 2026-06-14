# Agent Instructions (AGENTS.md)

このドキュメントは、このリポジトリを編集・拡張するAIエージェント（Antigravity等）に対する指示書です。
コードの生成、リファクタリング、バグ修正などを行う際は、必ず以下の基本制約および詳細ルールを厳守してください。

## 1. プロジェクト概要
* **目的**: マイコン組み込み向けの開発（ベアメタル環境）
* **言語**: C++11以上
* **コーディングスタイル**: Google C++ Style Guide 準拠
* **対応コンパイラ**: GCC, Clang
* **対応CPUアーキテクチャ**: Cortex-M シリーズ, PC用 ARM/ARM64, x86, x86_64

## 2. 開発上の重要制約（厳守）
ベアメタル環境向けにビルドされるため、以下の機能はコンパイラレベルで無効化されています。これらを使用したコードはビルドエラーになるか、実行時エラーを引き起こします。

1. **STL (Standard Template Library) の使用禁止**
   * 暗黙的な動的メモリ（ヒープ）割り当てを行うコンテナ（`std::vector`, `std::string`, `std::map` など）は使用できません。
   * 固定長（`std::array`）や、スタックまたは静的メモリ領域を使用するアロケータフリーな設計にしてください。
2. **例外処理 (Exceptions) の禁止**
   * `throw`, `try`, `catch` は使用できません（`-fno-exceptions`）。
   * エラーハンドリングは、エラーコードや `Result` / `Status` オブジェクトの返却で行ってください。
3. **RTTI (Run-Time Type Information) の禁止**
   * `dynamic_cast` や `typeid` は使用できません（`-fno-rtti`）。
   * 多態性のキャストが必要な場合は、`static_cast` または型判別用のメンバ変数（Tag/Enum）を介した安全なダウンキャストを使用してください。

## 3. コーディングスタイルの詳細
命名規則やC++機能の制限に関する詳細なガイドラインは、以下のドキュメントに定義されています。コードを変更する前に必ず参照してください。

* **詳細コーディングスタイル**: [docs/coding_style.md](file:///Users/nabeshimamasataka/CLionProjects/embwasm/docs/coding_style.md)

## 4. エージェントへの具体的アクション要求
* 新しいファイルを提案または作成する場合、[docs/coding_style.md](file:///Users/nabeshimamasataka/CLionProjects/embwasm/docs/coding_style.md) に記載されている命名規則（クラス名: `CamelCase`、メンバ変数: `snake_case_` など）を必ず適用してください。
* 動的メモリ割り当てを排除するため、配列サイズなどは `constexpr` による静的定数として決定してください。
* 未定義動作（UB）を避けるため、ポインタ演算や型キャストは最小限にし、必要であれば `reinterpret_cast` ではなく明示的なインタフェースを使用してください。
