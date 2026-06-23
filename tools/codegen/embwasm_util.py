#!/usr/bin/env python3
"""embwasm_util.py - embwasm コード生成ユーティリティ

サブコマンド:
  gen-hostapi-dispatch  WIT からホスト API ルックアップテーブル・ディスパッチャを生成
  gen-hostapi-proto     WIT からホストモジュールの HPP + CPP スケルトンを生成
"""

import os
import sys
import re
import argparse


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
# コード生成ヘルパー (gen-hostapi-dispatch 用)
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
            cpp_t = _PRIMITIVE_CPP.get(t, 'uint32_t')
            vf = _WASM_VALUE_FIELD[wasm_slots[0]]
            lines.append(f'{i}{cpp_t} _{name} = static_cast<{cpp_t}>(ctx->stack[--ctx->stack_top].value.{vf});')
        else:
            # string / list<T>: len が上、ptr が下
            lines.append(f'{i}uint32_t _{name}_len = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);')
            lines.append(f'{i}uint32_t _{name}_ptr = static_cast<uint32_t>(ctx->stack[--ctx->stack_top].value.i32);')

    if needs_mem:
        lines.append(f'{i}uint8_t* _mem = engine.GetLinearMemory();')

    for idx, rt in enumerate(result_wit_types):
        out_name = '_out_result' if idx == 0 else f'_out_result{idx}'
        lines.append(f'{i}{_wit_to_out_var_type(rt)} {out_name} = {{}};')

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
    ns_protos = {}

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

    module_init = None
    module_deinit = None

    with open(wit_path, 'r', encoding='utf-8') as f:
        content = f.read()

    is_interface = ("interface " in content)
    package_match = re.search(r"package\s+([\w:-]+);", content)
    package_name = package_match.group(1) if package_match else ""
    interface_match = re.search(r"interface\s+([\w-]+)\s*{", content)
    interface_name = interface_match.group(1) if interface_match else ""
    world_match = re.search(r"world\s+([\w-]+)\s*\{", content)
    world_name = world_match.group(1).replace('-', '_') if world_match else ""
    iface_or_world = interface_name.replace('-', '_') if interface_name else world_name

    current_module = interface_name.replace('-', '_') if interface_name else "env"

    if is_interface and package_name and interface_name:
        import_module = f"{package_name}/{interface_name}"
    else:
        import_module = "$root"

    header_matches = re.findall(r"^///\s*@cpp-header:\s*[\"']?([\w./-]+)[\"']?", content, re.MULTILINE)
    for h in header_matches:
        if h not in headers:
            headers.append(h)

    wit_imports = re.findall(r"^///\s*@wit-import:\s*[\"']?([\w./-]+\.wit)[\"']?", content, re.MULTILINE)

    pkg_ns = _wit_package_to_ns(package_name) if package_name else ""

    lines = content.splitlines()
    for line in lines:
        stripped = line.strip()

        if stripped.startswith("///"):
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

            if pkg_ns and iface_or_world:
                cpp_func = f"embwasm::hostmodules::{pkg_ns}::{iface_or_world}::{field_name}"
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
            else:
                print(f"Warning: cannot derive cpp_func for '{field_name}' in {wit_path}", file=sys.stderr)

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
# gen-hostapi-dispatch
# ---------------------------------------------------------------------------

