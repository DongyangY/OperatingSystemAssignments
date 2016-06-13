#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

// P3: swap table tracks in-use and free swap slots.
struct swap_table
  {
    struct block *swap_partition;    // the partition or block containing sectors for swapping.
    bool *swap_slots_status;         // record in-user or not for each page-sized slot in partition.
    size_t num_swap_slots;           // number of slots in partition.
    size_t num_sectors_per_page;     // number of sectors in one page.
    struct lock swap_lock;           // lock the shared slots status.
  };

// P3: the global instance for the swap table.
struct swap_table st;

void swap_init ();
void swap_in (size_t i, void* f);
size_t swap_out (void *f);

#endif
