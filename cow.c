#include "param.h"
#include "types.h"
#include "defs.h"
#include "mmu.h"
#include "proc.h"
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
    if ((*pgdir_adr & PTE_P) == 0) // page swappedout (swap it in)
    {
        cprintf("is not present\n");
    }
    if ((*pgdir_adr & PTE_W) == 0)
    {
        pa = PTE_ADDR(*pgdir_adr);
        flags = PTE_FLAGS(*pgdir_adr);
        
         if(check_rmap(P2V(pa))==1)
         {
            *pgdir_adr=*pgdir_adr|PTE_W;
            return;
         }
        if ((mem = kalloc()) == 0) // not allocating a new page instead coping the page table enteries
        {
            panic("cant kalloc in page_fault\n");
        }
        p->rss += PGSIZE;                      // as new pages is allocated (take care of redundant copy)
        memmove(mem, (char *)P2V(pa), PGSIZE); // copy the pages to new address
        dec_rmap(P2V(pa));
        *pgdir_adr = V2P(mem) |PTE_W|flags; // update the page table entry
        // lcr3(V2P(p->pgdir));
    }
    if((*pgdir_adr & PTE_U) == 0)
    {
          panic("is not user accessible \n");

    }

}