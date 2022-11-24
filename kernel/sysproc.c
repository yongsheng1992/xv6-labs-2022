#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
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
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
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
  // backtrace(); comment for leater labs.
  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
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


uint
sys_pgaccess(void)
{
  uint64 p, a, res;
  int n;
  struct proc *proc = myproc();
  unsigned int mask = 0;

  argaddr(0, &p);
  argint(1, &n);
  argaddr(2, &res);
  
  a = PGROUNDDOWN(p);
  for (int i = 0; i < n; i++) {
    pte_t *pte = walk(proc->pagetable, a, 0);
    if (*pte & PTE_A) {
      mask |= (1 << i);
      *pte = *pte & ~PTE_A;
    }
    a += PGSIZE;
  }

  if (copyout(proc->pagetable, res, (char *)&mask, sizeof(mask)) < 0) {
    return -1;
  }
  return 0;
}
