// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
extern struct superblock sb;

struct {
  struct spinlock lock;
  struct buf buf[MAXOPBLOCKS];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache[NBUCKET];

void
binit(void)
{
  struct buf *b;
  int i;
  // Create linked list of buffers
  for (i = 0; i < NBUCKET; i++) {
    initlock(&bcache[i].lock, "bcache");
    bcache[i].head.prev = &bcache[i].head;
    bcache[i].head.next = &bcache[i].head;
    for(b = bcache[i].buf; b < bcache[i].buf+MAXOPBLOCKS; b++){
      b->next = bcache[i].head.next;
      b->prev = &bcache[i].head;
      initsleeplock(&b->lock, "buffer");
      bcache[i].head.next->prev = b;
      bcache[i].head.next = b;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bindex = (blockno / 10 % NBUCKET);
  acquire(&bcache[bindex].lock);

  // Is the block already cached?
  for(b = bcache[bindex].head.next; b != &bcache[bindex].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[bindex].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache[bindex].head.prev; b != &bcache[bindex].head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache[bindex].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int bindex = b->blockno / 10 % NBUCKET;
  acquire(&bcache[bindex].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache[bindex].head.next;
    b->prev = &bcache[bindex].head;
    bcache[bindex].head.next->prev = b;
    bcache[bindex].head.next = b;
  }
  
  release(&bcache[bindex].lock);
}

void
bpin(struct buf *b) {
  int bindex = b->blockno / 10 % NBUCKET;
  acquire(&bcache[bindex].lock);
  b->refcnt++;
  release(&bcache[bindex].lock);
}

void
bunpin(struct buf *b) {
  int bindex = b->blockno / 10 % NBUCKET;
  acquire(&bcache[bindex].lock);
  b->refcnt--;
  release(&bcache[bindex].lock);
}


