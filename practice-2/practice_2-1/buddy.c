#include "buddy.h"

// #include <stdio.h>

typedef unsigned long int size_t; 
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef char bool;
#define true 1
#define false 0

#define NULL ((void *)0)

#define PAGE_BITS (12u)
#define PAGE_SIZE (1u << PAGE_BITS)

#define MIN_ALLOC_BITS 12
#define MAX_ALLOC_BITS 31
#define MIN_ALLOC_SIZE ((size_t) 1 << MIN_ALLOC_BITS)
#define MAX_ALLOC_SIZE ((size_t) 1 << MAX_ALLOC_BITS)

#define MAX_RANK_NUM 20u
#define MAX_PAGE_NUM (1u << MAX_RANK_NUM)

#define UNDEF 0
#define UNUSED 1
#define USED 2 

typedef struct list_t list_t;

struct list_t {
    uint8_t status;
    uint8_t rank;
    uint32_t index; 
    list_t *prev, *next;
};

#define LIST_INITIALIZER ((list_t){UNDEF, 0, 0, NULL, NULL})

#define list_remove(list, node) do { \
    if ((node) != NULL) { \
        if ((node)->next != NULL) (node)->next->prev = (node)->prev; \
        if ((node)->prev != NULL) (node)->prev->next = (node)->next; \
    } \
    if ((node) == (list)) (list) = (node)->next; \
} while(0) 

#define list_pop(list) do { \
    if ((list) == NULL) return NULL; \
    if ((list)->next != NULL) (list)->next->prev = NULL; \
    (list) = (list)->next; \
} while(0)

#define list_push(list, node) do { \
    (node)->prev = NULL; \
    (node)->next = list; \
    if ((list) != NULL) (list)->prev = (node); \
    (list) = (node); \
} while(0)

static void *base_ptr;
static uint32_t rank_num;
#define page_num (1u << (rank_num - 1))

static uint32_t count[MAX_RANK_NUM + 1];
static list_t* bucket[MAX_RANK_NUM + 1];
static list_t meta[MAX_PAGE_NUM];

static uint8_t _log2(uint32_t num) {
    if (num == 0) return -1;
    uint8_t ret = 0;
    for (; num > 1; num >>= 1) ret++;
    return ret;
}

#define ROOT 0
#define PARENT(node) ((node - 1) / 2)
#define CHILD(node, sel) ((node) * 2 + (sel) + 1)
#define BUDDY(node) ((((node) - 1) ^ 1) + 1)

static uint32_t block_to_page(uint32_t block, uint8_t rank) {
    return (block - (1 << (rank_num - rank)) + 1) << (rank - 1);
}

static void* block_to_ptr(uint32_t block, uint8_t rank) {
    return base_ptr + block_to_page(block, rank) * PAGE_SIZE;
}

static bool is_valid_ptr(void *ptr) {
    if (ptr < base_ptr) return false;
    if ((ptr - base_ptr) % PAGE_SIZE != 0) return false;
    return (ptr - base_ptr) / PAGE_SIZE <= page_num;
}

static uint32_t ptr_to_page(void *ptr) {
    return (uint32_t)(ptr - base_ptr) / PAGE_SIZE;
}

int init_page(void *p, int pgcount) {
    base_ptr = p;
    rank_num = _log2(pgcount) + 1;
// printf("[dbg] rank number %d, page_num %d\n", rank_num, page_num);
    for (int i = 1; i <= rank_num; ++i) bucket[i] = NULL;
    for (int i = 1; i <= rank_num; ++i) count[i] = 0;
    for (int i = 0; i < pgcount; ++i) meta[i] = LIST_INITIALIZER;
    meta[0].status = UNUSED;
    meta[0].rank = rank_num;
    meta[0].index = ROOT;
    list_push(bucket[rank_num], &meta[0]);
    count[rank_num]++;
    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > rank_num) return (void*)-EINVAL;
    
    uint8_t unused_rank = rank;
    while(unused_rank <= rank_num) {
        if (bucket[unused_rank] != NULL) break;
        unused_rank++;
    }
    if (unused_rank > rank_num) return (void*)-ENOSPC;
    
    uint8_t split_rank = unused_rank;
    while(split_rank > rank) {
        list_t *block = bucket[split_rank];
        list_pop(bucket[split_rank]);
        block->status = UNDEF;
        count[split_rank]--;
        
        split_rank--;
        uint32_t rchild = CHILD(block->index, 1);
        uint32_t lchild = CHILD(block->index, 0);
        list_t *rmeta = &meta[block_to_page(rchild, split_rank)];
        list_t *lmeta = &meta[block_to_page(lchild, split_rank)];
        
        *rmeta = (list_t) {UNUSED, split_rank, rchild, NULL, NULL};
        *lmeta = (list_t) {UNUSED, split_rank, lchild, NULL, NULL};
        list_push(bucket[split_rank], rmeta); 
        list_push(bucket[split_rank], lmeta); 
        count[split_rank] += 2;
    }

    list_t *block = bucket[rank];
    list_pop(bucket[rank]);
    block->status = USED;
    count[rank]--;

    return block_to_ptr(block->index, rank);
}

int return_pages(void *p) {
    list_t *node, *buddy;
// printf("[dbg] offset 0x%x, validity %d\n", p - base_ptr, is_valid_ptr(p));
    if (!is_valid_ptr(p)) return -EINVAL;
    node = &meta[ptr_to_page(p)];
// printf("[dbg] page %d, status %d\n", ptr_to_page(p), meta->status);
    if (node->status != USED) return -EINVAL;
    
    uint8_t rank = node->rank;
    uint32_t index = node->index;
// printf("[dbg] page %d, rank %d\n", ptr_to_page(p), rank);
// printf("[dbg] page %d, index %d\n", ptr_to_page(p), index);
    while (rank < rank_num) {
        node = &meta[block_to_page(index, rank)];
        buddy = &meta[block_to_page(BUDDY(index), rank)];
// printf("[dbg] node_page %d, buddy_page %d\n", 
    // block_to_page(index, rank), block_to_page(BUDDY(index), rank));
        
        if (buddy->rank != rank || buddy->status != UNUSED) break;
        node->status = buddy->status = UNDEF;
        list_remove(bucket[rank], buddy);
        count[rank]--;

        rank++;
        index = PARENT(index);
    }

    node = &meta[block_to_page(index, rank)];
    node->status = UNUSED;
    node->rank = rank;
    node->index = index;
    list_push(bucket[rank], node);
    count[rank]++;

    return OK;
}

int query_ranks(void *p) {
    if (!is_valid_ptr(p)) return -EINVAL;
    list_t *node = &meta[ptr_to_page(p)];
    return node->status != UNDEF? node->rank: -EINVAL;
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > rank_num) return -EINVAL;
    return count[rank];
}
