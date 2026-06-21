#include "embwasm_hostmodule_stdio.hpp"
#include <cstdio>
#include <cstring>

namespace embwasm {
namespace hostmodules {
namespace stdio {

WasmResult Printf(
    WasmEngine& engine,
    const char* fmt,
    uint32_t fmt_len,
    const int32_t* args,
    uint32_t args_len) noexcept
{
    uint8_t* mem_base = engine.GetLinearMemory();
    size_t mem_size = engine.GetLinearMemorySize();

    // 境界チェック
    ptrdiff_t fmt_off = reinterpret_cast<const uint8_t*>(fmt) - mem_base;
    ptrdiff_t args_off = reinterpret_cast<const uint8_t*>(args) - mem_base;
    if (fmt_off < 0 || static_cast<size_t>(fmt_off) + fmt_len > mem_size ||
        (args_len > 0 && (args_off < 0 || static_cast<size_t>(args_off) + args_len * sizeof(int32_t) > mem_size))) {
        return WasmResult::kErrorExecuteRuntimeError;
    }

    uint32_t arg_idx = 0;
    for (uint32_t i = 0; i < fmt_len; ) {
        if (fmt[i] == '%') {
            if (i + 1 >= fmt_len) {
                std::putchar('%');
                i++;
                continue;
            }
            if (fmt[i + 1] == '%') {
                std::putchar('%');
                i += 2;
                continue;
            }

            // フラグ・幅・精度・修飾子をスキャン
            uint32_t j = i + 1;
            while (j < fmt_len && (fmt[j] == '-' || fmt[j] == '+' || fmt[j] == ' ' || fmt[j] == '#' || fmt[j] == '0')) j++;
            while (j < fmt_len && (fmt[j] >= '0' && fmt[j] <= '9')) j++;
            if (j < fmt_len && fmt[j] == '.') {
                j++;
                while (j < fmt_len && (fmt[j] >= '0' && fmt[j] <= '9')) j++;
            }
            while (j < fmt_len && (fmt[j] == 'h' || fmt[j] == 'l' || fmt[j] == 'z' || fmt[j] == 'j' || fmt[j] == 't')) j++;

            if (j >= fmt_len) {
                std::putchar('%');
                i++;
                continue;
            }

            char specifier = fmt[j];

            if (specifier == 's') {
                if (arg_idx >= args_len) { i = j + 1; continue; }
                int32_t str_offset = args[arg_idx++];
                if (str_offset < 0 || static_cast<size_t>(str_offset) >= mem_size) {
                    std::printf("(null)");
                } else {
                    const char* s = reinterpret_cast<const char*>(mem_base + str_offset);
                    while (static_cast<size_t>(str_offset) < mem_size && *s) {
                        std::putchar(*s++);
                        str_offset++;
                    }
                }
                i = j + 1;
                continue;
            }

            if (specifier == 'd' || specifier == 'i' || specifier == 'u' ||
                specifier == 'x' || specifier == 'X' || specifier == 'c' || specifier == 'p') {
                if (arg_idx >= args_len) { i = j + 1; continue; }
                int32_t val = args[arg_idx++];

                char sub_fmt[32];
                size_t sub_len = j - i + 1;
                if (sub_len < sizeof(sub_fmt)) {
                    std::memcpy(sub_fmt, &fmt[i], sub_len);
                    sub_fmt[sub_len] = '\0';
                    if (specifier == 'p')
                        std::printf(sub_fmt, reinterpret_cast<void*>(static_cast<uintptr_t>(val)));
                    else if (specifier == 'd' || specifier == 'i')
                        std::printf(sub_fmt, static_cast<int>(val));
                    else
                        std::printf(sub_fmt, static_cast<unsigned int>(val));
                } else {
                    std::printf("%d", static_cast<int>(val));
                }
                i = j + 1;
                continue;
            }

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
    const char* s,
    uint32_t s_len,
    int32_t& out_result) noexcept
{
    uint8_t* mem_base = engine.GetLinearMemory();
    size_t mem_size = engine.GetLinearMemorySize();

    ptrdiff_t offset = reinterpret_cast<const uint8_t*>(s) - mem_base;
    if (offset < 0 || static_cast<size_t>(offset) + s_len > mem_size) {
        return WasmResult::kErrorExecuteRuntimeError;
    }

    for (uint32_t i = 0; i < s_len; ++i) {
        std::putchar(s[i]);
    }
    std::putchar('\n');
    std::fflush(stdout);

    out_result = static_cast<int32_t>(s_len + 1);
    return WasmResult::kOk;
}

} // namespace stdio
} // namespace hostmodules
} // namespace embwasm
