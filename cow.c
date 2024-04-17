#include "param.h"
#include "types.h"
#include "defs.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "x86.h"
#include "memlayout.h"

void swap_or_cow(void)
{
    uint pa, flags;
      char *mem;
    struct proc *p = myproc();
    uint vpage = rcr2();
    pte_t *pgdir_adr = walkpgdir(p->pgdir, (void *)vpage, 0);
    if (!pgdir_adr)
    {
        panic("Invalid page fault zero");
        return;
    }
    if ((*pgdir_adr & !PTE_P))// page swappedout (swap it in)
    {
    //    uint block_id = (*pgdir_adr >> PTXSHIFT);
    // char *phy_page = kalloc();
    // if (phy_page == 0)
    // {
    //     panic("Failed to allocate memory for swapped in page");
    //     return;
    // }
    // p->rss += PGSIZE;
    // disk_read(ROOTDEV, phy_page, (int)block_id);
    // *pgdir_adr = V2P(phy_page) | PTE_FLAGS(*pgdir_adr) | PTE_P;
    // *pgdir_adr = *pgdir_adr & (~PTE_SO);
    // swapfree(ROOTDEV, block_id);
    }
    if (*pgdir_adr & !PTE_W)// shared page
    {
         pa = PTE_ADDR(*pgdir_adr);
         flags = PTE_FLAGS(*pgdir_adr);
         flags=flags | (PTE_W);// set the writeable flag
          if((mem = kalloc()) == 0) // not allocating a new page instead coping the page table enteries
      {
        panic("can kalloc in swap_or_cow\n");
      }

    memmove(mem, (char*)P2V(pa), PGSIZE);// copy the pages to new address
    // if(mappages(p->pgdir,(void *)vpage , PGSIZE, pa, flags) < 0) {
    //   // kfree(mem);
    //   goto bad;
    //(mappages(d, (void *)i, PGSIZE, V2P(mem), flags)// page , virtual address, pg size, physical address, flags
    *pgdir_adr=V2P(mem)|flags|PTE_P;// update the page table entry


    }
}