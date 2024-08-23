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

// 为每个CPU分配kmem
struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // N 个 CPU 各维护一段

void
kinit()
{
  char lockname[10];  // 定义一个字符数组用于存储锁的名称

  // 为每个CPU初始化一个内存分配锁
  for (int i = 0; i < NCPU; i++)
  {
    // 使用 snprintf 生成每个 CPU 对应的锁名称，并存储在 lockname 数组中
    snprintf(lockname, 10, "kmem_CPU%d", i);

    // 为每个 kmem 数组中的元素初始化锁，确保每个 CPU 都有自己的内存分配锁
    initlock(&kmem[i].lock, lockname);
  }

  // 初始化并释放内核可用的物理内存范围
  freerange(end, (void*)PHYSTOP);
}


void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// 负责释放内存，将其归还到当前 CPU 的空闲内存链表中。
void
kfree(void *pa)
{
  struct run *r;  // 定义一个指向内存块的指针，将其作为链表节点
  int cpu_id;  // 用于存储当前 CPU 的 ID

  // 检查传入的内存地址是否有效
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 用垃圾数据填充内存块，帮助捕捉悬空引用问题
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;  // 将内存块转换为 `run` 结构，准备插入空闲链表

  // 获取当前 CPU 的 ID，并关闭中断以确保获取过程的原子性
  push_off();  // 关闭中断，防止上下文切换干扰
  cpu_id = cpuid();  // 获取当前 CPU 的 ID
  pop_off();  // 恢复中断状态

  // 锁定当前 CPU 的空闲列表，以确保操作的线程安全
  acquire(&kmem[cpu_id].lock);

  // 将当前内存块插入到当前 CPU 的空闲链表中
  r->next = kmem[cpu_id].freelist;  // 将内存块的 `next` 指针指向当前的空闲列表头
  kmem[cpu_id].freelist = r;  // 更新空闲列表头为当前内存块

  // 释放锁，允许其他线程或进程访问空闲列表
  release(&kmem[cpu_id].lock);
}


// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;  // 用于存储从空闲列表中分配出的内存块
  int cpu_id;

  // 获取当前 CPU 的 ID，并关闭中断以确保操作的原子性
  push_off();  // 关闭中断，防止上下文切换干扰
  cpu_id = cpuid();  // 获取当前 CPU 的 ID
  pop_off();  // 恢复中断状态

  // 锁定当前 CPU 的空闲列表，以保证对空闲内存的安全访问
  acquire(&kmem[cpu_id].lock);
  r = kmem[cpu_id].freelist;  // 尝试从当前 CPU 的空闲列表中获取一个内存块

  // 如果当前 CPU 的空闲列表中有可用的内存块
  if (r) {
    kmem[cpu_id].freelist = r->next;  // 将空闲列表的指针移动到下一个内存块
  }
  else {
    // 如果当前 CPU 的空闲列表为空，尝试从其他 CPU 的空闲列表中“窃取”一个内存块
    for (int i = 0; i < NCPU; i++) {
      if (i == cpu_id) {
        continue;  // 跳过当前 CPU，继续检查其他 CPU
      }
      acquire(&kmem[i].lock);  // 锁定其他 CPU 的空闲列表
      r = kmem[i].freelist;  // 从其他 CPU 的空闲列表中获取一个内存块
      if (r) {
        kmem[i].freelist = r->next;  // 如果获取成功，更新其他 CPU 的空闲列表
      }
      release(&kmem[i].lock);  // 释放其他 CPU 的空闲列表锁
      if (r) {
        break;  // 如果成功获取到一个内存块，则停止查找
      }
    }
  }
  
  // 释放当前 CPU 的空闲列表锁
  release(&kmem[cpu_id].lock);

  // 如果成功获取到一个内存块，则将其填充为5（用于调试）
  if (r)
    memset((char*)r, 5, PGSIZE);  // 用垃圾值填充整个内存块

  return (void*)r;  // 返回分配的内存块，如果没有可用内存块则返回 NULL
}