def cmd_gen_hostapi_dispatch(args):
    config_path = args.wit_file
    out_h_path = args.out_hpp
    out_cpp_path = args.out_cpp

    if not os.path.exists(config_path):
        print(f"Error: Configuration file '{config_path}' not found.", file=sys.stderr)
        sys.exit(1)

    headers, apis = load_all_configs(config_path)
    apis.sort(key=lambda x: (x['module'], x['field']))
    modules = sorted(list(set(api['module'] for api in apis)))

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

    module_enum_members_str = "\n".join(
        f"    k{''.join(w.capitalize() for w in re.sub(r'[^a-zA-Z0-9_]', '_', m).split('_') if w)} = {i},"
        for i, m in enumerate(modules)
    )

    enum_members = []
    for idx, api in enumerate(apis):
        mod_part = "".join(w.capitalize() for w in api["module"].split("_") if w)
        field_part = "".join(w.capitalize() for w in api["field"].split("_") if w)
        enum_members.append(
            f"constexpr HostFunctionId kWasmHostFuncId{mod_part}{field_part} = "
            f"static_cast<HostFunctionId>({idx});"
        )
    enum_members_str = "\n".join(enum_members)

    cpp_entries_str = ",\n".join(
        f'    {{ "{api["import_module"]}", "{api.get("wit_field") or api["field"]}", kWasmHostFuncId'
        f'{"".join(w.capitalize() for w in api["module"].split("_") if w)}'
        f'{"".join(w.capitalize() for w in api["field"].split("_") if w)}'
        f' }}'
        for api in apis
    )

    module_lookup_str = "\n".join(
        f'    if (module_len == {len(m)} && std::memcmp(module_name, "{m}", {len(m)}) == 0) '
        f'return HostModuleId::k{"".join(w.capitalize() for w in re.sub(r"[^a-zA-Z0-9_]", "_", m).split("_") if w)};'
        for m in modules
    )

    dispatch_cases_str = "\n".join(
        _gen_dispatch_case(api, em.split("=")[0].replace("constexpr HostFunctionId", "").strip())
        for api, em in zip(apis, enum_members)
    )

    validate_cases_str = "\n".join(
        _gen_validate_case(api, em.split("=")[0].replace("constexpr HostFunctionId", "").strip())
        for api, em in zip(apis, enum_members)
    )

    typed_protos_str = _gen_typed_protos(apis)

    include_directives_str = "\n".join(f'#include "{h}"' for h in headers)

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

    h_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-dispatch] - DO NOT EDIT DIRECTLY
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

    cpp_content = f"""// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-dispatch] - DO NOT EDIT DIRECTLY
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


# ---------------------------------------------------------------------------
# gen-hostapi-proto ヘルパー
# ---------------------------------------------------------------------------

def _wit_package_to_ns(package_raw):
    """'embwasm:threads' → 'embwasm::threads' (: → ::, - → _)"""
    return package_raw.replace('-', '_').replace(':', '::')


def _parse_wit_for_proto(wit_path):
    """proto 生成に必要な情報を WIT ファイルからパースして返す。

    Returns:
        (package_raw, interface_raw, headers, funcs)
        funcs: [(field_name, param_names, param_wit_types, result_wit_types), ...]
    """
    with open(wit_path, 'r', encoding='utf-8') as f:
        content = f.read()

    package_match = re.search(r"package\s+([\w:-]+);", content)
    package_raw = package_match.group(1) if package_match else "unknown:unknown"

    interface_match = re.search(r"interface\s+([\w-]+)\s*{", content)
    interface_raw = interface_match.group(1) if interface_match else "unknown"

    header_matches = re.findall(r"^///\s*@cpp-header:\s*[\"']?([\w./-]+)[\"']?", content, re.MULTILINE)

    funcs = []
    lines = content.splitlines()
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("///"):
            continue
        if "func" in stripped and ":" in stripped:
            parts = stripped.split(":", 1)
            field_name_raw = parts[0].replace("import ", "").strip()
            if not re.match(r'^[\w-]+$', field_name_raw):
                continue
            field_name = field_name_raw.replace("-", "_")
            sig_part = parts[1].strip() if len(parts) > 1 else ""
            param_names, param_wit_types, result_wit_types = _parse_wit_sig(sig_part)
            if 'func' in sig_part:
                funcs.append((field_name, param_names, param_wit_types, result_wit_types))

    return package_raw, interface_raw, header_matches, funcs


def _build_proto_hpp(package_raw, interface_raw, funcs, guard_macro):
    """gen-hostapi-proto 用の HPP 文字列を生成する。"""
    pkg_ns = _wit_package_to_ns(package_raw)
    iface_ns = interface_raw.replace('-', '_')
    # namespace parts after 'embwasm {' and 'hostmodules {'
    # full: embwasm::hostmodules::<pkg_ns>::<iface_ns>
    # inner parts (after embwasm::hostmodules::):
    inner_parts = pkg_ns.split('::') + [iface_ns]

    open_inner = '\n'.join(f'namespace {p} {{' for p in inner_parts)
    close_inner = '\n'.join(f'}} // namespace {p}' for p in reversed(inner_parts))

    decls = []
    for field_name, param_names, param_wit_types, result_wit_types in funcs:
        cpp_params = ['WasmEngine& engine']
        for pname, pwit in zip(param_names, param_wit_types):
            for cpp_t, cpp_n in _wit_to_cpp_proto_params(pname, pwit):
                cpp_params.append(f'{cpp_t} {cpp_n}')
        for idx, rt in enumerate(result_wit_types):
            cpp_t, cpp_n = _wit_to_cpp_result_param(rt, idx)
            cpp_params.append(f'{cpp_t} {cpp_n}')
        decls.append(f'WasmResult {field_name}({", ".join(cpp_params)}) noexcept;')

    decls_str = '\n'.join(decls)

    return f"""\
// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-proto] - DO NOT EDIT DIRECTLY
// =============================================================================

#ifndef {guard_macro}
#define {guard_macro}

#include "wasm_types.hpp"

