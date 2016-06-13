#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/frame.h"

// P3: the resource type of the page
// recorded in supplemental page table.
enum spage_types
  {
    FILE,
    SWAP,
    MMAP,
    ERROR
  };

// P3: record additional infomation of the page
// for page fault and process termination.
struct spage_table_entry 
  {
    // P3: all
    void *upage;                         // the user page address of the page
                                         // use this to connect to page table
    enum spage_types spage_type;         // the resource type of the page
    bool in_memory;                      // if the page is currently loaded in memory
    bool writable;                       // if the page is writable
    bool inevictable;                    // if the page is pinning and not evictable  

    // P3: spage_type == swap
    size_t swap_slot_id;                 // the index for swap slot storing this page
                                         // used for finding the loading page 

    // P3: spage_type == file | mmap          
    struct file *file;                   // the file addr storing this page
    size_t file_offset;                  // the file offset of this page
    size_t file_read_bytes;              // the read bytes of this page
    size_t file_zero_bytes;              // the zero bytes of this page

    struct hash_elem elem;
  };

void spage_table_init (struct hash *spt);
void spage_table_free (struct hash *spt);
struct spage_table_entry* spage_table_find (void *uaddr);
bool spage_table_load (struct spage_table_entry *spte, enum spage_types type);
bool spage_table_put_file (struct file *file, int32_t ofs, uint8_t *upage, uint32_t read_bytes, 
                           uint32_t zero_bytes, bool writable, enum spage_types type);
bool grow_stack (void *uaddr);

#endif
