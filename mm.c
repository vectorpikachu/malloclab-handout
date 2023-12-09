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
static char *base_ptr = 0; // the very first address of the heap
#define LISTMAXN 20

/* single word (4) or double word (8) alignment */
#define WSIZE 4
#define DSIZE 8
#define ALIGNMENT 8
#define CHUNKSIZE (1<<12)

/* 简单的求最大值的 */
#define MAX(x, y) ((x) > (y)? (x) : (y))

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

/* 给定一个块指针，他的payload最开的地方先是pred和succ */
#define PRED(bp) (*(unsigned int *)(bp)) // 要读取的是一个四字节的东西，所以不能用char *
#define SUCC(bp) (*(unsigned int *)((char *)(bp) + WSIZE)) // unsigned int *类型的指针加一就是加四个字节

/* 分别向块的前驱和后继的地址里面放东西 */
#define PUT_PRED(bp, val) (PRED(bp) = (val))
#define PUT_SUCC(bp, val) (SUCC(bp) = (val))

/* 这里是用来定义全局的变量的地方 */
static char *heap_listp = 0;  // 指向Prologue的指针

/* If you need space for large data structures, you can put them at the beginning of
the heap. */
/* 因此我们在mm_init函数里面开辟不同的大小类的头指针 */
static char *segragated_listp; // 指向分离链表的指针

