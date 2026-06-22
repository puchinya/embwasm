#!/usr/bin/env python3
"""gen_api.py - WASM Host API スタティック C++ コード生成スクリプト

WIT (WebAssembly Interface Type) ファイルを読み込み、ホスト関数のルックアップテーブル、
型付きプロトタイプ宣言、スタックベースのディスパッチャを自動生成します。

WIT ファイルのメタデータタグ:
    /// @cpp-func: <C++関数名>    # インポート関数に対応する C++ 関数のフルパス
    /// @cpp-header: <ヘッダー名> # インクルードするヘッダーファイル
    /// @wit-import: <WITファイル> # 別の WIT ファイルをインポート (相対パス)
"""

import os
import sys
import re


# ---------------------------------------------------------------------------
# WIT 型変換ヘルパー
# ---------------------------------------------------------------------------

_PRIMITIVES_I32 = {'s32', 'u32', 's8', 'u8', 's16', 'u16', 'bool', 'char'}
_PRIMITIVES_I64 = {'s64', 'u64'}

_PRIMITIVE_CPP = {
    's32': 'int32_t', 'u32': 'uint32_t',
    's8': 'int8_t',  'u8': 'uint8_t',
    's16': 'int16_t', 'u16': 'uint16_t',
    'bool': 'bool', 'char': 'char32_t',
    's64': 'int64_t', 'u64': 'uint64_t',
    'f32': 'float', 'f64': 'double',
}

_LIST_ELEM_CPP = {
    's32': 'int32_t', 'u32': 'uint32_t',
    's8': 'int8_t',   'u8': 'uint8_t',
    's16': 'int16_t', 'u16': 'uint16_t',
    's64': 'int64_t', 'u64': 'uint64_t',
    'f32': 'float', 'f64': 'double',
}

_WASM_TYPE_CPP = {
    'i32': 'WasmType::kI32',
    'i64': 'WasmType::kI64',
    'f32': 'WasmType::kF32',
    'f64': 'WasmType::kF64',
}

_WASM_FROM_FUNC = {
    'i32': 'WasmValue::FromI32',
    'i64': 'WasmValue::FromI64',
    'f32': 'WasmValue::FromF32',
    'f64': 'WasmValue::FromF64',
}

_WASM_CAST_CPP = {
    'i32': 'int32_t', 'i64': 'int64_t', 'f32': 'float', 'f64': 'double',
}

_WASM_VALUE_FIELD = {
    'i32': 'i32', 'i64': 'i64', 'f32': 'f32', 'f64': 'f64',
}


def _wit_to_wasm_types(wit_type):
    """WIT 型 → WASM スタック型リスト (例: 'string' → ['i32','i32'])"""
    t = wit_type.strip()
    if t in _PRIMITIVES_I32: return ['i32']
    if t in _PRIMITIVES_I64: return ['i64']
    if t == 'f32': return ['f32']
    if t == 'f64': return ['f64']
    if t == 'string': return ['i32', 'i32']
    if t.startswith('list<'): return ['i32', 'i32']
    return ['i32']


def _is_pointer_type(wit_type):
    t = wit_type.strip()
    return t == 'string' or t.startswith('list<')


def _list_elem_cpp(inner):
    return _LIST_ELEM_CPP.get(inner.strip(), 'uint8_t')


def _wit_to_cpp_proto_params(name, wit_type):
    """WIT パラメータ → C++ プロトタイプパラメータリスト (型付き・線形メモリ変換後)"""
    t = wit_type.strip()
    if t in _PRIMITIVE_CPP:
        return [(_PRIMITIVE_CPP[t], name)]
    if t == 'string':
        return [('const char*', name), ('uint32_t', f'{name}_len')]
    if t.startswith('list<') and t.endswith('>'):
        elem = _list_elem_cpp(t[5:-1])
        return [(f'const {elem}*', name), ('uint32_t', f'{name}_len')]
    return [('uint32_t', name)]


def _wit_to_cpp_result_param(wit_type, idx):
    """WIT 結果型 → C++ out パラメータ (参照型)"""
    t = wit_type.strip()
    cpp = _PRIMITIVE_CPP.get(t, 'uint32_t')
    name = 'out_result' if idx == 0 else f'out_result{idx}'
    return (f'{cpp}&', name)


def _wit_to_out_var_type(wit_type):
    """WIT 結果型 → ローカル変数の C++ 型"""
    t = wit_type.strip()
    return _PRIMITIVE_CPP.get(t, 'uint32_t')


