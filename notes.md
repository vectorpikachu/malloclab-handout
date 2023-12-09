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

似乎指向块里的指针都是`char *`的，一般的指针可以都设置成为`void *`的。

我们刚才用网上给出的方法解决了基本的代码，优化啥的还没做。

但是！！！！为什么网络上的都在使用`WSIZE`的大小来存储地址呢？他们的做法是不对的！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！！

所以我们可以注意到给出的Writeup里面有这样的一句话：

```
Because we are running on 64-bit machines, your allocator must be coded accordingly, with one exception: the size of the heap will never be greater than or equal to 232 bytes. This does not imply anything about the location of the heap, but there is a neat optimization that can be done using this information. However, be very, very careful if you decide to take advantage of this fact. There are certain invalid optimizations that will pass all the driver checks because of the limited range of functionality we can check in a reasonable amount of time, so we will be manually looking over your code for these violations.
```

所以最后就是说，我们实际上可以把和地址有关的地方都改变成为`ptr - the_very_first_address`的形式，这样就可以节省很多空间了。

```bash
Using default tracefiles in ./traces/
Measuring performance with a cycle counter.
Processor clock rate ~= 2000.0 MHz
mm_init called
heap_listp = 0x800000000
heap_listp = 0x800000028
new block bp = 0x800000030
new block size = 4096
segragated_list_insert called by 0x800000030
head = 0x80000001c
GET(head) = 52
actual address in head = 0x800000030
ptr's pred = 0x80000001c
ptr's succ = (nil)
Check heap at line 445
Heap (0x800000028):
Prologue header: [8:1] footer: [8:1]
0x800000028: header: [8:1] footer: [8:1]
0x800000030: header: [4096:0] footer: [4096:0]
Epilogue header: [0:1]
segragated_listp + 0 * WSIZE = 0x800000000
head = 0x800000000
head -> (nil)
segragated_listp + 1 * WSIZE = 0x800000004
head = 0x800000004
head -> (nil)
segragated_listp + 2 * WSIZE = 0x800000008
head = 0x800000008
head -> (nil)
segragated_listp + 3 * WSIZE = 0x80000000c
head = 0x80000000c
head -> (nil)
segragated_listp + 4 * WSIZE = 0x800000010
head = 0x800000010
head -> (nil)
segragated_listp + 5 * WSIZE = 0x800000014
head = 0x800000014
head -> (nil)
segragated_listp + 6 * WSIZE = 0x800000018
head = 0x800000018
head -> (nil)
segragated_listp + 7 * WSIZE = 0x80000001c
head = 0x80000001c
head -> 0x800000030
cur = 0x800000030
cur's pred = 0x80000001c
cur's succ = (nil)
segragated_listp + 8 * WSIZE = 0x800000020
head = 0x800000020
head -> (nil)
Check heap at line 364
Heap (0x800000028):
Prologue header: [8:1] footer: [8:1]
0x800000028: header: [8:1] footer: [8:1]
0x800000030: header: [4096:0] footer: [4096:0]
Epilogue header: [0:1]
segragated_listp + 0 * WSIZE = 0x800000000
head = 0x800000000
head -> (nil)
segragated_listp + 1 * WSIZE = 0x800000004
head = 0x800000004
head -> (nil)
segragated_listp + 2 * WSIZE = 0x800000008
head = 0x800000008
head -> (nil)
segragated_listp + 3 * WSIZE = 0x80000000c
head = 0x80000000c
head -> (nil)
segragated_listp + 4 * WSIZE = 0x800000010
head = 0x800000010
head -> (nil)
segragated_listp + 5 * WSIZE = 0x800000014
head = 0x800000014
head -> (nil)
segragated_listp + 6 * WSIZE = 0x800000018
head = 0x800000018
head -> (nil)
segragated_listp + 7 * WSIZE = 0x80000001c
head = 0x80000001c
head -> 0x800000030
cur = 0x800000030
cur's pred = 0x80000001c
cur's succ = (nil)
segragated_listp + 8 * WSIZE = 0x800000020
head = 0x800000020
head -> (nil)
coalesce called by 0x800000030
prev = 0x800000028
next = 0x800001030
prev_alloc = 1
next_alloc = 1
size = 4096
heap_listp = 0x800000028
malloc called by 16
adjusted_size = 16
find_fit called by 16
head = 0x800000000
cur = (nil)
head = 0x800000004
cur = (nil)
head = 0x800000008
cur = (nil)
head = 0x80000000c
cur = (nil)
head = 0x800000010
cur = (nil)
head = 0x800000014
cur = (nil)
head = 0x800000018
cur = (nil)
head = 0x80000001c
cur = 0x800000030
find_fit ptr = 0x800000030
place called by 0x800000030, 16
coalesce called by 0x800000040
prev = 0x800000030
next = 0x800001030
prev_alloc = 1
next_alloc = 1
size = 4080
Check heap at line 561
Heap (0x800000028):
Prologue header: [8:1] footer: [8:1]
0x800000028: header: [8:1] footer: [8:1]
0x800000030: header: [16:1] footer: [16:1]
0x800000040: header: [4080:0] footer: [4080:0]
Epilogue header: [0:1]
segragated_listp + 0 * WSIZE = 0x800000000
head = 0x800000000
head -> (nil)
segragated_listp + 1 * WSIZE = 0x800000004
head = 0x800000004
head -> (nil)
segragated_listp + 2 * WSIZE = 0x800000008
head = 0x800000008
head -> (nil)
segragated_listp + 3 * WSIZE = 0x80000000c
head = 0x80000000c
head -> (nil)
segragated_listp + 4 * WSIZE = 0x800000010
head = 0x800000010
head -> (nil)
segragated_listp + 5 * WSIZE = 0x800000014
head = 0x800000014
head -> (nil)
segragated_listp + 6 * WSIZE = 0x800000018
head = 0x800000018
head -> (nil)
segragated_listp + 7 * WSIZE = 0x80000001c
head = 0x80000001c
head -> (nil)
segragated_listp + 8 * WSIZE = 0x800000020
head = 0x800000020
head -> (nil)
Error: free_count != free_count_in_list
malloc called by 1234
adjusted_size = 1240
find_fit called by 1240
head = 0x800000014
cur = (nil)
head = 0x800000018
cur = (nil)
head = 0x80000001c
cur = (nil)
head = 0x800000020
cur = (nil)
new block bp = 0x800001030
new block size = 4096
segragated_list_insert called by 0x800001030
head = 0x80000001c
GET(head) = 4148
actual address in head = 0x800001030
ptr's pred = 0x80000001c
ptr's succ = (nil)
Check heap at line 445
Heap (0x800000028):
Prologue header: [8:1] footer: [8:1]
0x800000028: header: [8:1] footer: [8:1]
Error: Header and Footer do not match
0x800000030: header: [16:1] footer: [1084056136:1]
段错误 (核心已转储)
```

