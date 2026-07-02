# WASMv2対応 トランスコンパイル済みバイトコード仕様書

本ドキュメントは、WASMバイナリをPC上でトランスコンパイルし、マイコン上の高速インタプリタで実行するための独自バイトコード仕様を定義する。

## 1. 基本設計方針

1. **データ配置（32bitスロット構造）**
    * オペランドスタックおよびローカル変数の1スロットを32bit（4バイト）固定とする。
    * i32 / f32 は1スロット、i64 / f64 は2スロットを消費する。
2. **ローカル変数インデックスの変位（2スロット消費）**
    * i64 または f64 型の変数は連続する2スロットを消費し、インデックスを +2 する。パディングは不要。32-bitマイコンでは i64/f64 を 32-bit×2回のロード/ストアで処理するため、64-bitアライメントを考慮する必要がない。
3. **命令長の設計（1Bオペコード可変長）**
    * オペコードは常に **1バイト**。命令長はオペランドの有無とアライメント境界によって決まる可変長。
    * ゼロオペランド命令は **1B** でコード密度を最大化する。
    * 16-bit以上の即値はアライメントが必要な場合にオペコード直後に **パディングバイト（0x00）** を挿入する。トランスコンパイラが命令ごとのIPオフセットを追跡してパディング量を決定する。

    | 命令クラス | バイト構造 | 長さ |
    | :--- | :--- | :--- |
    | **ゼロオペランド** | `[Op:8]` | 1B |
    | **8-bit即値** | `[Op:8][Imm8:8]` | 2B |
    | **単体16-bit即値（2B整合）** | `[Op:8][pad:0-1B][Imm16:16]` | 3〜4B |
    | **16-bit×2 合計32-bit（4B整合）** | `[Op:8][pad:0-3B][Imm16a:16][Imm16b:16]` | 5〜8B |
    | **単体32-bit即値（4B整合）** | `[Op:8][pad:0-3B][Imm32:32]` | 5〜8B |
    | **メモリアクセス Offset（4B整合）** | `[Op:8][pad:0-3B][Offset:32]` | 5〜8B |
    | **64-bit即値（4B整合）** | `[Op:8][pad:0-3B][Lo32:32][Hi32:32]` | 5〜12B |
    | **Extension形式** | `[Prefix:8][ExtOp:8][payload...]` | 2B以上、(Prefix,ExtOp)テーブルで長さ確定 |

    Extension形式はWASMの 0xFC/0xFD プレフィックス命令と同構造。2バイト目の ExtOp が拡張オペコードで、`(Prefix, ExtOp)` のペアごとに命令長をテーブルで確定する（0xFF はWASM未定義のため embwasm 独自用途に使用）。

    **アライメント計算（ARM Thumb-2、ipはオペコード先頭アドレス）**:

    | 整合幅 | ip_imm 計算式 | ARM命令列（オペコード読出し後） |
    | :--- | :--- | :--- |
    | 2B | `(ip + 2) & ~1` | `ADD ip, #1; BIC ip, #1` |
    | 4B | `(ip + 4) & ~3` | `ADD ip, #3; BIC ip, #3` |

    Extension形式（2Bプレフィックス後）の場合: 2B整合は `(ip+3)&~1`、4B整合は `(ip+5)&~3`（ipは0xFCバイト先頭アドレス）。

    **アライメント調整の適用（MCU種別ごとの特性）**:

    | 即値幅 | M0 | M3 |
    | :--- | :--- | :--- |
    | 8-bit | 不要 | 不要 |
    | 単体16-bit | **必須**（HWフォルト回避） | 不要（アンアライメントLDRH avg 3cyc < 整合4cyc） |
    | 単体32-bit | **必須**（HWフォルト回避） | 不要（アンアライメントLDR avg 3.5cyc < 整合4cyc） |
    | 16+16 合計32-bit | **必須**（4B整合+2×LDRH） | **有利**（4B整合LDR 1回 4cyc < アンアライメント2×LDRH avg 6cyc） |
    | 64-bit | **必須**（4B整合+4×LDRH） | **有利**（4B整合2×LDR 6cyc < アンアライメント2×LDR avg 7cyc） |

    本仕様はM0での安全動作を保証するため**全16-bit以上の即値でアライメントパディングを適用する**。M3環境では同一バイトコードのままアンアライメントアクセスによる最適化デコーダを用いることも可能（コンパイル時条件分岐）。

    **ipの進め処理はディスパッチループではなく各命令ハンドラが行う**（可変長命令への対応のため）。ゼロオペランド命令ハンドラは `ip += 1` のみ実行する。

    **デコーダの命令クラス判定（オペランド有無で範囲分け）**:

    | オペコード範囲 | 命令クラス | サイズ |
    | :--- | :--- | :--- |
    | 0x00–0x5F | ゼロオペランド（制御・i32演算・型変換） | **1B** |
    | 0x60–0x6F | 8-bit即値 | **2B** |
    | 0x70–0x7F | 単体16-bit即値（2B整合） | **3〜4B** |
    | 0x80–0x9F | 32-bit整合オペランド（メモリOffset または 16-bit×2） | **5〜8B** |
    | 0xA0–0xA3 | 定数（4B整合） | **5〜8B / 5〜12B** |
    | 0xA4–0xE8 | ゼロオペランド（i64/f32/f64 演算） | **1B** |
    | 0xE9–0xF9 | 予約（無効命令） | — |
    | 0xFA | 低頻度ゼロオペランドプレフィックス | **2B** |
    | 0xFB | 低頻度オペランド付きプレフィックス | **3〜5B** |
    | 0xFC | WASMv2拡張プレフィックス（WASM互換） | `(0xFC, ExtOp)` テーブル |
    | 0xFD | SIMD（将来、WASM互換） | — |
    | 0xFE | 予約（WASM未定義、使用不可） | — |
    | 0xFF | embwasm独自拡張 | — |

    **事前検証用の簡易サイズ判定**（バリデータ擬似コード、ipはオペコード先頭アドレス）:
    ```c
    if (op <= 0x5F)      size = 1;
    else if (op <= 0x6F) size = 2;
    else if (op == 0x72) size = br_table_size(ip);      // BR_TABLE可変長
    else if (op <= 0x7F) size = ((ip+2)&~1) + 2 - ip;  // 単体16-bit: 3〜4B
    else if (op <= 0x9F) size = ((ip+4)&~3) + 4 - ip;  // 32-bit整合: 5〜8B
    else if (op == 0xA1 || op == 0xA3)
                         size = ((ip+4)&~3) + 8 - ip;  // 64-bit定数: 5〜12B
    else if (op <= 0xA3) size = ((ip+4)&~3) + 4 - ip;  // 32-bit定数: 5〜8B
    else if (op <= 0xE8) size = 1;
    else if (op <= 0xF9) return INVALID;
    else                 size = ext_size(op, ip);        // prefix テーブル引き
    ```