# ---------------------------------------------------------------------------
# WIT シグネチャパーサー
# ---------------------------------------------------------------------------

def _split_params(s):
    """コンマ区切りのパラメータ列を分割する。山括弧ネストを考慮。"""
    result, depth, cur = [], 0, ''
    for c in s:
        if c == '<': depth += 1; cur += c
        elif c == '>': depth -= 1; cur += c
        elif c == ',' and depth == 0:
            if cur.strip(): result.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip():
        result.append(cur.strip())
    return result


def _parse_wit_sig(sig_str):
    """'func(a: s32, b: string) -> s32' → (param_names, param_wit_types, result_wit_types)"""
    sig_str = sig_str.strip().rstrip(';').strip()
    m = re.match(r'func\s*\(([^)]*)\)\s*(?:->\s*(.+))?', sig_str)
    if not m:
        return [], [], []

    params_str = m.group(1).strip()
    results_str = (m.group(2) or '').strip().rstrip(';').strip()

    param_names, param_wit_types = [], []
    if params_str:
        for p in _split_params(params_str):
            if ':' in p:
                colon = p.index(':')
                raw_name = p[:colon].strip().lstrip('%').replace('-', '_')
                param_names.append(raw_name)
                param_wit_types.append(p[colon + 1:].strip())

    result_wit_types = []
    if results_str:
        if results_str.startswith('('):
            inner = results_str.strip('()')
            result_wit_types = [t.strip() for t in _split_params(inner) if t.strip()]
        else:
            result_wit_types = [results_str]

    return param_names, param_wit_types, result_wit_types


# ---------------------------------------------------------------------------
# コード生成ヘルパー
# ---------------------------------------------------------------------------

def _gen_dispatch_case(api, const_name):
    """DispatchHostFunction の各 case を生成する。"""
    i = '            '  # 12 spaces
    param_names = api.get('param_names', [])
    param_wit_types = api.get('param_wit_types', [])
    result_wit_types = api.get('result_wit_types', [])
    cpp_func = api['function']

    needs_mem = any(_is_pointer_type(t) for t in param_wit_types)

    lines = []

    # WIT パラメータを逆順に処理（LIFO: 最後のパラメータがスタックのトップ）
    for j in range(len(param_wit_types) - 1, -1, -1):
        name = param_names[j] if j < len(param_names) else f'arg{j}'
        t = param_wit_types[j].strip()
        wasm_slots = _wit_to_wasm_types(t)

        if len(wasm_slots) == 1:
            # プリミティブ型
            cpp_t = _PRIMITIVE_CPP.get(t, 'uint32_t')
            vf = _WASM_VALUE_FIELD[wasm_slots[0]]
            lines.append(f'{i}{cpp_t} _{name} = static_cast<{cpp_t}>(ctx->stack[--ctx->stack_top].value.{vf});')
        else:
            # string / list<T>: len が上、ptr が下（WASM の push 順 ptr→len に対し pop では len→ptr）
            lines.append(f'{i}uint32_t _{name}_len = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);')
            lines.append(f'{i}uint32_t _{name}_ptr = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);')

    if needs_mem:
        lines.append(f'{i}uint8_t* _mem = engine.GetLinearMemory();')

    # 結果用ローカル変数の宣言
    for idx, rt in enumerate(result_wit_types):
        out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
        lines.append(f'{i}{_wit_to_out_var_type(rt)} {out_name} = {{}};')

    # 関数呼び出し引数の構築
    call_args = ['engine']
    for name, t in zip(param_names, param_wit_types):
        t = t.strip()
        if t == 'string':
            call_args.append(f'reinterpret_cast<const char*>(_mem + _{name}_ptr), _{name}_len')
        elif t.startswith('list<') and t.endswith('>'):
            elem = _list_elem_cpp(t[5:-1])
            call_args.append(f'reinterpret_cast<const {elem}*>(_mem + _{name}_ptr), _{name}_len')
        else:
            call_args.append(f'_{name}')
    for idx in range(len(result_wit_types)):
        call_args.append('_out_result' if idx == 0 else f'_out_result{idx}')

    lines.append(f'{i}WasmResult res = {cpp_func}({", ".join(call_args)});')

    if result_wit_types:
        lines.append(f'{i}if (res == WasmResult::kYield) return res;')
        lines.append(f'{i}if (res != WasmResult::kOk) return res;')
        for idx, rt in enumerate(result_wit_types):
            out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
            wt = _wit_to_wasm_types(rt)[0] if _wit_to_wasm_types(rt) else 'i32'
            cast_t = _WASM_CAST_CPP.get(wt, 'int32_t')
            from_f = _WASM_FROM_FUNC.get(wt, 'WasmValue::FromI32')
            lines.append(f'{i}ctx->stack[ctx->stack_top++] = {from_f}(static_cast<{cast_t}>({out_name}));')

    lines.append(f'{i}return res;')
    body = '\n'.join(lines)
    return f'        case {const_name}: {{\n{body}\n        }}'


