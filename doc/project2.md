Design Document for Project 2: User Programs
============================================

## Group Members

* William Smith <williampsmith@berkeley.edu>
* John Seong <jwseong@berkeley.edu>
* Gera Groshev <groshevg@berkeley.edu>
* Joe Kuang <joekuang@berkeley.edu>


## __Task 1: Argument Passing__


### Data Structures and Functions
* `char *argv[]` and `int argc` will be created in `load()`
* `char *argv[]` will be created by using `strtok_r()` and each string will be NULL terminated
* `int argc` will be the count of strings that are in `char *argv[]`


### Algorithms

#### Changes to `bool load()`
`bool load (const char *file_name, void (**eip) (void), void **esp)`
* Parse using `strtok_r()` and count `filename` into `char *argv[]` and `int argc` before call to `filesys_open`
* Change call to `filesys_open` to pass in `argv[0]`.
* Change call to `setup_stack()` to pass in `char *argv[]`, `int argc`

#### Changes to `static bool setup_stack()`
`static bool setup_stack (void **esp, int argc, char *argv[])`
* Receive `int argc` and `char *argv[]` as arguments
* After `install_page ()`, populate the page according to the table in 3.1.9

##### Referring to 3.1.9 from project spec
* Provides support for parsing filename and adding values to user stack
* Add argc, argv, and all arguments to user stack.
* Add fake (0) return address to stack


### Synchronization
> We do not have to worry about synchronization here since a thread creates its own local variable that they pass on.

### Rationale
> The use of `strtok_r()` handles extra spaces and delimiters by tokenizing each argument by spaces and adds a NULL terminator, which is what we want to do before adding the argument to the stack. It is also thread safe, whereas `strtok()` is not.


## __Task 2: Process Control Syscalls__


### Data Structures and Functions

`#define NUMSYSCALLS 14`

#### Define array of syscall functions
`typedef static void (*syscall_t)( uint32_t *args UNUSED, uint32_t *eax );`
`syscall_t syscalls[NUMSYSCALLS];`

#### Addition to `struct thread`
```C
struct thread {
  struct list children; // to reference child wait statuses
  struct wait_status own_wait_status;
};
```

#### Definition of `struct wait_status`
```C
struct wait_status { // defines a single parent child relationship
  pid_t pid; // to denote which process this belongs to
  int exit_code;
  struct list_elem elem;
  struct semaphore sema; // init to 0
  int ref_count // initially 2
  struct lock lock; // strictly for changing value of ref_count
};
```

#### Definition of load_synch struct for synchronization of `process_execute()` with `load()`
```C
struct load_synch {
  char *filename;
  semaphore sema;
  bool loaded;
};
```

### Algorithms

#### Implementation of new function `bool args_valid()`
`bool args_valid(uint32_t argv, int num_args)`
* for each arg in *argv up to num_args:
	* check that it is not null
	* check `is_user_vaddr()` and `pagedir_get_page()` does not return NULL
	* If this is bad, return false. Otherwise:
		* Round down to the nearest page boundary
* Check that `argv + num_args + 1 /* argc */ )` is not `NULL` and `pagedir_get_page()` not `NULL`
* If bad, return false
* else, return true

#### Changes to `static void syscall_handler()` in `userprog/syscall.c`
`static void syscall_handler (struct intr_frame *f UNUSED)`
* Check for null value of `f->esp`
* `uint32_t* args = ((uint32_t*) f->esp);`
	* if args is NULL or is_user_vaddr returns false:
		* `f->eax = -1`
		* return
	* `int syscall = args[0]`
	* `args++` // now points to actual args
	* `syscalls[args[0]](args, (uint32_t *) &(f->eax));` // index syscalls array and call function `

#### Changes to `static void start_process()` in `userprog/process.c`
`static void start_process (void *file_name_)`
* Add support for synchronization of loading and thread_create so that we return TID_ERROR  if loading fails.
	* Retrieve `filename` from the `load_synch` struct
	* Set `loaded` to `false` if loading fails.
	* Sema up.

