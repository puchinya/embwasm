# WASMインタプリタ実装仕様書 (docs/wasm_vm_spec.md)

このドキュメントは、`embwasm`におけるWebAssembly (WASM) インタプリタ（VM）の実装方針、スタック構造、および制御フローの管理方法について記述した仕様書です。
インタプリタ本体（`src/wasm_engine.cpp`）を修正・拡張する際は、必ず本仕様に準拠してください。

## 1. 基本設計思想
`embwasm`はベアメタル環境（マイコン等のリソース制限環境）での動作を前提として設計されています。そのため、以下の制約を厳格に適用しています。
- **ヒープ不使用 (Zero dynamic memory allocation)**: `malloc` や `new` は使用せず、`WasmMemoryPool` を介した静的/スタックメモリのみで動作します。
- **再帰呼び出しの禁止 (No C++ Recursion)**: ホストのスタックオーバーフローを防ぐため、WASMの関数呼び出しをC++の再帰関数で実装してはなりません。明示的なコールスタック構造（`WasmFrame`）を用いたループ（反復アルゴリズム）で実装します。
- **関数ポインタの排除 (No Function Pointers)**: 静的解析ツールによる最悪スタック消費量の算出を容易にし、間接呼び出しによる予期せぬスタック消費を避けるため、ホストAPIの呼び出し等はIDと `switch` 文を用いた直接呼び出し（静的ディスパッチ）で行います。
- **ロード時型検査・実行時型チェック排除**: WASMバイナリのロード時（`Validate()`）に型検査を完結させ、実行ループ内の演算命令では型チェックを行いません。WebAssembly 1.0 の型システムにより、検証済みバイナリは実行時に型違反を起こさないことが静的に保証されるため、実行時の型チェックはリソースの無駄となります。これにより命令ディスパッチループを最小コストに保ちます。
- **`WasmValue` は型情報を持たない**: 演算スタック上の値を表す `WasmValue` は生ビット列（`int64_t` 等）のみを保持し、型タグフィールドを持ちません。型はバイトコード上の命令が静的に決定するため、値に型を添付する必要がなく、スタックサイズおよびコピーコストを削減できます。
- **WebAssembly 1.0でLinking以外を完全実装**: WASMインタプリタは WebAssembly 1.0 (W3C WebAssembly Core Specification Version 1.0) の全命令・全機能の完全実装を目標とします。MVP仕様に定義されたすべての数値演算・制御フロー・メモリ操作・テーブル操作・関数呼び出し命令を実装し、公式テストスイート（WebAssembly spec tests）による検証に合格することを品質基準とします。なお、極小フットプリント実現のため、一部のパラメータ上限はビルド設定（`include/wasm_config.hpp`）を介してカスタマイズ可能とします。MVP以降の拡張仕様（Bulk Memory Operations、Reference Types 等）は対象外とします。
- **入力コードはROMとして扱う**: 入力コードを書き換えてはいけない。

## 2. スタック構造とメモリレイアウト
WASMの実行状態は `WasmThreadContext` 構造体に保持されます。

### 2.1 データスタック (Evaluation Stack)
- **定義**: `WasmValue stack[kWasmStackSize]` および `stack_top`
- **用途**: 式の評価、演算、関数の引数・戻り値の受け渡し。
- **動作**: 
  - LIFO (Last-In, First-Out) 順で動作します。
  - 関数呼び出し時、呼び出し元が引数をスタックにプッシュします。
  - 被呼び出し側（関数）は、スタックトップから順に引数をポップし、ローカル変数（`locals`）に格納します。スタックの性質上、引数は後ろ（最後の引数）から順にポップされます。

### 2.2 コールスタック (Call Stack)
- **定義**: `WasmFrame call_stack[kWasmCallStackSize]` および `call_stack_top`
- **用途**: 関数呼び出しの階層（フレーム）の管理。
- **`WasmFrame` の構成**:
  - `func`: 実行対象の関数（`WasmFunction` へのポインタ）。
  - `ip`: 現在のインストラクションポインタ（バイトコード上の実行位置）。
  - `limit`: バイトコードの終端ポインタ。
  - `locals`: 引数およびローカル変数を保持する `WasmValue` の固定長配列。
  - `labels`: 制御フロー（`block`/`loop`/`if`）を管理するラベルスタック。
  - `label_stack_top`: ラベルスタックの現在の深さ。

### 2.3 ローカル変数 (Locals)
- **定義**: `WasmValue locals[kMaxLocals]` (各フレーム内に保持)
- **レイアウト**:
  - `locals[0 ... param_count-1]`: 関数の引数（パラメータ）が入ります。
  - `locals[param_count ... total_locals-1]`: 関数内で宣言されたローカル変数が入ります（初期値は0）。
- **アクセス**: `local.get`, `local.set`, `local.tee` 命令を介して、インデックスで読み書きします。