```bash
Using default tracefiles in ./traces/
Measuring performance with a cycle counter.
Processor clock rate ~= 2000.0 MHz
mm_init called
heap_listp = 0x800000000
heap_listp = 0x800000030
new block bp = 0x800000030
new block size = 4096
segragated_list_insert called by 0x800000030
head = 0x80000001c
GET(head) = 52
actual address in head = 0x800000030
ptr's pred = 0x80000001c
ptr's succ = (nil)
Check heap at line 445
Heap (0x800000030):
Bad prologue header
Prologue header: [4096:0] footer: [4096:0]
0x800000030: header: [4096:0] footer: [4096:0]
Epilogue header: [0:1]
segragated_listp + 0 * WSIZE = 0x800000000
head = 0x800000000
head -> (nil)
segragated_listp + 1 * WSIZE = 0x800000004
head = 0x800000004
head -> (nil)
segragated_listp + 2 * WSIZE = 0x800000008
head = 0x800000008
head -> (nil)
segragated_listp + 3 * WSIZE = 0x80000000c
head = 0x80000000c
head -> (nil)
segragated_listp + 4 * WSIZE = 0x800000010
head = 0x800000010
head -> (nil)
segragated_listp + 5 * WSIZE = 0x800000014
head = 0x800000014
head -> (nil)
segragated_listp + 6 * WSIZE = 0x800000018
head = 0x800000018
head -> (nil)
segragated_listp + 7 * WSIZE = 0x80000001c
head = 0x80000001c
head -> 0x800000030
cur = 0x800000030
cur's pred = 0x80000001c
cur's succ = (nil)
segragated_listp + 8 * WSIZE = 0x800000020
head = 0x800000020
head -> (nil)
Check heap at line 364
Heap (0x800000030):
Bad prologue header
Prologue header: [4096:0] footer: [4096:0]
0x800000030: header: [4096:0] footer: [4096:0]
Epilogue header: [0:1]
segragated_listp + 0 * WSIZE = 0x800000000
head = 0x800000000
head -> (nil)
segragated_listp + 1 * WSIZE = 0x800000004
head = 0x800000004
head -> (nil)
segragated_listp + 2 * WSIZE = 0x800000008
head = 0x800000008
head -> (nil)
segragated_listp + 3 * WSIZE = 0x80000000c
head = 0x80000000c
head -> (nil)
segragated_listp + 4 * WSIZE = 0x800000010
head = 0x800000010
head -> (nil)
segragated_listp + 5 * WSIZE = 0x800000014
head = 0x800000014
head -> (nil)
segragated_listp + 6 * WSIZE = 0x800000018
head = 0x800000018
head -> (nil)
segragated_listp + 7 * WSIZE = 0x80000001c
head = 0x80000001c
head -> 0x800000030
cur = 0x800000030
cur's pred = 0x80000001c
cur's succ = (nil)
segragated_listp + 8 * WSIZE = 0x800000020
head = 0x800000020
head -> (nil)
coalesce called by 0x800000030
prev = 0x800000028
next = 0x800001030
prev_alloc = 0
next_alloc = 1
size = 4096
segragated_list_insert called by 0x800000028
head = 0x80000001c
heap_listp = 0x800000030
malloc called by 16
adjusted_size = 16
find_fit called by 16
head = 0x800000000
cur = (nil)
head = 0x800000004
cur = (nil)
head = 0x800000008
cur = 0x8000ffffc
head = 0x80000000c
cur = (nil)
head = 0x800000010
cur = (nil)
head = 0x800000014
cur = (nil)
head = 0x800000018
cur = (nil)
head = 0x80000001c
cur = 0x800000028
find_fit ptr = 0x800000028
place called by 0x800000028, 16
coalesce called by 0x800000038
prev = 0x800000028
next = 0x800001028
prev_alloc = 1
next_alloc = 0
size = 4080
段错误 (核心已转储)
```

```bash
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/short1.rep
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver
段错误 (核心已转储)
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/short2.rep
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/short1-bal.rep
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/short2-bal.rep
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/stty.rep
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/tty.rep
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/xterm.rep
段错误 (核心已转储)
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/alaska.rep
段错误 (核心已转储)
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/bash.rep
段错误 (核心已转储)
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ ./mdriver -c ./traces/expr.rep
vectorpikachu@vectorpikachu-virtual-machine:~/Desktop/malloclab-handout$ 
```