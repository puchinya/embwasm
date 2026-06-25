#include <stdint.h>

// Iterative Fibonacci: fib(n)
__attribute__((export_name("fibonacci")))
int32_t fibonacci(int32_t n) {
    if (n <= 1) return n;
    int32_t a = 0, b = 1;
    for (int32_t i = 2; i <= n; i++) {
        int32_t c = a + b;
        a = b;
        b = c;
    }
    return b;
}

// Sum 1..n
__attribute__((export_name("sum_loop")))
int32_t sum_loop(int32_t n) {
    int32_t sum = 0;
    for (int32_t i = 1; i <= n; i++) {
        sum += i;
    }
    return sum;
}

// Count primes up to n (trial division)
__attribute__((export_name("count_primes")))
int32_t count_primes(int32_t n) {
    int32_t count = 0;
    for (int32_t i = 2; i <= n; i++) {
        int32_t is_prime = 1;
        for (int32_t j = 2; j * j <= i; j++) {
            if (i % j == 0) {
                is_prime = 0;
                break;
            }
        }
        if (is_prime) count++;
    }
    return count;
}

// Mandelbrot set: grid_size x grid_size pixels, max 64 iterations per pixel
// Returns total escape iteration count across all pixels (f32 arithmetic)
__attribute__((export_name("mandelbrot")))
int32_t mandelbrot(int32_t grid_size) {
    int32_t total = 0;
    for (int32_t py = 0; py < grid_size; py++) {
        for (int32_t px = 0; px < grid_size; px++) {
            float cx = -2.0f + 3.0f * (float)px / (float)grid_size;
            float cy = -1.0f + 2.0f * (float)py / (float)grid_size;
            float x = 0.0f;
            float y = 0.0f;
            int32_t iter = 0;
            for (; iter < 64; iter++) {
                float x2 = x * x;
                float y2 = y * y;
                if (x2 + y2 >= 4.0f) break;
                float xn = x2 - y2 + cx;
                y = 2.0f * x * y + cy;
                x = xn;
            }
            total += iter;
        }
    }
    return total;
}

// 4x4 int32 matrix multiply (global arrays in WASM linear memory), repeated n times
static int32_t g_mat_a[16];
static int32_t g_mat_b[16];
static int32_t g_mat_c[16];

__attribute__((export_name("matrix_mul")))
int32_t matrix_mul(int32_t n) {
    for (int32_t i = 0; i < 16; i++) {
        g_mat_a[i] = i + 1;
        g_mat_b[i] = (i * 3 + 7) & 0xFF;
    }
    int32_t checksum = 0;
    for (int32_t iter = 0; iter < n; iter++) {
        for (int32_t r = 0; r < 4; r++) {
            for (int32_t c = 0; c < 4; c++) {
                int32_t sum = 0;
                for (int32_t k = 0; k < 4; k++) {
                    sum += g_mat_a[r * 4 + k] * g_mat_b[k * 4 + c];
                }
                g_mat_c[r * 4 + c] = sum;
            }
        }
        checksum += g_mat_c[0];
    }
    return checksum;
}

// CRC32 over n synthetic bytes (branchless bitwise, polynomial 0xEDB88320)
__attribute__((export_name("crc32")))
int32_t crc32(int32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (int32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)(i & 0xFF);
        for (int32_t bit = 0; bit < 8; bit++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return (int32_t)(crc ^ 0xFFFFFFFFu);
}
