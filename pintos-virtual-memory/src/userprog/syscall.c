#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/frame.h"
#include "vm/spage.h"

static void syscall_handler (struct intr_frame *);

//bool is_lock_init = false;

// P2: return the file pointer given file descriptor
// held by the current thread
struct file*
get_file (int fd)
{
  struct file *f = NULL;
  struct thread *t = thread_current ();
  struct list_elem* e = list_begin (&t->file_list);
  
  while (e != list_end (&t->file_list))
    {
      struct process_file *p = list_entry (e, struct process_file, elem);
      if (p->fd == fd) f = p->file;
      e = list_next (e);
    }
  
  return f;
}

// P2: check if the address is bad, i.e.,
// above the PHYS_BASE or under the code seg
static bool
is_bad_addr (const void *addr)
{
  return addr >= PHYS_BASE || addr < (void *)0x0804800;
}

// P3: in P2, it is invalid if not present in page table.
// But in P3, it may be valid but needs to load page or 
// grow the stack.
static struct spage_table_entry * 
is_addr_valid (const void *vaddr, void* esp)
{
  // P3: check valid addr
  if (is_bad_addr (vaddr))
    syscall_exit (-1);

  // P3: check if need load page or grow the stack.
  bool success = false;
  struct spage_table_entry *spte = spage_table_find ((void *) vaddr);
  if (spte)
    {
      spage_table_load (spte, spte->spage_type);
      success = spte->in_memory;
    }
  if (!spte && vaddr >= esp - 32)
    success = grow_stack ((void *) vaddr);

  if (!success) syscall_exit (-1);
  return spte;
}

// P3: a better way to parse the arguments than it in P2.
static void 
parse_arugments (struct intr_frame *f, int *arg, int n)
{
  int i;
  int *ptr;
  for (i = 0; i < n; i++)
    {
      ptr = (int *) f->esp + i + 1;
      is_addr_valid ((const void *) ptr, f->esp);
      arg[i] = *ptr;
    }
}

// P2: check if the address of each char in buffer is valid
static void 
check_buf (void* buffer, unsigned size, void* esp, bool to_write)
{
  unsigned i;
  char* local_buffer = (char *)buffer;
  for (i = 0; i < size; i++)
    {
      struct spage_table_entry *spte = is_addr_valid ((const void*)local_buffer, esp);
      if (spte && to_write && !spte->writable) syscall_exit (-1);
      local_buffer++;
    }
}

// P3: check if the address of each char in string is valid
static void 
check_str (const void* str, void* esp)
{
  is_addr_valid (str, esp);
  while (* (char *) str != 0)
    {
      str = (char *) str + 1;
      is_addr_valid (str, esp);
    }
}

// P3: allow the page of the addr could be evicted.
static void 
unlock_ptr (void* vaddr)
{
  struct spage_table_entry *spte = spage_table_find (vaddr);
  if (spte) spte->inevictable = false;
}

// P3: allow the pages of the string could be evicted.
static void 
unlock_str (void* str)
{
  unlock_ptr (str);
  while (* (char *) str != 0)
    {
      str = (char *) str + 1;
      unlock_ptr (str);
    }
}

// P3: allow the pages of the buffer could be evicted.
static void 
unlock_buf (void* buffer, unsigned size)
{
  unsigned i;
  char* local_buffer = (char *) buffer;
  for (i = 0; i < size; i++)
    {
      unlock_ptr (local_buffer);
      local_buffer++;
    }
}

void
syscall_init (void) 
{
  // P3: cannot init lock in handler as in P2
  lock_init (&lock_f);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void 
sys_halt ()
{
  shutdown_power_off ();
}

void
syscall_exit (int status)
{
  struct thread *t = thread_current ();
  if (is_thread_alive (t->parent) && t->cp)
    {
      if (status < 0) status = -1;
      t->cp->ret = status;
    }
  printf ("%s: exit(%d)\n", t->name, status);
  thread_exit ();
}

static void 
sys_exit (struct intr_frame *f, int *arg)
{
  parse_arugments (f, arg, 1);
  int status = arg[0];

  syscall_exit (status);
}

static pid_t 
sys_exec (struct intr_frame *f, int *arg)
{
  parse_arugments (f, arg, 1);
  check_str ((const void *) arg[0], f->esp);
  const char *cmd_line = (const char *) arg[0];

  pid_t pid = process_execute (cmd_line);
  struct child_process* cp = process_get_child (pid);
  if (!cp) return -1;

  if (!(cp->status_reg & (1 << 1)) &&
      !(cp->status_reg & (1 << 2)))
    sema_down (&cp->sema_child_load);

  if (cp->status_reg & (1 << 2))
    {
      list_remove (&cp->elem);
      free (cp);
      return -1;
    }

  unlock_str ((void *)arg[0]);

  return pid;
}

static int 
sys_wait (struct intr_frame *f, int *arg)
{
  parse_arugments (f, arg, 1);
  pid_t pid = arg[0];

  return process_wait (pid);
}

static bool 
sys_create (struct intr_frame *f, int *arg)
{
  parse_arugments (f, arg, 2);
  check_str ((const void *)arg[0], f->esp);
  const char *file = (const char *)arg[0];
  unsigned initial_size = (unsigned) arg[1];

  lock_acquire (&lock_f);
  bool s = filesys_create (file, initial_size);
  lock_release (&lock_f);

  unlock_str ((void *)arg[0]);
  return s;
}

static bool 
sys_remove (struct intr_frame *f, int *arg)
{
  parse_arugments (f, arg, 1);
  check_str ((const void *)arg[0], f->esp);
  const char *file = (const char *) arg[0];

  lock_acquire (&lock_f);
  bool s = filesys_remove (file);
  lock_release (&lock_f);
  return s;
}

static int 
sys_open (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 1);
  check_str ((const void *)arg[0], intr_f->esp);
  const char *file = (const char *)arg[0];

  lock_acquire (&lock_f);
  struct file *f = filesys_open (file);
  if (!f)
    {
      lock_release (&lock_f);
      return -1;
    }

  struct process_file *pf = malloc (sizeof (struct process_file));
  if (!pf) return -1;
  pf->file = f;
  pf->fd = thread_current ()->next_fd++;
  list_push_back (&thread_current ()->file_list, &pf->elem);

  lock_release (&lock_f);

  unlock_str ((void *)arg[0]);
  return pf->fd;
}