namespace embwasm {{
class WasmEngine;

namespace hostmodules {{
{open_inner}

// [embwasm-proto:decl-begin]
{decls_str}
// [embwasm-proto:decl-end]

{close_inner}
}} // namespace hostmodules
}} // namespace embwasm

#endif // {guard_macro}
"""


def _build_proto_cpp_stub(field_name, param_names, param_wit_types, result_wit_types):
    """単一関数スタブ文字列（マーカー付き）を返す。"""
    cpp_params = ['WasmEngine& engine']
    void_args = ['(void)engine;']
    for pname, pwit in zip(param_names, param_wit_types):
        for cpp_t, cpp_n in _wit_to_cpp_proto_params(pname, pwit):
            cpp_params.append(f'{cpp_t} {cpp_n}')
            void_args.append(f'(void){cpp_n};')
    for idx, rt in enumerate(result_wit_types):
        cpp_t, cpp_n = _wit_to_cpp_result_param(rt, idx)
        cpp_params.append(f'{cpp_t} {cpp_n}')
        void_args.append(f'(void){cpp_n};')
    void_str = ' '.join(void_args)
    sig = f'WasmResult {field_name}({", ".join(cpp_params)}) noexcept'
    return (f'// [embwasm-proto:func:{field_name}]\n'
            f'{sig} {{\n    {void_str}\n    return WasmResult::kErrorExecuteRuntimeError;\n}}')


def _build_proto_cpp(package_raw, interface_raw, funcs, hpp_filename):
    """gen-hostapi-proto 用の CPP スタブ文字列を生成する。"""
    pkg_ns = _wit_package_to_ns(package_raw)
    iface_ns = interface_raw.replace('-', '_')
    inner_parts = pkg_ns.split('::') + [iface_ns]

    open_inner = '\n'.join(f'namespace {p} {{' for p in inner_parts)
    close_inner = '\n'.join(f'}} // namespace {p}' for p in reversed(inner_parts))

    stubs_str = '\n\n'.join(
        _build_proto_cpp_stub(fn, pn, pw, rw) for fn, pn, pw, rw in funcs
    )

    return f"""\
// =============================================================================
// Copyright (c) 2026 embwasm Project. All rights reserved.
// [Auto-generated by embwasm_util.py gen-hostapi-proto] - DO NOT EDIT DIRECTLY
// =============================================================================

#include "{hpp_filename}"
#include "wasm_engine.hpp"

