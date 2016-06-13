#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
#include "threads/thread.h"

// P2: make the file accessing critical section                                                                        
struct lock lock_f;

// P2: file struct in thread's file list                                                                               
struct process_file
{
  struct file *file;             // file reference                                                                     
  int fd;                        // file descriptor for this file                                                      
  struct list_elem elem;
};

// P2: struct of child process
struct child_process 
{
  int pid;                       // thread id   
  int status_reg;                // 0th bit for if the child is being waited
                                 // 1th and 2th bits for not loaded, loaded, or load failed
  int ret;                       // record the return status of the child
  struct semaphore sema_child_load;     // block parent proc until child is loaded
  struct semaphore sema_child_exit;     // block parent proc until child exits
  struct list_elem elem;
};

void syscall_init (void);
void sys_halt ();
void sys_exit (void *esp);
void syscall_exit (int status);
int sys_exec (void *esp);
int sys_wait (void *esp);
bool sys_create (void *esp);
bool sys_remove (void *esp);
int sys_open (void *esp);
int sys_filesize (void *esp);
int sys_read (void *esp);
int sys_write (void *esp);
void sys_seek (void *esp);
unsigned sys_tell (void *esp);
void sys_close (void *esp);

#endif /* userprog/syscall.h */
