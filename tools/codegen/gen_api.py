#!/usr/bin/env python3
"""gen_api.py - WASM Host API スタティック C++ コード生成スクリプト

設定ファイル (module_config.yaml) を読み込み、ホスト関数のルックアップテーブルと
ディスパッチャを自動生成します。

設定ファイルのフォーマット:
    imports:                          # 他の module_config.yaml をインポート（相対パス）
      - "common/module_config.yaml"

    headers:                          # 生成 .cpp にインクルードするヘッダー
      - "host_apis.h"

    modules:                          # モジュール名をキーにした API 定義
      env:
        apis:
          - field: print_val
            function: embwasm::PrintVal
      wasi:
        apis:
          - field: proc_exit
            function: embwasm::ProcExit
"""

import os
import sys


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
        for entry in (module_body.get('apis') or []):
            apis_flat.append({
                'module': str(module_name),
                'field': entry.get('field', ''),
                'function': entry.get('function', ''),
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
    current_api = {}

    def _flush_api():
        nonlocal current_api
        if current_api and current_module:
            apis_flat.append({
                'module': current_module,
                'field': current_api.get('field', ''),
                'function': current_api.get('function', ''),
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
                    if ':' in stripped:
                        current_module = stripped.split(':')[0].strip()
                    continue

                # indent==4: モジュール内サブセクション (例: "    apis:")
                if indent == 4 and not stripped.startswith('-'):
                    _flush_api()
                    if stripped.startswith('apis:'):
                        module_section = 'apis'
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
    """1 つの YAML ファイルをパースして (imports, headers, apis_flat) を返す。"""
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
    config_path = sys.argv[1] if len(sys.argv) > 1 else 'module_config.yaml'
    out_cpp_path = sys.argv[2] if len(sys.argv) > 2 else 'src/wasm_api_static.cpp'
    out_h_path = sys.argv[3] if len(sys.argv) > 3 else 'include/wasm_api_static.hpp'

    if not os.path.exists(config_path):
        print(f"Error: Configuration file '{config_path}' not found.", file=sys.stderr)
        sys.exit(1)

    headers, apis = load_all_configs(config_path)

    # 二分探索のため (module, field) の辞書順でソート
    apis.sort(key=lambda x: (x['module'], x['field']))

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

// 各ホスト関数のID定義
{enum_members_str}

HostFunctionId LookupStaticHostFunctionId(const char* module_name, const char* field_name) noexcept;

WasmResult DispatchHostFunction(
    HostFunctionId id,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count,
    void* user_data) noexcept;

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
        cpp_entries.append(f'    {{ "{api["module"]}", "{api["field"]}", {const_name} }}')
    cpp_entries_str = ",\n".join(cpp_entries)

    # switch 文のディスパッチケース生成
    dispatch_cases = []
    for api, enum_member in zip(apis, enum_members):
        const_name = enum_member.split("=")[0].replace("constexpr HostFunctionId", "").strip()
        dispatch_cases.append(
            f"        case {const_name}:\n"
            f"            return {api['function']}(args, arg_count, results, result_count, user_data);"
        )
    dispatch_cases_str = "\n".join(dispatch_cases)

    # 探索ロジックの生成（8 件以下は線形探索、それ以上は二分探索）
    if len(apis) <= 8:
        lookup_logic = """\
    for (std::size_t i = 0; i < kStaticApiTableSize; ++i) {
        if (std::strcmp(module_name, kStaticApiTable[i].module_name) == 0 &&
            std::strcmp(field_name, kStaticApiTable[i].field_name) == 0) {
            return kStaticApiTable[i].id;
        }
    }
    return HostFunctionId::kInvalid;"""
    else:
        lookup_logic = """\
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
    return HostFunctionId::kInvalid;"""

    # ソースファイルの内容生成
    cpp_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by gen_api.py] - DO NOT EDIT DIRECTLY
// =============================================================================

#include "wasm_api_static.hpp"
{include_directives_str}
#include <cstring>

namespace embwasm {{

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
    HostFunctionId id,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count,
    void* user_data) noexcept
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
