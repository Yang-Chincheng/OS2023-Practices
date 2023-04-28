/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  Blocks are never coalesced or reused.  The size of
 * a block is found at the first aligned word before the block (we need
 * it for realloc).
 *
 * This code is correct and blazingly fast, but very bad usage-wise since
 * it never frees anything.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"


/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
// #define DEBUG
#ifdef DEBUG
static int count = 0;
# define PRINT_MIN 17120
# define PRINT_MAX 17130
# define dbg_printf(...) if (count >= PRINT_MIN && count <= PRINT_MAX) printf(__VA_ARGS__);
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

typedef unsigned long long uint64_t, dword_t;
typedef unsigned int uint32_t, word_t;
typedef signed int int32_t, offset_t;
typedef unsigned char uint8_t, byte_t;
#define bool byte_t
#define true (1)
#define false (0)

#define BYTE_SIZE (sizeof(byte_t))
#define WORD_SIZE (sizeof(word_t))
#define DWORD_SIZE (sizeof(dword_t))

#define MIN_BLK_SIZE (SIZE_T_SIZE*2)

// getter and setter for a word with its pointer
#define GET(ptr) (*(word_t*)(ptr))
#define SET(ptr, val) (*(word_t*)(ptr) = (word_t)(val))
// pointer difference and increcement
#define PTR_DIFF(ptr, base) ( (word_t) ((char*)(ptr) - (char*)(base)) )
#define PTR_INCR(ptr, offset) ( (void*) ( (char*)(ptr) + (offset_t)(offset) ) )

// zip information for block status (last 3 bits) and size (the rest bits)
#define ZIP(size, status) (((size) & ~0x7) | ((status) & 0x7))
#define UNZIP_SIZE(ptr) (GET(ptr) & ~0x7)
#define UNZIP_STAT(ptr) (GET(ptr) & 0x7)

// possible status for a block
#define UNDEF 0
#define USED 1
#define UNUSED 2
#define BORDER 3

static void* heap_base;

// header (4byte) + prev (4byte) + footer (4byte) + next (4byte)
#define META_SIZE (4*WORD_SIZE)

#define HDR_PTR(ptr) PTR_INCR(ptr, -2*WORD_SIZE)
#define SIZE(ptr) UNZIP_SIZE(HDR_PTR(ptr))
#define STAT(ptr) UNZIP_STAT(HDR_PTR(ptr))
#define FTR_PTR(ptr) PTR_INCR(ptr, SIZE(ptr) - 4*WORD_SIZE)
#define NEX_PTR(ptr) PTR_INCR(ptr, -WORD_SIZE)
#define PRE_PTR(ptr) PTR_INCR(ptr, SIZE(ptr) - 3*WORD_SIZE)

#define LIST_NEXT(ptr) PTR_INCR(heap_base, GET(NEX_PTR(ptr)))
#define LIST_PREV(ptr) PTR_INCR(heap_base, GET(PRE_PTR(ptr)))
#define HEAP_NEXT(ptr) PTR_INCR(ptr, SIZE(ptr))
#define HEAP_PREV(ptr) PTR_INCR(ptr, -UNZIP_SIZE( PTR_INCR(ptr, -4*WORD_SIZE) ))

#define MIN_BLK_BITS 8
#define MAX_BLK_BITS 10
#define STP_BLK_BITS 1
#define RANK_NUM ((MAX_BLK_BITS - MIN_BLK_BITS) / STP_BLK_BITS + 1)

static int get_rank(word_t size) {
    for (int i = MIN_BLK_BITS; i < MAX_BLK_BITS; i += STP_BLK_BITS)
        if ((1u << i) >= size) return (i - MIN_BLK_BITS) / STP_BLK_BITS;
    return (MAX_BLK_BITS - MIN_BLK_BITS) / STP_BLK_BITS;
}

#define BDR_OFF ALIGN((RANK_NUM + 2) * WORD_SIZE)
#define PRO_BDR_PTR PTR_INCR(heap_base, BDR_OFF) 
#define EPI_BDR_PTR PTR_INCR(mem_heap_hi(), 1)
#define BUCK(rank) PTR_INCR(heap_base, (rank) * WORD_SIZE)
#define LIST(rank) PTR_INCR(heap_base, GET(BUCK(rank)))
#define RANK(size) (get_rank(size))

static void list_init(void *ptr) {
    SET(NEX_PTR(ptr), BDR_OFF);
    SET(PRE_PTR(ptr), BDR_OFF);
}

static void list_remove(void* entry, int rank) {
    void *prev = LIST_PREV(entry);
    void *next = LIST_NEXT(entry);
    if (STAT(prev) != BORDER) 
        SET(NEX_PTR(prev), PTR_DIFF(next, heap_base));
    if (STAT(next) != BORDER) 
        SET(PRE_PTR(next), PTR_DIFF(prev, heap_base));
    if (entry == LIST(rank)) 
        SET(BUCK(rank), PTR_DIFF(next, heap_base));
}

