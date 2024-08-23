#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  // 定义变量：地址、长度和位掩码
  uint64 addr;
  int len;
  int bitmask;

  // 从用户态获取第一个参数：虚拟地址
  if(argaddr(0, &addr) < 0){
    return -1;  // 获取失败则返回-1
  }

  // 从用户态获取第二个参数：长度（页数）
  if(argint(1, &len) < 0){
    return -1;  // 获取失败则返回-1
  }

  // 从用户态获取第三个参数：位掩码（存放结果的用户地址）
  if(argint(2, &bitmask) < 0){
    return -1;  // 获取失败则返回-1
  }

  // 检查长度是否超出合理范围，最多检查32页
  if(len > 32 || len < 0){
    return -1;  // 超出范围也返回-1
  }

  int res = 0;  // 结果初始化为0
  struct proc *p = myproc();  // 获取当前进程的进程结构

  // 遍历每一个页，计算访问状态
  for(int i = 0; i < len; i++){
    int va = addr + i * PGSIZE;  // 计算当前页的虚拟地址
    int abit = vm_pgaccess(p->pagetable, va);  // 检查当前页的访问情况
    res = res | (abit << i);  // 将结果位按页序存入结果变量res中
  }

  // 将结果复制回用户空间的位掩码变量中
  if(copyout(p->pagetable, bitmask, (char*)&res, sizeof(res)) < 0){
    return -1;  // 复制失败返回-1
  }

  return 0;  // 执行成功返回0
}

#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