4. **インデックス幅の統一（16-bit）**
    * FuncIdx / TypeIdx / TableIdx / GlobalIdx はすべて **16bit（最大 65535）** とする。
    * これにより MCU 向けインタプリタのデコードロジックが単純化される。WASM仕様の u32 インデックス上限は満たさないが、組み込み用途では 65535 を超えることはない。
5. **ラベル解決の事前インライン化**
    * block, loop, if/else などの構造化制御フローはトランスパイル時にすべて除去され、事前計算された相対PCオフセットを持つ BR / BR_IF に解決される。

---

## 2. 前提条件と実行モデル

### 2.1 トランスコンパイルの実行位置とデプロイ

トランスコンパイルは **PC（開発用ホストマシン）上でオフライン実行** し、生成されたバイトコードバイナリをマイコンのフラッシュROMに書き込んで配布する。

```
[WASM バイナリ]
      ↓
  PC上 トランスコンパイラ
      ↓
[embwasm バイトコード]
      ↓
  マイコン フラッシュROM 書き込み
      ↓
  マイコン起動
      ↓
  実行前安全検査（1パス、embwasm.meta ブロック構造テーブル使用） ← 実行開始前に1回だけ実施
      ↓ 合格時のみ実行開始
  マイコン上インタプリタ実行（block/loop/if/else/end なし・実行中チェックなし）
```

安全検査はマイコン上で実行を開始する前に1回だけ行う。実行ループ内での逐次チェックが不要になるため、ディスパッチループを最速に保てる。

### 2.2 実行前安全検査仕様

**目的**: インタープリタ（ホストプログラム）が暴走しないことの保証。バイナリは**信頼しない**（プラグイン方式・署名なし）。変なプログラムが変な結果を返すことは問題なく、ホストプロセス自体が破壊されないことだけを保証する。

**1パスアルゴリズム（RAM消費: O(MAX_NESTING_DEPTH)）**

embwasm.meta 内のブロック構造テーブルを用いて label_stack を構築しながら1回のスキャンで完了する:

1. アドレス A が `block_start_addr` に一致するテーブルエントリ → `(jump_target_addr, current_depth)` を label_stack に push
2. アドレス T が `jump_target_addr` に一致するテーブルエントリ → label_stack から pop し、スタック深度を照合
3. BR_NEAR / BR_FAR に遭遇 → 計算したジャンプ先が label_stack の top と一致するか確認

**検査項目（9項目）**

1. **命令境界整合性**: ジャンプ先 `current_ip + Offset_bytes` が embwasm.meta のブロック構造テーブルに記録された有効命令先頭アドレスに一致すること
2. **スタック深度下限**: スタック深度が負にならないこと（インタープリタ underflow 防止）
3. **スタック深度上限**: `MaxStackDepth` を超えないこと（インタープリタ overflow 防止）
4. **関数インデックス境界**: CALL の FuncIdx が `FunctionCount` 未満であること
5. **ローカル変数境界**: LGET / LSET / LTEE の MappedIdx が、Type セクション（引数型）と Code セクション（local 宣言）から 2-slot マッピングで計算したフレームサイズ未満であること
6. **ジャンプ先整合**: 全 BR のジャンプ先が label_stack エントリと対応すること（不明なジャンプ先は拒否）
7. **グローバル変数境界**: GGET / GSET の GlobalIdx が Global セクション要素数未満であること
8. **型シグネチャ境界**: CALL_IND の TypeIdx が Type セクション要素数未満であること
9. **テーブル/データ/要素境界**: 各インデックス系命令が対応するセクション要素数未満であること
   - TBL_GET / TBL_SET / table.grow / table.size / table.fill: TableIdx < Table セクション要素数
   - memory.init / data.drop: DataIdx < Data セクション要素数
   - table.init / elem.drop: ElemIdx < Elem セクション要素数

---

## 3. ファイル形式と識別方法

### 3.1 バイナリフォーマット識別子

WASMバイナリ形式と同一の構造を維持しつつ、Versionフィールドを変更して識別する。

