// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
int rmap[(PHYSTOP-EXTMEM)/PGSIZE];// for virtual address, subtract kernbase+extmem and divide by pagesize
void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  uint num_free_pages;  //store number of free pages
  struct run *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void init_rmap(char* v)
{
  if(V2P(v)<EXTMEM)
  panic("init rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  rmap[x]=1;
}
int check_rmap(char* v)
{
  if(V2P(v)<EXTMEM)
  panic("check rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  return rmap[x];
}
void inc_rmap(char* v)
{
  if(V2P(v)<EXTMEM)
  panic("inc rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  rmap[x]++;
}
void dec_rmap(char* v)
{
  if(V2P(v)<EXTMEM)
  panic("dec rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  rmap[x]--;
  // if(rmap[x]<0)
  // panic("dec rmap negative\n");
}

void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
  {
    kfree(p);
    // kmem.num_free_pages+=1;
  }
    
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");
  dec_rmap(v);
  if(check_rmap(v)>0)
    return;
  // if(check_rmap(v)!=0)
  // return;
  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);// virtual address of the physical page

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.num_free_pages+=1;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    kmem.num_free_pages-=1;
  }
    
  if(kmem.use_lock)
    release(&kmem.lock);
  init_rmap((char*)r);
  return (char*)r;
}
uint 
num_of_FreePages(void)
{
  acquire(&kmem.lock);

  uint num_free_pages = kmem.num_free_pages;
  
  release(&kmem.lock);
  
  return num_free_pages;
}
