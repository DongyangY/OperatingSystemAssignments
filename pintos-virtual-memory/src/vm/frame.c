#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"

// P3: initialze frame table.
void 
frame_table_init ()
{
  list_init (&ft.frame_list);
  lock_init (&ft.frame_lock);
}

// P3: page replacement algorithm using clock algorithm.
static void * 
frame_replace (enum palloc_flags fg)
{
  //lock_acquire (&ft.frame_lock);

  struct list_elem *e = list_begin (&ft.frame_list);
  while (1)
    {
      struct frame_table_entry *fte = list_entry (e, struct frame_table_entry, elem);

      // P3: if it is an evictable page.
      if (!fte->spte->inevictable)
	{
	  struct thread *t = fte->thread;

	  // P3: if the page has been read or written before, clear it.
	  if (pagedir_is_accessed (t->pagedir, fte->spte->upage))
	    pagedir_set_accessed (t->pagedir, fte->spte->upage, false);
          // P3: find the target page.
	  else
	    {
              // P3: if the page is dirty, we need to write update to disk.
	      if (pagedir_is_dirty (t->pagedir, fte->spte->upage))
		{
		  if (fte->spte->spage_type == MMAP)
		    {
		      lock_acquire (&lock_f);
		      file_write_at (fte->spte->file, fte->frame,
				     fte->spte->file_read_bytes,
				     fte->spte->file_offset);
		      lock_release (&lock_f);
		    }
                  // P3: SWAP or FILE.
		  else
		    {
		      fte->spte->spage_type = SWAP;
		      fte->spte->swap_slot_id = swap_out (fte->frame);
		    }
		}
              
              // P3: the page is not in memory now.
	      fte->spte->in_memory = false;

              // P3: free the resource of the evict page.
	      list_remove (&fte->elem);
	      pagedir_clear_page (t->pagedir, fte->spte->upage);
	      palloc_free_page (fte->frame);
	      free (fte);
	      //lock_release (&ft.frame_lock);
	      return palloc_get_page (fg);
	    }
	}

      // P3: simulate circular list.
      e = (list_next (e) == list_end (&ft.frame_list)) ?
	list_begin (&ft.frame_list) : list_next (e);
    }
}

// P3: obtain a frame from user pool.
void * 
frame_alloc (enum palloc_flags fg, struct spage_table_entry *spte)
{
  // P3: the frames used for user pages should be obtained from
  // the user pool and return if it is not.
  if ((fg & PAL_USER) == 0) return NULL;

  // P3: lock the whole process for frame allocation and record.
  lock_acquire (&ft.frame_lock);

  void *f = palloc_get_page (fg);

  // P3: if no frame is free, need to evict some pages.
  while (!f) f = frame_replace (fg);
  
  // P3: insert it into frame table.
  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
  fte->frame = f;
  fte->spte = spte;
  fte->thread = thread_current ();
  //lock_acquire (&ft.frame_lock);
  list_push_back (&ft.frame_list, &fte->elem);

  lock_release (&ft.frame_lock);

  return f;
}

// P3: delete a frame from frame table.
void 
frame_free (void *f)
{ 
  lock_acquire (&ft.frame_lock);
  
  struct list_elem *e;
  for (e = list_begin (&ft.frame_list); e != list_end (&ft.frame_list);
       e = list_next (e))
    {
      struct frame_table_entry *fte = list_entry (e, struct frame_table_entry, elem);
      if (fte->frame == f)
	{
	  list_remove (e);
	  free (fte);
	  palloc_free_page (f);
	  break;
	}
    }

  lock_release (&ft.frame_lock);
}
