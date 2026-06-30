# demo_cm3 — embwasm Cortex-M3 / FreeRTOS ベンチマーク

embwasm を ARM Cortex-M3 + FreeRTOS 上で動作させるクロスコンパイルデモです。
Renode エミュレータを使ってホスト PC 上で実行できます。

## 構成

```
demo_cm3/
├── cmake/
│   └── arm-clang-toolchain.cmake  # Clang クロスコンパイル ツールチェイン
├── freertos/                       # FreeRTOS V11.1.0 カーネルソース
├── renode/
│   ├── custom_cm3.repl             # Renode プラットフォーム定義
│   └── run_bench.resc              # Renode 実行スクリプト
├── CMakeLists.txt
├── FreeRTOSConfig.h
├── hostapi.wit                     # ホスト API 定義 (WIT)
├── link.ld                         # リンカスクリプト
├── bench_main.cpp                  # ベンチマークタスク
├── main.c                          # FreeRTOS スケジューラ起動
├── startup.c                       # ベクタテーブル・Reset_Handler
├── syscalls.c                      # newlib _write / _sbrk
├── uart.c / uart.h                 # STM32 USART1 ドライバ
└── host_apis.hpp
```

## ハードウェア構成 (エミュレーション)

| 項目 | 設定 |
|------|------|
| CPU | ARM Cortex-M3 |
| クロック | 100 MHz |
| Flash | 0x08000000, 1 MB |
| SRAM | 0x20000000, 512 KB |
| UART | STM32 USART1 (0x40013800) |
| Ethernet | LAN9118 (0x40028000) |

## ビルド要件

- **Clang / LLVM** (`/opt/homebrew/bin/clang`)
- **arm-none-eabi-gcc** (sysroot として使用、`/opt/homebrew/Cellar/arm-none-eabi-gcc/10.3-2021.10/gcc`)
- **CMake** 3.20 以上
- **Python 3**
- **Renode** (`~/Applications/Renode.app` または `/Applications/Renode.app`)

事前に embwasm 本体をビルドしておく必要があります（`bench.wasm` の生成のため）:

```bash
# プロジェクトルートで
cmake -B build && cmake --build build
```

## ビルド

```bash
cd demo_cm3
cmake -B build -S .
cmake --build build
```

ツールチェインファイルは `cmake/arm-clang-toolchain.cmake` が自動的に使用されます。
明示的に指定する場合:

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/arm-clang-toolchain.cmake
```

## Renode でのシミュレーション実行

```bash
cmake --build build --target run_renode
```

または手動で:

```bash
# プロジェクトルートから実行すること (@demo_cm3/... のパス解決のため)
cd /path/to/embwasm
~/Applications/Renode.app/Contents/MacOS/renode --plain --console \
    demo_cm3/renode/run_bench.resc
```

UART 出力は `[INFO] usart1:` プレフィックス付きでターミナルに表示されます。

## コード生成

ビルド時に以下のファイルが自動生成されます:

| 生成ファイル | 生成元 | ツール |
|---|---|---|
| `build/wasm_api_static.hpp/cpp` | `hostapi.wit` | `tools/codegen/embwasm_util.py` |
| `build/bench_wasm.hpp/cpp` | `build/demo/benchmark/bench.wasm` | `tools/codegen/wasm_to_cpp.py` |

---

## ライセンス

### embwasm

Copyright (c) 2026 embwasm Project. All rights reserved.

### FreeRTOS Kernel V11.1.0

`freertos/` ディレクトリ以下のファイルは FreeRTOS Kernel を含みます。

```
FreeRTOS Kernel V11.1.0
Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.

SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
