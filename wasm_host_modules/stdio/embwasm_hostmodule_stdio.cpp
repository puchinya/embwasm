#include "embwasm_hostmodule_stdio.hpp"
#include <cstdio>
#include <cstring>

namespace embwasm {
namespace hostmodules {
namespace stdio {

WasmResult Printf(
    WasmEngine& engine,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept
{
    (void)results; (void)result_count;
    // fmt (string) -> ptr, len (2 arguments)
    // args (list<s32>) -> ptr, len (2 arguments)
    if (arg_count < 4 || 
        args[0].type != WasmType::kI32 || args[1].type != WasmType::kI32 ||
        args[2].type != WasmType::kI32 || args[3].type != WasmType::kI32) 
    {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t fmt_ptr = args[0].value.i32;
    uint32_t fmt_len = args[1].value.i32;
    uint32_t list_ptr = args[2].value.i32;
    uint32_t list_len = args[3].value.i32;

    uint8_t* mem = engine.GetLinearMemory();
    size_t mem_size = engine.GetLinearMemorySize();

    // Bounds checking
    if (fmt_ptr + fmt_len > mem_size || list_ptr + list_len * sizeof(int32_t) > mem_size) {
        return WasmResult::kErrorRuntimeError;
    }

    const char* fmt = reinterpret_cast<const char*>(mem + fmt_ptr);
    const int32_t* format_args = reinterpret_cast<const int32_t*>(mem + list_ptr);

    uint32_t arg_idx = 0;
    for (uint32_t i = 0; i < fmt_len; ) {
        if (fmt[i] == '%') {
            if (i + 1 >= fmt_len) {
                std::putchar('%');
                i++;
                continue;
            }
            if (fmt[i+1] == '%') {
                std::putchar('%');
                i += 2;
                continue;
            }

            // Parse flags, width, precision, modifiers
            uint32_t j = i + 1;
            while (j < fmt_len && (fmt[j] == '-' || fmt[j] == '+' || fmt[j] == ' ' || fmt[j] == '#' || fmt[j] == '0')) {
                j++;
            }
            while (j < fmt_len && (fmt[j] >= '0' && fmt[j] <= '9')) {
                j++;
            }
            if (j < fmt_len && fmt[j] == '.') {
                j++;
                while (j < fmt_len && (fmt[j] >= '0' && fmt[j] <= '9')) {
                    j++;
                }
            }
            while (j < fmt_len && (fmt[j] == 'h' || fmt[j] == 'l' || fmt[j] == 'z' || fmt[j] == 'j' || fmt[j] == 't')) {
                j++;
            }

            if (j >= fmt_len) {
                std::putchar('%');
                i++;
                continue;
            }

            char specifier = fmt[j];

            // String parameter (%s)
            if (specifier == 's') {
                if (arg_idx >= list_len) {
                    i = j + 1;
                    continue;
                }
                int32_t str_offset = format_args[arg_idx++];
                if (str_offset < 0 || static_cast<size_t>(str_offset) >= mem_size) {
                    std::printf("(null)");
                } else {
                    const char* s = reinterpret_cast<const char*>(mem + str_offset);
                    // Print characters safely within linear memory bounds
                    while (static_cast<size_t>(str_offset) < mem_size && *s) {
                        std::putchar(*s);
                        s++;
                        str_offset++;
                    }
                }
                i = j + 1;
                continue;
            }

            // Integer formats (%d, %i, %u, %x, %X, %c, %p)
            if (specifier == 'd' || specifier == 'i' || specifier == 'u' || specifier == 'x' || specifier == 'X' || specifier == 'c' || specifier == 'p') {
                if (arg_idx >= list_len) {
                    i = j + 1;
                    continue;
                }
                int32_t val = format_args[arg_idx++];

                char sub_fmt[32];
                size_t sub_len = j - i + 1;
                if (sub_len < sizeof(sub_fmt)) {
                    std::memcpy(sub_fmt, &fmt[i], sub_len);
                    sub_fmt[sub_len] = '\0';

                    if (specifier == 'p') {
                        std::printf(sub_fmt, reinterpret_cast<void*>(static_cast<uintptr_t>(val)));
                    } else if (specifier == 'd' || specifier == 'i') {
                        std::printf(sub_fmt, static_cast<int>(val));
                    } else {
                        std::printf(sub_fmt, static_cast<unsigned int>(val));
                    }
                } else {
                    std::printf("%d", static_cast<int>(val));
                }
                i = j + 1;
                continue;
            }

            // Ignore unsupported specifiers (like float/double %f, %g)
            std::putchar('%');
            i++;
        } else {
            std::putchar(fmt[i]);
            i++;
        }
    }

    std::fflush(stdout);
    return WasmResult::kOk;
}

WasmResult Puts(
    WasmEngine& engine,
    const WasmValue* args,
    uint32_t arg_count,
    WasmValue* results,
    uint32_t result_count) noexcept
{
    // s (string) -> ptr, len (2 arguments)
    if (arg_count < 2 || args[0].type != WasmType::kI32 || args[1].type != WasmType::kI32) {
        return WasmResult::kErrorRuntimeError;
    }

    uint32_t str_ptr = args[0].value.i32;
    uint32_t str_len = args[1].value.i32;

    uint8_t* mem = engine.GetLinearMemory();
    size_t mem_size = engine.GetLinearMemorySize();

    if (str_ptr + str_len > mem_size) {
        return WasmResult::kErrorRuntimeError;
    }

    const char* s = reinterpret_cast<const char*>(mem + str_ptr);

    for (uint32_t i = 0; i < str_len; ++i) {
        std::putchar(s[i]);
    }
    std::putchar('\n');
    std::fflush(stdout);

    if (result_count >= 1) {
        results[0].type = WasmType::kI32;
        results[0].value.i32 = static_cast<int32_t>(str_len + 1);
    }

    return WasmResult::kOk;
}

} // namespace stdio
} // namespace hostmodules
} // namespace embwasm