| フィールド | オフセット | 標準WASM | embwasm バイトコード |
| :--- | :--- | :--- | :--- |
| Magic | 0–3 | `00 61 73 6D` ("\0asm") | `00 61 73 6D`（同一） |
| Version | 4–7 | `01 00 00 00` | **`01 00 45 42`** |

Version フィールドの末2バイトを `'E'(0x45), 'B'(0x42)` とする。標準のWASMツールはこのバージョン値を拒否するため誤処理を防げる。識別は4バイト比較1回のみで最速。

セクション構造はWASMと同一（Type/Import/Function/Memory/Export/Code/Custom 等）。ただしコードセクション内のバイトコードは本仕様の命令セットに置換されている。

### 3.2 embwasm.meta カスタムセクション仕様

セクションID: 0x00（カスタムセクション）、名前文字列: `"embwasm.meta"` を Code セクション直後に配置する。

```
[MetaVersion:       8bit]   — メタデータ形式バージョン（現在 0x01）
[FormatFlags:      16bit]   — ビットフラグ
                               bit0: デバッグ情報除去済み
                               bit1: マルチメモリ有効
                               bit2–15: 予約（0固定）
                               ※「安全検査合格済み」フラグは設けない
                                 （バイナリ非信頼モデルのため、MCU は常に再検査する）
[OriginalHash:     32byte]  — 元WASMファイルの SHA-256 ハッシュ
[TranspileTimestamp: 32bit] — トランスコンパイル時刻（Unixタイムスタンプ）
[FunctionCount:    16bit]   — 関数数
[MaxStackDepth:    16bit] × FunctionCount
                            — 関数ごとの最大評価スタック深度（スロット数）

--- ブロック構造テーブル（検証専用・実行時不使用）---
[BlockEntryCount:  16bit]   — 全関数の合計ブロック数
[BlockEntry × BlockEntryCount]:
  [block_type:   8bit]   0=block, 1=loop, 2=if
  [reserved:     8bit]   0x00
  [func_index:  16bit]   所属関数インデックス
  [start_addr:  32bit]   ブロック開始アドレス（Codeセクション内バイトオフセット）
  [target_addr: 32bit]   ジャンプ先アドレス（block/if → END直後, loop → 開始アドレス）
```

エントリサイズは 12B × BlockEntryCount。テーブルは検証器が label_stack を構築する際にのみ参照し、実行中はインタープリタから参照しない。

---

## 4. 変換（トランスパイル）ルール

### ① ローカル変数インデックスの再計算ロジック

WASMの関数シグネチャおよび local 宣言をスキャンし、インデックスのマッピングテーブルを構築する。

```python
current_target_index = 0
mapped_index = {}
for i, wasm_type in enumerate(wasm_local_variables):
    if wasm_type in ['i32', 'f32']:
        mapped_index[i] = current_target_index
        current_target_index += 1
    elif wasm_type in ['i64', 'f64']:
        mapped_index[i] = current_target_index
        current_target_index += 2  # 2スロット消費
```

マッピング例:
* WASM元の配置: [0: i32, 1: i64, 2: i32]
* トランスパイル後:
    * Idx 0 (i32) → 独自Idx 0（次の空き: 0 + 1 = 1）
    * Idx 1 (i64) → 独自Idx 1（次の空き: 1 + 2 = 3）
    * Idx 2 (i32) → 独自Idx 3

### ② ラベルジャンプの2パス解決

1. **1パス目**: WASM命令を一度独自命令に仮マッピングし、各命令の出力バイトサイズを累計して「独自バイトコード上の絶対PC」をテーブルに記録する。命令サイズはアライメントパディングを含む（トランスコンパイラがIPを追跡）。
2. **2パス目**: br, br_if 生成時、ターゲットとなるWASM命令の独自絶対PCから現在の独自PCを差し引いて「バイト単位の相対オフセット」を算出する。
    * インタプリタは `ip += Offset_bytes` で PC を更新する。
    * バイトオフセットが ±127B 以内 → 2バイト命令（BR_NEAR / BRIF_NEAR）を適用
    * 収まらない場合 → 3〜4バイト命令（BR_FAR / BRIF_FAR）を適用（バイトオフセット ±32767B まで対応）

### ③ Extension形式命令の変換ルール

WASMの 0xFC プレフィックス命令（trunc_sat / bulk memory / table ops）は Extension形式にトランスパイルする。

```
[WASM 0xFC][SubOp: LEB128][Payload...]
  → [0xFC][ExtOp: 8bit][Payload...]
```

ペイロードの即値は本仕様のアライメントルールに従ってパディングを挿入する:
- 16-bit即値: 2B整合（`(ip+3)&~1`、ipは0xFCバイトアドレス）
- 16-bit×2合計32-bit: 4B整合（`(ip+5)&~3`）
- 命令長の奇偶制約はない（1B可変長設計のため）

未知のWASM命令（サポート外 0xFC サブオペコード、0xFD 等）に遭遇した場合はトランスパイルエラーとして扱い、出力を中止する。未知命令はデプロイ前安全検査（Section 2.2）でも拒絶される。

---

## 5. 全命令セット・マッピングテーブル

### 5.1 制御フロー & パラメトリック命令 (Control Flow & Parametric)

WASMの構造化ブロック（block, loop, if, else, end）はすべて事前にアドレス解決され、独自命令からは除去される。

