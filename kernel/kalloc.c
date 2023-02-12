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
void initmapcount();

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} km;

struct {
  struct spinlock lock;
  int mapcount[PGTOTAL];
} kmapcount;

void
kinit()
{
  initmapcount();
  int i;
  for (i = 0; i < NCPU; i++) {
    initlock(&km.lock[i], "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
initmapcount() {
  initlock(&kmapcount.lock, "kmem-map");
  int i;
  for(i = 0; i < PGTOTAL; i++) {
    kmapcount.mapcount[i] = 1;
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  push_off();
  struct run *r;
  int hart = cpuid();
  acquire(&km.lock[hart]);
  decmapcount(pa);
  if (kmapcount.mapcount[PPN(pa)] > 0) {
    release(&km.lock[hart]);
    pop_off();
    return;
  }

  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  r->next = km.freelist[hart];
  km.freelist[hart] = r;
  release(&km.lock[hart]);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  int hart = cpuid();
  acquire(&km.lock[hart]);
  r = km.freelist[hart];
  if(r) {
    incmapcount(r, 0);
    km.freelist[hart] = r->next;
    memset((char*)r, 5, PGSIZE);
    release(&km.lock[hart]);
    pop_off();
    return (void*)r;
  }

  release(&km.lock[hart]);

  int i;
  for (i = 0; i < NCPU; i++) {
    if (i == hart) {
      continue;
    }
    acquire(&km.lock[i]);
    r = km.freelist[i];
    if (r) {
      incmapcount(r, 0);
      km.freelist[i] = r->next;
      memset((char*)r, 5, PGSIZE);
      release(&km.lock[i]);
      pop_off();
      return r;
    }
    release(&km.lock[i]);
  }

  pop_off();
  return (void*)r;
}

uint64
kfreemem(void) {
  uint64 count = 0;
  struct run *r;
  int i = 0;
  for (i = 0; i < NCPU; i++) {
    acquire(&km.lock[i]);
    r = km.freelist[i];
    while (r) {
      r = r->next;
      count += PGSIZE;
    }
    release(&km.lock[i]);
  }
  return count;
}

// increase page map count if a child process calls mappages.
void
incmapcount(void *pa, int lock) {
  if (lock == 1) {
    acquire(&kmapcount.lock);
  }
  kmapcount.mapcount[PPN(pa)] += 1;
  if (lock == 1) {
    release(&kmapcount.lock);
  }
}

// decrease page map count if a page fault happen or child process terminate.
void
decmapcount(void *pa) {
  acquire(&kmapcount.lock);
  kmapcount.mapcount[PPN(pa)] -= 1;
  release(&kmapcount.lock);
}

int
getmapcount(void *pa) {
  return kmapcount.mapcount[PPN(pa)];
}