static int in_heap(const void *p);
static int aligned(const void *p);
static void *extend_heap(size_t size);
static void *coalesce(void *ptr);
static void segragated_list_insert(void *ptr);
static void segragated_list_delete(void *ptr);
static void *find_fit(size_t size);
static void place(void *ptr, size_t size);
static void *segragated_list_search(size_t size);
static unsigned int GET_BIAS(void *ptr) {
    if (ptr == NULL) return 0;
    return (unsigned int)((char *)ptr - base_ptr);
}
static void *GET_PTR(unsigned int bias) {
    if (bias == 0) return NULL;
    return (void *)(base_ptr + bias);
}

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    // printf("mm_init called\n");
    // 根据内存的模型，我们先要初始化一个堆，这个堆的大小是2*DSIZE

    // You must reinitialize all of your global pointers in this function.
    if ((heap_listp = mem_sbrk(12*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0); // 大小类为0~31
    PUT(heap_listp + (1*WSIZE), 0); // 大小类为32~63
    PUT(heap_listp + (2*WSIZE), 0); // 大小类为64~127
    PUT(heap_listp + (3*WSIZE), 0); // 大小类为128~255
    PUT(heap_listp + (4*WSIZE), 0); // 大小类为256~511
    PUT(heap_listp + (5*WSIZE), 0); // 大小类为512~1023
    PUT(heap_listp + (6*WSIZE), 0); // 大小类为1024~2047
    PUT(heap_listp + (7*WSIZE), 0); // 大小类为2048~4095
    PUT(heap_listp + (8*WSIZE), 0); // 大小类为>=4096
    PUT(heap_listp + (9*WSIZE), PACK(DSIZE, 1)); // Prologue header
    PUT(heap_listp + (10*WSIZE), PACK(DSIZE, 1)); // Prologue footer
    PUT(heap_listp + (11*WSIZE), PACK(0, 1)); // Epilogue header

    // printf("heap_listp = %p\n", heap_listp);
    base_ptr = heap_listp - WSIZE;
    segragated_listp = heap_listp;
    heap_listp += (10*WSIZE);
    // printf("heap_listp = %p\n", heap_listp);
    // 然后我们把这个堆扩展到最大
    if (extend_heap(CHUNKSIZE) == NULL)
        return -1;
    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    // printf("malloc called by %ld\n", size);
    /* 给出的size只是payload的大小，我们必须加上Header和Footer的大小 */
    /* Your malloc implementation must always return 8-byte aligned pointers. */
    size_t adjusted_size; // Adjusted block size

    /* 因为有Header和Footer还有Pred和Succ，所以至少要分配2*DSIZE个字节 */
    if (size <= 0) return NULL;
    else if (size <= DSIZE) adjusted_size = 2*DSIZE;
    else adjusted_size = ALIGN(size + DSIZE);

    // printf("adjusted_size = %ld\n", adjusted_size);
    /* 先在空闲块中寻找一个合适的可以插入的位置 */
    void *ptr;
    if ((ptr = find_fit(adjusted_size)) != NULL ) {
        // printf("find_fit ptr = %p\n", ptr);
        place(ptr, adjusted_size);
        return ptr;
    }

    /* 如果没有找到合适的位置，就扩展堆 */
    /* 扩展堆里面是sbrk，速度会比较慢？所以一次要分配比较多的堆 */
    size_t extend_size = MAX(adjusted_size, CHUNKSIZE);
    if ((ptr = extend_heap(extend_size)) == NULL)
        return NULL;
    place(ptr, adjusted_size);
    return ptr;
}

/*
 * free
 */
void free (void *ptr) {
    if(!ptr) return;
    // printf("free called by %p\n", ptr);
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    /* 现在这个块的pred和succ没有用了，为什么？ */
    /* 因为经过合并后，我们要进行插入到分离空闲链表中，他会有新的pred和succ了 */
    PUT_PRED(ptr, 0);
    PUT_SUCC(ptr, 0);
    /* 在合并里面有插入链表的操作了 */
    coalesce(ptr);
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
        free(oldptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(oldptr == NULL) {
        return malloc(size);
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
        return 0;
    }

    /* Copy the old data. */
    oldsize = GET_SIZE(HDRP(oldptr));
    if(size < oldsize) oldsize = size;
    memcpy(newptr, oldptr, oldsize);

    /* Free the old block. */
    free(oldptr);

    return newptr;
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
 * mm_checkheap- Check the heap for correctness. Helpful hint: You
 *                can call this function using mm_checkheap(__LINE__);
 *                to identify the line number of the call site.
 * 
 */
void mm_checkheap(int lineno) {
    int free_count = 0;
    printf("Check heap at line %d\n", lineno);
    /* 输出Heap的指针 */
    printf("Heap (%p):\n", heap_listp);
    /* 检查Prologue和Epilogue */
    /* 如果Prologue的块大小不是DSIZE的话，说明是有问题的，或者Prologue直接是未分配的 */
    void *prologue = heap_listp;
    if ((GET_SIZE(HDRP(prologue)) != DSIZE) || !GET_ALLOC(HDRP(prologue)) 
    || (GET(HDRP(prologue)) != GET(FTRP(prologue))) || !aligned(prologue))
        printf("Bad prologue header\n");
    printf("Prologue header: [%d:%d] footer: [%d:%d]\n", GET_SIZE(HDRP(prologue)), GET_ALLOC(HDRP(prologue)), GET_SIZE(FTRP(prologue)), GET_ALLOC(FTRP(prologue)));
    /* 每个块都要检查 是否双字对齐的以及Header和Footer中存储的信息是否是一样的*/
    void *ptr = NEXT_BLKP(prologue);
    for (; GET_SIZE(HDRP(ptr)) > 0; ptr = NEXT_BLKP(ptr)) {
        if (!in_heap(ptr)) printf("Error: %p is not in heap\n", ptr);
        if (!aligned(ptr)) printf("Error: %p is not aligned\n", ptr);
        if (GET(HDRP(ptr)) != GET(FTRP(ptr))) printf("Error: Header and Footer do not match\n");
        // 检查是否有连续的空闲块
        if (!GET_ALLOC(HDRP(ptr)) && !GET_ALLOC(HDRP(NEXT_BLKP(ptr))))
            printf("Error: Continuous free blocks\n");
        if (!GET_ALLOC(HDRP(ptr))) free_count++;
        printf("%p: header: [%d:%d] footer: [%d:%d]\n", ptr, GET_SIZE(HDRP(ptr)), GET_ALLOC(HDRP(ptr)), GET_SIZE(FTRP(ptr)), GET_ALLOC(FTRP(ptr)));
    }
    /* 如果Epilogue的块大小不是0的话，说明是有问题的，或者Epilogue直接是未分配的 */
    if ((GET_SIZE(HDRP(ptr)) != 0) || !(GET_ALLOC(HDRP(ptr))) || !aligned(ptr))
        printf("Bad epilogue header\n");
    printf("Epilogue header: [%d:%d]\n", GET_SIZE(HDRP(ptr)), GET_ALLOC(HDRP(ptr)));


    /* 检查分离空闲链表 */
    /* 检查每个大小类的链表 */
    int free_count_in_list = 0;
    int i;
    for (i = 0; i < 9; i++) {
        void *head = segragated_listp + i * WSIZE;
        void *cur = GET_PTR(GET(head));
        printf("segragated_listp + %d * WSIZE = %p\n", i, head);
        printf("head = %p\n", head);
        printf("head -> %p\n", GET_PTR(GET(head)));
        while (cur != NULL) {
            void *pred = GET_PTR(PRED(cur));
            void *succ = GET_PTR(SUCC(cur));
            printf("cur = %p\n", cur);
            printf("cur's pred = %p\n", pred);
            printf("cur's succ = %p\n", succ);
            free_count_in_list++;

            if (!in_heap(cur)) printf("Error: %p is not in heap\n", cur);
            if (!aligned(cur)) printf("Error: %p is not aligned\n", cur);

            /* All next/previous pointers are consistent 
             * (if A’s next pointer points to B, B’s previous pointer
             * should point to A). */
            if (pred != NULL && ((pred == head && GET(head) != GET_BIAS(cur)) || (pred != head && SUCC(pred) != GET_BIAS(cur)))) {
                printf("Error: Pred and Succ do not match\n");
            }
            if (succ != NULL && PRED(succ) != GET_BIAS(cur)) printf("Error: Pred and Succ do not match\n");

            /* All free list pointers points between mem_heap_lo() and mem_heap_high(). */
            if (cur != 0 && cur < mem_heap_lo()) printf("Error: %p is not in heap\n", cur);
            if (cur != 0 && cur > mem_heap_hi()) printf("Error: %p is not in heap\n", cur);

            /* All blocks in each list bucket fall within bucket size range (segregated list). */
            size_t size = GET_SIZE(HDRP(cur));
            if (i == 0 && (size >= 32 || size <= 0)) printf("Error: %p with size %ld is not in range\n", cur, size);
            if (i == 1 && (size >= 64 || size < 32)) printf("Error: %p with size %ld is not in range\n", cur, size);
            if (i == 2 && (size >= 128 || size < 64)) printf("Error: %p with size %ld is not in range\n", cur, size);
            if (i == 3 && (size >= 256 || size < 128)) printf("Error: %p with size %ld is not in range\n", cur,size);
            if (i == 4 && (size >= 512 || size < 256)) printf("Error: %p with size %ld is not in range\n", cur, size);
            if (i == 5 && (size >= 1024 || size < 512)) printf("Error: %p with size %ld is not in range\n", cur, size);
            if (i == 6 && (size >= 2048 || size < 1024)) printf("Error: %p with size %ld is not in range\n", cur, size);
            if (i == 7 && (size >= 4096 || size < 2048)) printf("Error: %p with size %ld is not in range\n", cur, size);
            if (i == 8 && (size < 4096)) printf("Error: %p with size %ld is not in range\n", cur, size);
             
            if (GET_ALLOC(HDRP(cur))) printf("Error: %p is allocated\n", cur);
            
            if (GET(HDRP(cur)) != GET(FTRP(cur))) printf("Error: Header and Footer do not match\n");
            cur = GET_PTR(SUCC(cur));
        }
    }

    /* 检查空闲块的数量是否一致 */
    if (free_count != free_count_in_list) printf("Error: free_count != free_count_in_list\n");
}

/* extend_heap - 利用sbrk来扩展当前的堆，同时处理新加入的空闲块 */
static void *extend_heap(size_t size) {
    void *bp;
    size = ALIGN(size);

    /* 利用系统调用sbrk来把堆开大 */
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;
    // printf("new block bp = %p\n", bp);
    // printf("new block size = %ld\n", size);
    
    // 现在我们有一个很大的空闲块，对他的Header和Footer赋值
    PUT(HDRP(bp), PACK(size, 0)); // Free block header
    // 这里我认为因为bp实际上是指向的旧Epilogue的下一个WSIZE的地方，
    // 所以在HDRP(bp)里面得到的是旧Epilogue的Header
    // 这样就覆盖上了

    PUT(FTRP(bp), PACK(size, 0)); // Free block footer
    /* 经过这样的扩展之后我们应该有一个新的Epilogue */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // New epilogue header

    /* 而且我们要使用什么样的合并策略呢？先使用立即合并 */
    /* 合并里面有插入链表的操作了 */
    return coalesce(bp);
}

/* coalesce - 把一个空闲块和他的前后块合并 */
static void *coalesce(void *ptr) {
    // printf("coalesce called by %p\n", ptr);
    char *prev = PREV_BLKP(ptr);
    char *next = NEXT_BLKP(ptr);
    size_t prev_alloc = GET_ALLOC(FTRP(prev));
    size_t next_alloc = GET_ALLOC(HDRP(next));
    size_t size = GET_SIZE(HDRP(ptr));
    // printf("prev = %p\n", prev);
    // printf("next = %p\n", next);
    // printf("prev_alloc = %ld\n", prev_alloc);
    // printf("next_alloc = %ld\n", next_alloc);
    // printf("size = %ld\n", size);

    /* 前后的块都是被分配了，就不用合并了 */
    if (prev_alloc && next_alloc) {
    }
    /* 前面的块是被分配了，后面的块是空闲块 */
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(next));
        /* 这里删除掉next和ptr在链表中的，为了后面加入新的空闲块 */
        segragated_list_delete(next);
        segragated_list_delete(ptr);
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }
    /* 前面的块是没有被分配的空闲块，后面的块是已经被分配的 */
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(prev));
        /* 这里删除掉prev和ptr在链表中的，为了后面加入新的空闲块 */
        segragated_list_delete(prev);
        segragated_list_delete(ptr);
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(prev), PACK(size, 0));
        ptr = prev;
    }
    /* 前面和后面都是空闲块 */
    else {
        size += GET_SIZE(HDRP(prev)) + GET_SIZE(FTRP(next));
        /* 这里删除掉prev和next和ptr在链表中的，为了后面加入新的空闲块 */
        segragated_list_delete(prev);
        segragated_list_delete(next);
        segragated_list_delete(ptr);
        PUT(HDRP(prev), PACK(size, 0));
        PUT(FTRP(next), PACK(size, 0));
        ptr = prev;
    }

    /* 合并之后我们要把这个块加入到链表中去 */
    segragated_list_insert(ptr);
    return ptr;
}

