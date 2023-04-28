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
#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
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

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define GET(ptr) (*(size_t*)(ptr))
#define SET(ptr, val) (*(size_t*)(ptr) = (val))

#define MIN_BLK_SIZE (SIZE_T_SIZE*2)

#define ZIP(size, status) (((size) & ~0x7) | ((status) & 0x7))
#define UNZIP_SIZE(zip) ((zip) & ~0x7)
#define UNZIP_STAT(zip) ((zip) & 0x7)

#define USED 1
#define UNUSED 2
#define BORDER 3

#define HDR_PTR(ptr) (((void*)(ptr) - SIZE_T_SIZE))
#define SIZE(ptr) UNZIP_SIZE(GET(HDR_PTR(ptr)))
#define STAT(ptr) UNZIP_STAT(GET(HDR_PTR(ptr)))
#define FTR_PTR(ptr) (((void*)(ptr) + SIZE(ptr) - SIZE_T_SIZE*2))

#define NEXT_PTR(ptr) ((void*)(ptr) + SIZE(ptr))
#define NEXT_SIZE(ptr) SIZE(NEXT_PTR(ptr))
#define NEXT_STAT(ptr) STAT(NEXT_PTR(ptr))
#define PREV_SIZE(ptr) UNZIP_SIZE(GET((void*)(ptr) - SIZE_T_SIZE*2))
#define PREV_STAT(ptr) UNZIP_STAT(GET((void*)(ptr) - SIZE_T_SIZE*2))
#define PREV_PTR(ptr) ((void*)(ptr) - PREV_SIZE(ptr))

static void* heap_base;
static void* block_list;

static void split(void *ptr, int size) {
    int org_size = SIZE(ptr);
    SET(HDR_PTR(ptr), ZIP(size, UNUSED));
    SET(FTR_PTR(ptr), ZIP(size, UNUSED));
    ptr = NEXT_PTR(ptr);
    SET(HDR_PTR(ptr), ZIP(org_size - size, UNUSED));
    SET(FTR_PTR(ptr), ZIP(org_size - size, UNUSED));
}

static void coalesce(void *ptr) {
    int size = SIZE(ptr) + PREV_SIZE(ptr);
    ptr = PREV_PTR(ptr);
    SET(HDR_PTR(ptr), ZIP(size, UNUSED));
    SET(FTR_PTR(ptr), ZIP(size, UNUSED));
}

/*
 * mm_init - Called when a new trace starts.
 */
int mm_init(void) {
    heap_base = mem_sbrk(ALIGN(2 * SIZE_T_SIZE));
    block_list = heap_base + ALIGN(SIZE_T_SIZE);
    SET(heap_base, ZIP(0, BORDER)); // prologue padding
    SET(block_list, ZIP(0, BORDER)); // epilogue padding
    return 0;
}

/*
 * malloc - Allocate a block by incrementing the brk pointer.
 *      Always allocate a block whose size is a multiple of the alignment.
 */
void *malloc(size_t size) {
    if (size == 0) return NULL;
    size = ALIGN(size + 2 * SIZE_T_SIZE);
    void *ptr;
    for (ptr = block_list; STAT(ptr) != BORDER; ptr = NEXT_PTR(ptr)) {
        if (STAT(ptr) == UNUSED && SIZE(ptr) > size) {
            if (SIZE(ptr) > size + MIN_BLK_SIZE)
                split(ptr, size);
            else {
                size = SIZE(ptr);
                SET(HDR_PTR(ptr), ZIP(size, USED));
                SET(FTR_PTR(ptr), ZIP(size, USED));
            }
            return ptr;
        }
    }
    if (mem_sbrk(size) < 0) return NULL;
    SET(HDR_PTR(ptr), ZIP(size, USED));
    SET(FTR_PTR(ptr), ZIP(size, USED));
    SET(NEXT_PTR(ptr), ZIP(0, BORDER));
    return ptr;
}

/*
 * free - We don't know how to free a block.  So we ignore this call.
 *      Computers have big memories; surely it won't be a problem.
 */
void free(void *ptr) {
	if (PREV_STAT(ptr) == UNUSED) {
        coalesce(ptr);
    } else {
        int size = SIZE(ptr);
        SET(HDR_PTR(ptr), ZIP(size, UNUSED));
        SET(FTR_PTR(ptr), ZIP(size, UNUSED));
    }
}

/*
 * realloc - Change the size of the block by mallocing a new block,
 *      copying its data, and freeing the old block.  I'm too lazy
 *      to do better.
 */
void *realloc(void *oldptr, size_t size) {
    
    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        free(oldptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (oldptr == NULL) {
        return malloc(size);
    }

    int orgsize = size;
    size = ALIGN(size + 2 * SIZE_T_SIZE);

    if (NEXT_STAT(oldptr) == UNUSED) coalesce(NEXT_PTR(oldptr));
    
    int oldsize = SIZE(oldptr);
    if (oldsize >= size) {
        if (oldsize > size + MIN_BLK_SIZE) 
            split(oldptr, size);
        else {
            size = oldsize;
            SET(HDR_PTR(size), ZIP(size, USED));
            SET(FTR_PTR(size), ZIP(size, USED));
        } 
        return oldptr;
    } 
    
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
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}

/*
 * mm_checkheap - There are no bugs in my code, so I don't need to check,
 *      so nah!
 */
void mm_checkheap(int verbose){
	void *ptr;
    for (ptr = block_list; STAT(ptr) != BORDER; ptr = NEXT_PTR(ptr)) {
        if (STAT(ptr) == UNUSED) {
            dbg_printf("memleak detected");
            break;
        }
    }
    dbg_printf("no leak detected");
}
