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

struct km {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int mapcount[PGTOTAL];
} kmapcount;

struct km kmems[NCPU];
char klocknames[NCPU][6];

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initmapcount();
  freerange(end, (void*)PHYSTOP);

  int i;
  for (i = 0; i < NCPU; i++) {
    snprintf(klocknames[i], 6, "kmem-%d", i);
    initlock(&kmems[i].lock, klocknames[i]);
  }
}

void
initmapcount() {
  int i;
  for(i = 0; i < PGTOTAL; i++) {
    kmapcount.mapcount[i] = 0;
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kmapcount.mapcount[PPN(p)] = 1;
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
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);
  // if this page is mapped more than one process, just descrease mapcount and return.
  decmapcount(pa);
  if (kmapcount.mapcount[PPN(pa)] > 0) {
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
  kmapcount.mapcount[PPN(pa)] -= 1;
}

int
getmapcount(void *pa) {
  return kmapcount.mapcount[PPN(pa)];
}