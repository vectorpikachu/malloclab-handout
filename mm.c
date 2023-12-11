/* 
 * Simple, 32-bit and 64-bit clean allocator based on implicit free
 * lists, first-fit placement, and boundary tag coalescing, as described
 * in the CS:APP3e text. Blocks must be aligned to doubleword (8 byte) 
 * boundaries. Minimum block size is 16 bytes. 
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/*
 * If NEXT_FIT defined use next fit search, else use first-fit search 
 */
#define NEXT_FITx
#define SEGREGATED
#define DEFFERED_COALESCEx

/* 我们现在试图加入一些显式的空闲链表 */
/* 我们的pred和succ还是不要存储地址，而是存储和base_ptr相差的大小，*/
/* bias = (now_ptr - base_ptr)/WSIZE */
/* 如果bias=0,说明存储的是空指针 */
/* 把bias转换为指针，就是now_ptr = base_ptr + WSIZE * bias */
static void *base_ptr = NULL;
static void *free_list = NULL;
static void *segregated_list = NULL;

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ 
#define DSIZE       8       /* Double word size (bytes) */
#define ALIGNMENT   8       /* Single word (4) or double word (8) alignment */
#define CHUNKSIZE  16384  /* Extend heap by this amount (bytes) */  
#define LISTMAXN   11      /* 最多有11个链表 */

#define MAX(x, y) ((x) > (y)? (x) : (y))  

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) 

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   
#define GET_ALLOC(p) (GET(p) & 0x1)                    

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) 

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) 

/* 一个空闲块的前面分别是pred和succ指针的bias */
#define PRED_BIAS(bp)  (*(unsigned int *)(bp))
#define SUCC_BIAS(bp)  (*(unsigned int *)((char *)(bp) + WSIZE))

/* 给一个空闲块的pred和succ分别赋值 */
#define SET_PRED(bp, bias)  (PRED_BIAS(bp) = (bias))
#define SET_SUCC(bp, bias)  (SUCC_BIAS(bp) = (bias))

/* Global variables */
static char *heap_listp = 0;  /* Pointer to first block */  
#ifdef NEXT_FIT
static char *rover;           /* Next fit rover */
#endif

/* Function prototypes for internal helper routines */
static void *extend_heap(size_t words);
static void place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
static void free_list_insert(void *bp); /* 向空闲块链表里面插入新的空闲块 */
static void free_list_delete(void *bp); /* 从空闲块链表里面删除一个空闲块 */

static void *segregated_searh(size_t asize);
static void segregated_insert(void *bp);
static void segregated_delete(void *bp);

static int in_heap(const void *p);
static int aligned(const void *p);

static size_t get_bias(void *bp) {
    if (bp == NULL)
        return 0;
    return (unsigned int)((char *)bp - (char *)base_ptr)/WSIZE;
}
static void *get_ptr(size_t bias) {
    if (bias == 0)
        return NULL;
    return (void *)((char *)base_ptr + bias * WSIZE);
}


/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void) 
{
    /* Create the initial empty heap */

    if ((heap_listp = mem_sbrk(14*WSIZE)) == (void *)-1) 
        return -1;
    PUT(heap_listp, 0); /* 大小块为<=32的 */
    PUT(heap_listp + (1*WSIZE), 0); /* 大小块为33<=x<=64的 */
    PUT(heap_listp + (2*WSIZE), 0); /* 大小块为65<=x<=128的 */
    PUT(heap_listp + (3*WSIZE), 0); /* 大小块为129<=x<=256的 */
    PUT(heap_listp + (4*WSIZE), 0); /* 大小块为257<=x<=512的 */
    PUT(heap_listp + (5*WSIZE), 0); /* 大小块为513<=x<=1024的 */
    PUT(heap_listp + (6*WSIZE), 0); /* 大小块为1025<=x<=2048的 */
    PUT(heap_listp + (7*WSIZE), 0); /* 大小块为2049<=x<=4096的 */
    PUT(heap_listp + (8*WSIZE), 0); /* 大小块为4097<=x<=8192的 */
    PUT(heap_listp + (9*WSIZE), 0); /* 大小块为8193<=x<=16384的 */
    PUT(heap_listp + (10*WSIZE), 0); /* 大小块为16385<=x的 */
    PUT(heap_listp + (11*WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    PUT(heap_listp + (12*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp + (13*WSIZE), PACK(0, 1));     /* Epilogue header */

    segregated_list = heap_listp;
    base_ptr = heap_listp - WSIZE; 
    /* base_ptr是heap_listp的前面四个字节，有利于不出现bias = 0的情况 */
    heap_listp += (12*WSIZE);
    
    free_list = NULL;                 

#ifdef NEXT_FIT
    rover = heap_listp;
#endif

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) 
        return -1;
    return 0;
}

/* 
 * malloc - Allocate a block with at least size bytes of payload 
 */
void *malloc(size_t size) 
{

    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;      

    if (heap_listp == 0){
        mm_init();
    }
    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    /* 这里的大小已经算了header和footer了 */
    if (size <= DSIZE)                                          
        asize = 2*DSIZE;                                        
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); 

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {  
        place(bp, asize);                  
        return bp;
    }

    extendsize = MAX(asize,CHUNKSIZE);                 
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)  
        return NULL;                                  
    place(bp, asize);

    // printf("\n");
    // printf("malloc %ld\n", size);
    // printf("asize: %ld\n", asize);
    // mm_checkheap(__LINE__);

    return bp;
} 

