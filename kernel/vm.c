#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

#ifdef LAB_NET
  // PCI-E ECAM (configuration space), for pci.c
  kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

  // pci.c maps the e1000's registers here.
  kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);
#endif  

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the kernel_pagetable, shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
//
//  Modified to take a parameter tolevel.
//  walk will walk down to tolevel. It is usually 0, 
//  but can be set to 1 for the purposes of mapping superpages
//  (superpage PTE leaves are in the level 1 page table, 
//  since the lower 21 bits are the 2MB offset, not 12 for 4kb

// encode the level at which the the walk stopped
// in the upper 2 bits of PTE
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc, int tolevel)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > tolevel; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
      if(PTE_LEAF(*pte)) {
        //*pte = (*pte) | LEVEL2PTE(level);
        return pte;
      }
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0) {
        return 0;
      }
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  pte_t * pte = &pagetable[PX(tolevel, va)];
  //*pte = (*pte) | LEVEL2PTE(0);
  return pte;
}

// Same as walk but returns the pte_t pte instaed of pte_t * pte
// this way the level can be encoded
//
// Return the address of the PTE in page table pagetable
//
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
//
//  Modified to take a parameter tolevel.
//  walk will walk down to tolevel. It is usually 0, 
//  but can be set to 1 for the purposes of mapping superpages
//  (superpage PTE leaves are in the level 1 page table, 
//  since the lower 21 bits are the 2MB offset, not 12 for 4kb

// encode the level at which the the walk stopped
// in the upper 2 bits of PTE
pte_t 
softwalk(pagetable_t pagetable, uint64 va, int alloc, int tolevel)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > tolevel; level--) {
    pte_t pte = pagetable[PX(level, va)];
    if(pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(pte);
      if(PTE_LEAF(pte)) {
        return pte | LEVEL2PTE(level);
      }
    } else {
      panic("softwalk cannot allocate");
    }
  }
  pte_t pte = pagetable[PX(tolevel, va)];
  return pte | LEVEL2PTE(0);
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

void print_pte(uint64 pte, uint64 pa, int level, uint64 va) {
  char * prefix;
  switch (level) {
    case 2:
    prefix = "..";
    break;
  case 1:
    prefix = ".. ..";
    break;
  case 0:
    prefix = ".. .. ..";
    break;
  default: 
    prefix = "";
    break;

  }

  printf("%s%px pte %px pa %px\n", prefix, (uint64 *)va, (uint64 *)pte, (uint64 *)pa);
}

void vmprint_helper(pagetable_t pagetable, int level, uint64 va) {
  for(int offset = 0; offset < 512; offset++){
    uint64 curr_va = va | ((offset << (9 * level)) << 12);
    pte_t pte = pagetable[offset];
    if(pte & PTE_V) {
      print_pte(pte, PTE2PA(pte), level, curr_va);
    }
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      vmprint_helper((pagetable_t)child, level - 1, curr_va);
    } else if(pte & PTE_V){
      return; // don't recurse when at a level 0 page table
    }
  }
}


//#if defined(LAB_PGTBL) || defined(SOL_MMAP) || defined(SOL_COW)
void
vmprint(pagetable_t pagetable) {
  vmprint_helper(pagetable, 2, 0); 
}
//#endif



// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0) {
    panic("kvmmap");
  }
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
//
// regarding superpages, no assumptions can be made other than va nad size are page aligned
// so even if size > PGSUPERPGSIZE, we might not be able to fit an ALIGNED superpage
// And if we can fit an ALIGNED superpage, va isn't necessarily superpage aligned, 
// so we might need to put pages before and/or after the superpage
//
// therefore, during the loop, we check if address is superpage aligned 
// and if there's room for one.
//
// also, to calculate the last page, (the one to stop on), 
// it needs to be known whether that will be a page or a superpage
//
// if the end address, (va + size), MINUS PGSUPERPGSIZE, is 2MB aligned, 
// AND >= the start address, (va), then the last mapped page will be a superpage
// otherwise the last mapped page will be a regular page
//
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if(size == PGSUPERPGSIZE && (va % PGSUPERPGSIZE) != 0)
    panic("mappages: va not superpage aligned when mapping superpage");

  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size is 0");

  uint64 end = va + size;
  a = va;
  // is a superpage at the end of the allocation range superpage aligned? 
  // If so, the last allocated page will be a superpage
  // Otherwise, the last allocated page will be a page
  // look at function comment for more info
  // TODO: >= is hard
  if ((end - PGSUPERPGSIZE) % PGSUPERPGSIZE == 0 && (end - PGSUPERPGSIZE) >= va) {
    last = va + size - PGSUPERPGSIZE;
  } else {
    last = va + size - PGSIZE;
  }

  int tolevel;
  int i = 0;
  for(;;){
    // TODO: verify the end logic. that <= could be very problematic
    // if there's room for a superpage, map use a superpage
    if(a % PGSUPERPGSIZE == 0 && (a + PGSUPERPGSIZE) <= (va + size)) {
      // tolevel will ALSO tell us, during the scope of this loop, whether or not 
      // we are mapping a superpage
      tolevel = 1;
      i++;
    } else {
      tolevel = 0;
    }

    if((pte = walk(pagetable, a, 1, tolevel)) == 0) {
      return -1;
    }

    if(*pte & PTE_V)
      panic("mappages: remap");
    
    if(tolevel) {
      // SUPERPA2PTE is part of superpages!
      *pte = SUPERPA2PTE(pa) | perm | PTE_V;
    } else {
      *pte = PA2PTE(pa) | perm | PTE_V;
    }

    if(a == last)
      break;

    if(tolevel) {
      a += PGSUPERPGSIZE;
      pa += PGSUPERPGSIZE;
    } else {
      a += PGSIZE;
      pa += PGSIZE;
    }
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

void
copy_from_demoted_superpage(pagetable_t pagetable, uint64 superbase_va, uint64 superbase_pa, int npages)
{
  uint64 pa = superbase_pa;
  uint64 a = superbase_va;
  uint64 va_to_pa;
  pte_t pte;
  int i;
  for(i = 0; i < npages; i++, a+=PGSIZE, pa+=PGSIZE) {
    // get the PTE at the VA
    if((pte = softwalk(pagetable, a, 0, 0)) == 0)
      continue;
    if((pte & PTE_V) == 0) {
      continue;
    }

    va_to_pa = PTE2PA(pte);

    memmove((void*)va_to_pa, (void*)pa, PGSIZE);
  }
  return;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  // TODO: need logic in here for when shriking a superpage
  // When sbrk frees a superpage partially (e.g., freeing the last 4096 bytes of a superpage), you will need to "demote" a super page into regular pages.
  uint64 a;
  pte_t pte;
  pte_t * pteptr;
  int sz = PGSIZE;
  uint64 superbase;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += sz){
    if((pte = softwalk(pagetable, a, 0, 0)) == 0) // leaf page table entry allocated?
      continue;
    if((pte & PTE_V) == 0)  // has physical page been allocated?
      continue;

    // clear the entry
    pteptr = walk(pagetable, a, 0, 0);
    *pteptr = 0;

    if(PTE_LEVEL(pte) == 1) {
      // size will be equal to the distance between a and the end of the superpage
      // we find ourselves in a superpage
      // this will happen once per call to uvmunmap because after this, a is superpage aligned
      // we'll increment size to be from current a to the end of the current superpage
      // oldsz will never be between current a and the end of the current superpage (invariant)
      // sz == 0 implies a is at the beginning of the superpage
      if (a % PGSUPERPGSIZE == 0) {
        superbase = a;
      } else {
        superbase = SUPERPGROUNDDOWN(a);
      }
      // sz is how much to increment a by when done, so what is the size of virtual address range we are freeing
      sz = SUPERPGROUNDUP(a+1) - a;
    } else {
      sz = PGSIZE;
    }
    if(PTE_FLAGS(pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      // if pte is from a superpage mapping
      // PTE2PA effectivley gets the physical address of the base of the superpage
      uint64 pa = PTE2PA(pte);
      if(PTE_LEVEL(pte) == 1) {
        // always free the whole superpage
        // map the addresses from the base of a superpage until a
        // if a is superpage aligned, nothing needs to be remapped
        if(superbase != a) {
          uvmalloc(pagetable, superbase, a, PTE_FLAGS(pte));
          copy_from_demoted_superpage(pagetable, superbase, pa, (a - superbase) / PGSIZE);
        }
        ksuperfree((void*)pa);
      } else {
        kfree((void*)pa);
      }
    }
  }
}


// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;
  int sz;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += sz){
    // TODO: unsure about this <=
    // if address is 2mb aligned and there's room for a superpage
    if(a % PGSUPERPGSIZE == 0 && (a + PGSUPERPGSIZE) <= newsz) {
      // size will also indicate whether or not a superpage is being allocated
      sz = PGSUPERPGSIZE;
    } else {
      sz = PGSIZE;
    }

    if(sz == PGSUPERPGSIZE) {
      mem = ksuperalloc();
    } else {
      mem = kalloc();
    }
    // TODO: superpage implications?
    // mem is zero when there's no more free physical ram
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
#ifndef LAB_SYSCALL
    memset(mem, 0, sz);
 #endif
    if(mappages(pagetable, a, sz, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      // TODO: superpage implications?
      ksuperfree(mem);
      // TODO: superpage implications?
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  // TODO: i don't think this needs to change when it comes to superpages
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // backtrace();
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t pte;
  uint64 pa, i;
  uint flags;
  char *mem;
  int szinc = PGSIZE;

  for(i = 0; i < sz; i += szinc){
    if((pte = softwalk(old, i, 0, 0)) == 0)
      continue;
    if((pte & PTE_V) == 0) {
      continue;
    }
    if(PTE_LEVEL(pte) == 1) {
      szinc = PGSUPERPGSIZE;
    } else {
      szinc = PGSIZE;
    }

    // PTE2PA should still work for superpage PTEs
    pa = PTE2PA(pte);
    flags = PTE_FLAGS(pte);
    if(szinc == PGSUPERPGSIZE) {
      mem = ksuperalloc();
    } else {
      mem = kalloc();
    }
    if(mem == 0) {
      goto err;
    }

    memmove(mem, (char*)pa, szinc);
    if(mappages(new, i, szinc, (uint64)mem, flags) != 0){
      if(szinc == PGSUPERPGSIZE) {
        ksuperfree(mem);
      } else {
        kfree(mem);
      }
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    if((pte = walk(pagetable, va0, 0, 0)) == 0) {
      // printf("copyout: pte should exist %lx %ld\n", dstva, len);
      return -1;
    }


    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
    
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;
  
  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}




// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid or already mapped, or if
// out of physical memory, and physical address if successful.
uint64
vmfault(pagetable_t pagetable, uint64 va, int read)
{
  uint64 mem;
  struct proc *p = myproc();
  

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);
  if(ismapped(pagetable, va)) {
    return 0;
  }
  mem = (uint64) kalloc();
  if(mem == 0)
    return 0;
  memset((void *) mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  return mem;
}

int
ismapped(pagetable_t pagetable, uint64 va) {
  pte_t *pte = walk(pagetable, va, 0, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}



#ifdef LAB_PGTBL
pte_t*
pgpte(pagetable_t pagetable, uint64 va) {
  return walk(pagetable, va, 0, 0);
}
#endif
