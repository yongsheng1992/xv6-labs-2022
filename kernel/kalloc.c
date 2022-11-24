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
  struct spinlock lock;
  struct run *freelist;
  int mapcount[PGTOTAL];
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initmapcount();
  freerange(end, (void*)PHYSTOP);
}

void
initmapcount() {
  int i;
  for(i = 0; i < PGTOTAL; i++) {
    kmem.mapcount[i] = 1;
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  // if this page is mapped more than one process, just descrease mapcount and return.
  decmapcount(pa);
  if (kmem.mapcount[PPN(pa)] > 0) {
    release(&kmem.lock);
    return;
  }
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;

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
  if(r) {
    incmapcount(r, 0);
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint64
kfreemem(void) {
  uint64 count = 0;
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  while (r) {
    r = r->next;
    count += PGSIZE;
  }
  release(&kmem.lock);
  return count;
}

// increase page map count if a child process calls mappages.
void
incmapcount(void *pa, int lock) {
  if (lock == 1) {
    acquire(&kmem.lock);
  }
  kmem.mapcount[PPN(pa)] += 1;
  if (lock == 1) {
    release(&kmem.lock);
  }
}

// decrease page map count if a page fault happen or child process terminate.
void
decmapcount(void *pa) {
  kmem.mapcount[PPN(pa)] -= 1;
}

int
getmapcount(void *pa) {
  return kmem.mapcount[PPN(pa)];
}