// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// 这是一个用于跟踪物理页帧引用计数的数组。
// 数组的大小为物理内存页帧的数量。
// PHYSTOP 是物理内存的上限地址，KERNBASE 是内核的基地址。
// PGSIZE 是每个页帧的大小（通常是4096字节）。
// (PHYSTOP-KERNBASE)/PGSIZE 计算出了从内核基地址到物理内存上限之间的总页帧数。
// 每个元素存储了相应物理页帧的引用计数，用于管理内存分配和回收。
// 当页帧被多个进程共享时，引用计数用于确定何时可以释放该页帧。
uint64 reference_count[(PHYSTOP-KERNBASE)/PGSIZE];

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  memset(reference_count, 0, sizeof(reference_count));
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    k_incre_ref(p);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  uint64 index = ((uint64)pa-PGROUNDUP((uint64)end)) / PGSIZE;
  if(--reference_count[index] != 0)
    return;
    
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    // pa0 = walkaddr(pagetable, va0);
    uint64 index = (PGROUNDUP((uint64)r)-PGROUNDUP((uint64)end)) / PGSIZE;
    reference_count[index] = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void 
k_incre_ref(void* pa)
{
  // 计算给定物理地址对应的引用计数数组索引
  uint64 index = (PGROUNDUP((uint64)pa) - PGROUNDUP((uint64)end)) / PGSIZE;

  // 增加该物理页帧的引用计数
  reference_count[index]++;
}