/* 
 * free - Free a block 
 */
void free(void *bp)
{
    if (bp == 0) 
        return;

    size_t size = GET_SIZE(HDRP(bp));
    if (heap_listp == 0){
        mm_init();
    }

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    // free_list_insert(bp); /* 有了空闲块就应该加入空闲列表中 */
    segregated_insert(bp);
    
    coalesce(bp);
}

/*
 * realloc - Naive implementation of realloc
 */
void *realloc(void *ptr, size_t size)
{
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        mm_free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    size_t asize;
    if (size <= DSIZE)                                          
        asize = 2*DSIZE;                                        
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    
    oldsize = GET_SIZE(HDRP(ptr));
    if (asize + 2*DSIZE <= oldsize) {
        /* 旧的块的大小更大 */
        /* 把size - oldsize之间的块都free掉剩下的依然保留 */
        PUT(HDRP(ptr), PACK(asize, 1));
        PUT(FTRP(ptr), PACK(asize, 1));
        newptr = NEXT_BLKP(ptr);
        PUT(HDRP(newptr), PACK(oldsize - asize, 0));
        PUT(FTRP(newptr), PACK(oldsize - asize, 0));
        // free_list_insert(newptr);
        segregated_insert(newptr);
        return ptr;
    }
    else if (asize <= oldsize) {
        /* 旧的块更大，但是没有达到可以再分割出一块来的情况 */
        /* 那就还是这样吧，没必要修改 */
        return ptr;
    }
    else {
        /* 新的大小要更大了，这个时候不得不分配新的块了 */
        /* 先看后面是否刚好有空闲块 */
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
        void *next_ptr = NULL;
        if (!next_alloc && next_size + oldsize >= asize) {
            /* 后面的块可以合并 */
            /* 先把后面的块从空闲链表里面删除 */
            segregated_delete(NEXT_BLKP(ptr));
            if (oldsize + next_size >= asize + 2*DSIZE) {
                /* 说明后面的空闲块超级大 */
                /* 应该分割 */
                PUT(HDRP(ptr), PACK(asize, 1));
                PUT(FTRP(ptr), PACK(asize, 1));
                next_ptr = NEXT_BLKP(ptr);
                PUT(HDRP(next_ptr), PACK(oldsize + next_size - asize, 0));
                PUT(FTRP(next_ptr), PACK(oldsize + next_size - asize, 0));
                segregated_insert(next_ptr);
                return ptr;
            }
            /* 后面的块不是很大，可以不分割 */
            PUT(HDRP(ptr), PACK(oldsize + next_size, 1));
            PUT(FTRP(ptr), PACK(oldsize + next_size, 1));
            return ptr;
        }
        newptr = mm_malloc(size);
        memcpy(newptr, ptr, oldsize);
        mm_free(ptr);
        return newptr;
    }

    return NULL;
}
/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;
    newptr = malloc(bytes);
    memset(newptr, 0, bytes);
    return newptr;
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
 * mm_checkheap - Check the heap for correctness. Helpful hint: You
 *                can call this function using mm_checkheap(__LINE__);
 *                to identify the line number of the call site.
 */
