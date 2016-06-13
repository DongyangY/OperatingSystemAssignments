#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/synch.h"
#include "vm/spage.h"
#include <stdbool.h>
#include <stdint.h>
#include <list.h>

// P3: frame table associates each frame with a page 
// via supplemental page table entry.
struct frame_table
  {
    struct list frame_list;    // list of frame table entries.
                               // use list since clock algorithm.
    struct lock frame_lock;    // frame table is a shared data.
  };

// P3: the global instance of frame table.
struct frame_table ft;         

// P3: each entry of the frame table.
struct frame_table_entry 
  {
    void *frame;                        // the addr of the frame.
    struct spage_table_entry *spte;     // the corresponding page info.
    struct thread *thread;              // the owner of the frame. 
    struct list_elem elem;              // list element.
  };

void frame_table_init ();
void* frame_alloc (enum palloc_flags fg, struct spage_table_entry *spte);
void frame_free (void *frame);

#endif