def _gen_validate_case(api, const_name):
    """ValidateHostFunctionType の各 case を生成する。"""
    i = '        '
    param_wit_types = api.get('param_wit_types', [])
    result_wit_types = api.get('result_wit_types', [])

    wasm_params = []
    for t in param_wit_types:
        wasm_params.extend(_wit_to_wasm_types(t))
    wasm_results = []
    for t in result_wit_types:
        wasm_results.extend(_wit_to_wasm_types(t))

    lines = [f'{i}case {const_name}: {{']
    lines.append(f'{i}    if (sig->param_count != {len(wasm_params)} || sig->result_count != {len(wasm_results)}) return false;')

    checks = []
    for idx, wt in enumerate(wasm_params):
        checks.append(f'sig->params[{idx}] == {_WASM_TYPE_CPP[wt]}')
    for idx, wt in enumerate(wasm_results):
        checks.append(f'sig->results[{idx}] == {_WASM_TYPE_CPP[wt]}')

    if checks:
        lines.append(f'{i}    return {" && ".join(checks)};')
    else:
        lines.append(f'{i}    return true;')

    lines.append(f'{i}}}')
    return '\n'.join(lines)


def _gen_typed_protos(apis):
    """名前空間ごとにグループ化した型付きプロトタイプ宣言を生成する。"""
    ns_protos = {}  # namespace → [proto_str, ...]

    for api in apis:
        cpp_func = api['function']
        parts = cpp_func.rsplit('::', 1)
        ns = parts[0] if len(parts) == 2 else ''
        func_name = parts[-1]

        proto_params = ['WasmEngine& engine']
        for name, wit_type in zip(api.get('param_names', []), api.get('param_wit_types', [])):
            for cpp_t, cpp_n in _wit_to_cpp_proto_params(name, wit_type):
                proto_params.append(f'{cpp_t} {cpp_n}')
        for idx, rt in enumerate(api.get('result_wit_types', [])):
            cpp_t, cpp_n = _wit_to_cpp_result_param(rt, idx)
            proto_params.append(f'{cpp_t} {cpp_n}')

        proto = f'WasmResult {func_name}({", ".join(proto_params)}) noexcept;'
        ns_protos.setdefault(ns, []).append(proto)

    blocks = []
    for ns in sorted(ns_protos.keys()):
        protos = ns_protos[ns]
        # The generated header wraps everything in namespace embwasm { }.
        # Strip the leading 'embwasm::' (or bare 'embwasm') prefix so we don't double-nest.
        if ns == 'embwasm':
            rel_ns = ''
        elif ns.startswith('embwasm::'):
            rel_ns = ns[len('embwasm::'):]
        else:
            rel_ns = ns

        if rel_ns:
            ns_parts = rel_ns.split('::')
            open_ns = '\n'.join(f'namespace {p} {{' for p in ns_parts)
            close_ns = '\n'.join(f'}} // namespace {p}' for p in reversed(ns_parts))
            block = open_ns + '\n' + '\n'.join(protos) + '\n' + close_ns
        else:
            block = '\n'.join(protos)
        blocks.append(block)
    return '\n\n'.join(blocks)


# ---------------------------------------------------------------------------
# WIT パーサー
# ---------------------------------------------------------------------------