#### ゼロオペランド制御命令（1B、0x00–0x03）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ | バイト構造 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0x00 | unreachable | 0x00 | UNREACHABLE | 1B | `[0x00]` |
| 0x01 | nop | 0x01 | NOP | 1B | `[0x01]` |
| 0x0F | return | 0x02 | RETURN | 1B | `[0x02]` |
| 0x1B | select | 0x03 | SELECT | 1B | `[0x03]` |
| 0x02 | block | — | — | — | 除去（ジャンプ先PCターゲットとして記録） |
| 0x03 | loop | — | — | — | 除去（ジャンプ先PCターゲットとして記録） |
| 0x04 | if | — | — | — | 除去（条件不成立時のジャンプ命令に置換） |
| 0x05 | else | — | — | — | 除去（ブロックエンドへの無条件ジャンプに置換） |
| 0x0B | end | — | — | — | 除去（ターゲットPCの終端インデックス） |

#### 8-bit即値制御命令（2B、0x61–0x63）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ | バイト構造 & 変換仕様 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0x0C | br | 0x61 | BR_NEAR | 2B | `[0x61][Offset:i8]`（バイトオフセット ±127B） |
| 0x0D | br_if | 0x62 | BRIF_NEAR | 2B | `[0x62][Offset:i8]`（近傍条件ジャンプ） |
| 0x1A | drop | 0x63 | DROP | 2B | `[0x63][N:8]`（N = 破棄スロット数: i32/f32 → 1, i64/f64 → 2） |

#### 16-bit即値制御命令（2B整合 3〜4B、0x70–0x73）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ | バイト構造 & 変換仕様 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0x0C | br | 0x70 | BR_FAR | 3〜4B | `[0x70][pad:0-1B][Offset:i16]`（バイトオフセット ±32767B） |
| 0x0D | br_if | 0x71 | BRIF_FAR | 3〜4B | `[0x71][pad:0-1B][Offset:i16]`（遠隔条件ジャンプ） |
| 0x0E | br_table | 0x72 | BR_TABLE | 可変 | `[0x72][Count:8][pad:0-1B][Offset0:i16]...[Default:i16]`（Count+1個のターゲット、2B整合） |
| 0x10 | call | 0x73 | CALL | 3〜4B | `[0x73][pad:0-1B][FuncIdx:16]` |

#### 4B整合2×16-bit制御命令（5〜8B、0x97）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ | バイト構造 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0x11 | call_indirect | 0x97 | CALL_IND | 5〜8B | `[0x97][pad:0-3B][TypeIdx:16][TableIdx:16]`（4B整合） |

### 5.2 変数操作命令 (Variables)

※ MappedIdx には i64/f64 を2スロット消費として再計算したインデックスが格納される。

#### 8-bit即値 ローカル変数命令（2B、0x64–0x69）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ | バイト構造 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0x20 | local.get (i32/f32) | 0x64 | LGET_32 | 2B | `[0x64][MappedIdx:8]`（MappedIdx ≤ 255） |
| 0x20 | local.get (i64/f64) | 0x65 | LGET_64 | 2B | `[0x65][MappedIdx:8]`（2スロット分ロード） |
| 0x21 | local.set (i32/f32) | 0x66 | LSET_32 | 2B | `[0x66][MappedIdx:8]` |
| 0x21 | local.set (i64/f64) | 0x67 | LSET_64 | 2B | `[0x67][MappedIdx:8]` |
| 0x22 | local.tee (i32/f32) | 0x68 | LTEE_32 | 2B | `[0x68][MappedIdx:8]` |
| 0x22 | local.tee (i64/f64) | 0x69 | LTEE_64 | 2B | `[0x69][MappedIdx:8]` |

#### 16-bit即値 変数命令（2B整合 3〜4B、0x74–0x7B）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ | バイト構造 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0x20 | local.get (i32/f32) | 0x76 | LGET_32_L | 3〜4B | `[0x76][pad:0-1B][MappedIdx:16]`（MappedIdx ≥ 256） |
| 0x20 | local.get (i64/f64) | 0x77 | LGET_64_L | 3〜4B | `[0x77][pad:0-1B][MappedIdx:16]` |
| 0x21 | local.set (i32/f32) | 0x78 | LSET_32_L | 3〜4B | `[0x78][pad:0-1B][MappedIdx:16]` |
| 0x21 | local.set (i64/f64) | 0x79 | LSET_64_L | 3〜4B | `[0x79][pad:0-1B][MappedIdx:16]` |
| 0x22 | local.tee (i32/f32) | 0x7A | LTEE_32_L | 3〜4B | `[0x7A][pad:0-1B][MappedIdx:16]` |
| 0x22 | local.tee (i64/f64) | 0x7B | LTEE_64_L | 3〜4B | `[0x7B][pad:0-1B][MappedIdx:16]` |
| 0x23 | global.get | 0x74 | GGET | 3〜4B | `[0x74][pad:0-1B][GlobalIdx:16]` |
| 0x24 | global.set | 0x75 | GSET | 3〜4B | `[0x75][pad:0-1B][GlobalIdx:16]` |

### 5.3 メモリ & テーブル操作命令 (Memory & Tables)

全ロード/ストア命令は `[Opcode:8][pad:0-3B][Offset:32]` 形式（Offset は u32 フルレンジ対応、4B整合）。WASMのalignヒントは組み込みターゲットでは型から自明なため省略する。

#### メモリ ロード命令（WASM 0x28–0x35、全14命令）

