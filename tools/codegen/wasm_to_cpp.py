#!/usr/bin/env python3
import sys
import os

def main():
    if len(sys.argv) < 4:
        print("Usage: wasm_to_cpp.py <input.wasm> <output.cpp> <output.h>")
        sys.exit(1)

    wasm_path = sys.argv[1]
    cpp_path = sys.argv[2]
    header_path = sys.argv[3]

    # Read WASM binary
    with open(wasm_path, 'rb') as f:
        data = f.read()

    # Generate Header file
    header_name = os.path.basename(header_path)
    with open(header_path, 'w') as f:
        f.write('// Auto-generated header\n')
        f.write('#pragma once\n')
        f.write('#include <cstdint>\n')
        f.write('#include <cstddef>\n\n')
        f.write('extern const uint8_t kHelloWasmBinary[];\n')
        f.write('extern const std::size_t kHelloWasmBinarySize;\n')

    # Generate CPP file
    with open(cpp_path, 'w') as f:
        f.write('// Auto-generated data file\n')
        f.write(f'#include "{header_name}"\n\n')
        f.write('const uint8_t kHelloWasmBinary[] = {\n    ')
        # Format bytes as hex numbers, wrapped nicely
        hex_bytes = [f"0x{b:02x}" for b in data]
        # Chunk into lines of 16 bytes for readability
        for i in range(0, len(hex_bytes), 16):
            chunk = hex_bytes[i:i+16]
            f.write(', '.join(chunk))
            if i + 16 < len(hex_bytes):
                f.write(',\n    ')
        f.write('\n};\n\n')
        f.write(f'const std::size_t kHelloWasmBinarySize = {len(data)};\n')

if __name__ == '__main__':
    main()
