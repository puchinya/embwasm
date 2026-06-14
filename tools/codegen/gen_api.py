#!/usr/bin/env python3
"""gen_api.py - WASM Host API スタティック C++ コード生成スクリプト

WIT (WebAssembly Interface Type) ファイルを読み込み、ホスト関数のルックアップテーブルと
ディスパッチャを自動生成します。

WIT ファイルのメタデータタグ:
    /// @cpp-func: <C++関数名>    # インポート関数に対応する C++ 関数のフルパス
    /// @cpp-header: <ヘッダー名> # インクルードするヘッダーファイル
    /// @wit-import: <WITファイル> # 別の WIT ファイルをインポート (相対パス)
"""

import os
import sys
import re


# ---------------------------------------------------------------------------
# WIT パーサー (簡易版)
# ---------------------------------------------------------------------------

def _parse_wit(wit_path):
    """WIT ファイルをパースして (imports, headers, apis_flat) を返す。
    WIT にはメタデータとして C++ 関数名を埋め込む形式を想定。
    例:
      /// @cpp-func: embwasm::Print
      import print: func(val: i32);
    """
    apis_flat = []
    headers = []
    
    # 簡単なパース処理
    current_module = "env" # WITのデフォルトモジュール名としてenvを仮定 (後で調整可能)
    module_init = None
    module_deinit = None
    
    with open(wit_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # メタ情報の抽出 (ファイル全体から)
    # @cpp-header の抽出
    header_matches = re.findall(r"^///\s*@cpp-header:\s*[\"']?([\w./-]+)[\"']?", content, re.MULTILINE)
    for h in header_matches:
        if h not in headers:
            headers.append(h)

    # @wit-import の抽出
    wit_imports = re.findall(r"^///\s*@wit-import:\s*[\"']?([\w./-]+\.wit)[\"']?", content, re.MULTILINE)

    lines = content.splitlines()
    cpp_func = None
    for line in lines:
        line = line.strip()
        
        # Doc comment からメタ情報を抽出 (行単位)
        if line.startswith("///"):
            match = re.search(r"@cpp-func:\s*([\w:]+)", line)
            if match:
                cpp_func = match.group(1)
            # @cpp-module をパース
            match_mod = re.search(r"@cpp-module:\s*([\w:-]+)", line)
            if match_mod:
                current_module = match_mod.group(1).replace("-", "_")
            # @cpp-init と @cpp-deinit の抽出
            match_init = re.search(r"@cpp-init:\s*([\w::]+)", line)
            if match_init:
                module_init = match_init.group(1)
            match_deinit = re.search(r"@cpp-deinit:\s*([\w::]+)", line)
            if match_deinit:
                module_deinit = match_deinit.group(1)
            continue
            
        # import 行を抽出
        if line.startswith("import "):
            # 例: import print: func(val: i32);
            # フィールド名を取得
            parts = line.split(":")
            field_name_raw = parts[0].replace("import ", "").strip()
                
            # kebab-case を snake_case に変換 (WASMの慣習)
            field_name = field_name_raw.replace("-", "_")
                
            # シグネチャ情報の簡易抽出 (C宣言生成用)
            # import print-char: func(character: i32);
            #                    ^--- ここから
            sig_part = line.split(":", 1)[1].strip() if ":" in line else ""
                
            if cpp_func:
                apis_flat.append({
                    'module': current_module,
                    'field': field_name,
                    'function': cpp_func,
                    'wit_field': field_name_raw,
                    'wit_sig': sig_part
                })
                cpp_func = None
            else:
                # C++関数名が指定されていない場合は、警告を出すかデフォルト推論
                print(f"Warning: No @cpp-func specified for import '{field_name}' in {wit_path}", file=sys.stderr)
        
        # world 名をモジュール名として使う検討もできるが、
        # 現状の embwasm は import_module を重視するため、
        # メタデータで指定できるようにするのが良さそう。
        if line.startswith("world "):
            # @cpp-module: env のような指定があれば上書き
            pass

    for api in apis_flat:
        if api['module'] == current_module:
            api['init'] = module_init
            api['deinit'] = module_deinit

    return wit_imports, headers, apis_flat


# ---------------------------------------------------------------------------
# YAML パーサ
# ---------------------------------------------------------------------------

def _parse_yaml_with_pyyaml(config_path):
    """PyYAML を使って 1 ファイルをパースし (imports, headers, apis_flat) を返す。
    apis_flat は {'module': str, 'field': str, 'function': str} のリスト。
    """
    import yaml
    with open(config_path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)
    if data is None:
        data = {}

    imports = data.get('imports', []) or []
    headers = data.get('headers', []) or []
    apis_flat = []

    modules = data.get('modules') or {}
    for module_name, module_body in modules.items():
        if not module_body:
            continue
        module_init = module_body.get('init')
        module_deinit = module_body.get('deinit')
        for entry in (module_body.get('apis') or []):
            apis_flat.append({
                'module': str(module_name),
                'field': entry.get('field', ''),
                'function': entry.get('function', ''),
                'init': module_init,
                'deinit': module_deinit,
            })

    return imports, headers, apis_flat


def _parse_yaml_fallback(config_path):
    """PyYAML が無い場合の簡易フォールバックパーサー。

    対応する構造:
        imports:
          - path

        headers:
          - header.h

        modules:
          module_name:
            apis:
              - field: xxx
                function: yyy
    """
    imports = []
    headers = []
    apis_flat = []

    # 状態機械
    # section: 'imports' | 'headers' | 'modules' | None
    # module_section: 'apis' | None (modules 内のサブセクション)
    section = None
    current_module = None
    module_section = None
    module_init = None
    module_deinit = None
    current_api = {}

    def _flush_api():
        nonlocal current_api
        if current_api and current_module:
            apis_flat.append({
                'module': current_module,
                'field': current_api.get('field', ''),
                'function': current_api.get('function', ''),
                'init': module_init,
                'deinit': module_deinit,
            })
            current_api = {}

    with open(config_path, 'r', encoding='utf-8') as f:
        for raw_line in f:
            line = raw_line.rstrip('\n')
            stripped = line.strip()

            # 空行・コメント行はスキップ
            if not stripped or stripped.startswith('#'):
                continue

            # インデントレベルを取得（スペース数）
            indent = len(line) - len(line.lstrip(' '))

            # --- トップレベルセクション検知（indent == 0）---
            if indent == 0:
                _flush_api()
                current_module = None
                module_section = None

                if stripped.startswith('imports:'):
                    section = 'imports'
                    continue
                elif stripped.startswith('headers:'):
                    section = 'headers'
                    continue
                elif stripped.startswith('modules:'):
                    section = 'modules'
                    continue
                else:
                    section = None
                    continue

            # --- imports セクション ---
            if section == 'imports':
                if stripped.startswith('-'):
                    val = stripped.lstrip('-').strip().strip("'").strip('"')
                    imports.append(val)
                continue

            # --- headers セクション ---
            if section == 'headers':
                if stripped.startswith('-'):
                    val = stripped.lstrip('-').strip().strip("'").strip('"')
                    headers.append(val)
                continue

            # --- modules セクション ---
            if section == 'modules':
                # indent==2: モジュール名行 (例: "  env:")
                if indent == 2 and not stripped.startswith('-'):
                    _flush_api()
                    module_section = None
                    module_init = None
                    module_deinit = None
                    if ':' in stripped:
                        current_module = stripped.split(':')[0].strip()
                    continue

                # indent==4: モジュール内サブセクション (例: "    apis:")
                if indent == 4 and not stripped.startswith('-'):
                    _flush_api()
                    if stripped.startswith('apis:'):
                        module_section = 'apis'
                    elif stripped.startswith('init:'):
                        module_init = stripped.split(':', 1)[1].strip().strip("'").strip('"')
                    elif stripped.startswith('deinit:'):
                        module_deinit = stripped.split(':', 1)[1].strip().strip("'").strip('"')
                    else:
                        module_section = None
                    continue

                # indent==6: apis リストエントリ開始 (例: "      - field: xxx")
                if indent == 6 and module_section == 'apis':
                    if stripped.startswith('-'):
                        _flush_api()
                        # 同行にキー:値があるかもしれない
                        rest = stripped.lstrip('-').strip()
                        if ':' in rest:
                            key, val = rest.split(':', 1)
                            current_api[key.strip()] = val.strip().strip("'").strip('"')
                    elif ':' in stripped:
                        key, val = stripped.split(':', 1)
                        current_api[key.strip()] = val.strip().strip("'").strip('"')
                    continue

                # indent==8: apis エントリの追加フィールド (例: "        function: yyy")
                if indent == 8 and module_section == 'apis':
                    if ':' in stripped:
                        key, val = stripped.split(':', 1)
                        current_api[key.strip()] = val.strip().strip("'").strip('"')
                    continue

    _flush_api()
    return imports, headers, apis_flat


def _parse_one_file(config_path, use_pyyaml):
    """1 つの YAML または WIT ファイルをパースして (imports, headers, apis_flat) を返す。"""
    if config_path.endswith('.wit'):
        return _parse_wit(config_path)
    if use_pyyaml:
        return _parse_yaml_with_pyyaml(config_path)
    else:
        return _parse_yaml_fallback(config_path)


# ---------------------------------------------------------------------------
# マルチファイル読み込み
# ---------------------------------------------------------------------------

def load_all_configs(entry_path):
    """エントリポイント YAML から imports を辿り、全ファイルの設定をマージして返す。

    反復処理（スタック）で実装。循環インポートは visited セットで防止。

    Returns:
        (headers, apis): 重複排除済みのリスト
    """
    try:
        import yaml  # noqa: F401
        use_pyyaml = True
    except ImportError:
        use_pyyaml = False

    merged_headers = []
    seen_headers = set()
    merged_apis = []
    seen_api_keys = set()  # (module, field)
    seen_modules = {}  # module -> file_path

    entry_abs = os.path.abspath(entry_path)
    stack = [entry_abs]
    visited = set()

    while stack:
        current_path = stack.pop()

        if current_path in visited:
            print(f"Warning: Circular import detected, skipping '{current_path}'",
                  file=sys.stderr)
            continue
        visited.add(current_path)

        if not os.path.exists(current_path):
            print(f"Error: Configuration file '{current_path}' not found.",
                  file=sys.stderr)
            sys.exit(1)

        current_dir = os.path.dirname(current_path)
        file_imports, file_headers, file_apis = _parse_one_file(current_path, use_pyyaml)

        # モジュールの定義重複チェック
        file_modules = set(api.get('module', '') for api in file_apis)
        for m in file_modules:
            if m in seen_modules and seen_modules[m] != current_path:
                print(f"Error: Duplicate module definition '{m}' found in '{current_path}' (already defined in '{seen_modules[m]}').", file=sys.stderr)
                sys.exit(1)
            seen_modules[m] = current_path

        for h in file_headers:
            if h not in seen_headers:
                seen_headers.add(h)
                merged_headers.append(h)

        for api in file_apis:
            key = (api.get('module', ''), api.get('field', ''))
            if key not in seen_api_keys:
                seen_api_keys.add(key)
                merged_apis.append(api)

        # imports をスタックに積む（記述順で処理するため逆順）
        for import_rel in reversed(file_imports):
            import_abs = os.path.normpath(os.path.join(current_dir, import_rel))
            if import_abs not in visited:
                stack.append(import_abs)

    return merged_headers, merged_apis


# ---------------------------------------------------------------------------
# コード生成
# ---------------------------------------------------------------------------

def main():
    config_path = sys.argv[1] if len(sys.argv) > 1 else 'hostapi.wit'
    out_cpp_path = sys.argv[2] if len(sys.argv) > 2 else 'src/wasm_api_static.cpp'
    out_h_path = sys.argv[3] if len(sys.argv) > 3 else 'include/wasm_api_static.hpp'
    out_wasm_wit_path = sys.argv[4] if len(sys.argv) > 4 else None

    if not os.path.exists(config_path):
        print(f"Error: Configuration file '{config_path}' not found.", file=sys.stderr)
        sys.exit(1)

    headers, apis = load_all_configs(config_path)

    # 二分探索のため (module, field) の辞書順でソート
    apis.sort(key=lambda x: (x['module'], x['field']))

    # モジュールのリストを重複排除してソート
    modules = sorted(list(set(api['module'] for api in apis)))

    # 連結された WIT ファイルの生成
    if out_wasm_wit_path:
        wit_lines = [
            "// =============================================================================",
            "// Copyright (c) 2026 embwasm Project. All rights reserved.",
            "// [Auto-generated by gen_api.py] - DO NOT EDIT DIRECTLY",
            "// =============================================================================",
            "",
            "package embwasm:hostapi;",
            "",
            "world wasm-host-api {",
        ]

        for api in apis:
            field = api['field']
            sig = api.get('wit_sig', 'func()').rstrip(';')
            wit_field = api.get('wit_field', field)
            wit_lines.append(f"    import {wit_field}: {sig};")

        wit_lines.extend([
            "}",
            ""
        ])
        
        os.makedirs(os.path.dirname(os.path.abspath(out_wasm_wit_path)), exist_ok=True)
        with open(out_wasm_wit_path, 'w', encoding='utf-8') as f:
            f.write("\n".join(wit_lines) + "\n")
        print(f"Generated merged WIT file: {out_wasm_wit_path}")

    # Hostモジュールの初期化/終了関数の収集
    module_inits = {}
    module_deinits = {}
    for api in apis:
        m = api['module']
        if api.get('init'):
            module_inits[m] = api['init']
        if api.get('deinit'):
            module_deinits[m] = api['deinit']

    # 呼び出し部分の構築
    init_calls = []
    for mod in modules:
        if mod in module_inits:
            init_calls.append(f"    {module_inits[mod]}(engine);")
    init_calls_str = "\n".join(init_calls)

    deinit_calls = []
    for mod in modules:
        if mod in module_deinits:
            deinit_calls.append(f"    {module_deinits[mod]}(engine);")
    deinit_calls_str = "\n".join(deinit_calls)

    # HostModuleId 定数の定義を生成
    module_enum_members = []
    for idx, mod in enumerate(modules):
        mod_clean = re.sub(r'[^a-zA-Z0-9_]', '_', mod)
        mod_part = "".join(word.capitalize() for word in mod_clean.split("_") if word)
        module_enum_members.append(f"    k{mod_part} = {idx},")
    module_enum_members_str = "\n".join(module_enum_members)

    # HostFunctionId 定数の定義を生成
    enum_members = []
    for idx, api in enumerate(apis):
        mod_part = "".join(word.capitalize() for word in api["module"].split("_"))
        field_part = "".join(word.capitalize() for word in api["field"].split("_"))
        enum_members.append(
            f"constexpr HostFunctionId k{mod_part}{field_part} = "
            f"static_cast<HostFunctionId>({idx});"
        )
    enum_members_str = "\n".join(enum_members)

    # ヘッダーファイルの内容生成
    h_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by gen_api.py] - DO NOT EDIT DIRECTLY