def _parse_wit(wit_path):
    """WIT ファイルをパースして (imports, headers, apis_flat) を返す。"""
    apis_flat = []
    headers = []

    current_module = "env"
    module_init = None
    module_deinit = None

    with open(wit_path, 'r', encoding='utf-8') as f:
        content = f.read()

    is_interface = ("interface " in content)
    package_match = re.search(r"package\s+([\w:-]+);", content)
    package_name = package_match.group(1) if package_match else ""
    interface_match = re.search(r"interface\s+([\w-]+)\s*{", content)
    interface_name = interface_match.group(1) if interface_match else ""

    if is_interface and package_name and interface_name:
        import_module = f"{package_name}/{interface_name}"
    else:
        import_module = "$root"

    header_matches = re.findall(r"^///\s*@cpp-header:\s*[\"']?([\w./-]+)[\"']?", content, re.MULTILINE)
    for h in header_matches:
        if h not in headers:
            headers.append(h)

    wit_imports = re.findall(r"^///\s*@wit-import:\s*[\"']?([\w./-]+\.wit)[\"']?", content, re.MULTILINE)

    lines = content.splitlines()
    cpp_func = None
    for line in lines:
        stripped = line.strip()

        if stripped.startswith("///"):
            m = re.search(r"@cpp-func:\s*([\w:]+)", stripped)
            if m:
                cpp_func = m.group(1)
            m_mod = re.search(r"@cpp-module:\s*([\w:-]+)", stripped)
            if m_mod:
                current_module = m_mod.group(1).replace("-", "_")
            m_init = re.search(r"@cpp-init:\s*([\w::]+)", stripped)
            if m_init:
                module_init = m_init.group(1)
            m_deinit = re.search(r"@cpp-deinit:\s*([\w::]+)", stripped)
            if m_deinit:
                module_deinit = m_deinit.group(1)
            continue

        if "func" in stripped and ":" in stripped:
            parts = stripped.split(":", 1)
            field_name_raw = parts[0].replace("import ", "").strip()
            field_name = field_name_raw.replace("-", "_")
            sig_part = parts[1].strip() if len(parts) > 1 else ""

            param_names, param_wit_types, result_wit_types = _parse_wit_sig(sig_part)

            if cpp_func:
                apis_flat.append({
                    'module': current_module,
                    'field': field_name,
                    'function': cpp_func,
                    'wit_field': field_name_raw,
                    'wit_sig': sig_part,
                    'import_module': import_module,
                    'param_names': param_names,
                    'param_wit_types': param_wit_types,
                    'result_wit_types': result_wit_types,
                })
                cpp_func = None
            else:
                print(f"Warning: No @cpp-func for '{field_name}' in {wit_path}", file=sys.stderr)

    for api in apis_flat:
        if api['module'] == current_module:
            api['init'] = module_init
            api['deinit'] = module_deinit

    return wit_imports, headers, apis_flat


# ---------------------------------------------------------------------------
# マルチファイル読み込み
# ---------------------------------------------------------------------------

def load_all_configs(entry_path):
    """エントリポイント WIT から imports を辿り、全設定をマージして返す。"""
    merged_headers = []
    seen_headers = set()
    merged_apis = []
    seen_api_keys = set()
    seen_modules = {}

    entry_abs = os.path.abspath(entry_path)
    stack = [entry_abs]
    visited = set()

    while stack:
        current_path = stack.pop()
        if current_path in visited:
            print(f"Warning: Circular import detected, skipping '{current_path}'", file=sys.stderr)
            continue
        visited.add(current_path)

        if not os.path.exists(current_path):
            print(f"Error: Configuration file '{current_path}' not found.", file=sys.stderr)
            sys.exit(1)

        if not current_path.endswith('.wit'):
            print(f"Error: Only WIT files are supported. Got: '{current_path}'", file=sys.stderr)
            sys.exit(1)

        current_dir = os.path.dirname(current_path)
        file_imports, file_headers, file_apis = _parse_wit(current_path)

        file_modules = set(api.get('module', '') for api in file_apis)
        for m in file_modules:
            if m in seen_modules and seen_modules[m] != current_path:
                print(f"Error: Duplicate module '{m}' in '{current_path}' (already in '{seen_modules[m]}').", file=sys.stderr)
                sys.exit(1)
            seen_modules[m] = current_path

        for h in file_headers:
            if h not in seen_headers:
                seen_headers.add(h)
                merged_headers.append(h)

        for api in file_apis:
            if 'import_module' not in api:
                api['import_module'] = api.get('module', '$root')
            key = (api.get('module', ''), api.get('field', ''))
            if key not in seen_api_keys:
                seen_api_keys.add(key)
                merged_apis.append(api)

        for import_rel in reversed(file_imports):
            import_abs = os.path.normpath(os.path.join(current_dir, import_rel))
            if import_abs not in visited:
                stack.append(import_abs)

    return merged_headers, merged_apis


# ---------------------------------------------------------------------------
# コード生成メイン
# ---------------------------------------------------------------------------

