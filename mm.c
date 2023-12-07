/*
 * mm.c
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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

/* LISTMAXN是一共大约会有多少个分离链表 */
#define LISTMAXN 20

/* single word (4) or double word (8) alignment */
#define WSIZE 4
#define DSIZE 8
#define ALIGNMENT 8
#define CHUNKSIZE (1<<12)

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* 用来把一个大小(size)和是否分配(alloc)打包在一起的宏 */
#define PACK(size, alloc) ((size) | (alloc))

/* 从一个指针中读和写的宏 */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* 得到块的大小和是否分配的宏 */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* 给定一个块指针，来得到Header和Footer的宏 */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* 给定一个块指针，求出下一个块和前一个块指针 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
// 为什么还要减去DSIZE?是因为有自己块的Header和上一个块的Footer

/* 这里是用来定义全局的变量的地方 */
static char *heap_listp = 0;  // 指向Prologue的指针

/* 分离链表的还要把大的放在前面，因为这样找到第一个比自己大的可能性要好一点 */
static char *segragated_list[LISTMAXN]; // 指向每个链表的指针


/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    // 根据内存的模型，我们先要初始化一个堆，这个堆的大小是2*DSIZE
    // 其中一开始有一个头部，一个尾部，但是这样只有3*WSIZE
    // 于是在最开始加入一个Padding
    // You must reinitialize all of your global pointers in this function.
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0); // Alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // Prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // Prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // Epilogue header
    heap_listp += (2*WSIZE);

    // 然后我们把这个堆扩展到最大
    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    return NULL;
}

/*
 * free
 */
void free (void *ptr) {
    if(!ptr) return;
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
    return NULL;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
    return NULL;
}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int lineno) {
}

static void *extend_heap(size_t size) {
    void *bp;
    size = ALIGN(size);

    /* 利用系统调用sbrk来把堆开大 */
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;
    
    // 现在我们有一个很大的空闲块，对他的Header和Footer赋值
    PUT(HDRP(bp), PACK(size, 0)); // Free block header
    // 这里我认为因为bp实际上是指向的旧Epilogue的下一个WSIZE的地方，
    // 所以在HDRP(bp)里面得到的是旧Epilogue的Header
    // 这样就覆盖上了

    PUT(FTRP(bp), PACK(size, 0)); // Free block footer
    /* 经过这样的扩展之后我们应该有一个新的Epilogue */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // New epilogue header

    /* 而且现在这个空闲块可以被加入一个空闲链表中去了 */
    segragated_list_insert(bp, size);

    /* 而且我们要使用什么样的合并策略呢？先使用立即合并 */
    return coalesce(bp);
}