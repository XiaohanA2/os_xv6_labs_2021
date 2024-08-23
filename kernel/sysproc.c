#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
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

  backtrace(); // add

  return 0;
}

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

// sys_sigalarm: 设置一个定时器，当定时间隔到达时，执行指定的信号处理函数
uint64 
sys_sigalarm(void) 
{
  int interval;  // 用于存储从用户态传递过来的定时间隔
  uint64 handler;  // 用于存储从用户态传递过来的信号处理函数的地址

  // 获取第一个参数（定时间隔），并将其存储在interval变量中
  if (argint(0, &interval) < 0)
    return -1;  // 如果获取失败，返回错误代码-1

  // 获取第二个参数（信号处理函数的地址），并将其存储在handler变量中
  if (argaddr(1, &handler) < 0)
    return -1;  // 如果获取失败，返回错误代码-1

  // 设置当前进程的定时器间隔和信号处理函数
  myproc()->alarm_interval = interval;  // 将定时间隔赋值给当前进程的alarm_interval变量
  myproc()->alarm_handler = handler;  // 将信号处理函数的地址赋值给当前进程的alarm_handler变量
  return 0;  // 成功返回0
}

// sys_sigreturn: 从信号处理函数返回，并恢复进程执行状态
uint64 
sys_sigreturn(void) 
{
  // 将保存的陷阱帧状态（alarm_trapframe）复制回当前进程的trapframe，以恢复执行状态
  memmove(myproc()->trapframe, &(myproc()->alarm_trapframe), sizeof(struct trapframe));

  // 重置定时器计数器，将alarm_ticks清零
  myproc()->alarm_ticks = 0;
  return 0;  // 成功返回0
}