WASM即値: `align:u32（省略）, offset:u32`

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ |
| :--- | :--- | :--- | :--- | :--- |
| 0x28 | i32.load | 0x80 | LOAD_I32 | 5〜8B |
| 0x29 | i64.load | 0x81 | LOAD_I64 | 5〜8B |
| 0x2A | f32.load | 0x82 | LOAD_F32 | 5〜8B |
| 0x2B | f64.load | 0x83 | LOAD_F64 | 5〜8B |
| 0x2C | i32.load8_s | 0x84 | LOAD_I32_8S | 5〜8B |
| 0x2D | i32.load8_u | 0x85 | LOAD_I32_8U | 5〜8B |
| 0x2E | i32.load16_s | 0x86 | LOAD_I32_16S | 5〜8B |
| 0x2F | i32.load16_u | 0x87 | LOAD_I32_16U | 5〜8B |
| 0x30 | i64.load8_s | 0x88 | LOAD_I64_8S | 5〜8B |
| 0x31 | i64.load8_u | 0x89 | LOAD_I64_8U | 5〜8B |
| 0x32 | i64.load16_s | 0x8A | LOAD_I64_16S | 5〜8B |
| 0x33 | i64.load16_u | 0x8B | LOAD_I64_16U | 5〜8B |
| 0x34 | i64.load32_s | 0x8C | LOAD_I64_32S | 5〜8B |
| 0x35 | i64.load32_u | 0x8D | LOAD_I64_32U | 5〜8B |

全て `[独自Op][pad:0-3B][Offset:32]`、4B整合。

#### メモリ ストア命令（WASM 0x36–0x3E、全9命令）

WASM即値: `align:u32（省略）, offset:u32`

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ |
| :--- | :--- | :--- | :--- | :--- |
| 0x36 | i32.store | 0x8E | STORE_I32 | 5〜8B |
| 0x37 | i64.store | 0x8F | STORE_I64 | 5〜8B |
| 0x38 | f32.store | 0x90 | STORE_F32 | 5〜8B |
| 0x39 | f64.store | 0x91 | STORE_F64 | 5〜8B |
| 0x3A | i32.store8 | 0x92 | STORE_I32_8 | 5〜8B |
| 0x3B | i32.store16 | 0x93 | STORE_I32_16 | 5〜8B |
| 0x3C | i64.store8 | 0x94 | STORE_I64_8 | 5〜8B |
| 0x3D | i64.store16 | 0x95 | STORE_I64_16 | 5〜8B |
| 0x3E | i64.store32 | 0x96 | STORE_I64_32 | 5〜8B |

全て `[独自Op][pad:0-3B][Offset:32]`、4B整合。

### 5.4 数値演算命令 (Numerics)

#### 定数生成 (Constants)

| WASM Op | WASM命令名 | WASM即値 | 独自Op | 独自命令名 | 長さ | バイト構造 & 変換仕様 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 0x41 | i32.const | `n:i32 (LEB128)` | 0x60 | I32C_8 | 2B | `[0x60][Imm:i8]`（−128〜127 の即値用） |
| | | | 0xA0 | I32C_32 | 5〜8B | `[0xA0][pad:0-3B][Imm:32]`（32bit即値、4B整合） |
| 0x42 | i64.const | `n:i64 (LEB128)` | 0xA1 | I64C_64 | 5〜12B | `[0xA1][pad:0-3B][Lo:32][Hi:32]`（4B整合、2回読み） |
| 0x43 | f32.const | `z:f32 (4B)` | 0xA2 | F32C_32 | 5〜8B | `[0xA2][pad:0-3B][Imm:32]`（4B整合） |
| 0x44 | f64.const | `z:f64 (8B)` | 0xA3 | F64C_64 | 5〜12B | `[0xA3][pad:0-3B][Lo:32][Hi:32]`（4B整合、2回読み） |

#### i32 比較命令（WASM 0x45–0x4F、全11命令、1B ゼロオペランド）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 |
| :--- | :--- | :--- | :--- |
| 0x45 | i32.eqz | 0x10 | I32_EQZ |
| 0x46 | i32.eq | 0x11 | I32_EQ |
| 0x47 | i32.ne | 0x12 | I32_NE |
| 0x48 | i32.lt_s | 0x13 | I32_LT_S |
| 0x49 | i32.lt_u | 0x14 | I32_LT_U |
| 0x4A | i32.gt_s | 0x15 | I32_GT_S |
| 0x4B | i32.gt_u | 0x16 | I32_GT_U |
| 0x4C | i32.le_s | 0x17 | I32_LE_S |
| 0x4D | i32.le_u | 0x18 | I32_LE_U |
| 0x4E | i32.ge_s | 0x19 | I32_GE_S |
| 0x4F | i32.ge_u | 0x1A | I32_GE_U |

全て 1B: `[独自Op]`

#### i32 演算命令（WASM 0x67–0x78、全18命令、1B ゼロオペランド）

| WASM Op | WASM命令名 | 独自Op | 独自命令名 |
| :--- | :--- | :--- | :--- |
| 0x67 | i32.clz | 0x1B | I32_CLZ |
| 0x68 | i32.ctz | 0x1C | I32_CTZ |
| 0x69 | i32.popcnt | 0x1D | I32_POPCNT |
| 0x6A | i32.add | 0x1E | I32_ADD |
| 0x6B | i32.sub | 0x1F | I32_SUB |
| 0x6C | i32.mul | 0x20 | I32_MUL |
| 0x6D | i32.div_s | 0x21 | I32_DIV_S |
| 0x6E | i32.div_u | 0x22 | I32_DIV_U |
| 0x6F | i32.rem_s | 0x23 | I32_REM_S |
| 0x70 | i32.rem_u | 0x24 | I32_REM_U |
| 0x71 | i32.and | 0x25 | I32_AND |
| 0x72 | i32.or | 0x26 | I32_OR |
| 0x73 | i32.xor | 0x27 | I32_XOR |
| 0x74 | i32.shl | 0x28 | I32_SHL |
| 0x75 | i32.shr_s | 0x29 | I32_SHR_S |
| 0x76 | i32.shr_u | 0x2A | I32_SHR_U |
| 0x77 | i32.rotl | 0x2B | I32_ROTL |
| 0x78 | i32.rotr | 0x2C | I32_ROTR |

