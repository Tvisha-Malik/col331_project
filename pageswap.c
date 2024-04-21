// a function to find the victim process (in proc.c)
// a function to find the victim page (in vm.c)
// a function to set 10% of the accessed pages as unaccesed (in proc.c)
// a function to swap out the page , we have the victim page, not swap it out (here)
// a function to swap in a page
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

// #define SWAPSIZE (PGSIZE / BSIZE) // Size of one swap slot, 4096/512 = 8

#define SWAPSIZE (PGSIZE / BSIZE) // Size of one swap slot, 4096/512 = 8
#define NSWAPSLOTS (SWAPBLOCKS / SWAPSIZE)

// Global in-memory array storing metadata of swap slots
struct swap_slot swap_array[NSWAPSLOTS];

void swaparrayinit(int dev)
{
    for (int i = 0; i < NSWAPSLOTS; i++)
    {
        swap_array[i].is_free = 1;
        swap_array[i].start = SWAPSTART + i * SWAPSIZE;
        swap_array[i].dev = dev;
    }
};

struct swap_slot *swapalloc(void)
{
    for (int i = 0; i < NSWAPSLOTS; i++)
    {
        if (swap_array[i].is_free == 1)
        {                              // If the slot is free
            swap_array[i].is_free = 0; // Mark it as used
            return &swap_array[i];
        }
    }
    panic("swapalloc: out of swap slots");
};


void swapfree(int dev, int blockno)
{
    for (int i = 0; i < NSWAPSLOTS; i++)
    {
        struct swap_slot *sw = &swap_array[i];
        if (sw->start == blockno && sw->dev == dev)
        {
            if (sw->is_free == 1)
                panic("swapfree: slot already free");
            sw->is_free = 1;
            return;
        }
    }
    panic("swapfree: blockno not found");
};

void swap_out(void)
{
    struct proc *v_proc = victim_proc();
    pte_t *v_page = find_victim_page(v_proc->pgdir, v_proc->sz);
    if (v_page == 0)
    {
        unacc_proc(v_proc->pgdir);
        v_page = find_victim_page(v_proc->pgdir, v_proc->sz);
    }
  
    if (v_page == 0)
        panic("still cant find victim page \n");
    v_proc->rss -= PGSIZE; // as its page is swapped out
    struct swap_slot *slot = swapalloc();
    swap_out_page(v_page, slot->start, slot->dev);
    lcr3(V2P(v_proc->pgdir));
}

void swap_out_page(pte_t *vp, uint blockno, int dev)
{
    
    uint physicalAddress = PTE_ADDR(*vp);
    char *va = (char *)P2V(physicalAddress);
    struct buf *buffer;
    int ithPartOfPage = 0;
    for (int i = 0; i < 8; i++)
    {
       
        ithPartOfPage = i * BSIZE;
        buffer = bread(ROOTDEV, blockno + i);
        memmove(buffer->data, va + ithPartOfPage, BSIZE); // write 512 bytes to the block
        bwrite(buffer);
        brelse(buffer);
    }
    
    *vp = ((blockno << 12) | PTE_FLAGS(*vp) | PTE_SO);
    *vp = *vp & (~PTE_P); // setting the top 20 bits as the block number, setting the present bit as unset and the swapped out bit as set
    // The above two are updated in the below function for each proc mentioned in rmap.
    swap_out_pids(physicalAddress, blockno);
    kfree(P2V(physicalAddress), -1, 0);

}
void disk_read(uint dev, char *page, int block)
{
    struct buf *buffer;
    int page_block;
    int part_block;
    // 512 size
    for (int i = 0; i < 8; i++)
    {
        part_block = BSIZE * i;
        page_block = block + i;
        buffer = bread(dev, page_block);
        memmove(page + part_block, buffer->data, BSIZE);
        brelse(buffer);
    }
}



void swap_in_page()
{
   
    struct proc *p = myproc();
    uint vpage = rcr2();
    pte_t *pgdir_adr = walkpgdir(p->pgdir, (void *)vpage, 0);
    if (!pgdir_adr)
    {
        panic("Invalid page fault zero");
        return;
    }
    if ((*pgdir_adr & PTE_P))
    {
        panic("Invalid page fault present");
        return;
    }
    uint block_id = (*pgdir_adr >> PTXSHIFT);
    char *phy_page = kalloc(p->pid,1);
    if (phy_page == 0)
    {
        panic("Failed to allocate memory for swapped in page");
        return;
    }
    p->rss += PGSIZE;
    disk_read(ROOTDEV, phy_page, (int)block_id);
    *pgdir_adr = V2P(phy_page) | PTE_FLAGS(*pgdir_adr) | PTE_P; // setting the present bit as set
    *pgdir_adr = *pgdir_adr & (~PTE_SO); // setting the swapped out bit as unset
    swapfree(ROOTDEV, block_id);
}
