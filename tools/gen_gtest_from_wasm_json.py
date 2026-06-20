#!/usr/bin/env python3
import json
import os
import sys
import re

def sanitize_cpp_identifier(name):
    """C++の識別子（クラス名や関数名）として安全な文字列に変換する"""
    if not name:
        return "_empty_"
    sanitized = re.sub(r'[^a-zA-Z0-9_]', '_', name)
    sanitized = re.sub(r'_+', '_', sanitized)
    if not sanitized or sanitized == "_":
        return "_empty_id_"
    if sanitized[0].isdigit():
        sanitized = '_' + sanitized
    return sanitized

def python_type_to_cpp(wasm_type, value):
    """Wasmの型と値をC++の型・値表現に変換する"""
    if value is None or str(value).lower() == "null":
        return "nullptr"

    val_str = str(value).strip()

    if "inf" in val_str.lower():
        is_negative = val_str.startswith("-")
        if wasm_type == "f32":
            return "0xff800000" if is_negative else "0x7f800000"
        else:
            return "0xfff0000000000000" if is_negative else "0x7ff0000000000000"

    if "nan" in val_str.lower():
        if wasm_type == "f32":
            return "0x7fc00000"
        else:
            return "0x7ff8000000000000"

    if wasm_type == "i32":
        return f"int32_t({val_str}LL)"
    elif wasm_type == "i64":
        if val_str.startswith("-"):
            return f"int64_t(0x10000000000000000ULL - {val_str[1:]}ULL)" if val_str != "-9223372036854775808" else "int64_t(0x8000000000000000ULL)"
        return f"int64_t({val_str}ULL)"
    elif wasm_type == "f32":
        if "." not in val_str and "e" not in val_str.lower():
            return f"uint32_t({val_str}ULL)"
        return f"{val_str}f"
    elif wasm_type == "f64":
        if "." not in val_str and "e" not in val_str.lower():
            return f"uint64_t({val_str}ULL)"
        return f"{val_str}"

    return val_str

def escape_wasm_string_literal(name):
    """Wasmの関数名をC++の通常の文字列リテラルとして安全に出力する"""
    utf8_bytes = name.encode('utf-8')
    escaped = []
    for b in utf8_bytes:
        if b == ord('\\'):
            escaped.append('\\\\')
        elif b == ord('"'):
            escaped.append('\\"')
        elif b == 10: # \n
            escaped.append('\\n')
        elif b == 13: # \r
            escaped.append('\\r')
        elif b == 9: # \t
            escaped.append('\\t')
        elif b == 12: # \f
            escaped.append('\\f')
        elif b == 11: # \v
            escaped.append('\\v')
        elif b < 32 or b >= 127:
            escaped.append(f'\\{b:03o}')
        else:
            escaped.append(chr(b))
    return '"' + "".join(escaped) + '"'

def wasm_method_name(snake_name):
    """WasmInterpreterのスネークケースメソッド名をCamelCaseに変換する"""
    camel_map = {
        'create': 'Create', 'to': 'To', 'is': 'Is', 'nan': 'Nan', 'bits': 'Bits',
        'i32': 'I32', 'i64': 'I64', 'f32': 'F32', 'f64': 'F64',
        'externref': 'Externref', 'funcref': 'Funcref',
    }
    return ''.join(camel_map.get(p, p.capitalize()) for p in snake_name.split('_'))

def file_to_c_array(file_path):
    if not os.path.exists(file_path):
        return "0x00", 0
    with open(file_path, 'rb') as f:
        binary_data = f.read()
    size = len(binary_data)
    hex_bytes = [f"0x{b:02x}" for b in binary_data]
    lines = []
    for i in range(0, len(hex_bytes), 12):
        lines.append("        " + ", ".join(hex_bytes[i:i+12]))
    return "{\n" + ",\n".join(lines) + "\n    }", size

