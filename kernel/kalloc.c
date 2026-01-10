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

// the number of superpages for kinit to set aside during initialization
// these superpages will go in freelist_super
int num_superpages = 25;

struct run {
  struct run *next;
};


struct {
  struct spinlock lock;
  struct run *freelist;
  // freelist of superpages
  struct run *freelist_super;
} kmem;

// basically copied kfree but used superpages
void
ksuperfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSUPERPGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist_super;
  kmem.freelist_super = r;
  release(&kmem.lock);
}

// basically the same as kalloc but used superpages
void *
ksuperalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist_super;
  if(r)
    kmem.freelist_super = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSUPERPGSIZE); // fill with junk
  return (void*)r;
}

// pa_end is basically the first byte-level address that should NOT belong to free memory
void
superfreerange(void *pa_start, void *pa_end)
{
  char * p = (char *)pa_start;
  if(((uint64)p % PGSUPERPGSIZE) != 0) {
    panic("superfreerange: misalgined");
  }
  for(; p + PGSUPERPGSIZE <= (char*)pa_end; p += PGSUPERPGSIZE) {
    ksuperfree(p);
  }

  // panic if for some reason we did not reach pa_end
  if(p != pa_end) {
    printf("p:%p\nend:%p\n", (p + PGSUPERPGSIZE), pa_end);
    panic("superfreerange: misalgined");
  }
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // end of kernel image is start of physical memory available for allocation
  void * start = (void*)end;
  // round up start to be superpage aligned
  start = (void*)PGSUPERPGROUNDUP((uint64)start);

  // free <num_superpages> superpages.  
  void * end_superpages = (void*)(start + (num_superpages * PGSUPERPGSIZE));
  superfreerange(start, end_superpages);
  // free the rest of physical memory with regular pages
  freerange(end_superpages, (void*)PHYSTOP);
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
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
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
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

