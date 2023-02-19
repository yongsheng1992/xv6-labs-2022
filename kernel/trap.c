#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fcntl.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } 
  else if (r_scause() == 15) {
    uint64 va = r_stval();
    if (va >= p->sz || pagefault(p->pagetable, va) != 0) {
      setkilled(p);
    }
  }
  else if (r_scause() == 13) {
    uint64 va = r_stval();
    if (va < MMAPBASE) {
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      setkilled(p);
    } else {
      pagefault(p->pagetable, va);
    }
  }
  else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2) {
    if (p->alarm_interval != 0 && ++p->alarm_passed == p->alarm_interval) {
      memmove(p->trapframeepc, p->trapframe, sizeof(struct trapframe));
      p->trapframe->epc = p->alarm_handler;
    }
    yield();
  }

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    }
#ifdef LAB_NET
    else if(irq == E1000_IRQ){
      e1000_intr();
    }
#endif
    else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

int pagefault(pagetable_t pagetable, uint64 va) {
  // invalid va
  if (va >= MAXVA) {
    return -1;
  }
  
  // struct proc *proc = myproc();
  if (va >= MMAPBASE) {
      struct proc *proc = myproc();
      uint64 va = r_stval();
      struct vma *vma = &proc->vma[(va-MMAPBASE)/MMAPMAX];
      int offset = PGROUNDDOWN(va) - vma->vastart + vma->offset;
      // printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), proc->pid);
    // printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      uint64 *pa = (uint64 *)kalloc();
      // printf("kalloc %p\n", pa);
      int perm = 0;
      if (vma->mode & PROT_READ) {
        perm |= PTE_R;
      }
      if (vma->mode & PROT_WRITE) {
        perm |= PTE_W;
      }
      if (vma->mode & PROT_EXEC) {
        perm |= PTE_U;
      }
      memset(pa, 0, PGSIZE);
      ilock(vma->ip);
      readi(vma->ip, 0, (uint64)pa, offset, PGSIZE);
      iunlock(vma->ip);
      if (mappages(proc->pagetable, va, PGSIZE,  (uint64)pa, perm | PTE_U) < 0) {
        return -1;
      }
      return 0;
  }

  pte_t *pte = (pte_t *)walk(pagetable, va, 0);
  // va not mapped
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ) {
    return -1;
  }

  // handle for copyout. if copyout counters non cow page,
  // just return 1
  if ((*pte & PTE_W)) {
    return 1;
  }

  // if not a PTE_M page, this is no need to handle this kind of page fault
  // just return -1
  if ((*pte & PTE_M) == 0) {
    return -1;
  }

  // it is time to handle cow page
  uint64 pa = PTE2PA(*pte);

  if (pa == 0) {
    return -1;
  }
  pagetable = (pde_t*)kalloc();
  if (pagetable == 0) {
    return -1;
  }

  memmove((void *)pagetable, (void *)pa, PGSIZE);
  *pte = (PTE_FLAGS(*pte) & ~PTE_M) | PTE_W | PA2PTE(pagetable);
  kfree((void *)pa);
  return 0;
}