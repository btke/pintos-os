#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/cache.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

typedef void (*syscall_t) (uint32_t *args UNUSED, uint32_t *eax UNUSED);

/* Array of syscall functions */
syscall_t syscalls[NUM_SYSCALLS];

static void syscall_handler (struct intr_frame *);
static void syscall_halt (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_exit (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_exec (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_wait (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_create (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_remove (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_open (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_filesize (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_read (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_write (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_seek (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_tell (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_close (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_practice (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_chdir (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_mkdir (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_readdir (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_isdir (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_inumber (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_invcache (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_cachestat (uint32_t *args UNUSED, uint32_t *eax UNUSED);
static void syscall_diskstat (uint32_t *args UNUSED, uint32_t *eax UNUSED);

static bool args_valid (uint32_t *arg, int num_args);
static bool arg_addr_valid (void *arg);


void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  /* initialize syscall function array */
  syscalls[SYS_HALT]     = syscall_halt;
  syscalls[SYS_EXIT]     = syscall_exit;
  syscalls[SYS_EXEC]     = syscall_exec;
  syscalls[SYS_WAIT]     = syscall_wait;
  syscalls[SYS_CREATE]   = syscall_create;
  syscalls[SYS_REMOVE]   = syscall_remove;
  syscalls[SYS_OPEN]     = syscall_open;
  syscalls[SYS_FILESIZE] = syscall_filesize;
  syscalls[SYS_READ]     = syscall_read;
  syscalls[SYS_WRITE]    = syscall_write;
  syscalls[SYS_SEEK]     = syscall_seek;
  syscalls[SYS_TELL]     = syscall_tell;
  syscalls[SYS_CLOSE]    = syscall_close;
  syscalls[SYS_PRACTICE] = syscall_practice;
  syscalls[SYS_CHDIR]    = syscall_chdir;
  syscalls[SYS_MKDIR]    = syscall_mkdir;
  syscalls[SYS_READDIR]  = syscall_readdir;
  syscalls[SYS_ISDIR]    = syscall_isdir;
  syscalls[SYS_INUMBER]  = syscall_inumber;
  syscalls[SYS_INVCACHE] = syscall_invcache;
  syscalls[SYS_CACHESTAT] = syscall_cachestat;
  syscalls[SYS_DISKSTAT] = syscall_diskstat;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  if (f == NULL)
    exit_ (-1);

  /* check the stack pointer arg */
  uint32_t* args = ((uint32_t*) f->esp);
  if (!args_valid (args, 1))
    exit_ (-1);

  /* move args from syscall number to first syscall argument and call function */
  int syscall = (int) args[0];
  args++;
  syscalls[syscall](args, &(f->eax));
}

static void
syscall_halt (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  shutdown_power_off ();
}

static void
syscall_exit (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  int status;
  if (!args_valid (args, 1))
    status = -1;
  else
    status = (int) args[0];

  /* set exit status first. Likely not used */
  *eax = status;
  exit_ (status);
}

/* Wrapper function to support passing exit code.
   Specifically for use with exception exits. */
void
exit_ (int status)
{
  /* for parent */
  thread_current ()->own_wait_status->exit_code = status;
  thread_current ()->own_wait_status->valid = true;

  printf("%s: exit(%d)\n", thread_current ()->name, status);
  thread_exit ();
}

static void
syscall_exec (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1) || !arg_addr_valid ((void *) args[0]))
    exit_ (-1);

  char *file = (char *) args[0];
  *eax = (uint32_t) process_execute (file);
}

static void
syscall_wait (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1))
    exit_ (-1);

  pid_t pid = args[0];
  *eax = (uint32_t) process_wait ((int) pid);
}

static void
syscall_create (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 2) || !arg_addr_valid ((void *) args[0]))
    exit_ (-1);

  char *file = (char *) args[0];
  if (strlen (file) > 14)
    *eax = false;
  else
    {
      off_t size = args[1];
      *eax = filesys_create (file, size, false);
    }
}

static void
syscall_remove (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1) || !arg_addr_valid ((void *) args[0]))
    exit_ (-1);

  char *file = (char *) args[0];
  *eax = filesys_remove (file);
}

static void
syscall_open (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1) || !arg_addr_valid ((void *) args[0]))
    exit_ (-1);

  char *file_name = (char *) args[0];
  struct file *file = filesys_open (file_name);

  int fd;
  if (file)
    {
      fd = add_fd (file);
      struct inode *inode = file_get_inode (file);
      if (inode != NULL && inode_is_dir (inode))
        assign_fd_dir (thread_current (), dir_open (inode_reopen (inode)), fd);
    }
  else
    fd = -1;

  *eax = fd;
}

static void
syscall_filesize (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1))
    exit_ (-1);

  int fd = args[0];
  struct file *file = get_file (thread_current (), fd);
  if (file)
    *eax = file_length (file);
  else
    *eax = -1;
}

static void
syscall_read (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 3) || !arg_addr_valid ((void *) args[1]))
    exit_ (-1);

  int fd = args[0];
  char *buf = (char *) args[1];
  off_t size = args[2];

  if (fd == 0)
    {
      int i;
      char c;
      for (i = 0; i < (int) size; i++)
        {
          c = input_getc ();
          /* '\r' is enter */
          if (c == '\r')
            {
              buf[i] = '\n';
              *eax = i;
              break;
            }
          else
            buf[i] = c;
        }
    }
  /* ERROR: CAN'T READ FROM STDOUT */
  else if (fd == 1)
    exit_ (-1);
  else
    {
      struct file *file = get_file (thread_current (), fd);
      if (file)
        *eax = file_read (file, (void *) buf, size);
      else
        *eax = -1;
    }
}

static void
syscall_write (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 3) || !arg_addr_valid ((void *) args[1]))
    exit_ (-1);

  int fd = (int) args[0];
  char *buffer = (char *) args[1];
  int size = (int) args[2];

  if (fd == STDOUT_FILENO)
    {
      putbuf (buffer, size);
      *eax = size;
      return;
    }

  struct file *file = get_file (thread_current (), fd);
  if (file)
    {
      struct inode *inode = file_get_inode (file);
      if (!inode_is_dir (inode))
        {
          *eax = file_write (file, buffer, size);
          return;
        }
    }
  *eax = -1;
}

static void
syscall_seek (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 2))
    exit_ (-1);

  int fd = args[0];
  int position = args[1];

  struct file *file = get_file (thread_current (), fd);
  if (file)
    file_seek (file, position);
}

static void
syscall_tell (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1))
    exit_ (-1);

  int fd = args[0];
  struct file *file = get_file (thread_current (), fd);
  if (file)
    *eax = file_tell (file);
  else
    *eax = -1;
}