static void list_push(void *entry, int rank) {
    void *list = LIST(rank);
    if (STAT(list) != BORDER) 
        SET(PRE_PTR(list), PTR_DIFF(entry, heap_base));
    SET(NEX_PTR(entry), PTR_DIFF(list, heap_base));
    SET(PRE_PTR(entry), BDR_OFF);
    SET(BUCK(rank), PTR_DIFF(entry, heap_base));
}

static void* extend(word_t size) {
    void *ptr = mem_sbrk(size);
    if (ptr == NULL) return NULL;
    SET(HDR_PTR(EPI_BDR_PTR), ZIP(0, BORDER));
    return ptr;
}

// split an UNUSED segment by size, push the rest part into free list
static void split(void *ptr, word_t size) {
    word_t orgsize = SIZE(ptr);
    void* newptr = ptr + size;
    SET(HDR_PTR(newptr), ZIP(orgsize - size, UNUSED));
    SET(FTR_PTR(newptr), ZIP(orgsize - size, UNUSED));
    list_push(newptr, RANK(orgsize - size));
    assert(STAT(newptr) == UNUSED);
}

// coalesce two adjacent blocks (both in the free list)
static void coalesce(void *pred_ptr, void *succ_ptr, bool sel) {
    word_t succ_size = SIZE(succ_ptr);
    word_t pred_size = SIZE(pred_ptr);
    if (sel) {
        list_remove(succ_ptr, RANK(succ_size));
        list_remove(pred_ptr, RANK(pred_size));
    }
    word_t size = pred_size + succ_size;
    SET(HDR_PTR(pred_ptr), ZIP(size, UNUSED));
    SET(FTR_PTR(pred_ptr), ZIP(size, UNUSED));
    list_push(pred_ptr, RANK(size));
    assert(STAT(pred_ptr) == UNUSED);
}

// place a block in an UNUSED segment (advancedly removed from free list)
static void place(void* ptr, word_t size, bool sel) {
    word_t orgsize = SIZE(ptr);
    if (!sel) list_remove(ptr, RANK(orgsize));
    if (orgsize - size > META_SIZE) split(ptr, size);
    else size = orgsize;
    SET(HDR_PTR(ptr), ZIP(size, USED));
    SET(FTR_PTR(ptr), ZIP(size, USED));
}

// first fit
static void* find_fit(word_t size, int rank) {
    void *ptr;
    for (ptr = LIST(rank); STAT(ptr) != BORDER; ptr = LIST_NEXT(ptr)) {
        if (SIZE(ptr) >= size) return ptr;
    }
    return NULL;
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void) {
    word_t size = BDR_OFF + 2*WORD_SIZE;
    heap_base = mem_sbrk(size);
    if (heap_base == NULL) return -1;
    for (int i = 0; i < RANK_NUM; ++i) SET(BUCK(i), BDR_OFF);
    SET(HDR_PTR(PRO_BDR_PTR), ZIP(2*WORD_SIZE, BORDER));
    SET(HDR_PTR(EPI_BDR_PTR), ZIP(0, BORDER));
    return 0;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN(size + META_SIZE);

    void *ptr;
    int rank = RANK(size);
    do {
        ptr = find_fit(size, rank);
    } 
    while(ptr == NULL && (rank += STP_BLK_BITS) < RANK_NUM);

    
    dbg_printf("#%d [malloc] size %ld, rank %d, ", count, size, rank);
    if (ptr == NULL) {
        ptr = extend(size);
        dbg_printf("fit NULL(%p)\n", ptr);
        if (ptr == NULL) return NULL;
        SET(HDR_PTR(ptr), ZIP(size, USED));
        SET(FTR_PTR(ptr), ZIP(size, USED));
        list_init(ptr);
    } else {
        dbg_printf("fit %d\n", PTR_DIFF(ptr, heap_base));
        list_remove(ptr, rank);
        place(ptr, size, true);
    }
    assert(STAT(ptr) == USED);
    return ptr;
}

/*
 * free - Deallocate a block, try coalescing its adjacent blocks, 
        and add it into free list.
        We simply check the validity of the provided pointer, 
        and will do nothing if we determine it as invalid.
 */