全て 1B: `[独自Op]`

#### i64 / f32 / f64 比較・演算命令（1B ゼロオペランド）

*スタック上の i64/f64 はインタプリタ内部で2スロット（32bit×2）の POP/PUSH として処理される。*

| WASM範囲 | WASM分類 | 独自Op範囲 | 命令数 | 備考 |
| :--- | :--- | :--- | :--- | :--- |
| 0x50–0x5A | i64 比較（eqz + eq〜ge_u） | 0xA4–0xAE | 11 | i64.eqz(0x50)〜i64.ge_u(0x5A) |
| 0x5B–0x60 | f32 比較（eq〜ge） | 0xAF–0xB4 | 6 | f32.eq(0x5B)〜f32.ge(0x60) |
| 0x61–0x66 | f64 比較（eq〜ge） | 0xB5–0xBA | 6 | f64.eq(0x61)〜f64.ge(0x66) |
| 0x79–0x8A | i64 演算（clz〜rotr） | 0xBB–0xCC | 18 | i64.clz(0x79)〜i64.rotr(0x8A) |
| 0x8B–0x98 | f32 演算（abs〜copysign） | 0xCD–0xDA | 14 | f32.abs(0x8B)〜f32.copysign(0x98) |
| 0x99–0xA6 | f64 演算（abs〜copysign） | 0xDB–0xE8 | 14 | f64.abs(0x99)〜f64.copysign(0xA6) |

全て 1B: `[独自Op]`

### 5.5 型変換命令（WASM 0xA7–0xBF、直接1B ゼロオペランド 0x2D–0x45）

型変換命令は純粋なスタック操作のため、直接オペコード（ゼロオペランド範囲）に1Bで割り付ける。

| WASM Op | WASM命令名 | 独自Op | 独自命令名 | 長さ |
| :--- | :--- | :--- | :--- | :--- |
| 0xA7 | i32.wrap_i64 | 0x2D | CONV_I32_WRAP | 1B |
| 0xA8 | i32.trunc_f32_s | 0x2E | CONV_I32_TRUNC_F32S | 1B |
| 0xA9 | i32.trunc_f32_u | 0x2F | CONV_I32_TRUNC_F32U | 1B |
| 0xAA | i32.trunc_f64_s | 0x30 | CONV_I32_TRUNC_F64S | 1B |
| 0xAB | i32.trunc_f64_u | 0x31 | CONV_I32_TRUNC_F64U | 1B |
| 0xAC | i64.extend_i32_s | 0x32 | CONV_I64_EXT_S | 1B |
| 0xAD | i64.extend_i32_u | 0x33 | CONV_I64_EXT_U | 1B |
| 0xAE | i64.trunc_f32_s | 0x34 | CONV_I64_TRUNC_F32S | 1B |
| 0xAF | i64.trunc_f32_u | 0x35 | CONV_I64_TRUNC_F32U | 1B |
| 0xB0 | i64.trunc_f64_s | 0x36 | CONV_I64_TRUNC_F64S | 1B |
| 0xB1 | i64.trunc_f64_u | 0x37 | CONV_I64_TRUNC_F64U | 1B |
| 0xB2 | f32.convert_i32_s | 0x38 | CONV_F32_CVT_I32S | 1B |
| 0xB3 | f32.convert_i32_u | 0x39 | CONV_F32_CVT_I32U | 1B |
| 0xB4 | f32.convert_i64_s | 0x3A | CONV_F32_CVT_I64S | 1B |
| 0xB5 | f32.convert_i64_u | 0x3B | CONV_F32_CVT_I64U | 1B |
| 0xB6 | f32.demote_f64 | 0x3C | CONV_F32_DEMOTE | 1B |
| 0xB7 | f64.convert_i32_s | 0x3D | CONV_F64_CVT_I32S | 1B |
| 0xB8 | f64.convert_i32_u | 0x3E | CONV_F64_CVT_I32U | 1B |
| 0xB9 | f64.convert_i64_s | 0x3F | CONV_F64_CVT_I64S | 1B |
| 0xBA | f64.convert_i64_u | 0x40 | CONV_F64_CVT_I64U | 1B |
| 0xBB | f64.promote_f32 | 0x41 | CONV_F64_PROMOTE | 1B |
| 0xBC | i32.reinterpret_f32 | 0x42 | CONV_I32_REINT | 1B |
| 0xBD | i64.reinterpret_f64 | 0x43 | CONV_I64_REINT | 1B |
| 0xBE | f32.reinterpret_i32 | 0x44 | CONV_F32_REINT | 1B |
| 0xBF | f64.reinterpret_i64 | 0x45 | CONV_F64_REINT | 1B |

全て 1B: `[独自Op]`

### 5.6 低頻度命令プレフィックス（0xFB）

組み込み用途で使用頻度が低い命令を 0xFB プレフィックスにまとめる。全て `[0xFB][ExtOp:8][payload...]`。