def generate_interpreter_template():
    """WasmInterpreter.hppの型安全版雛形コードを生成"""
    return """#pragma once
#include <cstdint>
#include <cstddef>

struct MyInternalValue {
    int64_t raw_bits; 
    uint8_t type_tag; 
};

using WasmValue = MyInternalValue;

class WasmInterpreter {
public:
    WasmInterpreter() {}
    ~WasmInterpreter() {}
    
    bool LoadModule(const uint8_t* bytes, size_t size) {
        (void)bytes; (void)size;
        return true;
    }

    WasmValue CreateI32(int32_t val)        { (void)val; return WasmValue{}; }
    WasmValue CreateI64(int64_t val)        { (void)val; return WasmValue{}; }
    WasmValue CreateF32(float val)          { (void)val; return WasmValue{}; }
    WasmValue CreateF64(double val)         { (void)val; return WasmValue{}; }

    WasmValue CreateF32Bits(uint32_t bits)  { (void)bits; return WasmValue{}; }
    WasmValue CreateF64Bits(uint64_t bits)  { (void)bits; return WasmValue{}; }

    WasmValue CreateF32Nan(uint32_t bit_pattern) { (void)bit_pattern; return WasmValue{}; }
    WasmValue CreateF64Nan(uint64_t bit_pattern) { (void)bit_pattern; return WasmValue{}; }

    WasmValue CreateExternref(const void* val) { (void)val; return WasmValue{}; }
    WasmValue CreateFuncref(const void* val)   { (void)val; return WasmValue{}; }

    int32_t Invoke(const char* func_name, const WasmValue* args, size_t args_count,
                   WasmValue* out_result) {
        (void)func_name; (void)args; (void)args_count; (void)out_result;
        return 0;
    }

    int32_t  ToI32(WasmValue val)     { (void)val; return 0; }
    int64_t  ToI64(WasmValue val)     { (void)val; return 0; }
    float    ToF32(WasmValue val)     { (void)val; return 0.0f; }
    double   ToF64(WasmValue val)     { (void)val; return 0.0; }
    bool     IsNanF32(WasmValue val)  { (void)val; return false; }
    bool     IsNanF64(WasmValue val)  { (void)val; return false; }

    uint32_t ToF32Bits(WasmValue val) { (void)val; return 0; }
    uint64_t ToF64Bits(WasmValue val) { (void)val; return 0; }

    void* ToExternref(WasmValue val)  { (void)val; return nullptr; }
    void* ToFuncref(WasmValue val)    { (void)val; return nullptr; }
};
"""

