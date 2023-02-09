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
};

struct {
  struct spinlock lock;
  int mapcount[PGTOTAL];
} kmapcount;

struct km kmems[NCPU];

void
kinit()
{
  initlock(&kmapcount.lock, "kmem-map");
  initmapcount();
  int i;
  for (i = 0; i < NCPU; i++) {
    initlock(&kmems[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
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
    // printf("%d\n", PPN(p));
    kfree(p);
  }
  printf("freerange!!!!\n");
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  // printf("kfree start...\n");
  struct run *r;
  int total = PPN((uint64)PHYSTOP);
  int sz = (total + NCPU - 1) / NCPU;
  int index = (PPN(pa)) / sz;
  // printf("%d total %d sz=%d hart=%d ppn=%d start=%d\n", PPN(pa), total, sz, index, PPN(pa), PPN((uint64)end));
  // printf("index=%d %s\n", index, kmems[index].lock.name);
  acquire(&kmems[index].lock);
  decmapcount(pa);
  if (kmapcount.mapcount[PPN(pa)] > 0) {
    release(&kmems[index].lock);
    return;
  }

  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  r->next = kmems[index].freelist;
  kmems[index].freelist = r;
  release(&kmems[index].lock);
  // printf("kfree end...\n");
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
  pop_off();
  acquire(&kmems[hart].lock);
  r = kmems[hart].freelist;
  if(r) {
    incmapcount(r, 0);
    kmems[hart].freelist = r->next;
  }
  release(&kmems[hart].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

uint64
kfreemem(void) {
  uint64 count = 0;
  struct run *r;
  int i = 0;
  for (i = 0; i < NCPU; i++) {
    acquire(&kmems[i].lock);
    r = kmems[i].freelist;
    while (r) {
      r = r->next;
      count += PGSIZE;
    }
    release(&kmems[i].lock);
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
  kmapcount.mapcount[PPN(pa)] -= 1;
}

int
getmapcount(void *pa) {
  return kmapcount.mapcount[PPN(pa)];
}