void mm_checkheap(int lineno)  
{ 
    lineno = lineno; /* keep gcc happy */
    unsigned int free_blocks_in_heap = 0;
    unsigned int free_blocks_in_list = 0;
    printf("\n");
    printf("Starting checkheap at line %d\n", lineno);
    /* 检查Prologue块 */
    printf("Prologue block:\n");
    printf("Header: [%d: %d] Footer: [%d: %d]\n", 
            GET_SIZE(HDRP(heap_listp)), GET_ALLOC(HDRP(heap_listp)),
            GET_SIZE(FTRP(heap_listp)), GET_ALLOC(FTRP(heap_listp)));
    if (GET_SIZE(HDRP(heap_listp)) != DSIZE || GET_ALLOC(HDRP(heap_listp)) != 1 || 
        GET(HDRP(heap_listp)) != GET(FTRP(heap_listp))) {
        printf("Error: Prologue block header or footer error!\n");
    }
    if (aligned(heap_listp) == 0) {
        printf("Error: Prologue block is not aligned!\n");
    }

    /* 检查当前的堆 */
    void *now_ptr = heap_listp;
    void *pred_ptr = heap_listp;
    now_ptr = NEXT_BLKP(now_ptr);
    for (; GET_SIZE(HDRP(now_ptr)) > 0; now_ptr = NEXT_BLKP(now_ptr)) {
        printf("Current block: %p\n", now_ptr);
        printf("Header: [%d: %d] Footer: [%d: %d]\n", 
                GET_SIZE(HDRP(now_ptr)), GET_ALLOC(HDRP(now_ptr)),
                GET_SIZE(FTRP(now_ptr)), GET_ALLOC(FTRP(now_ptr)));
        if (GET(HDRP(now_ptr)) != GET(FTRP(now_ptr))) {
            printf("Error: Header and footer content not equal!\n");
        }
        if (aligned(now_ptr) == 0) {
            printf("Error: Current block is not aligned!\n");
        }
        if (GET_ALLOC(HDRP(now_ptr)) == 0) {
            free_blocks_in_heap++;
            /* 空闲块 */
            if (GET_ALLOC(HDRP(pred_ptr)) == 0) {
                /* 前面的块也是空闲块 */
                printf("Error: Two consecutive free blocks!\n");
            }
            /* 后面如果是空闲块的的话，循环辖区依然会得到检查的 */
        }
    }

    /* 检查Epilogue块 */
    printf("Epilogue block:\n");
    printf("Header: [%d: %d]\n", 
            GET_SIZE(HDRP(now_ptr)), GET_ALLOC(HDRP(now_ptr)));
    if (GET_SIZE(HDRP(now_ptr)) != 0 || GET_ALLOC(HDRP(now_ptr)) != 1) {
        printf("Error: Epilogue block header error!\n");
    }
    if (aligned(now_ptr) == 0) {
        printf("Error: Epilogue block is not aligned!\n");
    }

#ifdef SEGREGATED
    /* 检查分离链表 */
    printf("Segregated list:\n");
    void *list_ptr = segregated_list;
    pred_ptr = NULL;
    void *succ_ptr = NULL;
    for (;list_ptr != segregated_list + LISTMAXN*WSIZE; list_ptr = list_ptr + WSIZE) {
        pred_ptr = list_ptr;
        now_ptr = get_ptr(GET(list_ptr));
        printf("Current list: %p\n", list_ptr);
        for (; now_ptr != NULL; now_ptr = succ_ptr) {
            free_blocks_in_list++;
            succ_ptr = get_ptr(SUCC_BIAS(now_ptr));
            printf("Current block: %p\n", now_ptr);
            printf("Header: [%d: %d] Footer: [%d: %d]\n", 
                    GET_SIZE(HDRP(now_ptr)), GET_ALLOC(HDRP(now_ptr)),
                    GET_SIZE(FTRP(now_ptr)), GET_ALLOC(FTRP(now_ptr)));
            if (GET(HDRP(now_ptr)) != GET(FTRP(now_ptr))) {
                printf("Error: Header and footer content not equal!\n");
            }
            if (aligned(now_ptr) == 0) {
                printf("Error: Current block is not aligned!\n");
            }
            if (GET_ALLOC(HDRP(now_ptr)) == 1) {
                printf("Error: Current block is not free!\n");
            }
            if (!in_heap(now_ptr)) {
                printf("Error: Current block is not in heap!\n");
            }

            /* 检查链表的一致性 */
            if (pred_ptr != NULL) {
                if (pred_ptr == list_ptr) {
                    if(get_ptr(GET(pred_ptr)) != now_ptr) {
                        printf("Error: Free list is not consistent!\n");
                    }
                }
                else {
                    if (get_ptr(SUCC_BIAS(pred_ptr)) != now_ptr) {
                        printf("pred's succ %p %p\n", get_ptr(SUCC_BIAS(pred_ptr)), now_ptr);
                        printf("Error: Free list is not consistent!\n");
                    }
                }
            }
            if (succ_ptr != NULL) {
                if (get_ptr(PRED_BIAS(succ_ptr)) != now_ptr) {
                    printf("succ's pred %p %p\n", get_ptr(PRED_BIAS(succ_ptr)), now_ptr);
                    printf("Error: Free list is not consistent!\n");
                }
            }

            pred_ptr = now_ptr;
        }
    }

#else
    /* 检查空闲链表 */
    printf("Free list:\n");
    now_ptr = free_list;
    pred_ptr = NULL;
    void *succ_ptr = NULL;
    for (; now_ptr != NULL; now_ptr = succ_ptr) {
        free_blocks_in_list++;
        succ_ptr = get_ptr(SUCC_BIAS(now_ptr));
        printf("Current block: %p\n", now_ptr);
        printf("Header: [%d: %d] Footer: [%d: %d]\n", 
                GET_SIZE(HDRP(now_ptr)), GET_ALLOC(HDRP(now_ptr)),
                GET_SIZE(FTRP(now_ptr)), GET_ALLOC(FTRP(now_ptr)));
        if (GET(HDRP(now_ptr)) != GET(FTRP(now_ptr))) {
            printf("Error: Header and footer content not equal!\n");
        }
        if (aligned(now_ptr) == 0) {
            printf("Error: Current block is not aligned!\n");
        }
        if (GET_ALLOC(HDRP(now_ptr)) == 1) {
            printf("Error: Current block is not free!\n");
        }
        if (!in_heap(now_ptr)) {
            printf("Error: Current block is not in heap!\n");
        }

        /* 检查链表的一致性 */
        if (pred_ptr != NULL) {
            if (pred_ptr == list_ptr) {
                    if(get_ptr(GET(pred_ptr)) != now_ptr) {
                        printf("Error: Free list is not consistent!\n");
                    }
                }
            else {
                if (get_ptr(SUCC_BIAS(pred_ptr)) != now_ptr) {
                    printf("pred's succ %p %p\n", get_ptr(SUCC_BIAS(pred_ptr)), now_ptr);
                    printf("Error: Free list is not consistent!\n");
                }
            }
        }
        if (succ_ptr != NULL) {
            if (get_ptr(PRED_BIAS(succ_ptr)) != now_ptr) {
                printf("Error: Free list is not consistent!\n");
            }
        }

        pred_ptr = now_ptr;
    }
#endif    

    if (free_blocks_in_heap != free_blocks_in_list) {
        printf("Error: Free blocks in heap and free list not equal!\n");
    }
}

