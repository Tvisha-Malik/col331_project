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
        for (int j = 0; j < 64; j++)
            swap_array[i].rmap_pid[j]=-1;
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

struct swap_slot * swapfind(int dev, int blockno)
{
    for (int i = 0; i < NSWAPSLOTS; i++)
    {
        struct swap_slot *sw = &swap_array[i];
        if (sw->start == blockno && sw->dev == dev)
            return sw;
    }
    panic("swapfree: blockno not found");
};

void swapfree(struct swap_slot *sw)
{
    if (sw->is_free == 1)
        panic("swapfree: slot already free");
    sw->is_free = 1;
};

void swap_out_page(int idx, struct proc* v_proc, struct swap_slot *slot, int dev)
{
    uint blockno = slot->start;
    pte_t* vp = walkpgdir(v_proc->pgdir, (void *)idx, 0);
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
    
    // *vp = ((blockno << 12) | PTE_FLAGS(*vp) | PTE_SO);
    // *vp = *vp & (~PTE_P); // setting the top 20 bits as the block number, setting the present bit as unset and the swapped out bit as set
    // The above two are updated in the below function for each proc mentioned in rmap.
    update_rmap_swap_out(idx, physicalAddress, blockno, slot);
    kfree(P2V(physicalAddress), -1, 0);

}

void swap_out(void)
{
    struct proc *v_proc = victim_proc();
    // cprintf("the rss of v_proc is %d pages\n", (v_proc->rss)/PGSIZE);
    if(v_proc->rss<=10*PGSIZE)
        panic("too less pages\n");
    int idx = find_victim_page_idx(v_proc->pgdir, v_proc->sz);
    if (idx == -1)
    {
        // cprintf("here in id -1 %x\n",v_proc->name);
        unacc_proc(v_proc->pgdir, v_proc->sz);
        //  lcr3(V2P(v_proc->pgdir));
        lcr3(V2P(myproc()->pgdir));
        idx = find_victim_page_idx(v_proc->pgdir, v_proc->sz);
        // cprintf("here in id -1 %x\n",v_proc->name);
    }
  
    if (idx < 0)
        panic("still cant find victim page \n");
    // v_proc->rss -= PGSIZE; // as its page is swapped out
    // we are updating rss in update_proc_flags() later
    struct swap_slot *slot = swapalloc();
    swap_out_page(idx, v_proc, slot, slot->dev);
    lcr3(V2P(myproc()->pgdir));
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

void update_swap_rmap_pid(int pid, pte_t *pgdir_adr){
    uint block_id = (*pgdir_adr >> PTXSHIFT);
    struct swap_slot *slot = swapfind(ROOTDEV, block_id);
    for(int i=0; i < NPROC; i++)
    {
        if(slot->rmap_pid[i] == -1)
        {
            slot->rmap_pid[i] = pid;
            return;
        }
    }
    panic("No space in swapslot");
}

void swap_in_page()
{
   
    struct proc *p = myproc();
    uint vpage = rcr2();
    pte_t *pgdir_adr = walkpgdir(p->pgdir, (void *)vpage, 0);
    if (!pgdir_adr)
    {
        panic("Invalid page fault so");
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
    disk_read(ROOTDEV, phy_page, (int)block_id);
    uint physicalAddress = V2P(phy_page);
    struct swap_slot *slot = swapfind(ROOTDEV, block_id);
    // p->rss += PGSIZE;
    // *pgdir_adr = physicalAddress | PTE_FLAGS(*pgdir_adr) | PTE_P; // setting the present bit as set
    // *pgdir_adr = *pgdir_adr & (~PTE_SO); // setting the swapped out bit as unset
    update_rmap_swap_in(vpage, physicalAddress, slot);
    
    swapfree(slot);
}