/* segragated_list_search - 找到当前的大小对应的大小块链表的头指针 */
static void *segragated_list_search(size_t size) {
    /* 这里我们要找到一个合适的大小类 */
    int i = 0;
    size_t adjusted_size = size;
    if (adjusted_size <= DSIZE) adjusted_size = 2*DSIZE;
    else adjusted_size = ALIGN(size);

    /* 大小类为>4096的大小块的头指针 */
    if (adjusted_size >= 4096)
        return segragated_listp + 8 * WSIZE;

    /* 大小类为<=32的大小块的头指针 */
    if (adjusted_size < 32)
        return segragated_listp;
    /* 大小类在两者之间的大小块的头指针 */
    adjusted_size >>= 5; // 除以32，因为32-64是第二个大小类也就是i=1
    while (adjusted_size >= 1) {
        adjusted_size >>= 1;
        i++;
    }
    return segragated_listp + i * WSIZE;
}

/* segragated_list_insert - 将某个块插入链表中 */
static void segragated_list_insert(void *ptr) {
    if (ptr == NULL) return;
    size_t size = GET_SIZE(HDRP(ptr));
    void *head = segragated_list_search(size);
    // printf("segragated_list_insert called by %p\n", ptr);
    // printf("head = %p\n", head);
    /* 如果这个链表是空的，那么就直接插入 */
    /* 当一个指针却被复制成为0的时候，说明他是空的，而GET(head)实际上是head的后继 */
    if (GET(head) == 0) {
        PUT(head, GET_BIAS(ptr));
        // printf("GET(head) = %d\n", GET(head));
        // printf("actual address in head = %p\n", GET_PTR(GET(head)));
        PUT_PRED(ptr, GET_BIAS(head));
        PUT_SUCC(ptr, 0);
        // printf("ptr's pred = %p\n", GET_PTR(PRED(ptr)));
        // printf("ptr's succ = %p\n", GET_PTR(SUCC(ptr)));

        // mm_checkheap(445);
        return;
    }

    /* 非空的链表找到合适的插入的地方 */
    void *cur = GET_PTR(GET(head));
    void *prev = head;
    while (cur != NULL) {
        if (cur == ptr) return; // 如果这个块已经在链表中了，那么就不用插入了
        if (size >= GET_SIZE(HDRP(cur))) break;
        prev = cur; // 因为最后有可能出现cur变成了NULL，这样就找不到前面的那个指针了
        cur = GET_PTR(SUCC(cur));
    }
    /* 有可能现在要放在第一个地方 */
    if (prev == head) {
        PUT(head, GET_BIAS(ptr)); // head是没有PUT_PRED,PUT_SUCC这样的
        PUT_PRED(ptr, GET_BIAS(head));
        PUT_SUCC(ptr, GET_BIAS(cur));
        PUT_PRED(cur, GET_BIAS(ptr));
    }
    else {
        PUT_SUCC(prev, GET_BIAS(ptr));
        PUT_PRED(ptr, GET_BIAS(prev));
        PUT_SUCC(ptr, GET_BIAS(cur));
        if (cur != NULL) PUT_PRED(cur, GET_BIAS(ptr));
    }
}

