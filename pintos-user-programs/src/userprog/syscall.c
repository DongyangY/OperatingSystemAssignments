#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <user/syscall.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"

static void syscall_handler (struct intr_frame *);

bool is_lock_init = false;

// P2: check if the address is bad, i.e.,
// above the PHYS_BASE or under the code seg
static bool
is_bad_addr (const void *addr)
{
  return addr >= PHYS_BASE || addr < (void *)0x0804800;
}

// P2: check if the address is existing in page table
static bool
is_addr_not_exist (const void *addr)
{
  return !pagedir_get_page (thread_current ()->pagedir, addr); 
}

// P2: check if the address valid
static bool
is_addr_valid (const void *addr)
{
  return !is_bad_addr (addr) && !is_addr_not_exist (addr);
}

// P2: check if the address of each char in buffer is valid
static void
check_buf (const void* buffer, unsigned size)
{
  char* buf = (char *) buffer;

  unsigned i;
  for (i = 0; i < size; i++)
    {
      if (!is_addr_valid ((const void *) buf))
	syscall_exit (-1);
      buf++;
    }
}

// P2: return the file pointer given file descriptor
// held by the current thread
static struct file*
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

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  if (!is_lock_init)
  {
    lock_init(&lock_f);
    is_lock_init = true;
  }
  
  // P2: store the args without the NUMBER
  int arg[3];

  // P2: get the pointer to the 1st arg - NUMBER in stack
  int *p = (int *)f->esp;
  if (!is_addr_valid ((const void *)p)) syscall_exit (-1);

  switch (*p)
  {
    case SYS_HALT:
      sys_halt ();
      break;
      
    case SYS_EXIT:
      sys_exit (f->esp);
      break;
      
    case SYS_EXEC:
      f->eax = sys_exec (f->esp);
      break;
      
    case SYS_WAIT:
      f->eax = sys_wait (f->esp);
      break;
      
    case SYS_CREATE:
      f->eax = sys_create (f->esp);
      break;
      
    case SYS_REMOVE:
      f->eax = sys_remove (f->esp);
      break;
      
    case SYS_OPEN:
      f->eax = sys_open (f->esp);
      break;
      
    case SYS_FILESIZE:
      f->eax = sys_filesize (f->esp);
      break;
      
    case SYS_READ:
      f->eax = sys_read (f->esp);
      break;
      
    case SYS_WRITE:
      f->eax = sys_write (f->esp);
      break;
      
    case SYS_SEEK:
      sys_seek (f->esp);
      break;
      
    case SYS_TELL:
      f->eax = sys_tell (f->esp);
      break;
    
    case SYS_CLOSE:
      sys_close (f->esp);
      break;
      
    default:
      break;
  }
}

void
sys_halt ()
{
  shutdown_power_off ();
}

void
sys_exit (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  int status = *p;

  syscall_exit (status);
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

pid_t
sys_exec (void  *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  const char *cmd_line = (const char *) *p;


  if (!is_addr_valid ((const void *) cmd_line))
    syscall_exit (-1);
  
  cmd_line = (const char *) pagedir_get_page (thread_current ()->pagedir, (const void *) cmd_line);
 
  pid_t pid = process_execute (cmd_line);
  struct child_process *cp = process_get_child (pid);
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

  return pid;
}


int
sys_wait (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  pid_t pid = (pid_t) *p;

  return process_wait (pid);
}


bool
sys_create (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  const char *file = (const char *) *p;

  p = (int *)esp + 2;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  unsigned initial_size = (unsigned) *p;

  if (!is_addr_valid ((const void *) file))
    syscall_exit (-1);

  lock_acquire (&lock_f);
  bool s = filesys_create (file, initial_size);
  lock_release (&lock_f);
  return s;
}

bool
sys_remove (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  const char *file = (const char *) *p;

  if (!is_addr_valid ((const void *) file))
    syscall_exit (-1);

  lock_acquire (&lock_f);
  bool s = filesys_remove (file);
  lock_release (&lock_f);
  return s;
}

int
sys_open (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  const char *file = (const char *) *p;

  const void *s = file;
  struct thread *t = thread_current ();
  void *k = pagedir_get_page (t->pagedir, s);
  if (!k) syscall_exit (-1);

  while (* (char *) k != '\0')
    {
      s = (char *) s + 1;
      k = pagedir_get_page (t->pagedir, s);
      if (!k) syscall_exit (-1);
    }
    
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
  return pf->fd;
}

int
sys_filesize (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  int fd = *p;

  lock_acquire (&lock_f);
  struct file *f = get_file (fd);
  if (!f)
    {
      lock_release (&lock_f);
      return -1;
    }
  int fs = file_length (f);
  lock_release (&lock_f);
  return fs;
}

int
sys_read (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  int fd = *p;

  p = (int *)esp + 2;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  void *buffer = (void *) *p;

  p = (int *)esp + 3;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  unsigned size = (unsigned) *p;

  if (size <= 0) return size;

  check_buf (buffer, size);
  
  if (fd == 0)
    {
      uint8_t *buf = (uint8_t *) buffer;
      int i;
      for (i = 0; i < size; i++)
	{
	  buf[i] = input_getc ();
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
  return nbytes;
}

int 
sys_write (void* esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  int fd = *p;

  p = (int *)esp + 2;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  const void *buffer = (const void *) *p;

  p = (int *)esp + 3;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  unsigned size = (unsigned) *p;

  if (size <= 0) return size;
  
  check_buf (buffer, size);

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
  return nbytes;
}

void
sys_seek (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  int fd = *p;

  p = (int *)esp + 2;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  unsigned position = (unsigned) *p;

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
sys_tell (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  int fd = *p;
  
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

void
sys_close (void *esp)
{
  int *p = (int *)esp + 1;
  if (is_bad_addr ((const void *) p))
    syscall_exit (-1);
  int fd = *p;

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
