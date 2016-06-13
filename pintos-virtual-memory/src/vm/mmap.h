#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include "vm/spage.h"

// P3: the entry for memory mapped file table in thread structure.
struct mmap_table_entry 
  {
    int mapid;                         // The identifier to each mmap file.
    struct spage_table_entry *spte;    // The user page contains the mmap file.
    struct list_elem elem;             // list element.
  };

bool mmap_alloc (struct spage_table_entry *spte);
int mmap_map (int fd, void *addr);
void mmap_unmap (int mapping);

#endif