/* 
 * The remaining routines are internal helper routines 
 */


/* free_list_insert - 向空闲块链表里面插入新的空闲块 */
static void free_list_insert(void *bp) {
    if (bp == NULL)
        return;
    if (free_list == NULL) {
        free_list = bp;
        SET_PRED(bp, 0);
        SET_SUCC(bp, 0);
        return;
    }
    
    void *now_ptr = free_list;
    
    /* 直接把新释放的块放在链表的最前面 */
    SET_PRED(bp, 0);
    SET_SUCC(bp, get_bias(now_ptr));
    SET_PRED(now_ptr, get_bias(bp));
    free_list = bp;
    return;
}

/* free_list_delete - 删除一个空闲块 */
static void free_list_delete(void *bp) {
    if (bp == NULL)
        return;
    if (free_list == NULL) {
        /* 本来就是空的，删了也没用 */
        printf("Error: Try to delete a block from an empty free list!\n");
        return;
    }
    // printf("deleting...\n");
    void *pred_ptr = NULL;
    void *succ_ptr = NULL;

    pred_ptr = get_ptr(PRED_BIAS(bp));
    succ_ptr = get_ptr(SUCC_BIAS(bp));
    // printf("deleting... again\n");
    // printf("pred_ptr: %p\n", pred_ptr);
    // printf("succ_ptr: %p\n", succ_ptr);

    if (pred_ptr == NULL && succ_ptr == NULL) {
        /* 说明是唯一的一个块 */
        free_list = NULL;
        // printf("0. free_list_delete succeeds with %p\n",bp);
        return;
    }

    if (pred_ptr == NULL) {
        /* 说明是第一个块 */
        free_list = succ_ptr;
        SET_PRED(succ_ptr, 0);
        // printf("1. free_list_delete succeeds with %p\n",bp);
    }
    else if (succ_ptr == NULL) {
        /* 说明是最后一个块 */
        SET_SUCC(pred_ptr, 0);
        // printf("2. free_list_delete succeeds with %p\n",bp);
    }
    else {
        /* 说明是中间的块 */
        SET_SUCC(pred_ptr, get_bias(succ_ptr));
        SET_PRED(succ_ptr, get_bias(pred_ptr));
        // printf("3. free_list_delete succeeds with %p\n",bp);
    }

}


