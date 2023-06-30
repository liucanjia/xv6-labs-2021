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

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
#ifdef LAB_LOCK
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
#else
  struct spinlock lock;
  struct run *freelist;
#endif
} kmem;

#ifdef LAB_LOCK
int bufSize = 32;
#endif

void
kinit()
{
#ifdef LAB_LOCK
  char buf[bufSize];
  int n ;

  for(int i = 0; i < NCPU; i++) {
    n = snprintf(buf, bufSize, "kmem%d", i);
    buf[n] = 0;
    initlock(&kmem.lock[i], buf);
  }
#else
  initlock(&kmem.lock, "kmem");
#endif

  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

#ifdef LAB_LOCK
  int hartid;
#endif

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

#ifdef LAB_LOCK
  push_off();
  hartid = cpuid();
  pop_off();

  acquire(&kmem.lock[hartid]);
  r->next = kmem.freelist[hartid];
  kmem.freelist[hartid] = r;
  release(&kmem.lock[hartid]);
#else
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
#endif
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
#ifdef LAB_LOCK
  int hartid;

  push_off();
  hartid = cpuid();
  pop_off();

  acquire(&kmem.lock[hartid]);
  r = kmem.freelist[hartid];
  if(r)
    kmem.freelist[hartid] = r->next;
  release(&kmem.lock[hartid]);


  if(!r) {
    int i = 0;
    for(; i < NCPU; i++) {
      if(i == hartid) 
        continue;
        
      acquire(&kmem.lock[i]);
      r = kmem.freelist[i];
      if(r) {
        kmem.freelist[i] = r->next;
        release(&kmem.lock[i]);
        break;
      }
      release(&kmem.lock[i]);
    }
  }
#else
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
#endif

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