def process_combined_assets(input_dir, output_dir):
    os.makedirs(output_dir, exist_ok=True)

    # hpp_path = os.path.join(output_dir, "WasmInterpreter.hpp")
    # with open(hpp_path, 'w') as f:
    #     f.write(generate_interpreter_template())
    # print(f"生成: {hpp_path}")

    data_lines = [
        "// 自動生成されたWebAssemblyテスト用バイナリデータヘッダー",
        "#pragma once",
        "#include <cstdint>",
        "#include <cstddef>",
        ""
    ]

    test_lines = [
        "// 自動生成されたWebAssembly公式仕様テストコード",
        "#include <gtest/gtest.h>",
        "#include <cstdint>",
        "#include <cstddef>",
        '#include "WasmInterpreter.hpp"',
        '#include "wasm_embedded_data.h"',
        ""
    ]

    global_wasm_idx = 0
    json_count = 0

    SKIP_SUITES = {
    }

    for root, dirs, files in os.walk(input_dir):
        for file in sorted(files):
            if file.endswith('.json'):
                json_path = os.path.join(root, file)
                base_name = os.path.splitext(os.path.basename(json_path))[0]
                if base_name in SKIP_SUITES:
                    print(f"スキップ: {json_path} (未サポート仕様)")
                    continue

                print(f"解析中: {json_path}")

                with open(json_path, 'r') as f:
                    data = json.load(f)

                json_dir = os.path.dirname(json_path)
                safe_base_name = sanitize_cpp_identifier(base_name)
                suite_name = f"WasmCoreTest_{safe_base_name}"

                test_lines.append(f'// =========================================================================')
                test_lines.append(f'// Test Suite for {base_name}.json')
                test_lines.append(f'// =========================================================================')
                test_lines.append(f'class {suite_name} : public ::testing::Test {{')
                test_lines.append('protected:')
                test_lines.append('    WasmInterpreter interpreter;')
                test_lines.append('};')
                test_lines.append('')

                local_wasm_map = {}
                test_lines.append(f'TEST_F({suite_name}, RunSpecTests) {{')
                test_lines.append(f'    interpreter.UnloadAll();')

                for cmd in data.get("commands", []):
                    line_num = cmd.get("line", 0)
                    cmd_type = cmd.get("type")

                    if cmd_type == "module":
                        wasm_filename = cmd.get("filename")
                        if wasm_filename:
                            if wasm_filename not in local_wasm_map:
                                wasm_full_path = os.path.join(json_dir, wasm_filename)
                                array_str, array_size = file_to_c_array(wasm_full_path)

                                var_name = f"wasm_data_{global_wasm_idx}"
                                size_name = f"wasm_size_{global_wasm_idx}"
                                local_wasm_map[wasm_filename] = (var_name, size_name)
                                global_wasm_idx += 1

                                data_lines.append(f'// From {base_name}.json Line {line_num} ({wasm_filename})')
                                data_lines.append(f'static const uint8_t {var_name}[] = {array_str};')
                                data_lines.append(f'static const size_t {size_name} = {array_size};')
                                data_lines.append('')

                            var_name, size_name = local_wasm_map[wasm_filename]
                            module_name = cmd.get("name")
                            if module_name:
                                name_arg = escape_wasm_string_literal(module_name)
                            else:
                                name_arg = "nullptr"
                            test_lines.append(f'    {{ // Line {line_num} (Load module {wasm_filename})')
                            test_lines.append(f'        ASSERT_LE(0, interpreter.LoadModule({name_arg}, {var_name}, {size_name}));')
                            test_lines.append(f'    }}')

                    elif cmd_type == "register":
                        as_name = cmd.get("as", "")
                        module_name_ref = cmd.get("name")
                        if as_name:
                            test_lines.append(f'    {{ // Line {line_num} (Register as "{as_name}")')
                            if module_name_ref:
                                test_lines.append(f'        interpreter.RegisterModule({escape_wasm_string_literal(module_name_ref)}, {escape_wasm_string_literal(as_name)});')
                            else:
                                test_lines.append(f'        interpreter.RegisterModule({escape_wasm_string_literal(as_name)});')
                            test_lines.append(f'    }}')

                    elif cmd_type in ["assert_return", "action"]:
                        action = cmd.get("action", {})
                        if action.get("type") == "invoke":
                            func_name = action.get("field")
                            args = action.get("args", [])
                            expected = cmd.get("expected", []).copy() if cmd_type == "assert_return" else []

                            escaped_func_name = escape_wasm_string_literal(func_name)
                            test_lines.append(f'    {{ // Line {line_num}')

                            if args:
                                arg_vals = []
                                for a in args:
                                    t = a['type']
                                    val = python_type_to_cpp(t, a['value'])

                                    if "nan" in str(a['value']).lower() or "inf" in str(a['value']).lower():
                                        factory_method = wasm_method_name(f"create_{t}_bits")
                                    elif (t == "f32" or t == "f64") and "uint" in val:
                                        factory_method = wasm_method_name(f"create_{t}_bits")
                                    elif t in ["externref", "funcref"] and val != "nullptr":
                                        factory_method = wasm_method_name(f"create_{t}")
                                        val = f"reinterpret_cast<const void*>({val}ULL)"
                                    else:
                                        factory_method = wasm_method_name(f"create_{t}")

                                    arg_vals.append(f'interpreter.{factory_method}({val})')

                                vals_str = ", ".join(arg_vals)
                                test_lines.append(f'        WasmValue args[] = {{ {vals_str} }};')
                                args_param, args_count_param = "args", str(len(args))
                            else:
                                args_param, args_count_param = "nullptr", "0"

                            action_module = action.get("module")
                            if action_module:
                                invoke_module_arg = escape_wasm_string_literal(action_module) + ", "
                            else:
                                invoke_module_arg = ""
                            invoke_base = f'interpreter.Invoke({invoke_module_arg}{escaped_func_name}, {args_param}, {args_count_param}'

                            if expected:
                                exp = expected[0]
                                exp_type = exp['type']
                                exp_val = python_type_to_cpp(exp_type, exp['value'])

                                test_lines.append(f'        WasmValue result = {{}};')
                                test_lines.append(f'        ASSERT_EQ(0, {invoke_base}, &result));')
                                if "f32" in exp_type or "f64" in exp_type:
                                    if "nan" in str(exp['value']).lower():
                                        test_lines.append(f'        EXPECT_TRUE(interpreter.{wasm_method_name("is_nan_" + exp_type)}(result));')
                                    elif "uint" in exp_val or "inf" in str(exp['value']).lower():
                                        test_lines.append(f'        EXPECT_EQ({exp_val}, interpreter.{wasm_method_name("to_" + exp_type + "_bits")}(result));')
                                    else:
                                        test_lines.append(f'        EXPECT_NEAR({exp_val}, interpreter.{wasm_method_name("to_" + exp_type)}(result), 1e-5);')
                                elif exp_type in ["externref", "funcref"] and exp_val != "nullptr":
                                    test_lines.append(f'        EXPECT_EQ(reinterpret_cast<void*>({exp_val}ULL), interpreter.{wasm_method_name("to_" + exp_type)}(result));')
                                else:
                                    test_lines.append(f'        EXPECT_EQ({exp_val}, interpreter.{wasm_method_name("to_" + exp_type)}(result));')
                            else:
                                test_lines.append(f'        ASSERT_EQ(0, {invoke_base}, nullptr));')

                            test_lines.append('    }')

                test_lines.append('}')
                test_lines.append('')

                json_count += 1

    data_path = os.path.join(output_dir, "wasm_embedded_data.h")
    with open(data_path, 'w') as f:
        f.write("\n".join(data_lines))
    print(f"生成: {data_path}")

    cpp_path = os.path.join(output_dir, "all_spec_tests.cpp")
    with open(cpp_path, 'w') as f:
        f.write("\n".join(test_lines))
    print(f"生成: {cpp_path}")

    print(f"\n[成功]")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("使い方: python script.py <入力jsonディレクトリ> <アセット出力先ディレクトリ>")
        sys.exit(1)
    process_combined_assets(sys.argv[1], sys.argv[2])