/* segregated_search - 找到合适的插入的大小块头指针 */
static void *segregated_searh(size_t asize) {
    if (asize <= 32)
        return segregated_list;
    
    if (asize >= 16385)
        return segregated_list + (10 * WSIZE);
    
    size_t temp = (asize - 1) >> 5;
    int idx = 0;
    while (temp != 0) {
        temp >>= 1;
        idx++;
    }
    return segregated_list + (idx * WSIZE);
}
/* segregated_insert - 找到合适的链表和合适的地方插入 
 * 链表是从小到大组织的
 */
static void segregated_insert(void *bp) {
    if (bp == NULL)
        return;
    size_t size = GET_SIZE(HDRP(bp));
    void *list_ptr = segregated_searh(size);
    void *now_ptr = get_ptr(GET(list_ptr));
    void *pred_ptr = list_ptr;
    while (now_ptr != NULL) {
        if (size <= GET_SIZE(HDRP(now_ptr))) {
            /* 找到了合适的位置 */
            break;
        }
        pred_ptr = now_ptr;
        now_ptr = get_ptr(SUCC_BIAS(now_ptr));
    }

    /* 原来是空的链表，现在插入第一个块 */
    if (now_ptr == NULL && pred_ptr == list_ptr) {
        SET_PRED(bp, get_bias(list_ptr));
        SET_SUCC(bp, 0);
        PUT(list_ptr, get_bias(bp));
        return;
    }
    /* 原来是不空的链表，现在插入第一个快 */
    if (now_ptr != NULL && pred_ptr == list_ptr) {
        SET_PRED(bp, get_bias(list_ptr));
        SET_SUCC(bp, get_bias(now_ptr));
        SET_PRED(now_ptr, get_bias(bp));
        PUT(list_ptr, get_bias(bp));
        return;
    }
    /* 现在插入最后一个快，因为空的链表的最后一个快也是第一个块，前面处理过了，所以pred_ptr肯定不是头 */
    if (now_ptr == NULL && pred_ptr != list_ptr) {
        SET_PRED(bp, get_bias(pred_ptr));
        SET_SUCC(bp, 0);
        SET_SUCC(pred_ptr, get_bias(bp));
        return;
    }
    /* 在中间的块 */
    if (now_ptr != NULL && pred_ptr != list_ptr) {
        SET_PRED(bp, get_bias(pred_ptr));
        SET_SUCC(bp, get_bias(now_ptr));
        SET_SUCC(pred_ptr, get_bias(bp));
        SET_PRED(now_ptr, get_bias(bp));
        return;
    }
    return;
}