def main():
    config_path = sys.argv[1] if len(sys.argv) > 1 else 'hostapi.wit'
    out_cpp_path = sys.argv[2] if len(sys.argv) > 2 else 'src/wasm_api_static.cpp'
    out_h_path = sys.argv[3] if len(sys.argv) > 3 else 'include/wasm_api_static.hpp'

    if not os.path.exists(config_path):
        print(f"Error: Configuration file '{config_path}' not found.", file=sys.stderr)
        sys.exit(1)

    headers, apis = load_all_configs(config_path)
    apis.sort(key=lambda x: (x['module'], x['field']))
    modules = sorted(list(set(api['module'] for api in apis)))

    # 初期化 / 終了関数の収集
    module_inits, module_deinits = {}, {}
    for api in apis:
        m = api['module']
        if api.get('init'):
            module_inits[m] = api['init']
        if api.get('deinit'):
            module_deinits[m] = api['deinit']

    init_calls_str = "\n".join(
        f"    {module_inits[m]}(engine);" for m in modules if m in module_inits
    )
    deinit_calls_str = "\n".join(
        f"    {module_deinits[m]}(engine);" for m in modules if m in module_deinits
    )

    # HostModuleId 定数
    module_enum_members_str = "\n".join(
        f"    k{''.join(w.capitalize() for w in re.sub(r'[^a-zA-Z0-9_]', '_', m).split('_') if w)} = {i},"
        for i, m in enumerate(modules)
    )

    # HostFunctionId 定数 (kWasmHostFuncId プレフィックス)
    enum_members = []
    for idx, api in enumerate(apis):
        mod_part = "".join(w.capitalize() for w in api["module"].split("_") if w)
        field_part = "".join(w.capitalize() for w in api["field"].split("_") if w)
        enum_members.append(
            f"constexpr HostFunctionId kWasmHostFuncId{mod_part}{field_part} = "
            f"static_cast<HostFunctionId>({idx});"
        )
    enum_members_str = "\n".join(enum_members)

    # 静的テーブルエントリ
    cpp_entries_str = ",\n".join(
        f'    {{ "{api["import_module"]}", "{api.get("wit_field") or api["field"]}", kWasmHostFuncId'
        f'{"".join(w.capitalize() for w in api["module"].split("_") if w)}'
        f'{"".join(w.capitalize() for w in api["field"].split("_") if w)}'
        f' }}'
        for api in apis
    )

    # モジュール ID 検索
    module_lookup_str = "\n".join(
        f'    if (module_len == {len(m)} && std::memcmp(module_name, "{m}", {len(m)}) == 0) '
        f'return HostModuleId::k{"".join(w.capitalize() for w in re.sub(r"[^a-zA-Z0-9_]", "_", m).split("_") if w)};'
        for m in modules
    )

    # DispatchHostFunction の case 文
    dispatch_cases_str = "\n".join(
        _gen_dispatch_case(api, em.split("=")[0].replace("constexpr HostFunctionId", "").strip())
        for api, em in zip(apis, enum_members)
    )

    # ValidateHostFunctionType の case 文
    validate_cases_str = "\n".join(
        _gen_validate_case(api, em.split("=")[0].replace("constexpr HostFunctionId", "").strip())
        for api, em in zip(apis, enum_members)
    )

    # 型付きプロトタイプ宣言
    typed_protos_str = _gen_typed_protos(apis)

    # インクルード
    include_directives_str = "\n".join(f'#include "{h}"' for h in headers)

    # 二分探索ルックアップロジック（既存コードを維持）
    lookup_logic = """\
    if ((module_len == 5 && std::memcmp(module_name, "$root", 5) == 0) || (module_len == 3 && std::memcmp(module_name, "env", 3) == 0)) {
        for (std::size_t i = 0; i < kStaticApiTableSize; ++i) {
            std::size_t entry_field_len = std::strlen(kStaticApiTable[i].field_name);
            if (field_len == entry_field_len && std::memcmp(field_name, kStaticApiTable[i].field_name, field_len) == 0) {
                return kStaticApiTable[i].id;
            }
        }
    } else {
        int low = 0;
        int high = static_cast<int>(kStaticApiTableSize) - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            const auto& entry = kStaticApiTable[mid];
            std::size_t entry_mod_len = std::strlen(entry.module_name);
            std::size_t min_mod_len = (module_len < entry_mod_len) ? module_len : entry_mod_len;
            int cmp = std::memcmp(module_name, entry.module_name, min_mod_len);
            if (cmp == 0) {
                if (module_len < entry_mod_len) cmp = -1;
                else if (module_len > entry_mod_len) cmp = 1;
                else {
                    std::size_t entry_field_len = std::strlen(entry.field_name);
                    std::size_t min_field_len = (field_len < entry_field_len) ? field_len : entry_field_len;
                    cmp = std::memcmp(field_name, entry.field_name, min_field_len);
                    if (cmp == 0) {
                        if (field_len < entry_field_len) cmp = -1;
                        else if (field_len > entry_field_len) cmp = 1;
                    }
                }
            }
            if (cmp == 0) return entry.id;
            else if (cmp < 0) high = mid - 1;
            else low = mid + 1;
        }
    }
    bool is_root = ((module_len == 5 && std::memcmp(module_name, "$root", 5) == 0) || (module_len == 3 && std::memcmp(module_name, "env", 3) == 0));
    for (std::size_t i = 0; i < kStaticApiTableSize; ++i) {
        std::size_t entry_mod_len = std::strlen(kStaticApiTable[i].module_name);
        if (is_root || (module_len == entry_mod_len && std::memcmp(module_name, kStaticApiTable[i].module_name, module_len) == 0)) {
            const char* s2 = kStaticApiTable[i].field_name;
            std::size_t s2_len = std::strlen(s2);
            if (field_len == s2_len) {
                bool match = true;
                for (std::size_t j = 0; j < field_len; ++j) {
                    char c1 = field_name[j]; char c2 = s2[j];
                    if (c1 == '-') c1 = '_'; if (c2 == '-') c2 = '_';
                    if (c1 != c2) { match = false; break; }
                }
                if (match) return kStaticApiTable[i].id;
            }
        }
    }
    return HostFunctionId::kInvalid;"""

    # ヘッダーファイル生成
    h_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by gen_api.py] - DO NOT EDIT DIRECTLY
