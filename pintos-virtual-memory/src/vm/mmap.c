#include "vm/mmap.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "threads/vaddr.h"

bool 
mmap_alloc (struct spage_table_entry *spte)
{
  struct mmap_table_entry *mm = malloc (sizeof (struct mmap_table_entry));
  if (!mm) return false;
  mm->spte = spte;
  mm->mapid = thread_current ()->last_mapid;
  list_push_back (&thread_current ()->mmap_table, &mm->elem);
  return true;
}

int 
mmap_map (int fd, void *addr)
{
  struct file *old_file = get_file (fd);
  if (!old_file || !is_user_vaddr (addr) || addr < (void *) 0x08048000 ||
      ((uint32_t) addr % PGSIZE) != 0)
    return -1;

  struct file *file = file_reopen (old_file);
  if (!file || file_length (old_file) == 0) return -1;

  struct thread *t = thread_current ();

  t->last_mapid++;
  int32_t ofs = 0;
  uint32_t read_bytes = file_length (file);

  // P3: note that one mapid may mapping to many sptes (user pages)
  // Thus, we choose linked list data structure.
  while (read_bytes > 0)
    {
      uint32_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      uint32_t page_zero_bytes = PGSIZE - page_read_bytes;
      if (!spage_table_put_file (file, ofs, addr, page_read_bytes, page_zero_bytes, true, MMAP))
	{
	  mmap_unmap (t->last_mapid);
	  return -1;
	}
      read_bytes -= page_read_bytes;
      ofs += page_read_bytes;
      addr += PGSIZE;
  }
  return t->last_mapid;
}

void 
mmap_unmap (int mapping)
{
  struct thread *t = thread_current ();
  struct list_elem *next, *e = list_begin (&t->mmap_table);
  struct file *f = NULL;
  int close = 0;

  while (e != list_end (&t->mmap_table))
    {
      next = list_next (e);
      struct mmap_table_entry *mm = list_entry (e, struct mmap_table_entry, elem);
      // P3: -1 indicates unmap all files and it occurs when process exits.
      if (mm->mapid == mapping || mapping == -1)
	{
          // P3: reduce the race condition with frame eviction.
	  mm->spte->inevictable = true;

	  if (mm->spte->in_memory)
	    {
              // P3: must write the update to disk. It is similar to 
              // evict a dirty mmap page.
	      if (pagedir_is_dirty (t->pagedir, mm->spte->upage))
		{
		  lock_acquire (&lock_f);
		  file_write_at (mm->spte->file, mm->spte->upage,
				 mm->spte->file_read_bytes, mm->spte->file_offset);
		  lock_release (&lock_f);
		}
	      frame_free (pagedir_get_page (t->pagedir, mm->spte->upage));
	      pagedir_clear_page (t->pagedir, mm->spte->upage);
	    }
	  if (mm->spte->spage_type != ERROR)
	    {
	      hash_delete (&t->spage_table, &mm->spte->elem);
	    }
	  list_remove (&mm->elem);
	  if (mm->mapid != close)
	    {
	      if (f)
		{
		  lock_acquire (&lock_f);
		  file_close (f);
		  lock_release (&lock_f);
		}
	      close = mm->mapid;
	      f = mm->spte->file;
	    }
	  free (mm->spte);
	  free (mm);
	}
      e = next;
    }
  if (f)
    {
      lock_acquire (&lock_f);
      file_close (f);
      lock_release (&lock_f);
    }
}
