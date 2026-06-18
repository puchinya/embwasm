#!/usr/bin/env python3
import subprocess
import os
import tempfile
import sys

WASM_SOURCES = {
    "normal": {
        "code": """
extern void print_val(int);
void add_and_print() {
    print_val(10 + 20);
}
""",
        "exports": ["add_and_print"],
        "extra_args": []
    },
    "exec_err": {
        "code": """
void my_func() {}
""",
        "exports": ["my_func"],
        "extra_args": []
    },
    "too_many_locals": {
        "code": """
int test() {
    volatile int a0=0,a1=0,a2=0,a3=0,a4=0,a5=0,a6=0,a7=0,a8=0,a9=0;
    volatile int b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0,b7=0,b8=0,b9=0;
    volatile int c0=0,c1=0,c2=0,c3=0,c4=0,c5=0,c6=0,c7=0,c8=0,c9=0;
    volatile int d0=0,d1=0,d2=0;
    return a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+b0+b1+b2+b3+b4+b5+b6+b7+b8+b9+c0+c1+c2+c3+c4+c5+c6+c7+c8+c9+d0+d1+d2;
}
""",
        "exports": ["test"],
        "extra_args": []
    },
    "i32_arith": {
        "code": """
int test_calc(int a, int b) {
    return a + b;
}
""",
        "exports": ["test_calc"],
        "extra_args": []
    },
    "i32_comp": {
        "code": """
int test_calc(int a, int b) {
    return a == b;
}
""",
        "exports": ["test_calc"],
        "extra_args": []
    },
    "i64_arith": {
        "code": """
long long test_calc(long long a, long long b) {
    return a + b;
}
""",
        "exports": ["test_calc"],
        "extra_args": []
    },
    "unary": {
        "code": """
int test_calc(int a) {
    return !a;
}
""",
        "exports": ["test_calc"],
        "extra_args": []
    },
    "tee": {
        "code": """
int test_calc(int a) {
    int res;
    __asm__(
        "local.get %1\\n"
        "local.tee %1\\n"
        "drop\\n"
        "local.get %1\\n"
        "local.set %0"
        : "=r"(res)
        : "r"(a)
    );
    return res;
}
""",
        "exports": ["test_calc"],
        "extra_args": []
    },
    "ret": {
        "code": """
int test_calc(int a) {
    int res;
    __asm__(
        "nop\\n"
        "local.get %1\\n"
        "return\\n"
        "i32.const 99\\n"
        "local.set %0"
        : "=r"(res)
        : "r"(a)
    );
    return res;
}
""",
        "exports": ["test_calc"],
        "extra_args": []
    },
    "call": {
        "code": """
__attribute__((noinline)) int double_val(int a) {
    return a + a;
}
int quadruple(int a) {
    return double_val(double_val(a));
}
""",
        "exports": ["quadruple"],
        "extra_args": []
    },
    "overflow": {
        "code": """
int infinite_call(int a) {
    int res;
    __asm__(
        "local.get %1\\n"
        "call infinite_call\\n"
        "local.set %0"
        : "=r"(res)
        : "r"(a)
    );
    return res;
}
""",
        "exports": ["infinite_call"],
        "extra_args": []
    },
    "block": {
        "code": """
int block() {
    volatile int val = 0;
    do {
        val = 1;
        break;
        val = 2;
    } while (0);
    return val + 3;
}
""",
        "exports": ["block"],
        "extra_args": []
    },
    "loop": {
        "code": """
int loop(int count) {
    int sum = 0;
    while (count > 0) {
        sum += count;
        count--;
    }
    return sum;
}
""",
        "exports": ["loop"],
        "extra_args": []
    },
    "if_else": {
        "code": """
int if_else(int cond) {
    if (cond) {
        return 10;
    } else {
        return 20;
    }
}
""",
        "exports": ["if_else"],
        "extra_args": []
    },
    "global": {
        "code": """
int g_val = 100;
int get() {
    return g_val;
}
void set() {
    g_val = 50;
}
""",
        "exports": ["get", "set"],
        "extra_args": []
    },
    "mem": {
        "code": """
unsigned char memory_data[8] = {'A', 'B', 'C', 'D', 0, 0, 0, 0};
int load() {
    return *(volatile int*)&memory_data[0];
}
void store() {
    *(volatile int*)&memory_data[4] = 0x12345678;
}
""",
        "exports": ["load", "store"],
        "extra_args": ["-Wl,--global-base=0"]
    },
    "float": {
        "code": """
float f32_add(float a, float b) {
    return a + b;
}
double f64_mul(double a, double b) {
    return a * b;
}
""",
        "exports": ["f32_add", "f64_mul"],
        "extra_args": []
    },
    "block_simple": {
        "code": """
int test() {
    volatile int a = 0;
    do {
        a = 42;
    } while (0);
    return a;
}
""",
        "exports": ["test"],
        "extra_args": []
    },
    "float_i64": {
        "code": """
double div() {
    return 10.0 / 4.0;
}
""",
        "exports": ["div"],
        "extra_args": []
    }
}

def find_tools():
    clang_path = "/opt/homebrew/opt/llvm/bin/clang"
    lld_bin = "/opt/homebrew/Cellar/lld@20/20.1.8/bin"
    
    # 存在確認
    if not os.path.exists(clang_path):
        clang_path = "clang"
    
    return clang_path, lld_bin

def format_bytes(data):
    lines = []
    current_line = []
    for i, b in enumerate(data):
        current_line.append(f"0x{b:02x}")
        if len(current_line) == 8:
            lines.append("        " + ", ".join(current_line) + ",")
            current_line = []
    if current_line:
        lines.append("        " + ", ".join(current_line))
    return "\\n".join(lines)

def main():
    clang_path, lld_bin = find_tools()
    
    # 環境変数PATHにlld_binを追加
    env = os.environ.copy()
    if os.path.exists(lld_bin):
        env["PATH"] = f"{lld_bin}:{env.get('PATH', '')}"

    for name, info in WASM_SOURCES.items():
        with tempfile.NamedTemporaryFile(suffix=".c", mode="w", delete=False) as f:
            f.write(info["code"])
            c_file = f.name
            
        wasm_file = c_file + ".wasm"
        
        cmd = [
            clang_path,
            "-target", "wasm32",
            "-O2",
            "-nostdlib",
            "-Wl,--no-entry",
            "-Wl,--allow-undefined",
            "-o", wasm_file,
            c_file
        ]
        for exp in info["exports"]:
            cmd.append(f"-Wl,--export={exp}")
        cmd.extend(info["extra_args"])
        
        try:
            res = subprocess.run(cmd, env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            with open(wasm_file, "rb") as wf:
                data = wf.read()
            
            print(f"// --- {name} (size: {len(data)}) ---")
            print(format_bytes(data))
            print()
            
        except subprocess.CalledProcessError as e:
            print(f"Error compiling {name}:", file=sys.stderr)
            print(e.stderr.decode(), file=sys.stderr)
        finally:
            if os.path.exists(c_file):
                os.remove(c_file)
            if os.path.exists(wasm_file):
                os.remove(wasm_file)

if __name__ == "__main__":
    main()
