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

#define PA2INDEX(x) (x-PGROUNDUP(KERNBASE))/PGSIZE

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  uint16  palist[(PHYSTOP-KERNBASE)/PGSIZE];
  struct spinlock lock;
}kref;

void kref_inc(void * pa){
    int index=PA2INDEX((uint64)pa);
    acquire(&kref.lock);
    kref.palist[index]++;
    release(&kref.lock);
}
uint16 kref_dec(void * pa){
    int index=PA2INDEX((uint64)pa);
    uint16 count;
    acquire(&kref.lock);
    count=--kref.palist[index];
    release(&kref.lock);
    return count;
}
uint16 kref_get(void * pa){
    int index=PA2INDEX((uint64)pa);
    acquire(&kref.lock);
    uint16 count=kref.palist[index];
    release(&kref.lock);
    return count;
}
void kref_set(void * pa,uint16 val){
    int index=PA2INDEX((uint64)pa);
    acquire(&kref.lock);
    kref.palist[index]=val;
    release(&kref.lock);
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");
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
    uint16 count=kref_get(pa);
    if(count<=1){
      memset(pa, 1, PGSIZE);
      r = (struct run*)pa;
      r->next = kmem.freelist;
      kmem.freelist = r;
    }
    
    if(count>=1){
      kref_dec(pa);
    }
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
  if(r){
    kmem.freelist = r->next;
  }
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
    kref_set((void *)r, 1);
  }
  return (void*)r;
}