| WASM Op | WASM命令名 | ExtOp | 独自命令名 | 長さ | バイト構造 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0xD0 | ref.null | 0x00 | REF_NULL | 3B | `[0xFB][0x00][RefType:8]` |
| 0xD1 | ref.is_null | 0x01 | REF_ISNULL | 2B | `[0xFB][0x01]` |
| 0x3F | memory.size | 0x02 | MEM_SIZE | 3B | `[0xFB][0x02][MemIdx:8]` |
| 0x40 | memory.grow | 0x03 | MEM_GROW | 3B | `[0xFB][0x03][MemIdx:8]` |
| 0x1C | select (t*) | 0x04 | SELECT_T | 4〜5B | `[0xFB][0x04][pad:0-1B][Type:16]`（2B整合、`(ip+3)&~1`） |
| 0x25 | table.get | 0x05 | TBL_GET | 4〜5B | `[0xFB][0x05][pad:0-1B][TableIdx:16]`（2B整合） |
| 0x26 | table.set | 0x06 | TBL_SET | 4〜5B | `[0xFB][0x06][pad:0-1B][TableIdx:16]`（2B整合） |
| 0xD2 | ref.func | 0x07 | REF_FUNC | 4〜5B | `[0xFB][0x07][pad:0-1B][FuncIdx:16]`（2B整合） |

### 5.7 WASMv2 拡張命令（0xFC プレフィックス、WASM互換）

WASMv2 で追加された 0xFC 系命令。`[0xFC][ExtOp][Payload...]`。

#### Non-trapping Truncation（WASM 0xFC 0x00–0x07）

| WASM ExtOp | WASM命令名 | 独自構造 | 長さ |
| :--- | :--- | :--- | :--- |
| 0x00 | i32.trunc_sat_f32_s | `[0xFC][0x00]` | 2B |
| 0x01 | i32.trunc_sat_f32_u | `[0xFC][0x01]` | 2B |
| 0x02 | i32.trunc_sat_f64_s | `[0xFC][0x02]` | 2B |
| 0x03 | i32.trunc_sat_f64_u | `[0xFC][0x03]` | 2B |
| 0x04 | i64.trunc_sat_f32_s | `[0xFC][0x04]` | 2B |
| 0x05 | i64.trunc_sat_f32_u | `[0xFC][0x05]` | 2B |
| 0x06 | i64.trunc_sat_f64_s | `[0xFC][0x06]` | 2B |
| 0x07 | i64.trunc_sat_f64_u | `[0xFC][0x07]` | 2B |

#### Bulk Memory / Reference Types（WASM 0xFC 0x08–0x11）

アライメントパディングは 0xFC バイトアドレスを ip として計算: 2B整合 `(ip+3)&~1`、4B整合 `(ip+5)&~3`。

| WASM ExtOp | WASM命令名 | WASM即値 | 独自構造 | 長さ | 分類 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0x08 | memory.init | `dataidx, memidx` | `[0xFC][0x08][DataIdx:8][MemIdx:8]` | 4B | Bulk Memory |
| 0x09 | data.drop | `dataidx` | `[0xFC][0x09][DataIdx:8]` | 3B | Bulk Memory |
| 0x0A | memory.copy | `dstmem, srcmem` | `[0xFC][0x0A][DstMem:8][SrcMem:8]` | 4B | Bulk Memory |
| 0x0B | memory.fill | `memidx` | `[0xFC][0x0B][MemIdx:8]` | 3B | Bulk Memory |
| 0x0C | table.init | `elemidx, tableidx` | `[0xFC][0x0C][ElemIdx:8][pad:0-1B][TblIdx:16]` | 4〜5B | Reference Types |
| 0x0D | elem.drop | `elemidx` | `[0xFC][0x0D][ElemIdx:8]` | 3B | Reference Types |
| 0x0E | table.copy | `dsttbl, srctbl` | `[0xFC][0x0E][pad:0-3B][DstTbl:16][SrcTbl:16]` | 4〜9B | Reference Types（4B整合） |
| 0x0F | table.grow | `tableidx` | `[0xFC][0x0F][pad:0-1B][TblIdx:16]` | 3〜4B | Reference Types（スタック: delta:i32, init:ref → 旧サイズ:i32） |
| 0x10 | table.size | `tableidx` | `[0xFC][0x10][pad:0-1B][TblIdx:16]` | 3〜4B | Reference Types（→ サイズ:i32） |
| 0x11 | table.fill | `tableidx` | `[0xFC][0x11][pad:0-1B][TblIdx:16]` | 3〜4B | Reference Types（スタック: offset:i32, val:ref, count:i32） |

---

## 6. オペコード空間まとめ

