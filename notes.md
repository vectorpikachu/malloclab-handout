# Malloclab的笔记

## 评分规则

首先会计算出来一个performance index，然后根据这个index来给分，这个index的计算公式如下：

$$
P(U,T)=100(w\min (1, \frac{U-U_{min}}{U_{max}-U_{min}})+ (1-w)\min (1, \frac{T-T_{min}}{T{max}-T_{min}}))
$$

其中 $U$ 为utilization， $T$ 为throughput， $w$ 为权重， $U_{min}$ 为最小utilization， $U_{max}$ 为最大utilization， $T_{min}$ 为最小throughput， $T_{max}$ 为最大throughput，对于一个优化的malloc包来说。

$w=0.60$ ， $(U_{min},U_{max},T_{min},T_{max})=(0.70,0.90,4000\text{Kop/s},14000\text{Kop/s})$ 。

```
if ($perfindex < 50) {
    $perfpoints = 0;
}
else {
    $perfpoints = $perfindex;
}
```

## 基础概念

### 块的结构

```
|Header|Payload|Padding|Footer|
^p     ^bp
```

其中`bp`就是`mm_malloc`要返回的指针。

### 内存模型

```
|Padding|Prologue Header|Prologue Footer|Normal Block|...|Epilogue Header|
                        ^heap_listp
```

### 空闲链表的结构

隐式空闲链表：Root->Block1->Block2->...->Blockn

显式空闲链表：Root->Free Block1->Free Block2->...->Free Blockn

这个时候，要在`Header`后面加入`pred`和`succ`两个指针，指向前一个和后一个空闲块。

分离式空闲链表：每个空闲块都在自己的大小类中，每个大小类都有一个空闲链表。

### 合并策略

立即合并(Immediate Coalescing)和延迟合并(Deffered Coalescing)。

### 插入策略

LIFO：后进先出，直接插入开头处。

按地址顺序(Address Order)：按地址顺序插入。

### 放置（搜索）策略

首次适配(First Fit)：从头开始搜索，找到第一个合适的就停止。配合LIFO插入策略。

下一次适配(Next Fit)：从上次搜索的地方开始搜索，找到第一个合适的就停止。配合LIFO插入策略。

最佳适配(Best Fit)：从头开始搜索，找到最合适的就停止。配合按地址顺序插入策略。

## 实验思路和实现

一些基础的书上的代码，而且我们要注意到Header和Footer这一共DSIZE的字节，是也算在当前块的大小里面的。

```c
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
```

[MallocLab实验](https://www.cnblogs.com/liqiuhao/p/8252373.html)

```
The realloc() function changes the size of the memory block pointed to by ptr to size bytes.  The contents will  be  unchanged  in  the
       range  from the start of the region up to the minimum of the old and new sizes.  If the new size is larger than the old size, the added
       memory will not be initialized.  If ptr is NULL, then the call is equivalent to malloc(size), for all values of size; if size is  equal
       to  zero,  and ptr is not NULL, then the call is equivalent to free(ptr).  Unless ptr is NULL, it must have been returned by an earlier
       call to malloc(), calloc(), or realloc().  If the area pointed to was moved, a free(ptr) is done.
```