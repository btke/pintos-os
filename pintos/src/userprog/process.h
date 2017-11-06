#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include "lib/kernel/list.h"
#include "filesys/directory.h"

typedef int tid_t;
typedef int pid_t;

int process_execute (const char *file_name);
tid_t process_wait (tid_t child_tid);
void process_exit (void);
void process_activate (void);

/* Child thread wait status with exit code */
struct wait_status
  {
    int exit_code;
    pid_t pid;
    struct list_elem wait_elem;
    struct semaphore sema;
    int ref_count;
    struct lock lock;
    bool valid;
    bool parent_waited;
  };

/* Used to synchronize and wait for load to complete */
struct load_synch
  {
    char *filename;
    struct semaphore sema;
    bool success;
    struct dir *parent_working_dir;
  };

#endif /* userprog/process.h */
