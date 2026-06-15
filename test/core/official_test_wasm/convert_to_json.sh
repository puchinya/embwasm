#!/bin/sh

# エラーが起きたら停止
#set -e

# 引数のチェック（第1引数：wastファイルがあるディレクトリ、第2引数：出力先ディレクトリ）
if [ "$#" -ne 2 ]; then
    echo "使い方: $0 <wastファイルのあるディレクトリ> <変換後の出力先ディレクトリ>"
    echo "例: $0 ./spec/test/core ./test_out"
    exit 1
fi

INPUT_DIR="$1"
OUTPUT_DIR="$2"

# 出力ディレクトリがなければ作成
mkdir -p "$OUTPUT_DIR"

# 入力ディレクトリ内のすべての .wast ファイルをループ処理
find "$INPUT_DIR" -maxdepth 1 -name "*.wast" | while read -r wast_path; do
    # ファイル名（拡張子なし）を取得 (例: i32.wast -> i32)
    filename=$(basename "$wast_path" .wast)

    # テストごとの専用出力フォルダを定義 (例: ./test_out/i32)
    target_dir="$OUTPUT_DIR/$filename"
    mkdir -p "$target_dir"

    echo "変換中: $filename.wast -> $target_dir/$filename.json"

    # wast2json を実行
    # -o で指定したパスのディレクトリに、付随する .wasm ファイルも一緒に生成されます
    wast2json "$wast_path" -o "$target_dir/$filename.json"
done

echo "すべての変換が完了しました！"