/* segregated_delete - 删除掉链表里的这个块 */
static void segregated_delete(void *bp) {
    if (bp == NULL)
        return;
    size_t size = GET_SIZE(HDRP(bp));
    void *list_ptr = segregated_searh(size);
    void *pred_ptr = get_ptr(PRED_BIAS(bp));
    void *succ_ptr = get_ptr(SUCC_BIAS(bp));
    if (pred_ptr == list_ptr) {
        /* 说明是第一个块 */
        PUT(list_ptr, get_bias(succ_ptr));
        if (succ_ptr != NULL) {
            /* 说明不是最后一个块 */
            SET_PRED(succ_ptr, get_bias(list_ptr));
        }
        return;
    }
    if (succ_ptr == NULL) {
        /* 说明是最后一个块 */
        SET_SUCC(pred_ptr, 0);
        return;
    }
    /* 说明是中间的块 */
    SET_SUCC(pred_ptr, get_bias(succ_ptr));
    SET_PRED(succ_ptr, get_bias(pred_ptr));
    return;
}

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) 
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; 
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                        

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */   
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */

    /* 有新的空闲块，要把他放到空闲链表里 */
    // free_list_insert(bp);
    segregated_insert(bp);

    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ 

    /* Coalesce if the previous block was free */
    return coalesce(bp);                                          
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static void *coalesce(void *bp) 
{
#ifdef DEFFERED_COALESCE
    void *now_ptr = bp;
    size_t tot_size = 0;
    for(; GET_ALLOC(HDRP(now_ptr)) == 0; now_ptr = NEXT_BLKP(now_ptr)) {
        /* 现在是空闲块 */
        /* 先把这个块从空闲链表里面删除 */
        tot_size += GET_SIZE(HDRP(now_ptr));
        segregated_delete(now_ptr);
    }
    /* 现在now_ptr指向的是第一个不是空闲块的块 */
    PUT(HDRP(bp), PACK(tot_size, 0));
    PUT(FTRP(bp), PACK(tot_size, 0));
    /* 把这个块插入到空闲链表里面 */
    segregated_insert(bp);
    return bp;
#else
    /* bp一定是要本来就在空闲块列表里面的！！！ */
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* Case 1 */
        /* 这种情况说明合并不了，什么事都不要做 */
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
        /* 后面没有被合并 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        /* 把后面的块从空闲链表里面删除 */
        // free_list_delete(bp);
        // free_list_delete(NEXT_BLKP(bp));
        segregated_delete(bp);
        segregated_delete(NEXT_BLKP(bp));

        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
        // free_list_insert(bp);
        segregated_insert(bp);
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        /* 把前面的块从空闲链表里面删除 */
        // free_list_delete(PREV_BLKP(bp));
        // free_list_delete(bp);
        segregated_delete(PREV_BLKP(bp));
        segregated_delete(bp);

        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        // free_list_insert(bp);
        segregated_insert(bp);
    }

    else {                                     /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        /* 这下两个都要删掉了 */
        // free_list_delete(PREV_BLKP(bp));
        // free_list_delete(NEXT_BLKP(bp));
        // free_list_delete(bp);
        segregated_delete(PREV_BLKP(bp));
        segregated_delete(NEXT_BLKP(bp));
        segregated_delete(bp);

        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        // free_list_insert(bp);
        segregated_insert(bp);
    }
#endif
    return bp;
}

/* 
 * place - Place block of asize bytes at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
static void place(void *bp, size_t asize)
{
    // printf("\n");
    // printf("place %p %ld\n", bp, asize);
    // mm_checkheap(__LINE__);

    size_t csize = GET_SIZE(HDRP(bp));   

    if ((csize - asize) >= (2*DSIZE)) { 
        /* 先要在空闲块链表里面删掉 */
        // free_list_delete(bp);
        segregated_delete(bp);

        // printf("free_list_delete succeeds with %p\n",bp);        

        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        // free_list_insert(bp);
        segregated_insert(bp);
    }
    else { 
        /* 先要在空闲块链表里面删掉 */
        // free_list_delete(bp);
        segregated_delete(bp);
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }

    // printf("\n");
    // printf("place finished!\n");
    // mm_checkheap(__LINE__);
}

/* 
 * find_fit - Find a fit for a block with asize bytes 
 */
static void *find_fit(size_t asize)
{
    // printf("\n");
    // printf("find_fit %ld\n", asize);
    // mm_checkheap(__LINE__);

#ifdef NEXT_FIT 
    /* Next fit search */
    char *oldrover = rover;

    /* Search from the rover to the end of list */
    for ( ; GET_SIZE(HDRP(rover)) > 0; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && (asize <= GET_SIZE(HDRP(rover))))
            return rover;

    /* search from start of list to old rover */
    for (rover = heap_listp; rover < oldrover; rover = NEXT_BLKP(rover))
        if (!GET_ALLOC(HDRP(rover)) && (asize <= GET_SIZE(HDRP(rover))))
            return rover;

    return NULL;  /* no fit found */
#else 
    /* First-fit search */
    /* void *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            return bp;
        }
    }
    */
    /* 这一段是利用一个一个块来寻找的，我们改成在空闲链表里面来寻找 */
    /* 从free_list开始，一直找到结尾,free_list指向第一个空闲块 */
    /*void *bp;
    for (bp = free_list; bp != NULL; bp = get_ptr(SUCC_BIAS(bp))) {
        if (asize <= GET_SIZE(HDRP(bp))) {
            // printf("\nfind_fit %p\n", bp);            
            return bp;
        }
    }*/
    void *list_ptr = segregated_searh(asize);
    for (; list_ptr != segregated_list + LISTMAXN*WSIZE; list_ptr += WSIZE) {
        void *now_ptr = get_ptr(GET(list_ptr));
        while (now_ptr != NULL) {
            if (asize <= GET_SIZE(HDRP(now_ptr))) {
                return now_ptr;
            }
            now_ptr = get_ptr(SUCC_BIAS(now_ptr));
        }
    }
    return NULL; /* No fit */
#endif
}

