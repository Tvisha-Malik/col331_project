// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

struct rmap_list rmap[(PHYSTOP-EXTMEM)/PGSIZE][NPROC];// for virtual address, subtract kernbase+extmem and divide by pagesize

// struct {
//   struct spinlock lock;
// 	struct rmap_list rmap[(PHYSTOP-EXTMEM)/PGSIZE][NPROC];// for virtual address, subtract kernbase+extmem and divide by pagesize
// } rmap_table;

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

// For virtual addr v, process pid, it sets rmap of page of v as available for all processes except pid
void init_rmap(char* v, int pid)
{
  if(V2P(v)<EXTMEM)
    panic("init rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  for(int i=0; i<NPROC;i++)
  {
    rmap[x][i].available=1;
  }
  rmap[x][0].available=0;
  rmap[x][0].pid=pid;

}

// Checks if the page is set as unavailable for the process pid
// to determine whether the process has a page mapped to it.
int check_rmap(char* v, int pid)// 1 if present 0 otherwise
{
  if(V2P(v)<EXTMEM)
    panic("check rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  for(int i=0; i<NPROC;i++)
  {
    if((rmap[x][i].available==0) && (rmap[x][i].pid==pid))
    return 1;
    
  }
  return 0;
  // panic("rmap dec not found \n");
}

// Count the number of processes using the page
int count_rmap(char* v)
{
  if(V2P(v)<EXTMEM)
    panic("count_rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  int ans=0;
  for(int i=0; i<NPROC; i++)
  {
    if(rmap[x][i].available==0)// not available
    ans++;
  }
  return ans;
}

// Add process pid to the rmap of the page
void inc_rmap(char* v, int pid)
{
  if(V2P(v)<EXTMEM)
    panic("inc rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  for(int i=0; i<NPROC;i++)
  {
    if(rmap[x][i].available==1)// if available
    {
      rmap[x][i].available=0;
      rmap[x][i].pid=pid;
      return;
    }
  }
  panic("not free rmap slot");
}

// Remove process pid from the rmap of the page
void dec_rmap(char* v, int pid)
{
  if(V2P(v)<EXTMEM)
    panic("dec rmap\n");
  uint x=(V2P(v)-EXTMEM)/PGSIZE;
  for(int i=0; i<NPROC;i++)
  {
    if((rmap[x][i].available==0) && (rmap[x][i].pid==pid))
    {
      rmap[x][i].available=1;
      return;
    }
  }
  panic("rmap dec not found \n");
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
    kfree(p, -1,0);
    // kmem.num_free_pages+=1;
  }
    
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// @param set: whether the rmap is already set.
void
kfree(char *v, int pid, int set)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");
  if(set)
  {
    if(check_rmap(v, pid)==0)
      panic("no page mapped to it\n");
    dec_rmap(v,pid);
    if(count_rmap(v)!=0)// if other processes
      return;
  }
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
// @param set: whether the rmap is already set.
char*
kalloc(int pid, int set)
{
  struct run *r;

  while(!kmem.freelist)
  {
    cprintf("before swapout \n");
    swap_out();
  }

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
  if(set)// if not set then dont initialise it
    init_rmap((char*)r, pid);
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


void update_rmap_swap_out(int idx, uint physicalAddress, uint blockno, struct swap_slot *slot){
  // uint physicalAddress = PTE_ADDR(*vp);
  physicalAddress-=EXTMEM;
  for(int i=0;i<NPROC;i++){ // Iterating through rmap slots
    int pid = rmap[physicalAddress/PGSIZE][i].pid;
    if (rmap[physicalAddress/PGSIZE][i].available == 1)
      pid = -1;
    slot->rmap_pid[i] = pid;
    if (pid == -1)
      continue;
    update_flags_swap_out(idx, blockno, pid);
    // clear the values stored in rmap
    rmap[physicalAddress/PGSIZE][i].available = 1;
  }
}

void update_rmap_swap_in(int idx, uint physicalAddress, struct swap_slot* slot){
  physicalAddress-=EXTMEM;
  for(int i=0;i<NPROC;i++){ // Iterating through rmap slots
    int pid = slot->rmap_pid[i];
    if (pid == -1)
      rmap[physicalAddress/PGSIZE][i].available = 1;
    else {
      rmap[physicalAddress/PGSIZE][i].available = 0;
      rmap[physicalAddress/PGSIZE][i].pid = pid;
      update_flags_swap_in(idx, physicalAddress+EXTMEM, pid);
    }
    
  }
}