namespace embwasm {{
namespace hostmodules {{
{open_inner}

{stubs_str}

// [embwasm-proto:funcs-end]
{close_inner}
}} // namespace hostmodules
}} // namespace embwasm
"""


# ---------------------------------------------------------------------------
# gen-hostapi-proto インクリメンタル更新ヘルパー
# ---------------------------------------------------------------------------

_DECL_BEGIN = '// [embwasm-proto:decl-begin]'
_DECL_END   = '// [embwasm-proto:decl-end]'
_FUNCS_END  = '// [embwasm-proto:funcs-end]'


def _update_proto_hpp(existing, new_decls_block):
    """既存 HPP の decl-begin/end 間を new_decls_block で置き換える。
    マーカーが無ければ None を返す（→ 全上書きフォールバック）。"""
    begin_idx = existing.find(_DECL_BEGIN)
    end_idx   = existing.find(_DECL_END)
    if begin_idx == -1 or end_idx == -1 or end_idx <= begin_idx:
        return None
    return (existing[:begin_idx]
            + _DECL_BEGIN + '\n'
            + new_decls_block + '\n'
            + _DECL_END
            + existing[end_idx + len(_DECL_END):])


def _get_known_cpp_funcs(content):
    """[embwasm-proto:func:NAME] マーカーから関数名 set を返す。"""
    return set(re.findall(r'//\s*\[embwasm-proto:func:(\w+)\]', content))


def _update_proto_cpp(existing_content, new_funcs):
    """既存 CPP を差分更新する。
    - 削除関数: func マーカーを obsolete マーカーに置き換える
    - 追加関数: funcs-end の直前にスタブを挿入
    マーカーが無ければ None を返す（→ 全上書きフォールバック）。"""
    if _FUNCS_END not in existing_content:
        return None

    known = _get_known_cpp_funcs(existing_content)
    new_names = {f[0] for f in new_funcs}

    result = existing_content

    # 削除: func マーカー → obsolete マーカー
    for fname in sorted(known - new_names):
        old_tag = f'// [embwasm-proto:func:{fname}]'
        new_tag = (f'// [embwasm-proto:obsolete:{fname}]'
                   f' -- WIT から削除されました。実装を確認後に削除してください。')
        result = result.replace(old_tag, new_tag, 1)

    # 追加: funcs-end の直前に新スタブを挿入
    added = [f for f in new_funcs if f[0] not in known]
    if added:
        stubs = '\n\n'.join(
            _build_proto_cpp_stub(fn, pn, pw, rw) for fn, pn, pw, rw in added
        )
        result = result.replace(_FUNCS_END, stubs + '\n\n' + _FUNCS_END, 1)

    return result


# ---------------------------------------------------------------------------
# gen-hostapi-proto
# ---------------------------------------------------------------------------

def cmd_gen_hostapi_proto(args):
    wit_path = args.wit_file
    out_h_path = args.out_hpp
    out_cpp_path = args.out_cpp

    if not os.path.exists(wit_path):
        print(f"Error: WIT file '{wit_path}' not found.", file=sys.stderr)
        sys.exit(1)

    package_raw, interface_raw, _headers, funcs = _parse_wit_for_proto(wit_path)

    hpp_basename = os.path.basename(out_h_path)
    guard_macro = re.sub(r'[^A-Z0-9]', '_', hpp_basename.upper()) + '_'

    # --- HPP ---
    decls_lines = []
    for field_name, param_names, param_wit_types, result_wit_types in funcs:
        cpp_params = ['WasmEngine& engine']
        for pname, pwit in zip(param_names, param_wit_types):
            for cpp_t, cpp_n in _wit_to_cpp_proto_params(pname, pwit):
                cpp_params.append(f'{cpp_t} {cpp_n}')
        for idx, rt in enumerate(result_wit_types):
            cpp_t, cpp_n = _wit_to_cpp_result_param(rt, idx)
            cpp_params.append(f'{cpp_t} {cpp_n}')
        decls_lines.append(f'WasmResult {field_name}({", ".join(cpp_params)}) noexcept;')
    new_decls_block = '\n'.join(decls_lines)

    if os.path.exists(out_h_path):
        with open(out_h_path, 'r', encoding='utf-8') as f:
            existing_hpp = f.read()
        hpp_content = _update_proto_hpp(existing_hpp, new_decls_block)
        if hpp_content is None:
            print(f"Warning: no proto markers in {out_h_path}, overwriting.", file=sys.stderr)
            hpp_content = _build_proto_hpp(package_raw, interface_raw, funcs, guard_macro)
    else:
        hpp_content = _build_proto_hpp(package_raw, interface_raw, funcs, guard_macro)

    # --- CPP ---
    if os.path.exists(out_cpp_path):
        with open(out_cpp_path, 'r', encoding='utf-8') as f:
            existing_cpp = f.read()
        cpp_content = _update_proto_cpp(existing_cpp, funcs)
        if cpp_content is None:
            print(f"Warning: no proto markers in {out_cpp_path}, overwriting.", file=sys.stderr)
            cpp_content = _build_proto_cpp(package_raw, interface_raw, funcs, hpp_basename)
    else:
        cpp_content = _build_proto_cpp(package_raw, interface_raw, funcs, hpp_basename)

    os.makedirs(os.path.dirname(os.path.abspath(out_h_path)), exist_ok=True)
    os.makedirs(os.path.dirname(os.path.abspath(out_cpp_path)), exist_ok=True)

    with open(out_h_path, 'w', encoding='utf-8') as f:
        f.write(hpp_content)
    with open(out_cpp_path, 'w', encoding='utf-8') as f:
        f.write(cpp_content)

    print(f"Generated {out_h_path} and {out_cpp_path} from {wit_path}")


# ---------------------------------------------------------------------------
# エントリポイント
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        prog='embwasm_util.py',
        description='embwasm コード生成ユーティリティ',
    )
    subparsers = parser.add_subparsers(dest='subcommand', required=True)

    p_dispatch = subparsers.add_parser(
        'gen-hostapi-dispatch',
        help='WIT からホスト API ルックアップテーブル・ディスパッチャを生成',
    )
    p_dispatch.add_argument('wit_file', help='入力 WIT ファイル')
    p_dispatch.add_argument('out_hpp', help='出力 HPP パス')
    p_dispatch.add_argument('out_cpp', help='出力 CPP パス')
    p_dispatch.set_defaults(func=cmd_gen_hostapi_dispatch)

    p_proto = subparsers.add_parser(
        'gen-hostapi-proto',
        help='WIT からホストモジュール HPP + CPP スケルトンを生成',
    )
    p_proto.add_argument('wit_file', help='入力 WIT ファイル')
    p_proto.add_argument('out_hpp', help='出力 HPP パス')
    p_proto.add_argument('out_cpp', help='出力 CPP パス')
    p_proto.set_defaults(func=cmd_gen_hostapi_proto)

    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
