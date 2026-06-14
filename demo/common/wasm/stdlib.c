#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define HEAP_SIZE 16384

// 8バイトアライメントされた静的ヒープ領域
static uint8_t heap_mem[HEAP_SIZE] __attribute__((aligned(8)));

// フリーリストブロックヘッダー
typedef struct Block {
    size_t size;         // 割り当てられたデータ領域のサイズ（ヘッダーは含まない）
    bool is_free;        // 空きブロックフラグ
    struct Block* next;  // 次のブロックへのポインタ
} Block;

static Block* free_list_head = NULL;

// ヒープ初期化
static void init_heap(void) {
    free_list_head = (Block*)heap_mem;
    free_list_head->size = HEAP_SIZE - sizeof(Block);
    free_list_head->is_free = true;
    free_list_head->next = NULL;
}

// 隣接する空きブロックの結合 (Coalescing)
static void coalesce(void) {
    Block* curr = free_list_head;
    while (curr != NULL) {
        if (curr->is_free) {
            while (curr->next != NULL && curr->next->is_free) {
                curr->size += sizeof(Block) + curr->next->size;
                curr->next = curr->next->next;
            }
        }
        curr = curr->next;
    }
}

// メモリの割り当て (First-Fit)
void* malloc(size_t size) {
    if (size == 0) return NULL;

    // 8バイトアライメントに丸める
    size = (size + 7) & ~7;

    if (free_list_head == NULL) {
        init_heap();
    }

    Block* curr = free_list_head;
    while (curr != NULL) {
        if (curr->is_free && curr->size >= size) {
            // ブロックの分割を検討
            // 分割後の残りサイズが、ヘッダーサイズ + 最小データサイズ(8バイト)以上ある場合に分割する
            if (curr->size >= size + sizeof(Block) + 8) {
                Block* new_block = (Block*)((uint8_t*)curr + sizeof(Block) + size);
                new_block->size = curr->size - size - sizeof(Block);
                new_block->is_free = true;
                new_block->next = curr->next;

                curr->size = size;
                curr->next = new_block;
            }
            curr->is_free = false;
            return (void*)((uint8_t*)curr + sizeof(Block));
        }
        curr = curr->next;
    }

    return NULL; // OOM
}

// メモリの解放
void free(void* ptr) {
    if (ptr == NULL) return;

    Block* block = (Block*)((uint8_t*)ptr - sizeof(Block));
    block->is_free = true;

    // 空きブロックの結合
    coalesce();
}

// メモリの再割り当て
void* realloc(void* ptr, size_t size) {
    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    // 8バイトアライメントに丸める
    size = (size + 7) & ~7;
    Block* block = (Block*)((uint8_t*)ptr - sizeof(Block));

    // 現在のサイズで十分な場合
    if (block->size >= size) {
        // 分割可能であれば分割する
        if (block->size >= size + sizeof(Block) + 8) {
            Block* new_block = (Block*)((uint8_t*)block + sizeof(Block) + size);
            new_block->size = block->size - size - sizeof(Block);
            new_block->is_free = true;
            new_block->next = block->next;

            block->size = size;
            block->next = new_block;

            // 分割により新たな空きブロックができたので結合
            coalesce();
        }
        return ptr;
    }

    // 次のブロックが空きかつ、結合すれば十分なサイズがある場合
    if (block->next != NULL && block->next->is_free && (block->size + sizeof(Block) + block->next->size) >= size) {
        size_t combined_size = block->size + sizeof(Block) + block->next->size;
        Block* next_next = block->next->next;

        if (combined_size >= size + sizeof(Block) + 8) {
            Block* new_block = (Block*)((uint8_t*)block + sizeof(Block) + size);
            new_block->size = combined_size - size - sizeof(Block);
            new_block->is_free = true;
            new_block->next = next_next;

            block->size = size;
            block->next = new_block;
        } else {
            block->size = combined_size;
            block->next = next_next;
        }
        return ptr;
    }

    // 新たに領域を確保してコピーする
    void* new_ptr = malloc(size);
    if (new_ptr == NULL) return NULL;

    size_t copy_size = (block->size < size) ? block->size : size;
    memcpy(new_ptr, ptr, copy_size);

    free(ptr);
    return new_ptr;
}

// 異常終了時のループ
void abort(void) {
    while (1) {}
}

// 文字列長取得
size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

// メモリコピー
void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}