/* segragated_list_delete - 将某个块从链表中删除 */
static void segragated_list_delete(void *ptr) {
    if (ptr == NULL) return;
    void *head = segragated_list_search(GET_SIZE(HDRP(ptr)));
    void *pred = GET_PTR(PRED(ptr));
    void *succ = GET_PTR(SUCC(ptr));
    // printf("segragated_list_delete called by %p\n", ptr);
    // printf("head = %p\n", head);
    // printf("pred = %p\n", pred);
    // printf("succ = %p\n", succ);

    if (pred == NULL && succ == NULL) {
        /* 目前根本就不在链表里，也没有必要删除了 */
        return;
    }
    PUT_PRED(ptr, 0);
    PUT_SUCC(ptr, 0);
    
    if (pred == head) {
        PUT(head, GET_BIAS(succ));
        if (succ != NULL) PUT_PRED(succ, GET_BIAS(head));
    }
    else {
        PUT_SUCC(pred, GET_BIAS(succ));
        if (succ != NULL) PUT_PRED(succ, GET_BIAS(pred));
    }
    // printf("check the heap after delete\n");
    // mm_checkheap(514);
}

/* find_fit - 在分离空闲链表中找到一个合适的块 */
static void *find_fit(size_t size) {
    // printf("find_fit called by %ld\n", size);
    /* 总的说来，就是先在自己的大小类链表里面找，有可能找不到的话去更高的大小类里面找 */
    void *head = segragated_list_search(size);
    // printf("segragated_list_search(size) = %p\n", head);
    // printf("head = %p\n", head);
    void *cur = GET_PTR(GET(head));
    // printf("cur = %p\n", cur);
    while (cur != NULL) {
        if (size <= GET_SIZE(HDRP(cur))) return cur;
        cur = GET_PTR(SUCC(cur));
    }
    /* 如果在自己的大小类里面找不到，就去更高的大小类里面找 */
    head = (void *)((char *)head + WSIZE);
    while (head != segragated_listp + 9 * WSIZE) {
        cur = GET_PTR(GET(head));
        // printf("head = %p\n", head);
        // printf("cur = %p\n", cur);
        while (cur != NULL) {
            if (size <= GET_SIZE(HDRP(cur))) return cur;
            cur = GET_PTR(SUCC(cur));
        }
        head = (void *)((char *)head + WSIZE);
    }
    return NULL; // 上面的都找不到，那么肯定是返回NULL了
}