static void
syscall_close (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1))
    exit_ (-1);

  int fd = args[0];
  struct thread *t = thread_current ();

  struct file *file = get_file (t, fd);
  if (file)
    {
      file_close (file);
      remove_fd (t, fd);
    }
}

static void
syscall_practice (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1))
    exit_ (-1);

  *eax = args[0] + 1;
}

static void
syscall_chdir (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1) || !arg_addr_valid ((void *) args[0]))
    exit_ (-1);

  char *name = (char *) args[0];
  struct dir *dir = dir_open_directory (name);
  if (dir != NULL)
    {
      dir_close (thread_current ()->working_dir);
      thread_current ()->working_dir = dir;
      *eax = true;
    }
  else
    *eax = false;
}

static void
syscall_mkdir (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1) || !arg_addr_valid ((void *) args[0]))
    exit_ (-1);

  char *name = (char *) args[0];
  *eax = filesys_create (name, 0, true);
}

static void
syscall_readdir (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 2) || !arg_addr_valid ((void *) args[1]))
    exit_ (-1);

  int fd = args[0];
  char *name = (char *) args[1];
  struct file *file = get_file (thread_current (), fd);
  if (file)
    {
      struct inode *inode = file_get_inode (file);
      if (inode == NULL || !inode_is_dir (inode))
        *eax = false;
      else
        {
          struct dir *dir = get_fd_dir (thread_current (), fd);
          *eax = dir_readdir (dir, name);
        }
    }
  else
    *eax = -1;
}

static void
syscall_isdir (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1))
    exit_ (-1);

  int fd = args[0];
  struct file *file = get_file (thread_current (), fd);
  if (file)
    {
      struct inode *inode = file_get_inode (file);
      *eax = inode_is_dir (inode);
    }
  else
    *eax = -1;
}

static void
syscall_inumber (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 1))
    exit_ (-1);

  int fd = args[0];
  struct file *file = get_file (thread_current (), fd);
  if (file)
    {
      struct inode *inode = file_get_inode (file);
      *eax = (int) inode_get_inumber (inode);
    }
  else
    *eax = -1;
}

static void
syscall_invcache (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  cache_invalidate (fs_device);
}

static void
syscall_cachestat (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 3) || !arg_addr_valid ((void *) args[0]) ||
      !arg_addr_valid ((void *) args[1]) || !arg_addr_valid ((void *) args[2]))
    {
      *eax = -1;
      return;
    }

  *eax = cache_get_stats ((long long *) args[0], (long long *) args[1], (long long *) args[2]);
}

static void
syscall_diskstat (uint32_t *args UNUSED, uint32_t *eax UNUSED)
{
  if (!args_valid (args, 2) || !arg_addr_valid ((void *) args[0]) ||
      !arg_addr_valid ((void *) args[1]))
    {
      *eax = -1;
      return;
    }

  *eax = block_get_stats (fs_device, (long long *) args[0], (long long *) args[1]);
}

/* Does not check validity of dispatch function arguments.
   Only checks that stack arguments (argv, argc, etc.) are valid.
   Keep in mind that each argv[i] points to a char * which could
   have any value. The validity of these arguments are not generalizable
   and depend on the particular system call. */
static bool
args_valid (uint32_t *args, int num_args)
{
  struct thread *t = thread_current ();

  /* argv and all argv[i]. Check pointer address and value
     (should also be a user space ptr) */
  int i;
  for (i = 0; i < num_args + 1; i++)
    {
      if (args == NULL || !(is_user_vaddr (args)) ||
          pagedir_get_page (t->pagedir, args) == NULL)
        return false;

      args++;
    }
  return true;
}

static bool
arg_addr_valid (void *arg)
{
  if ((arg == NULL) || !(is_user_vaddr (arg)) ||
      pagedir_get_page (thread_current () ->pagedir, arg) == NULL)
    return false;

  return true;
}