// =============================================================================

#ifndef EMBWASM_WASM_API_STATIC_HPP_
#define EMBWASM_WASM_API_STATIC_HPP_

#include "wasm_api.hpp"

namespace embwasm {{

// ホストモジュールのID定義
enum class HostModuleId : uint32_t {{
{module_enum_members_str}
}};

// 各ホスト関数のID定義
{enum_members_str}

HostModuleId LookupStaticHostModuleId(const char* module_name) noexcept;

HostFunctionId LookupStaticHostFunctionId(const char* module_name, const char* field_name) noexcept;

class WasmEngine;

void InitializeAllHostModules(WasmEngine& engine) noexcept;
void DeinitializeAllHostModules(WasmEngine& engine) noexcept;

WasmResult DispatchHostFunction(
    WasmEngine& engine,
    HostFunctionId id,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept;

}} // namespace embwasm

#endif // EMBWASM_WASM_API_STATIC_HPP_
"""

    # インクルード指示子の生成
    include_directives = [f'#include "{h}"' for h in headers]
    include_directives_str = "\n".join(include_directives)

    # 静的テーブルのエントリ生成
    cpp_entries = []
    for api, enum_member in zip(apis, enum_members):
        const_name = enum_member.split("=")[0].replace("constexpr HostFunctionId", "").strip()
        field_string = api.get('wit_field') or api['field']
        cpp_entries.append(f'    {{ "{api["module"]}", "{field_string}", {const_name} }}')
    cpp_entries_str = ",\n".join(cpp_entries)

    # switch 文のディスパッチケース生成
    dispatch_cases = []
    for api, enum_member in zip(apis, enum_members):
        const_name = enum_member.split("=")[0].replace("constexpr HostFunctionId", "").strip()
        dispatch_cases.append(
            f"        case {const_name}:\n"
            f"            return {api['function']}(engine, args, arg_count, results, result_count);"
        )
    dispatch_cases_str = "\n".join(dispatch_cases)

    # 探索ロジックの生成（二分探索＋ハイフン/アンダースコア同一視フォールバック）
    lookup_logic = """\
    // 1. 完全一致で探索を試みる（高速パス）
    if (std::strcmp(module_name, "$root") == 0) {
        for (std::size_t i = 0; i < kStaticApiTableSize; ++i) {
            if (std::strcmp(field_name, kStaticApiTable[i].field_name) == 0) {
                return kStaticApiTable[i].id;
            }
        }
    } else {
        int low = 0;
        int high = static_cast<int>(kStaticApiTableSize) - 1;

        while (low <= high) {
            int mid = low + (high - low) / 2;
            const auto& entry = kStaticApiTable[mid];

            int cmp = std::strcmp(module_name, entry.module_name);
            if (cmp == 0) {
                cmp = std::strcmp(field_name, entry.field_name);
            }

            if (cmp == 0) {
                return entry.id;
            } else if (cmp < 0) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
    }

    // 2. フォールバック：ハイフンとアンダースコアを同一視した線形探索（互換性用）
    bool is_root = (std::strcmp(module_name, "$root") == 0);
    for (std::size_t i = 0; i < kStaticApiTableSize; ++i) {
        if (is_root || std::strcmp(module_name, kStaticApiTable[i].module_name) == 0) {
            const char* s1 = field_name;
            const char* s2 = kStaticApiTable[i].field_name;
            bool match = true;
            while (*s1 && *s2) {
                char c1 = *s1;
                char c2 = *s2;
                if (c1 == '-') c1 = '_';
                if (c2 == '-') c2 = '_';
                if (c1 != c2) {
                    match = false;
                    break;
                }
                s1++;
                s2++;
            }
            if (match && !*s1 && !*s2) {
                return kStaticApiTable[i].id;
            }
        }
    }

    return HostFunctionId::kInvalid;"""

    # モジュールID探索ロジックの生成
    module_lookup_cases = []
    for idx, mod in enumerate(modules):
        mod_clean = re.sub(r'[^a-zA-Z0-9_]', '_', mod)
        mod_part = "".join(word.capitalize() for word in mod_clean.split("_") if word)
        module_lookup_cases.append(
            f'    if (std::strcmp(module_name, "{mod}") == 0) return HostModuleId::k{mod_part};'
        )
    module_lookup_cases_str = "\n".join(module_lookup_cases)

    # ソースファイルの内容生成
    cpp_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by gen_api.py] - DO NOT EDIT DIRECTLY
// =============================================================================

#include "wasm_api_static.hpp"
{include_directives_str}
#include <cstring>

namespace embwasm {{

extern const std::size_t kHostModuleCount = {len(modules)};

void InitializeAllHostModules(WasmEngine& engine) noexcept {{
    (void)engine;
{init_calls_str}
}}

void DeinitializeAllHostModules(WasmEngine& engine) noexcept {{
    (void)engine;
{deinit_calls_str}
}}

HostModuleId LookupStaticHostModuleId(const char* module_name) noexcept {{
{module_lookup_cases_str}
    return static_cast<HostModuleId>(0xFFFFFFFF);
}}

struct StaticApiEntry {{
    const char* module_name;
    const char* field_name;
    HostFunctionId id;
}};

// アルファベット順にソートされた静的テーブル
static const StaticApiEntry kStaticApiTable[] = {{
{cpp_entries_str}
}};
static constexpr std::size_t kStaticApiTableSize = sizeof(kStaticApiTable) / sizeof(kStaticApiTable[0]);

HostFunctionId LookupStaticHostFunctionId(const char* module_name, const char* field_name) noexcept {{
{lookup_logic}
}}

WasmResult DispatchHostFunction(
    WasmEngine& engine,
    HostFunctionId id,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept
{{
    switch (id) {{
{dispatch_cases_str}
        default:
            return WasmResult::kErrorRuntimeError;
    }}
}}

}} // namespace embwasm
"""

    os.makedirs(os.path.dirname(os.path.abspath(out_cpp_path)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(out_h_path)), exist_ok=True)

    with open(out_h_path, 'w', encoding='utf-8') as f:
        f.write(h_content)
    with open(out_cpp_path, 'w', encoding='utf-8') as f:
        f.write(cpp_content)

    print(f"Generated {out_h_path} and {out_cpp_path} from {config_path}")


if __name__ == '__main__':
    main()