/* place - 把一个块放到合适的位置 */
static void place(void *ptr, size_t size) {
    // printf("place called by %p, %ld\n", ptr, size);
    size_t ptr_size = GET_SIZE(HDRP(ptr));

    /* 先把这个块从链表中删除 */
    segragated_list_delete(ptr);

    /* 如果这个块的大小比我们要求的大，那么就要分割这个块 */
    if (ptr_size > size) {
        /* 我们的长度计算都是包括Header和Footer的 */
        PUT(HDRP(ptr), PACK(size, 1));
        PUT(FTRP(ptr), PACK(size, 1));
        /* 这里我们要把剩下的部分放到分离空闲链表中去 */
        void *new_ptr = NEXT_BLKP(ptr);
        PUT(HDRP(new_ptr), PACK(ptr_size - size, 0));
        PUT(FTRP(new_ptr), PACK(ptr_size - size, 0));
        PUT_PRED(new_ptr, 0);
        PUT_SUCC(new_ptr, 0);
        /* 要把新的new_ptr加入分离链表中，合并之后会插入分离链表的 */
        coalesce(new_ptr);
    }
    /* 如果这个块的大小和我们要求的大小差不多，那么就不用分割了 */
    else {
        PUT(HDRP(ptr), PACK(ptr_size, 1));
        PUT(FTRP(ptr), PACK(ptr_size, 1));
    }
    // printf("\n");
    // printf("check the heap after place\n");
    // mm_checkheap(561);
    // printf("\n");
}