## 3. 制御フローとブロック構造の管理
WASMの `block` (0x02), `loop` (0x03), `if` (0x04) などの制御ブロックは、フレームごとのラベルスタックで管理されます。

### 3.1 ラベルスタック (Label Stack)
- **定義**: `WasmLabel labels[kMaxLabels]`
- **用途**: 分岐（`br`, `br_if`, `br_table`）時のジャンプ先およびデータスタックの巻き戻し位置の管理。
- **`WasmLabel` の構成**:
  - `pc`: 分岐時のジャンプ先IP。
    - `block`/`if` の場合: 対応する `end` の**次の**バイト。
    - `loop` の場合: ループ本体の**先頭**バイト（`loop` オペコードの次のブロックタイプ情報のさらに次のバイト）。
  - `stack_top`: ブロック進入時のデータスタックの高さ。
  - `opcode`: 進入した制御ブロックのオペコード（`0x02`, `0x03`, `0x04`）。

### 3.2 ブロック進入時の動的スキャン
`block`/`if` に進入する際、インタプリタはバイトコードを前方にスキャンして対応する `end` (0x0B) または `else` (0x05) を探索し、`pc`（ジャンプ先）を特定します。
- スキャン中、他の命令の可変長引数（LEB128等）を正しくスキップする必要があります（`DecodeVarUint32` 等のヘルパーを使用）。
- 入れ子になったブロックを考慮するため、`nest_level` をカウントして対応するペアを見つけます。

### 3.3 分岐処理 (`br`, `br_if`, `br_table`)
`br <label_idx>` 命令が実行されると、以下の手順でジャンプとクリーンアップを行います。
1. **ターゲットラベル of 特定**: ラベルスタックのトップから `label_idx` 個遡った位置にある `WasmLabel` を取得します。
2. **スタックの巻き戻し (Unwind)**:
   - 脱出するブロックが戻り値を返す場合（かつターゲットが `loop` 以外の場合）、現在のスタックトップにある結果値（`stack_[stack_top_ - 1]`）を一時退避します。
   - スタックポインタ `stack_top` をターゲットラベルの `stack_top` に巻き戻します。
   - 退避した戻り値があれば、巻き戻したスタックにプッシュし直します。
3. **IPの更新**: `ip` をターゲットラベルの `pc` に更新します。
4. **ラベルのポップ**:
   - `loop` へのジャンプ（ループ先頭への戻り）の場合: ループ自体は脱出しないため、`label_idx` 個のラベルをポップします（ループのラベルは残す）。
   - `block`/`if` からの脱出の場合: ブロック自体を抜けるため、そのラベルも含めて `label_idx + 1` 個のラベルをポップします。
5. **フレームキャッシュのリフレッシュ**: `ip` を書き換えたため、`goto frame_changed;` によりデコードループのローカルキャッシュをリフレッシュします。

## 4. 実行ループとフレーム遷移
`ExecuteInternal` は以下のような反復的ループで動作します。

```cpp
WasmResult WasmEngine::ExecuteInternal(uint32_t func_index) noexcept {
    // 最初のフレームを積む（初期化処理）
    ...
    
    while (ctx_->call_stack_top > 0) {
        WasmFrame& frame = ctx_->call_stack[ctx_->call_stack_top - 1];
        
        // ローカル変数にキャッシュして高速化
        const uint8_t* ip = frame.ip;
        const uint8_t* limit = frame.limit;
        WasmValue* locals = frame.locals;
        uint32_t total_locals = frame.total_locals;
        
        while (ip < limit) {
            uint8_t op = *ip++;
            switch (op) {
                // 各命令の処理
                ...
            }
        }
        
    frame_changed:
        // フレームの切り替え、ジャンプ、関数コール/リターンが発生した場合は、
        // 変更後のフレーム情報をローカル変数に再ロードしてループを継続する
        continue;
    }
    return WasmResult::kOk;
}
```

- **関数呼び出し (`call`, `call_indirect`)**:
  - 現在のフレームの `frame.ip` に `ip` を書き戻します。
  - 新しい `WasmFrame` を `call_stack` にプッシュし、引数をデータスタックからローカル変数にコピーします。
  - `goto frame_changed;` でキャッシュを更新し、新しい関数の実行を開始します。
- **関数終了 (`return` または関数の `end`)**:
  - `call_stack_top` をデクリメントします。
  - 最上位フレームが終了した場合は `WasmResult::kOk` を返します。
  - 呼び出し元に戻る場合は `goto frame_changed;` で呼び出し元の状態を再ロードします。

## 5. 事前検査 (Pre-Validation)

`Load()` は `ParseSections()` 完了後、Start 関数の実行前に `Validate()` を呼び出す。
実行時エラーをロード時に早期検出することで、リソース制約の厳しい環境における安全性を高める。

