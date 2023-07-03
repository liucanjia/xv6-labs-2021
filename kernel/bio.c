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

#ifdef LAB_LOCK
#define BUCKETSZ 13
#define LOCKNAMESIZE 32
#define HASH(blockno) (blockno % BUCKETSZ)

extern uint ticks;
#endif

struct {
#ifdef LAB_LOCK
  struct spinlock hashLock;     //搜索哈希表时的全局锁
  struct buf buf[NBUF];
  struct buf bufBucket[BUCKETSZ];
  struct spinlock bucketLock[BUCKETSZ];
#else
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
#endif
} bcache;

void
binit(void)
{
#ifdef LAB_LOCK
  char lockName[LOCKNAMESIZE];
  int n;

  initlock(&bcache.hashLock, "bcacheHashLock");

  for(int i = 0; i < BUCKETSZ; i++) {
    n = snprintf(lockName, LOCKNAMESIZE, "bcacheBucket%d", i);
    lockName[n] = 0;
    initlock(&bcache.bucketLock[i], lockName);
  }

  for(int i = 0; i < BUCKETSZ; i++) {
    bcache.bufBucket[i].prev = &bcache.bufBucket[i];
    bcache.bufBucket[i].next = &bcache.bufBucket[i];
  }

  for(struct buf *b = bcache.buf; b < bcache.buf+NBUF; b++) {
    b->next = bcache.bufBucket[0].next;
    b->prev = &bcache.bufBucket[0];
    bcache.bufBucket[0].next->prev = b;
    bcache.bufBucket[0].next = b;
    initsleeplock(&b->lock, "buffer");
    b->timeStamp = ticks;
  }
#else
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
#endif
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
#ifdef LAB_LOCK
  struct buf *b;
  int bucketIdx = HASH(blockno);

  acquire(&bcache.bucketLock[bucketIdx]);

  for(b = bcache.bufBucket[bucketIdx].next; b != &bcache.bufBucket[bucketIdx]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.bucketLock[bucketIdx]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  release(&bcache.bucketLock[bucketIdx]);
  acquire(&bcache.hashLock);

  // 两个进程同时查找相同的dev, blockno. 第二个进程进到遍历时, 已经分配过buf了, 故再次查找是否buf已存在
  acquire(&bcache.bucketLock[bucketIdx]);

  for(b = bcache.bufBucket[bucketIdx].next; b != &bcache.bufBucket[bucketIdx]; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.bucketLock[bucketIdx]);
      release(&bcache.hashLock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.bucketLock[bucketIdx]);
  
  // 确实未分配, 则寻找可被驱逐的buf进行分配
  struct buf *replaceBuf = 0;
  uint minTimeStamp = ticks;

  // 遍历哈希表
  for(int i = 0; i < BUCKETSZ; i++) {
    acquire(&bcache.bucketLock[i]);

    for(b = bcache.bufBucket[i].next; b != &bcache.bufBucket[i]; b = b->next) {
      if(b->refcnt == 0 && minTimeStamp >= b->timeStamp) {
        replaceBuf = b;
        minTimeStamp = b->timeStamp;
      }
    }

    if(replaceBuf) {
      replaceBuf->dev = dev;
      replaceBuf->blockno = blockno;
      replaceBuf->valid = 0;
      replaceBuf->refcnt = 1;

      if(i != bucketIdx) {
        replaceBuf->next->prev = replaceBuf->prev;
        replaceBuf->prev->next = replaceBuf->next;
        release(&bcache.bucketLock[i]);

        acquire(&bcache.bucketLock[bucketIdx]);
        replaceBuf->next = bcache.bufBucket[bucketIdx].next;
        replaceBuf->prev = &bcache.bufBucket[bucketIdx];
        bcache.bufBucket[bucketIdx].next->prev = replaceBuf;
        bcache.bufBucket[bucketIdx].next = replaceBuf;
        release(&bcache.bucketLock[bucketIdx]);
      } else {
        release(&bcache.bucketLock[i]);
      }

      release(&bcache.hashLock);
      acquiresleep(&replaceBuf->lock);
      return replaceBuf;
    } 

    release(&bcache.bucketLock[i]);
  }

  release(&bcache.hashLock);
#else
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
#endif
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

#ifdef LAB_LOCK
  int bucketIdx = HASH(b->blockno);

  acquire(&bcache.bucketLock[bucketIdx]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timeStamp = ticks;
  }
  release(&bcache.bucketLock[bucketIdx]);
#else
  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
#endif
}

void
bpin(struct buf *b) {
#ifdef LAB_LOCK
  int bucketIdx = HASH(b->blockno);

  acquire(&bcache.bucketLock[bucketIdx]);
  b->refcnt++;
  release(&bcache.bucketLock[bucketIdx]);
#else
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
#endif
}

void
bunpin(struct buf *b) {
#ifdef LAB_LOCK
  int bucketIdx = HASH(b->blockno);

  acquire(&bcache.bucketLock[bucketIdx]);
  b->refcnt--;
  release(&bcache.bucketLock[bucketIdx]);
#else
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
#endif
}