#### Implementation of `static void syscall_practice(uint32_t *args, uint32_t *eax)` in `userprog/syscall.c`
`static void syscall_practice(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* Increment `args[0]` by 1 and store result in `*eax`

#### Implementation of `static void syscall_halt(uint32_t *args, uint32_t *eax)` in `userprog/syscall.c`
`static void syscall_halt(uint32_t *args, uint32_t *eax)`
* Call `shutdown_power_off()`

#### Implementation of `static void syscall_exec(uint32_t *args, uint32_t *eax)` in `userprog/syscall.c`
`static void syscall_exec(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* cast `args[0]` to `(char *)` and store in variable `char *file_name`
* Call `process_execute(file_name)`
* Check if TID_ERROR returned by `process_execute()`, if so set `*eax` to -1
* Otherwise, set `*eax` to pid returned by `process_execute()`

#### Changes to `tid_t process_execute()` in `userprog/process.c`
`tid_t process_execute (const char *file_name)`
* Add support for synchronization of loading and `thread_create()` so that we return `TID_ERROR` if loading fails.
* Initialize load_synch struct and insert `file_name` (defined below)
* Pass in pointer to `load_synch` member in lieu of `fn_copy` to `thread_create()` function call.
* Sema down on `load_synch` semaphore.
* Check value of `loaded` and return appropriate return value.

#### Implementation of `static int syscall_wait(uint32_t *args, uint32_t *eax)` in `userprog/syscall.c`
`static int syscall_wait(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* cast `args[0]` to tid_t and store in variable `tid_t child_tid`
* call `process_wait(child_tid)`
* set `*eax` to `child_exit_status` value returned by `process_wait()`

#### Implementation of `int process_wait()` in `userprog/process.c`
`int process_wait (tid_t child_tid UNUSED)`
* Iterate through `thread_current()->children` and find child.
* sema_down on `child->own_wait_status->sema` to wait for child to exit
* return `child->own_wait-status->exit_code`

#### Implementation of `static void syscall_exit(uint32_t *args, uint32_t *eax)` in `userprog/syscall.c`
`static void syscall_exit(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* assign `args[0]` to variable `int status`
* set `thread_current()->own_wait_status->exit_code` to `status`
* call `thread_exit()`

#### Implementation of `void process_exit()` in `userprog/process.c`
`void process_exit (void)`
* If `thread_current()->own_wait_status->exit_code == NULL`, then:
	* Set to -1 `// must have been called by kernel due to exception`
* acquire `thread_current()->own_wait_status->lock`
* decrement `thread_current()->ref_count`
* sema_up on `thread_current()->own_wait_status->sema` to wake parent
* If `ref_count` is now 0, then free `thread_current()->own_wait_status`
* If children list is not NULL:
	* Iterate through list of children, and for each child:
		* aqcuire `child->own_wait_status->lock`
		* decrement `child->own_wait_status->ref_count`
		* If ref_count is now 0, then free `child->own_wait_status`

#### Additions to `tid_t thread_create()` in `threads/threads.c`
`tid_t thread_create (const char *name, int priority, thread_func *function, void *aux)`
* Add initialization of thread struct members after creation of thread t.
```C
	* struct list children;
	* list_init(&children);
	* wait_status *own_wait_status = malloc( sizeof(struct wait_status) );
	* own_wait_status->exit_code = NULL;
	* own_wait_status->pid = (pid_t)t->tid;
	* own_wait_status->elem = t->elem;
	* struct semaphore status;
	* sema_init(&status, 0);
	* own_wait_status->sema = status;
	* own_wait_status->ref_count = 2;
	* own_wait_status->lock = struct lock;
	* lock_init(&(own_wait_status->lock));
	* t->own_wait_status = own_wait_status;
```
* Directly after, add elem to parent's list of children.
	* `list_push_back( &(thread_current()->children), &(t->wait_status->elem) );`


### Synchronization
> We use a semaphore to synchronize the parent and the child. The parent waits by calling sema down, which, in the case of a child that has already died, will already have been sema upped. We acquire a lock to synchronize access of the ref_count member, since this is the only member of the wait_status struct that both the parent and the child access (aside from the semaphore, whose operations are atomic and thus do not require synchronization). Regarding an invalid load of a new executable during `process_exec()`, we take care of sending an error message back to the syscall handler by the creation of the `load_synch struct`, whose address is passed to the call to load. We then sema down, ensuring that the parent thread waits for the child to complete the load process and sema up, indicating its status in the boolean field of the struct.

### Rationale
> By setting the exit status in the `syscall_exit()` function, we are able to maintain the function signature of `thread_exit()` to taking no arguments. However, in the case of an exception, the kernel will call `thread_exit()`, bypassing the `exit()` userspace system call, and therefore the `syscall_exit()` function. In this case, we needed to ensure that the exit status is set to -1 appropriately. We therefore set an initial value of the exit status to NULL. In `thread_exit()`, we check this value, assuming that if the thread exited gracefully, this should already be set in `syscall_exit()`. If not, we set it to -1.


## __Task 3: File Operation Syscalls__

### Data Structures and Functions

#### Additions to `struct thread`
```C
struct thread {
  struct fd *fd[128];
};
```

#### Definition of `struct fd`
```C
struct fd {
  int fd_num;
  struct file *file
};
```

### Algorithms

#### Additions to `load()`
* Add call to `file_deny_write(file)` after call to `file_read()`.

#### Additions to `process_exit()`
* Add call to `file_allow_write(file)` at the end.

#### Additions to `thread_exit()`
`static int thread_exit()`
* As last step before dying, iterate through list of fd’s and call `close()` on each.

#### Additions to `thread_create()`
`tid_t thread_create()`
* Add call to a function `init_fd_list()`

#### Implementation of `init_fd_list()` in `thread.c`
* Initialize the array of struct fd with size 128
* Create two dummy struct fd for fd number 0 and 1
* Unused fd will have a member variable fd_num initialized to -1 and file pointer to a NULL
* When a fd is acquired, its index in the array will correspond to its fd number

#### Implementation of `syscall_create()` in `userprog/syscall.c`
`static void syscall_create(uint32_t *args, uint32_t *eax)`
* Acquire global file lock
* `args[0]` contains the file_name
* `args[1]` contains the initialize_size
* store value returned by `filesys_create(args[0], args[1])` into `*eax`
* Release file lock

#### Implementation of `syscall_remove()` in `userprog/syscall.c`
`static void syscall_remove(uint32_t *args, uint32_t *eax)`
* `args[0]` contains the file_name
* Acquire global file lock
* store value returned by `filesys_remove(args[0])` into `*eax`
* Release file lock

#### Implementation of `syscall_open()` in `userprog/syscall.c`
`static void syscall_open(uint32_t *args, uint32_t *eax)`
* `args[0]` contains the file_name
* Acquire global file lock
* store return value of `filesys_open(args[0])` into `struct file *file` variable
* Release file lock
* check if file is NULL
  * if not: iterate `thread_current()->fd` to find the first available file descriptor and initialize struct fd by calling `add_fd()`
* store the file descriptor number in `*eax`

#### Implementation of `add_fd()` in `thread.c`
`int add_fd (struct file *file)`
* Make a call to `next_fd_num()` to obtain next available fd number
* Assign `fd->fd_num` to its index and `fd->file` from argument

#### Implementation of `next_fd_num()` in `thread.c`
`static int next_fd_num (struct thread *t)`
* Iterate through the `struct fd *fd[128]` and find first fd available
* Available fd is shown as `fd_num = -1`

#### Implementation of `syscall_filesize()` in `userprog/syscall.c`
`static void syscall_filesize(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* Read argc and argv from stack and create appropriate args
* Acquire global file lock
* Open the file assigned with fd by calling `get_file()`
* call and save return value of `file_length()` in `*eax` with args
* release file lock

#### Implementation of `get_file()` in `thread_c`
`struct file *get_file (struct thread *t, int fd)`
* Iterate through `struct fd *fd[128]` and return `file *` that is assigned with the given fd

#### Implementation of `syscall_read()` in `userprog/syscall.c`
`static void syscall_filesize(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* Read argc and argv from stack and create appropriate args
* If `fd == 0` STDIN
  * Acquire file lock
  * Read from `input_getc()` and store into given buffer
  * Release file lock
* Else
  * Acquire global file lock
  * Open the file assigned with fd by calling `get_file()`
  * call and save return value of `file_read()` in `*eax` with appropriate args
  * release file lock

#### Implementation of `syscall_write()` in `userprog/syscall.c`
`static void syscall_filesize(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* Read argc and argv from stack and create appropriate args
* Acquire global file lock
* call and save return value of `file_write()` in `*eax` with appropriate args
* release file lock

#### Implementation of `syscall_seek()` in `userprog/syscall.c`
`static void syscall_seek(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* Read argc and argv from stack and create appropriate args
* Acquire global file lock
* Obtain the `file*` by calling `get_file()`
* call `file_seek()` with appropriate args
* release file lock

#### Implementation of `syscall_tell()` in `userprog/syscall.c`
`static void syscall_tell(uint32_t *args, uint32_t *eax)`
* call `args_valid()` with appropriate number of args
* Read argc and argv from stack and create appropriate args
* Acquire global file lock
* Obtain the `file*` by calling `get_file()`
* call and save return value of `file_tell()` with appropriate args and save into `*eax`
* release file lock

#### Implementation of `syscall_close()` in `userprog/syscall.c`
`static void syscall_close(uint32_t *args, uint32_t *eax)`
* `args[0]` contains `file_name`
* Acquire global file lock
* Obtain the `file*` by calling `get_file()`
* Call `file_close()` to close the file
* Call `remove_fd()` to free the fd from the fd array
* Release file lock

#### Implementation of `remove_fd()` in `thread.c`
`void remove_fd (struct thread *t, int fd)`
* Reset the `struct fd` at index `int fd`
 * `fd_->fd_num = -1`
 * `fd_->file = NULL`

### Synchronization
> We use a simple global lock, which is acquired before, and released after, each file system call. At the expense of huge inefficiency, it provides a simplistic and guaranteed synchronization of file accesses.

### Rationale
> We call `file_deny_write()` right after executable creation in `load()`, and call `file_allow_write()` at the end of `process_exit()`, to ensure that the executable is not written to while it is executing. We use a list of fd’s and add it to the thread struct in order to make fd’s thread specific and for ease of lookup (only have to look through our list). We create a struct to wrap the file information which keeps track of the number of references to it. This ensures that a file is not removed while still referenced by a fd. The bool removed variable is set upon a call to remove, so that subsequent calls to close or remove that result in no references will also remove the file struct.


## Additional Questions

### Question 1
> `sc-bad-sp.c` performs a test case where it invokes a system call with the stack pointer `%esp` set to a bad address. It only has two lines of code, line 18 and 19, and each does following:

> Line 18: Moves `%esp` to 64MB below using movl command which is invalid and invokes an interrupt `int $0x30`

> Line 19: This line should have never been called since line 18 should have terminated with `exit(-1)`

### Question 2
> `sc-bad-arg.c` performs a test case where it invokes a system call at the valid address but its argument is outside the boundary. It only has two lines of code, line 14 and 15, and each does following:

> Line 14: Places a system call number `SYS_EXIT` at top of the stack using movl command and places `%esp` at the boundary ($0xbffffffc) which is still a valid location. Then it invokes the system call by invoking `int $0x30`

> Line 15: This line should have never been called since line 14 should have terminated with `exit(-1)` since the argument to the system call is located above the top of the user address space

### Question 3
> There is no test to check whether reading from `stderr` causes error. The test will be exactly same as `read-stdout.c`.

> Additional tests we can add is checking whether or not multiple file descriptors of same files that are spawned by `open()` share status and offset. We can make a test for read and another for write. For read, we can read first few bytes and check if another fd reads the same bytes or the next few bytes. For write, we can make each fd write some random bytes of same length, and the file should contain contents the very last fd has wrote.


## GDB Questions

### Question 1
> The thread currently executing is main. The only other thread in pintos is the idle
> thread. See below for their struct threads:
```c
pintos-debug: dumplist #0: 0xc000e000
{tid = 1, status = THREAD_RUNNING, name = "main", '\000'
<repeats 11 times>, stack = 0xc000ee0c "\210", <incomplete sequence \357>,
priority = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020},
elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>},
pagedir = 0x0, magic = 3446325067}

pintos-debug: dumplist #1: 0xc0104000
{tid = 2, status = THREAD_BLOCKED, name = "idle", '\000'
<repeats 11 times>, stack = 0xc0104f34 "", priority = 0,
allelem = {prev = 0xc000e020, next = 0xc0034b58 <all_list+8>},
elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>},
pagedir = 0x0, magic = 3446325067}
```

### Question 2
> Backtrace is as follows:
```c
 --Executed at /userprog/process.c line 32
#0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none")
      at ../../userprog/process.c:32

--Executed at threads/init.c line 288
#1  0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288

--Executed at threads/init.c at line 340
#2  0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340

--Executed at threads/init.c at line 133
#3  main () at ../../threads/init.c:133
```

### Question 3
> The thread currently executing is args-none. Other threads running are idle and main.
> See below for their struct threads and addresses :
```c
pintos-debug: dumplist #0: 0xc000e000
{tid = 1, status = THREAD_BLOCKED, name = "main", '\000' <repeats 11 times>,
stack = 0xc000eebc "\001", priority = 31, allelem = {prev = 0xc0034b50 <all_list>,
next = 0xc0104020}, elem = {prev = 0xc0036554 <temporary+4>,
next = 0xc003655c <temporary+12>}, pagedir = 0x0, magic= 3446325067}

pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED,
name = "idle", '\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0,
allelem = {prev = 0xc000e020, next = 0xc010a020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}

pintos-debug: dumplist #2: 0xc010a000 {tid = 3, status = THREAD_RUNNING,
name = "args-none\000\000\000\000\000\000", stack = 0xc010afd4 "",
priority = 31, allelem = {prev = 0xc0104020, next = 0xc0034b58 <all_list+8>},
elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>},
pagedir = 0x0, magic =3446325067}
```

### Question 4
> The line that creates the thread that executes start_process is shown below:
```c
tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
```

### Question 5
> The line is as follows:
`#0  0x0804870c in ?? ()`

### Question 6
> Now the line is as follows:
```c
#0  _start (argc=<error reading variable: can't compute CFA for this frame>, argv=<error reading variable : can't compute CFA for this frame>) at ../../lib/user/entry.c:9
```

### Question 7
> argv and argc are not defined yet so it’s causing error when trying to access those.