### 5.1 検査フロー

```
Load()
 └─ ParseSections()   // セクションを解析し関数・シグネチャ等を確定
 └─ Validate()        // 事前検査（以下を順に実施）
     ├─ [型シグネチャ]
     │   ├─ 全シグネチャの戻り値数が 1 以下であること（WebAssembly 1.0 制約）
     │   └─ 全関数の type_index が signature_count_ 未満であること
     ├─ [メモリセクション]
     │   └─ initial ≤ maximum（maximum が指定されている場合）かつ initial ≤ 65536
     ├─ [グローバルセクション]
     │   └─ 各グローバルの初期化式が定数式（i32.const / i64.const / f32.const /
     │       f64.const / global.get のみ許可）であること
     │       ※ global.get はインポートされたグローバルのみ参照可
     ├─ [エクスポートセクション]
     │   └─ エクスポートされた関数・メモリ・グローバル・テーブルの各インデックスが
     │       それぞれの有効範囲内であること
     ├─ [データセクション]
     │   ├─ 各データセグメントのメモリインデックスが 0 であること（メモリは最大1つ）
     │   └─ オフセット初期化式が定数式であること
     ├─ [エレメントセクション]
     │   ├─ 各エレメントセグメントのテーブルインデックスが 0 であること（テーブルは最大1つ）
     │   ├─ オフセット初期化式が定数式であること
     │   └─ 各関数インデックスが function_count_ 未満であること
     ├─ [スタート関数]
     │   ├─ スタート関数インデックスが function_count_ 未満であること
     │   └─ 対応するシグネチャが引数・戻り値ともになし（[] → []）であること
     └─ 各内部関数について ValidateFunctionBody()
         ├─ バイトコードを線形スキャン:
         │   ├─ ラベルネスト深度の追跡 → 最大値を max_label_depth に記録
         │   ├─ データスタック深度のシミュレーション → 最大値を max_stack_depth に記録
         │   ├─ local.get/set/tee のインデックスが total_locals 未満
         │   ├─ global.get/set のインデックスが global_count_ 未満
         │   ├─ call/call_indirect の関数・型インデックスが有効範囲内
         │   ├─ br/br_if のラベルインデックスが現在のネスト深度未満であること
         │   ├─ br_table の全ターゲットインデックスが現在のネスト深度未満であること
         │   ├─ メモリ命令（load/store/memory.size/memory.grow）使用時に
         │   │   メモリセクションが存在すること（memory_count_ > 0）
         │   ├─ call_indirect 使用時にテーブルセクションが存在すること
         │   ├─ メモリ命令のアラインメントが log2(アクセスバイト数) 以下であること
         │   │   例: i32.load (4バイト) → align ≤ 2
         │   └─ else を持たない if ブロックのブロック型が void であること
         └─ max_label_depth ≤ kMaxLabels かつ max_stack_depth ≤ kWasmStackSize
 └─ Start 関数の実行
```

### 5.2 スタック深度シミュレーション

バイトコードを線形スキャンしながら `stack_depth` を整数カウンタで管理する。
制御フロー分岐（`br`, `return`）後の到達不能コードについては保守的にカウンタをそのまま維持する（過大評価になるが安全側に倒れる）。

ブロック構造 (`block`/`loop`/`if`) の進入・退出では、ラベルスタック（`ValLabel` 配列）を用いて
進入時のスタック深度と期待される結果数を記録し、`end` / `else` で正確に復元する。

### 5.3 算出値の保存先

`ValidateFunctionBody()` が算出した値は `WasmFunction::InternalFunc` のメンバーに格納する:

| フィールド | 意味 |
|---|---|
| `local_count` | 関数内で宣言されたローカル変数数 (引数を除く) |
| `max_label_depth` | ラベルスタックの最大深度（関数ブロック = 深度 1 を含む） |
| `max_stack_depth` | データスタックの最大深度（バイトコード線形スキャンで得た上界） |

### 5.4 失敗コード

検査に失敗した場合 `WasmResult::kErrorValidationFailed` を返す。
`Load()` はこれを呼び出し元に伝播させ、以降の Start 関数実行は行わない。

---

## 6. マルチスレッドと Yield (協調的マルチタスク)
マルチスレッドが有効（`EMBWASM_ENABLE_MULTITHREADING`）な場合、インタプリタはスレッドの一時中断（Yield）をサポートします。
- ホストAPI（スレッド同期、スリープ等）が `WasmResult::kYield` を返すと、インタプリタは現在のフレーム状態（`ip`等）を保存した上で、`ExecuteInternal` から一旦リターンします。
- スケジューラ（`WasmScheduler`）がスレッドを切り替えて実行し、再度対象スレッドの実行順が回ってきたときに、再び `ExecuteInternal` が呼ばれて中断した `ip` から実行を再開します。
