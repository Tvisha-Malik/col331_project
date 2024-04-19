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
    cprintf("the page requested is %x \n", vpage);
    cprintf("the process is %s \n", p->name);
    cprintf("the eip is %x \n", p->tf->eip);
    cprintf("the size of the process is %x\n",p->sz);
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
        cprintf("is not writeable\n");
        

        pa = PTE_ADDR(*pgdir_adr);
        cprintf("the old page table pa is %x\n", pa);

        flags = PTE_FLAGS(*pgdir_adr);
        if ((mem = kalloc()) == 0) // not allocating a new page instead coping the page table enteries
        {
            panic("cant kalloc in page_fault\n");
        }
        p->rss += PGSIZE;                      // as new pages is allocated (take care of redundant copy)
        memmove(mem, (char *)P2V(pa), PGSIZE); // copy the pages to new address
        *pgdir_adr = V2P(mem) |flags|PTE_W| PTE_P; // update the page table entry
    //     if(mappages(p->pgdir, (void*)vpage, PGSIZE, V2P(mem), flags|PTE_W) < 0) {
    //   // kfree(mem);
    //   panic("cant mappages");
    // }
          cprintf("the new page table pa is %x\n", PTE_ADDR(*pgdir_adr));
    }
    if((*pgdir_adr & PTE_U) == 0)
    {
          cprintf("is not user accessible\n");
          panic("is not user accessible \n");

    }

}