#include <string.h>
#include <stdbool.h>
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/spage.h"
#include "vm/swap.h"
#include "filesys/file.h"

static bool 
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

bool 
spage_table_load (struct spage_table_entry *spte, enum spage_types type)
{
  // P3: reduce race condition with frame eviction process.
  spte->inevictable = true;

  if (spte->in_memory) return false;

  if (type == SWAP)
    {
      uint8_t *f = frame_alloc (PAL_USER, spte);
      if (!f) return false;

      if (!install_page (spte->upage, f, spte->writable))
	{
	  frame_free (f);
	  return false;
	}

      swap_in (spte->swap_slot_id, spte->upage);
      spte->in_memory = true;
    }
  
  if (type == FILE || type == MMAP)
    {
      enum palloc_flags fg = PAL_USER;
      if (spte->file_read_bytes == 0) fg |= PAL_ZERO;

      uint8_t *f = frame_alloc (fg, spte);
      if (!f) return false;

      if (spte->file_read_bytes > 0)
	{
	  lock_acquire (&lock_f);
	  if ((int) spte->file_read_bytes != file_read_at (spte->file, f, spte->file_read_bytes,
							   spte->file_offset))
	    {
	      lock_release (&lock_f);
	      frame_free (f);
	      return false;
	    }
	  lock_release (&lock_f);
	  memset (f + spte->file_read_bytes, 0, spte->file_zero_bytes);
	}
      
      if (!install_page (spte->upage, f, spte->writable))
	{
	  frame_free (f);
	  return false;
	}
      
      spte->in_memory = true;  
    }
  
  return true;
}

bool 
spage_table_put_file (struct file *file, int32_t ofs, uint8_t *upage, uint32_t read_bytes, 
		      uint32_t zero_bytes, bool writable, enum spage_types type)
{
  if (type != FILE && type != MMAP) return false;

  struct spage_table_entry *spte = malloc (sizeof (struct spage_table_entry));
  if (!spte) return false;
  spte->file = file;
  spte->file_offset = ofs;
  spte->upage = upage;
  spte->file_read_bytes = read_bytes;
  spte->file_zero_bytes = zero_bytes;
  spte->writable = writable;
  spte->in_memory = false;
  spte->inevictable = false;
  spte->spage_type = type;

  if (type == MMAP)
    {
      if (!mmap_alloc (spte))
        {
          free(spte);
          return false;
        }
    }

  if (hash_insert (&thread_current()->spage_table, &spte->elem))
    {
      spte->spage_type = ERROR;
      return false;
    }
  
  return true;
}

static unsigned 
spage_hash_code (const struct hash_elem *e, void *aux)
{
  return hash_int ((int) hash_entry (e, struct spage_table_entry, elem)->upage);
}

static bool 
spage_hash_less (const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  return (hash_entry (a, struct spage_table_entry, elem)->upage < 
	  hash_entry (b, struct spage_table_entry, elem)->upage) ? true : false;
}

void 
spage_table_init (struct hash *spt)
{
  hash_init (spt, spage_hash_code, spage_hash_less, NULL);
}

static void 
spage_hash_remove (struct hash_elem *e, void *aux)
{
  struct spage_table_entry *spte = hash_entry (e, struct spage_table_entry, elem);
  struct thread *t = thread_current ();
  if (spte->in_memory)
    {
      frame_free (pagedir_get_page (t->pagedir, spte->upage));
      pagedir_clear_page (t->pagedir, spte->upage);
    }
  free (spte);
}

void 
spage_table_free (struct hash *spt)
{
  hash_destroy (spt, spage_hash_remove);
}

struct spage_table_entry * 
spage_table_find (void *uaddr)
{
  struct spage_table_entry spte;
  spte.upage = pg_round_down (uaddr);

  struct hash_elem *e = hash_find (&thread_current()->spage_table, &spte.elem);
  if (!e) return NULL;
  return hash_entry (e, struct spage_table_entry, elem);
}

bool 
grow_stack (void *uaddr)
{
  void *upage = pg_round_down (uaddr);
  if((size_t)(PHYS_BASE - upage) > (1 << 23)) return false;

  struct spage_table_entry *spte = malloc (sizeof (struct spage_table_entry));
  if (!spte) return false;
  spte->upage = upage;
  spte->in_memory = true;
  spte->writable = true;
  spte->spage_type = SWAP;
  spte->inevictable = true;

  uint8_t *f = frame_alloc (PAL_USER, spte);
  if (!f)
    {
      free (spte);
      return false;
    }

  if (!install_page (spte->upage, f, spte->writable))
    {
      free (spte);
      frame_free (f);
      return false;
    }

  if (intr_context ()) spte->inevictable = false;

  return hash_insert (&thread_current ()->spage_table, &spte->elem) == NULL;
}