// =============================================================================

#ifndef EMBWASM_WASM_API_STATIC_HPP_
#define EMBWASM_WASM_API_STATIC_HPP_

#include "wasm_api.hpp"

namespace embwasm {{

// HostModuleId 定数
enum class HostModuleId : uint32_t {{
{module_enum_members_str}
}};

// HostFunctionId 定数
{enum_members_str}

HostModuleId LookupStaticHostModuleId(const char* module_name, std::size_t module_len) noexcept;
HostFunctionId LookupStaticHostFunctionId(const char* module_name, std::size_t module_len, const char* field_name, std::size_t field_len) noexcept;

class WasmEngine;
struct WasmThreadContext;

void InitializeAllHostModules(WasmEngine& engine) noexcept;
void DeinitializeAllHostModules(WasmEngine& engine) noexcept;

WasmResult DispatchHostFunction(WasmEngine& engine, HostFunctionId id, WasmThreadContext* ctx) noexcept;
bool ValidateHostFunctionType(HostFunctionId id, const WasmTypeSignature* sig) noexcept;

// ---------------------------------------------------------------------------
// Typed host function declarations (auto-generated from WIT)
// ---------------------------------------------------------------------------
{typed_protos_str}

}} // namespace embwasm

#endif // EMBWASM_WASM_API_STATIC_HPP_
"""

    # ソースファイル生成
    cpp_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by gen_api.py] - DO NOT EDIT DIRECTLY
// =============================================================================

#include "wasm_api_static.hpp"
#include "wasm_engine.hpp"
#include "wasm_thread.hpp"
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

HostModuleId LookupStaticHostModuleId(const char* module_name, std::size_t module_len) noexcept {{
{module_lookup_str}
    return static_cast<HostModuleId>(0xFFFFFFFF);
}}

struct StaticApiEntry {{
    const char* module_name;
    const char* field_name;
    HostFunctionId id;
}};

static const StaticApiEntry kStaticApiTable[] = {{
{cpp_entries_str}
}};
static constexpr std::size_t kStaticApiTableSize = sizeof(kStaticApiTable) / sizeof(kStaticApiTable[0]);

HostFunctionId LookupStaticHostFunctionId(const char* module_name, std::size_t module_len, const char* field_name, std::size_t field_len) noexcept {{
{lookup_logic}
}}

WasmResult DispatchHostFunction(WasmEngine& engine, HostFunctionId id, WasmThreadContext* ctx) noexcept {{
    switch (id) {{
{dispatch_cases_str}
        default:
            return WasmResult::kErrorExecuteRuntimeError;
    }}
}}

bool ValidateHostFunctionType(HostFunctionId id, const WasmTypeSignature* sig) noexcept {{
    switch (id) {{
{validate_cases_str}
        default:
            return false;
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