void free(void *ptr) {
    if (ptr == NULL || STAT(ptr) != USED) return ;
    word_t size = SIZE(ptr);
	list_push(ptr, RANK(size));
    SET(HDR_PTR(ptr), ZIP(size, UNUSED));
    SET(FTR_PTR(ptr), ZIP(size, UNUSED));
    assert(STAT(ptr) == UNUSED);

    dbg_printf("#%d [free] ptr (%p, %d), size %d, rank %d\n", 
        count, ptr, PTR_DIFF(ptr, heap_base), SIZE(ptr), RANK(SIZE(ptr)));

    void *prev = HEAP_PREV(ptr);
    void *next = HEAP_NEXT(ptr);
    
    if (STAT(next) == UNUSED) {
        dbg_printf("coalesce next %d %d\n", 
            PTR_DIFF(ptr, heap_base), PTR_DIFF(next, heap_base));
        coalesce(ptr, next, true);
    }
    if (STAT(prev) == UNUSED) {
        dbg_printf("coalesce prev %d %d\n", 
            PTR_DIFF(prev, heap_base), PTR_DIFF(ptr, heap_base));
        coalesce(prev, ptr, true);
    }
}

/*
 * realloc - Change the size of the block. 
        We prefer coalescing the successive segment to avoid data copying 
        if the successive block is large enough. Otherwise we copy data to 
        a new block, and the old block will be deallocated.
 */
void *realloc(void *oldptr, size_t size) {
    
    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        free(oldptr);
        return NULL;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (oldptr == NULL) {
        return malloc(size);
    }

    word_t orgsize = size;
    word_t oldsize = SIZE(oldptr);
    size = ALIGN(size + META_SIZE);

    /* If the original block is large enough. */
    if (oldsize >= size) return oldptr;

    /* Try to extend segment by coalescing. */
    void *next = HEAP_NEXT(oldptr);
    word_t nexsize;
    if (STAT(next) == UNUSED && oldsize + (nexsize = SIZE(next)) >= size) {
        // 'coalesce' implies both blocks are in the free list
        list_remove(next, RANK(nexsize));
        coalesce(oldptr, next, false);
        // 'place' implies the block is removed from the free list
        place(oldptr, size, false);
        return oldptr;
    }
    
    /* Otherwise we have to allocate a new segment, and copy the original data. */
    void *newptr = malloc(orgsize);
    memcpy(newptr, oldptr, orgsize);
    free(oldptr);
    return newptr;
}

/*
 * calloc - Allocate the block and set it to zero.
 */
void *calloc (size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr = malloc(bytes);
    memset(newptr, 0, bytes);
    return newptr;
}

/*
 * mm_checkheap - There are no bugs in my code, so I don't need to check,
 *      so nah!
 */
void mm_checkheap(int verbose){
    verbose = verbose;
    dbg_printf("\n\n");
	dbg_printf("=============== start ================\n");
    dbg_printf("heap size %ld\n", mem_heapsize());
    dbg_printf("[check buckets]\n");
    for (int i = 0; i < RANK_NUM; ++i) dbg_printf("%d ", GET(BUCK(i)));
    dbg_printf("\n");
    dbg_printf("[check border]\n");
    dbg_printf("prologue %d %s %s\n", PTR_DIFF(PRO_BDR_PTR, heap_base), 
        (STAT(PRO_BDR_PTR) == BORDER)? "yes": "no", 
        (SIZE(PRO_BDR_PTR) == 2*WORD_SIZE)? "yes": "no");
    dbg_printf("epilogue %d %s %s\n", PTR_DIFF(EPI_BDR_PTR, heap_base), 
        (STAT(EPI_BDR_PTR) == BORDER)? "yes": "no",
        (SIZE(EPI_BDR_PTR) == 0)? "yes": "no");
    dbg_printf("[check blocks - heap]\n");
    int cnt1 = 50;
    void *ptr = PTR_INCR(PRO_BDR_PTR, 2*WORD_SIZE);
    for (; STAT(ptr) != BORDER && --cnt1 > 0; ptr = HEAP_NEXT(ptr)) {
        dbg_printf("%d(%d,%d|%d,%d) ", PTR_DIFF(ptr, heap_base), 
            SIZE(ptr), STAT(ptr), UNZIP_SIZE(FTR_PTR(ptr)), UNZIP_STAT(FTR_PTR(ptr)));
    }
    dbg_printf("\n");
    dbg_printf("[check blocks - list]\n");
    for (int i = 0; i < RANK_NUM; ++i) {
        dbg_printf("rank %02d: ", i);
        int cnt2 = 50;
        for (void *ptr = LIST(i); STAT(ptr) != BORDER && --cnt2 > 0; ptr = LIST_NEXT(ptr)) {
            dbg_printf("%d(%d,%d) ", PTR_DIFF(ptr, heap_base), SIZE(ptr), STAT(ptr));
            assert(STAT(ptr) == UNUSED);
        }
        dbg_printf("\n");
    }
    dbg_printf("===============  end  ================\n");
    dbg_printf("\n\n");
}