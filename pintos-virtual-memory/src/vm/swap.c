#include "vm/swap.h"

// P3: allocate memory for swap table contents.
void 
swap_init ()
{
  //st = malloc (sizeof (struct swap_table));
  lock_init (&st.swap_lock);
  st.swap_partition = block_get_role (BLOCK_SWAP);
  if (!st.swap_partition) return;
  st.num_sectors_per_page = PGSIZE / BLOCK_SECTOR_SIZE;

  // P3: get the number of page-sized swap slots in the partition.
  st.num_swap_slots = block_size (st.swap_partition) / st.num_sectors_per_page;
  st.swap_slots_status = malloc (sizeof (bool) * st.num_swap_slots);
  if (!st.swap_slots_status) return;

  // P3: must clear all status here, since no default in test.
  size_t i;
  for (i = 0; i < st.num_swap_slots; i++) st.swap_slots_status[i] = false;
}

// P3: read in one page-sized swap slot given index to the frame addr.
void 
swap_in (size_t index, void* f)
{
  lock_acquire (&st.swap_lock);

  // P3: read a empty slot.
  if (!st.swap_slots_status[index]) return;
  st.swap_slots_status[index] = false;

  // P3: read a page in.
  size_t i;
  for (i = 0; i < st.num_sectors_per_page; i++)
      block_read (st.swap_partition, index * st.num_sectors_per_page + i,
		  (uint8_t *) f + i * BLOCK_SECTOR_SIZE);
  lock_release (&st.swap_lock);
}

// P3: write out a free one page-sized swap slot give the frame addr.
// Return the index of swap slot written.
size_t 
swap_out (void *f)
{
  lock_acquire (&st.swap_lock);

  // P3: find the first free swap slot for writing.
  size_t i, index;
  for (i = 0; i < st.num_swap_slots; i++)
    {
      if (!st.swap_slots_status[i])
        {
          index = i;
          st.swap_slots_status[i] = true;
          break;
        }
    }

  if (i >= st.num_swap_slots) PANIC("No free swap slots any more!");

  // P3: write a page out.
  for (i = 0; i < st.num_sectors_per_page; i++)
    block_write (st.swap_partition, index * st.num_sectors_per_page + i,
		 (uint8_t *) f + i * BLOCK_SECTOR_SIZE);
  lock_release (&st.swap_lock);

  return index;
}