```
【ゼロオペランド (1B) — 0x00–0x5F】
  0x00      : UNREACHABLE
  0x01      : NOP
  0x02      : RETURN
  0x03      : SELECT
  0x04–0x0F : 予約（12スロット）
  0x10–0x1A : i32 比較（EQZ, EQ, NE, LT_S, LT_U, GT_S, GT_U, LE_S, LE_U, GE_S, GE_U）
  0x1B–0x2C : i32 演算（CLZ, CTZ, POPCNT, ADD, SUB, MUL, DIV_S/U, REM_S/U,
               AND, OR, XOR, SHL, SHR_S/U, ROTL, ROTR）
  0x2D–0x45 : 型変換 25命令（i32.wrap_i64〜f64.reinterpret_i64）
  0x46–0x5F : 予約（26スロット、ゼロオペランド拡張用）

【8-bit即値 (2B) — 0x60–0x6F】
  0x60      : I32C_8（i32.const 小即値 ±127）
  0x61      : BR_NEAR（バイトオフセット ±127B）
  0x62      : BRIF_NEAR（条件ジャンプ ±127B）
  0x63      : DROP（N:8 スロット破棄）
  0x64      : LGET_32（MappedIdx:8）
  0x65      : LGET_64（MappedIdx:8）
  0x66      : LSET_32（MappedIdx:8）
  0x67      : LSET_64（MappedIdx:8）
  0x68      : LTEE_32（MappedIdx:8）
  0x69      : LTEE_64（MappedIdx:8）
  0x6A–0x6F : 予約（6スロット）

【単体16-bit即値 (2B整合 3〜4B) — 0x70–0x7F】
  0x70      : BR_FAR（Offset:i16 バイトオフセット ±32767B）
  0x71      : BRIF_FAR（Offset:i16）
  0x72      : BR_TABLE（Count:8 + 2B整合 [Offset:i16]×(Count+1)、可変長）
  0x73      : CALL（FuncIdx:16）
  0x74      : GGET（GlobalIdx:16）
  0x75      : GSET（GlobalIdx:16）
  0x76      : LGET_32_L（MappedIdx:16 ≥ 256）
  0x77      : LGET_64_L（MappedIdx:16）
  0x78      : LSET_32_L（MappedIdx:16）
  0x79      : LSET_64_L（MappedIdx:16）
  0x7A      : LTEE_32_L（MappedIdx:16）
  0x7B      : LTEE_64_L（MappedIdx:16）
  0x7C–0x7F : 予約（4スロット）

【4B整合Offset:32 メモリアクセス (5〜8B) — 0x80–0x9F】
  0x80–0x8D : メモリ ロード 14命令（[Op][pad:0-3B][Offset:32]）
  0x8E–0x96 : メモリ ストア 9命令（[Op][pad:0-3B][Offset:32]）
  0x97      : CALL_IND（[0x97][pad:0-3B][TypeIdx:16][TableIdx:16]、4B整合 5〜8B）
  0x98–0x9F : 予約（8スロット）

【定数（4B整合）— 0xA0–0xA3】
  0xA0      : I32C_32（5〜8B: [0xA0][pad:0-3B][Imm:32]）
  0xA1      : I64C_64（5〜12B: [0xA1][pad:0-3B][Lo:32][Hi:32]）
  0xA2      : F32C_32（5〜8B: [0xA2][pad:0-3B][Imm:32]）
  0xA3      : F64C_64（5〜12B: [0xA3][pad:0-3B][Lo:32][Hi:32]）

【ゼロオペランド i64/f32/f64 演算 (1B) — 0xA4–0xE8】
  0xA4–0xAE : i64 比較 11命令
  0xAF–0xB4 : f32 比較 6命令
  0xB5–0xBA : f64 比較 6命令
  0xBB–0xCC : i64 演算 18命令
  0xCD–0xDA : f32 演算 14命令
  0xDB–0xE8 : f64 演算 14命令

  0xE9–0xF9 : 予約（17スロット、無効命令）

【プレフィックス命令 — 0xFA–0xFF】
  0xFA + ExtOp : embwasm低頻度ゼロオペランド（2B: [0xFA][ExtOp]）予約（将来用）
  0xFB + ExtOp : embwasm低頻度オペランド付き（3〜5B）
                  ExtOp 0x00: REF_NULL [RefType:8]
                  ExtOp 0x01: REF_ISNULL（2B）
                  ExtOp 0x02: MEM_SIZE [MemIdx:8]
                  ExtOp 0x03: MEM_GROW [MemIdx:8]
                  ExtOp 0x04: SELECT_T [pad?][Type:16]
                  ExtOp 0x05: TBL_GET [pad?][TableIdx:16]
                  ExtOp 0x06: TBL_SET [pad?][TableIdx:16]
                  ExtOp 0x07: REF_FUNC [pad?][FuncIdx:16]
  0xFC + ExtOp : WASMv2拡張（WASM互換: trunc_sat 2B, bulk memory/table 3〜9B）
  0xFD + ExtOp : SIMD/v128（将来、WASM互換）
  0xFE        : 予約（WASM未定義、使用不可）
  0xFF + ExtOp : embwasm独自拡張（将来）
```

---

## 7. 将来的な未知の命令（WASMv3 / ベンダー拡張）の取り込み規格

今後、WASMコア仕様に新しいOpcode が追加された場合は、トランスパイラの更新により新たな `(Prefix, ExtOp)` エントリとして定義する。

* **0xFC 系の新サブオペコード**: ExtOp に WASM の新しいサブオペコードを割り当て、ペイロードは本仕様のアライメントルールに従ってパディングを挿入する。
* **0xFD 系（SIMD）**: v128 型操作。エンコード定義は将来の仕様追加時に策定する。
* **0xFA 系**: embwasm 独自ゼロオペランド拡張。ExtOp の割り当ては embwasm 拡張仕様として別途定義する。
* **0xFF 系**: embwasm 独自拡張命令用に予約。ExtOp の割り当ては embwasm 拡張仕様として別途定義する。

**命令長**: プレフィックス命令の長さに奇偶制約はない（1B可変長設計のため）。未知の `(Prefix, ExtOp)` はインタプリタがエラーとして扱う（スキップ機能はない）。デプロイ前安全検査（Section 2.2）で全命令の妥当性を確認済みのため、実行時には未知命令に到達しない。

**値オペランドの最大ビット幅**: 各命令の値オペランドは最大 64bit（Lo:32 + Hi:32 の2回読み）まで取り得る。アライメントは4B整合（`(ip+4)&~3`）を使用する。
