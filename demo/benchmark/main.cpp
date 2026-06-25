#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include "embwasm.hpp"
#include "bench_wasm.hpp"

namespace {
alignas(16) uint8_t g_wasm_pool_buf[embwasm::kMemoryPoolSize];
}

struct BenchResult {
    double per_call_us;
    int32_t ret_val;
};

static BenchResult RunByName(embwasm::WasmEngine& engine,
                             const char* mod_name, std::size_t mod_len,
                             const char* fn_name,  std::size_t fn_len,
                             int32_t arg_val, int warmup, int iterations) {
    embwasm::WasmValue arg = embwasm::WasmValue::FromI32(arg_val);
    embwasm::WasmValue result;

    for (int i = 0; i < warmup; i++) {
        engine.Execute(mod_name, mod_len, fn_name, fn_len, &arg, 1, &result, 1);
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        engine.Execute(mod_name, mod_len, fn_name, fn_len, &arg, 1, &result, 1);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double total_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    BenchResult r;
    r.per_call_us = total_us / iterations;
    r.ret_val     = result.value.i32;
    return r;
}

static BenchResult RunByIndex(embwasm::WasmEngine& engine,
                              int32_t instance_id, int32_t func_idx,
                              int32_t arg_val, int warmup, int iterations) {
    embwasm::WasmValue arg = embwasm::WasmValue::FromI32(arg_val);
    embwasm::WasmValue result;

    for (int i = 0; i < warmup; i++) {
        engine.ExecuteByIndex(instance_id, func_idx, &arg, 1, &result, 1);
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        engine.ExecuteByIndex(instance_id, func_idx, &arg, 1, &result, 1);
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    double total_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    BenchResult r;
    r.per_call_us = total_us / iterations;
    r.ret_val     = result.value.i32;
    return r;
}

static void PrintRow(const char* variant, const char* fn, int32_t arg,
                     int iters, const BenchResult& r) {
    long cps = (r.per_call_us > 0.0) ? static_cast<long>(1e6 / r.per_call_us) : 0;
    std::cout << "  [" << variant << "] " << fn << "(" << arg << ")"
              << "  ret=" << r.ret_val
              << "  " << r.per_call_us << " us/call"
              << "  " << cps << " calls/sec"
              << "  (x" << iters << ")"
              << std::endl;
}

int main() {
    std::cout << "=== embwasm Benchmark ===" << std::endl;
    std::cout << "Memory Pool: " << embwasm::kMemoryPoolSize << " bytes" << std::endl;

    embwasm::WasmMemoryPool pool;
    pool.Init(g_wasm_pool_buf, sizeof(g_wasm_pool_buf));

    embwasm::WasmEngine engine;
    engine.Init(pool);

    int32_t instance_id = engine.LoadModule("bench", 5, kBenchWasmBinary, kBenchWasmBinarySize);
    if (instance_id < 0) {
        std::cerr << "LoadModule failed: " << instance_id << std::endl;
        return 1;
    }

    std::cout << "Memory used after load: "
              << pool.GetUsedBytes() << " / " << pool.GetTotalBytes() << " bytes\n"
              << std::endl;

    // ExecuteByIndex 用に関数インデックスを事前解決
    int32_t fib_idx    = engine.GetExportFunctionIndex("bench", 5, "fibonacci",    9);
    int32_t sum_idx    = engine.GetExportFunctionIndex("bench", 5, "sum_loop",     8);
    int32_t prime_idx  = engine.GetExportFunctionIndex("bench", 5, "count_primes", 12);
    int32_t mdb_idx    = engine.GetExportFunctionIndex("bench", 5, "mandelbrot",   10);
    int32_t matmul_idx = engine.GetExportFunctionIndex("bench", 5, "matrix_mul",   10);
    int32_t crc_idx    = engine.GetExportFunctionIndex("bench", 5, "crc32",         5);

    if (fib_idx < 0 || sum_idx < 0 || prime_idx < 0 ||
        mdb_idx < 0 || matmul_idx < 0 || crc_idx < 0) {
        std::cerr << "Function not found." << std::endl;
        return 1;
    }

    BenchResult rn, ri;

    std::cout << "--- simple ---\n" << std::endl;

    std::cout << "[fibonacci(30)]  iterative fib" << std::endl;
    rn = RunByName (engine, "bench", 5, "fibonacci", 9, 30, 5, 20000);
    ri = RunByIndex(engine, instance_id, fib_idx,    30, 5, 20000);
    PrintRow("Execute      ", "fibonacci", 30, 20000, rn);
    PrintRow("ExecuteByIndex", "fibonacci", 30, 20000, ri);

    std::cout << "\n[sum_loop(1000)]  sum 1..n" << std::endl;
    rn = RunByName (engine, "bench", 5, "sum_loop", 8, 1000, 5, 5000);
    ri = RunByIndex(engine, instance_id, sum_idx,   1000, 5, 5000);
    PrintRow("Execute      ", "sum_loop", 1000, 5000, rn);
    PrintRow("ExecuteByIndex", "sum_loop", 1000, 5000, ri);

    std::cout << "\n[count_primes(500)]  trial division" << std::endl;
    rn = RunByName (engine, "bench", 5, "count_primes", 12, 500, 3, 500);
    ri = RunByIndex(engine, instance_id, prime_idx,     500, 3, 500);
    PrintRow("Execute      ", "count_primes", 500, 500, rn);
    PrintRow("ExecuteByIndex", "count_primes", 500, 500, ri);

    std::cout << "\n--- complex ---\n" << std::endl;

    std::cout << "[mandelbrot(16)]  16x16 grid, max 64 iter/pixel, f32" << std::endl;
    rn = RunByName (engine, "bench", 5, "mandelbrot", 10, 16, 3, 200);
    ri = RunByIndex(engine, instance_id, mdb_idx,     16, 3, 200);
    PrintRow("Execute      ", "mandelbrot", 16, 200, rn);
    PrintRow("ExecuteByIndex", "mandelbrot", 16, 200, ri);

    std::cout << "\n[matrix_mul(100)]  4x4 i32 matmul x100, linear memory" << std::endl;
    rn = RunByName (engine, "bench", 5, "matrix_mul", 10, 100, 3, 1000);
    ri = RunByIndex(engine, instance_id, matmul_idx,  100, 3, 1000);
    PrintRow("Execute      ", "matrix_mul", 100, 1000, rn);
    PrintRow("ExecuteByIndex", "matrix_mul", 100, 1000, ri);

    std::cout << "\n[crc32(500)]  bitwise CRC32 over 500 bytes" << std::endl;
    rn = RunByName (engine, "bench", 5, "crc32", 5, 500, 3, 1000);
    ri = RunByIndex(engine, instance_id, crc_idx,  500, 3, 1000);
    PrintRow("Execute      ", "crc32", 500, 1000, rn);
    PrintRow("ExecuteByIndex", "crc32", 500, 1000, ri);

    std::cout << "\nMax call stack depth : " << engine.GetMaxCallStackDepth() << std::endl;
    std::cout << "Max VM   stack depth : " << engine.GetMaxStackDepth()     << std::endl;

    engine.Deinit();
    pool.Deinit();
    return 0;
}
