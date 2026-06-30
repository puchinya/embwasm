#include <cstdio>
#include <cstdint>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

#include "embwasm.hpp"
#include "bench_wasm.hpp"

// DWT cycle counter (Cortex-M3)
#define DEMCR       (*(volatile uint32_t*)0xE000EDFC)
#define DWT_CTRL    (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT  (*(volatile uint32_t*)0xE0001004)

static void dwt_init() {
    DEMCR     |= (1U << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL  |= 1U;
}

static inline uint32_t dwt_cycles() { return DWT_CYCCNT; }
static double cycles_to_us(uint32_t c) { return (double)c / 100.0; }

static constexpr size_t kPoolSize = 160 * 1024;
static uint8_t g_pool[kPoolSize] __attribute__((aligned(16)));

struct BenchResult {
    double us_per_call;
    int32_t ret;
};

static BenchResult run_bench(embwasm::WasmEngine& eng,
                              int32_t inst, int32_t fidx,
                              int32_t arg, int warmup, int iters) {
    embwasm::WasmValue a = embwasm::WasmValue::FromI32(arg);
    embwasm::WasmValue r;
    for (int i = 0; i < warmup; i++)
        eng.ExecuteByIndex(inst, fidx, &a, 1, &r, 1);

    uint32_t t0 = dwt_cycles();
    for (int i = 0; i < iters; i++)
        eng.ExecuteByIndex(inst, fidx, &a, 1, &r, 1);
    uint32_t elapsed = dwt_cycles() - t0;

    BenchResult res;
    res.us_per_call = iters > 0 ? cycles_to_us(elapsed) / iters : 0.0;
    res.ret = r.value.i32;
    return res;
}

static void print_row(const char* fn, int arg, int iters, const BenchResult& r) {
    long cps = r.us_per_call > 0.0 ? (long)(1e6 / r.us_per_call) : 0;
    printf("  %-16s(%4d)  ret=%12d  %10.2f us/call  %ld calls/sec  (x%d)\r\n",
           fn, arg, r.ret, r.us_per_call, cps, iters);
}

extern "C" void bench_task_entry(void* /*param*/) {
    dwt_init();

    embwasm::WasmMemoryPool pool;
    pool.Init(g_pool, sizeof(g_pool));

    embwasm::WasmEngine engine;
    engine.Init(pool);

    int32_t inst = engine.LoadModule("bench", 5, kBenchWasmBinary, kBenchWasmBinarySize);
    if (inst < 0) {
        printf("LoadModule failed: %d\r\n", (int)inst);
        vTaskDelete(NULL);
        return;
    }
    printf("Module loaded. Pool: %u / %u bytes\r\n",
           (unsigned)pool.GetUsedBytes(), (unsigned)pool.GetTotalBytes());

    int32_t fib_idx   = engine.GetExportFunctionIndex("bench", 5, "fibonacci",    9);
    int32_t sum_idx   = engine.GetExportFunctionIndex("bench", 5, "sum_loop",     8);
    int32_t prime_idx = engine.GetExportFunctionIndex("bench", 5, "count_primes", 12);
    int32_t mdb_idx   = engine.GetExportFunctionIndex("bench", 5, "mandelbrot",   10);
    int32_t mat_idx   = engine.GetExportFunctionIndex("bench", 5, "matrix_mul",   10);
    int32_t crc_idx   = engine.GetExportFunctionIndex("bench", 5, "crc32",         5);

    printf("--- simple ---\r\n");
    print_row("fibonacci",     30,  100, run_bench(engine, inst, fib_idx,    30,  1, 100));
    print_row("sum_loop",    1000,  100, run_bench(engine, inst, sum_idx,  1000,  1, 100));
    print_row("count_primes", 500,   20, run_bench(engine, inst, prime_idx, 500,  1,  20));
    printf("--- complex ---\r\n");
    print_row("mandelbrot",    16,    5, run_bench(engine, inst, mdb_idx,    16,  1,   5));
    print_row("matrix_mul",   100,   50, run_bench(engine, inst, mat_idx,   100,  1,  50));
    print_row("crc32",        500,   20, run_bench(engine, inst, crc_idx,   500,  1,  20));

    printf("Max call depth: %u  Max VM stack: %u\r\n",
           (unsigned)engine.GetMaxCallStackDepth(),
           (unsigned)engine.GetMaxStackDepth());

    engine.Deinit();
    pool.Deinit();
    printf("Benchmark done.\r\n");
    // Signal Renode to quit via watchpoint on magic address
    *(volatile uint32_t*)0x40099000 = 0xDEADBEEFU;
    vTaskDelete(NULL);
}