static int 
sys_filesize (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 1);
  int fd = arg[0];

  lock_acquire (&lock_f);
  struct file *f = get_file (fd);
  if (!f)
    {
      lock_release (&lock_f);
      return -1;
    }
  int size = file_length (f);
  lock_release (&lock_f);
  return size;
}

static int 
sys_read (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 3);
  check_buf ((void *)arg[1], (unsigned)arg[2], intr_f->esp, true);
  int fd = arg[0];
  void *buffer = (void *)arg[1];
  unsigned size = (unsigned)arg[2];

  if (fd == 0)
    {
      unsigned i;
      uint8_t* local_buffer = (uint8_t *) buffer;
      for (i = 0; i < size; i++)
	{
	  local_buffer[i] = input_getc ();
	}
      return size;
    }

  lock_acquire (&lock_f);
  struct file *f = get_file (fd);
  if (!f)
    {
      lock_release (&lock_f);
      return -1;
    }
  int nbytes = file_read (f, buffer, size);
  lock_release (&lock_f);

  unlock_buf ((void *)arg[1], (unsigned)arg[2]);
  return nbytes;
}

static int 
sys_write (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 3);
  check_buf ((void *)arg[1], (unsigned)arg[2], intr_f->esp, false);
  int fd = arg[0];
  const void *buffer = (const void *)arg[1];
  unsigned size = (unsigned)arg[2];

  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }

  lock_acquire (&lock_f);
  struct file *f = get_file (fd);
  if (!f)
    {
      lock_release (&lock_f);
      return -1;
    }
  int nbytes = file_write (f, buffer, size);
  lock_release (&lock_f);

  unlock_buf ((void *)arg[1], (unsigned)arg[2]);
  return nbytes;
}

static void 
sys_seek (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 2);
  int fd = arg[0];
  unsigned position = (unsigned)arg[1];

  lock_acquire (&lock_f);
  struct file *f = get_file (fd);
  if (!f)
    {
      lock_release (&lock_f);
      return;
    }
  file_seek (f, position);
  lock_release (&lock_f);
}

unsigned 
sys_tell (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 1);
  int fd = arg[0];

  lock_acquire (&lock_f);
  struct file *f = get_file (fd);
  if (!f)
    {
      lock_release (&lock_f);
      return -1;
    }
  off_t offset = file_tell (f);
  lock_release (&lock_f);
  return offset;
}

static void 
sys_close (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 1);
  int fd = arg[0];

  lock_acquire (&lock_f);
  struct thread *t = thread_current ();
  struct list_elem* e = list_begin (&t->file_list);

  while (e != list_end (&t->file_list))
    {
      struct process_file *pf = list_entry (e, struct process_file, elem);
      if (pf->fd == fd) 
	{
	  file_close (pf->file);
	  list_remove (&pf->elem);
	  free (pf);
	  break;
	}
      e = list_next (e);
    }
  lock_release (&lock_f);
}

static int 
sys_mmap (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 2);
  int fd = arg[0];
  void *addr = (void *)arg[1];

  return mmap_map (fd, addr);
}

static void 
sys_munmap (struct intr_frame *intr_f, int *arg)
{
  parse_arugments (intr_f, arg, 1);
  int mapping = arg[0];

  mmap_unmap (mapping);
}

static void
syscall_handler (struct intr_frame *f) 
{
  /*
  if (!is_lock_init)
  {
    lock_init(&lock_f);
    is_lock_init = true;
  }
  */

  // P2: store the args without the NUMBER
  int arg[3];

  // P2: get the pointer to the 1st arg - NUMBER in stack
  int *p = (int *)f->esp;
  is_addr_valid ((const void*)f->esp, f->esp);

  switch (*p)
  {
    case SYS_HALT:
      sys_halt (); 
      break;

    case SYS_EXIT:
      sys_exit (f, arg);
      break;

    case SYS_EXEC:
      f->eax = sys_exec (f, arg);
      break;

    case SYS_WAIT:
      f->eax = sys_wait (f, arg);
      break;

    case SYS_CREATE:
      f->eax = sys_create (f, arg);
      break;
      
    case SYS_REMOVE:
      f->eax = sys_remove (f, arg);
      break;

    case SYS_OPEN:
      f->eax = sys_open (f, arg);
      break; 		

    case SYS_FILESIZE:
      f->eax = sys_filesize (f, arg);
      break;

    case SYS_READ:
      f->eax = sys_read (f, arg);
      break;

    case SYS_WRITE:
      f->eax = sys_write (f, arg);
      break;
      
    case SYS_SEEK:
      sys_seek (f, arg);
      break;
 
    case SYS_TELL:
      f->eax = sys_tell (f, arg);
      break;

    case SYS_CLOSE:
      sys_close (f, arg);
      break;

    case SYS_MMAP:
      f->eax = sys_mmap (f, arg);
      break;
     
    case SYS_MUNMAP:
      sys_munmap (f, arg);
      break;
  }

  unlock_ptr (f->esp);
}
