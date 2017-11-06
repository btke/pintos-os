Final Report for Project 2: User Programs
=========================================

## __Changes to Task 1: Argument Passing__

### Data Structures and Functions

#### Addition of function `void exit_(int exit_code)` function
> This is a wrapper function of exit to support passing exit code. Specifically for use with exception exits.

### Algorithms

#### Parsing strings
> `strtok_r()` was used instead of `strtok()`. This allowed for thread safe concurrent parsing.

### Synchronization
> We added logic to acquire the file system lock for file system function calls in `load()` and `setup_stack()`.

## __Changes to Task 2: Process Control Syscalls__

### Data Structures and Functions

#### Changes to addtions to `struct thread`

##### Before
```C
struct thread {
  struct thread_t *parent;
  struct list child_exit_statuses;
  struct semaphore wait_sema;
  bool parent_waiting;
  bool parent_died;
};
```

##### After
```C
struct thread {
  struct list children; // to reference child wait statuses
  struct wait_status *own_wait_status;
};
```

> We simplified the design by saving a pointer to a wait_status struct in order to support the new design pattern, in which we malloc a wait status struct and share it between the parent and the child, i.e. they both have a reference. We keep a reference count and destroy the shared varaiable when the reference count reaches 0. We use the semaphore to synchronize exiting and waiting.

#### Changes to definition of `struct exit status`

##### Before
```C
struct exit_status {
  pid_t pid;
  int status;
};
```

##### After
```C
struct wait_status {
  pid_t pid; // to denote which process this belongs to
  int exit_code;
  struct list_elem elem;
  struct semaphore sema; // init to 0
  int ref_count // initially 2
  struct lock lock; // strictly for changing value of ref_count
  bool valid;
  bool parent_waited;
};
```

> The wait status struct now encapsulates a single parent child relationship, and is shared between the parent and the child. The ref count keeps count of how many observers are alive, and allows the last surviving member to destroy the malloc'ed struct before exiting. The `valid` field was added to get rid of compiler errors inherent in initializing the value of the exit code to NULL (since it is of type int), which might be cast to a 0, which is undesirable, as it would give a false successful exit code. `bool parent_waited` field was added in order to handle the behavior of not allowing the parent to wait on the same child more than once. Other methods were considered, but proved to be less general. For example, we considered freeing the wait status struct of the child in the parent's list of children once the parent has waited on that child (since we know at that point it must be dead). But we realized that this would keep us from being able to use the list of children struct for uses other than waiting on the child, which would make it less general. Also, this implementation was more straightforward.

#### Changes to `static void page_fault (struct intr_frame *f)` in `exception.c`
> We changed the handling of page faults to call exit_(-1) and set *eax to -1. This keeps the kernel from panicking when the user tries to dereference an invalid address without making a system call.

### Algorithms

#### Changes to `static bool args_valid(uint32_t *args, int num_args)`
* Removed the logic that checks for invalid addresses of *args (arg dereferenced).
* Placed this logic in a new function `arg_addr_valid()`.
* This function must be called once for each *arg to check.

> The purpose of this restructuring was because not all *arg values are themselves pointers, and therefore we cannot generalize this logic to all system calls. Instead, this is only called for args of type char*. Added call to `pagedir_get_page()` for each arg. This checks for addresses that will result in page fault.

#### Addition of function `static bool arg_addr_valid(void *arg)`
> This serves as a single arg check for arguments of type char *, to check for NULL, as well as non-user vaddr and addresses that will lead to page fault.


## __Changes to Task 3: File Operation Syscalls__

### Data Structures and Functions

#### Removed Definition of struct file_info

> Removed since we don't need to keep track of `inode` and `file_name`

#### Changes in Additions to `struct thread`

##### Before
```C
struct thread {
  struct list fd_list;
};
```

##### After
```C
struct thread {
  struct fd *fd[128];
};
```

> Rather than having a list of fd (file descriptor) list elem, we decided to keep an array of struct fd pointer. We did this for lookup efficiency since we are constraining the number of fd's a thread can have open at a given time, which therfore lends itself to a fixed size array which can be indexed, rather than iterated on. The index of the array is the fd number. Unallocated fd will have a null pointer in the file member in the correspopnding location in the array.

#### Changes in Definition of `struct fd`

##### Before
```C
struct fd {
  int fd_num;
  struct file *file
  list_elem elem;
};
```

##### After
```C
struct fd {
  int fd_num;
  struct file *file
};
```

> Because of the change in design to keep track of file descriptors, we removed the list_elem variable inside struct fd.

### Algorithms

#### Changes in additions to `thread_exit()`

##### Before
`static int thread_exit()`
* As last step before dying, iterate through list of fd’s and call `close()` on each.

##### After
`static int thread_exit()`
* As last step before dying, call `close_all_files()` that iterate through `*fd[]` and call `close()` on each.
* Call `lock_held_by_current_thread(&file_lock))` to check if `file_lock` is acquired
  * If acquired, call `release_file_lock()` to release file_lock

#### Additions to `thread_create()`
`tid_t thread_create()`
* Add call to a function `init_fd_list()`

#### Implementation of `init_fd_list()` in `thread.c`
* Initialize the array of struct fd with size 128
* Create two dummy struct fd for fd number 0 and 1
* Unused fd will have a member variable fd_num initialized to -1 and file pointer to a NULL
* When a fd is acquired, its index in the array will correspond to its fd number

#### Addition to Implementation of `syscall_create()` in `userprog/syscall.c`
`static void syscall_create(uint32_t *args, uint32_t *eax)`
* Check whether the length of filename is greater than 14 characters

#### Changes in Implementation of `syscall_remove()` in `userprog/syscall.c`

##### Before
`static void syscall_remove(uint32_t *args, uint32_t *eax)`
* `args[0]` contains the file_name
* iterate through `file_list` and check `file_name` to find the `file_info` element corresponding to the file with name `args[0]`
* if found, set the `removed` member of the `file_info` element to true
* check `open_count` member, if it’s 0 then close the file
* store value returned by `filesys_remove(args[0])` into `*eax`

##### After
`static void syscall_remove(uint32_t *args, uint32_t *eax)`
* `args[0]` contains the file_name
* Acquire global file lock
* store value returned by `filesys_remove(args[0])` into `*eax`
* Release file lock

> We changed design so that we don't use the `struct file_info`.
> So we just simply call `filesys_remove()`

#### Changes in Implementation of `syscall_open()` in `userprog/syscall.c`
`static void syscall_open(uint32_t *args, uint32_t *eax)`

##### Before
* `args[0]` contains the file_name
* store return value of `filesys_open(args[0])` into `struct file *file` variable
* check if file is NULL, if not
* iterate `thread_current()->fd_list` to find the first available file descriptor and allocate a `struct fd` for the file descriptor
* assign the `file` and `fd_num` in the new `struct fd` and add it to `thread_current()->fd_list`
* iterate through `file_list`, and increment `open_count` corresponding the the appropriate `struct inode`
* store the file descriptor in `*eax`

##### After
* `args[0]` contains the file_name
* Acquire global file lock
* store return value of `filesys_open(args[0])` into `struct file *file` variable
* Release file lock
* check if file is NULL
  * if not: iterate `thread_current()->fd` to find the first available file descriptor and initialize struct fd by calling `add_fd()`
* store the file descriptor number in `*eax`

> Changes in design due to removal of `struct file_info`

#### Implementation of `add_fd()` in `thread.c`
`int add_fd (struct file *file)`
* Make a call to `next_fd_num()` to obtain next available fd number
* Assign `fd->fd_num` to its index and `fd->file` from argument

#### Implementation of `next_fd_num()` in `thread.c`
`static int next_fd_num (struct thread *t)`
* Iterate through the `struct fd *fd[128]` and find first fd available
* Available fd is shown as `fd_num = -1`

#### Additions to Implementation of `syscall_filesize()` in `userprog/syscall.c`
`static void syscall_filesize(uint32_t *args, uint32_t *eax)`
* Open the file assigned with fd by calling `get_file()`

> Added details on how to get the file asked for filesize

#### Implementation of `get_file()` in `thread_c`
`struct file *get_file (struct thread *t, int fd)`
* Iterate through `struct fd *fd[128]` and return `file *` that is assigned with the given fd

#### Changes in Implementation of `syscall_read()` in `userprog/syscall.c`
`static void syscall_filesize(uint32_t *args, uint32_t *eax)`

##### Before
* call `args_valid()` with appropriate number of args
* Read argc and argv from stack and create appropriate args
* Acquire global file lock
* call and save return value of `file_read()` in `*eax` with appropriate args
* release file lock

##### After
* call `args_valid()` with appropriate number of args
* Read argc and argv from stack and create appropriate args
* If `fd == 0` STDIN
  * Acquire file lock
  * Read from `input_getc()` and store into given buffer
  * Release file lock
* Else If `fd == 1` STDOUT
  * Call exit
* Else
  * Acquire global file lock
  * Open the file assigned with fd by calling `get_file()`
  * call and save return value of `file_read()` in `*eax` with appropriate args
  * release file lock

> We had to add a check for `fd` since reading from `STDIN` requires reading from `input_getc()` and can't read from `STDOUT`

#### Addition to Implementation of `syscall_seek()` in `userprog/syscall.c`
`static void syscall_seek(uint32_t *args, uint32_t *eax)`
* Obtain the `file*` by calling `get_file()`

#### Addition Implementation of `syscall_tell()` in `userprog/syscall.c`
`static void syscall_tell(uint32_t *args, uint32_t *eax)`
* Obtain the `file*` by calling `get_file()`

#### Changes in Implementation of `syscall_close()` in `userprog/syscall.c`
`static void syscall_close(uint32_t *args, uint32_t *eax)`

##### Before
* `args[0]` contains `file_name`
* store return value of `filesys_close(args[0])` into `struct file *file` variable
* check if file is NULL. If so, set `*eax` to -1
* iterate `thread_current()->fd_list` to find corresponding fd.
* assign the `file` and `fd_num` for the struct fd and add the element to `fd_list`
* iterate through `file_list`, and decrement `open_count` corresponding the the appropriate `struct inode`
* check if `open_count` is 0 and `removed` is true, if so then close the file

##### After
* `args[0]` contains `file_name`
* Acquire global file lock
* Obtain the `file*` by calling `get_file()`
* Call `file_close()` to close the file
* Call `remove_fd()` to free the fd from the fd array
* Release file lock

> Revision due to changes in design to keep track of files

#### Implementation of `remove_fd()` in `thread.c`
`void remove_fd (struct thread *t, int fd)`
* Reset the `struct fd` at index `int fd`
 * `fd_->fd_num = -1`
 * `fd_->file = NULL`

## Reflections
> Everything worked very well for us. William and Gera teamed up on implementing and debugging tasks 1 and 2, John and Joe teamed up to work on implementing and debugging task 3 as well as implementing and debugging student test cases. One thing that could be improved upon for the future is the division of labor with respect to the test cases. We found that having a subset of the group handle test cases made it difficult, as they did not understand all of the implementation details of all parts of the project, and therefore the edge cases that could arise. We decided that, going forward, we should divide testing amongst all group members, making each group member responsible for creating a test or two for his section of the project.

## __Student Testing Report__

### Test 1: Seek and Tell

#### Description
* The test "Seek and Tell" contained in `seek-tell.c` tests whether or not `seek()` and `tell()` works.
> `seek()` is used in couple places but `tell()` is not tested, so we made a test for `tell()`.
> But in order to check whether `tell()` works, we had to use `seek()`.
> In addition, we used `filesize()` to get the size of the file, but this addition didn't get pushed by accident.

#### Overview
1. The test first creates a file called `seek-tell.txt` with size of `char sample[]` in `sample.inc` using `create()`.
> Expected output: create "seek-tell.txt"

2. Open the file `seek-tell.txt` using `open()` and save the fd into `handle`.
> Expected output: open "seek-tell.txt"

3. Write the `char sample[]` into the file using `write()` and assign the result into `byte_cnt`

4. Check whether `byte_cnt` is same as `sizeof sample - 1`

5. Call `seek()` at `sizeof sample - 10`
> Expected output: seek "seek-tell.txt"

6. Call `tell()` at `sizeof sample - 10`
> Expected output: tell "seek-tell.txt"

7. Check whether `tell()` returned the value we set with `seek()`
> This check concludes whether or not `seek()` and `tell()` works or not

* We used `CHECK()` to see if output behavior is something we anticipated.
> This test allows us to ensures `create()`, `open()` and `write()` works correctly in addition to `seek()` and `tell()`

* We changed the test so that `sizeof sample` is changed to `filesize()` later but didn't get pushed by accident.
> This would have ensured `filesize()` to work correctly as well.

#### Output
> Copying tests/userprog/seek-tell to scratch partition...

> Copying ../../tests/userprog/sample.txt to scratch partition...

> qemu -hda /tmp/DDqxZ1A6UZ.dsk -m 4 -net none -nographic -monitor null

> PiLo hda1

> Loading...........

> Kernel command line: -q -f extract run seek-tell

> Pintos booting with 4,088 kB RAM...

> 382 pages available in kernel pool.

> 382 pages available in user pool.

> Calibrating timer...  360,038,400 loops/s.

> hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"

> hda1: 176 sectors (88 kB), Pintos OS kernel (20)

> hda2: 4,096 sectors (2 MB), Pintos file system (21)

> hda3: 106 sectors (53 kB), Pintos scratch (22)

> filesys: using hda2

> scratch: using hda3

> Formatting file system...done.

> Boot complete.

> Extracting ustar archive from scratch device into file system...

> Putting 'seek-tell' into the file system...

> Putting 'sample.txt' into the file system...

> Erasing ustar archive...

> Executing 'seek-tell':

> (seek-tell) begin

> (seek-tell) create "seek-tell.txt"

> (seek-tell) open "seek-tell.txt"

> (seek-tell) seek "seek-tell.txt"

> (seek-tell) tell "seek-tell.txt"

> (seek-tell) end

> seek-tell: exit(0)

> Execution of 'seek-tell' complete.

> Timer: 59 ticks

> Thread: 0 idle ticks, 58 kernel ticks, 1 user ticks

> hda2 (filesys): 118 reads, 224 writes

> hda3 (scratch): 105 reads, 2 writes

> Console: 1065 characters output

> Keyboard: 0 keys pressed

> Exception: 0 page faults

> Powering off...

#### Result
> PASS

#### Non-Trivial Potential Kernel Bugs
1. If `write()` returned some other value instead of `sizeof sample - 1`, then the test case would output `FAIL` instead.

2. If `tell()` returned some other value instead of value that was used to set `seek()`, then the test case would output `FAIL` instead.

### Test 2: Open Shared File Descriptor

#### Description
* The test "Open Shared File Descriptor" contained in `open-shared.c` tests whether or not `read()` and `write()` use same file descriptor when used separate file descriptors for each function. The goal is to make multiple file descriptors not share the same file descriptor.
> int fd1 = open ("test.txt"));

> int fd2 = open ("test.txt"));

> fd1 and fd2 should not be same, and when read(fd1) and write(fd2) was called, they should be have a separate file descriptors, meaning each have different offset and status.

#### Overview
1. Create a file called `test.txt` using `create()`.
> Expected output: create "test.txt"

2. Call `open()` twice on `test.txt` and assign it to `handle1` and `handle2`
> Expected output:

> open "test.txt"

> open "test.txt" again

3. Write the `char sample[]` into the `test.txt` using `write()` and assign the result into `byte_cnt1`

4. Read `test.txt` using `read()` and assign the result into `byte_cnt2`

5. Check whether `byte_cnt1` is equal to `sizeof sample - 1`
> This ensures `write()` works correctly.

6. Check whether `byte_cnt1` is equal to `byte_cnt2`
> They should be same since they wrote and read the same thing. Because they used different file descriptors, `read()` shouldn't have read from where `write()` has finished. This means that `read()` should have started from `off_t pos = 0`.

* We used `CHECK()` to see if output behavior is something we anticipated.
> This test allows us to ensures `create()` and `open()` work correctly.

#### Output

> Copying tests/userprog/open-shared to scratch partition...

> Copying ../../tests/userprog/sample.txt to scratch partition...

> qemu -hda /tmp/CgYTY28ciB.dsk -m 4 -net none -nographic -monitor null

> PiLo hda1

> Loading...........

> Kernel command line: -q -f extract run open-shared

> Pintos booting with 4,088 kB RAM...

> 382 pages available in kernel pool.

> 382 pages available in user pool.

> Calibrating timer...  209,510,400 loops/s.

> hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"

> hda1: 176 sectors (88 kB), Pintos OS kernel (20)

> hda2: 4,096 sectors (2 MB), Pintos file system (21)

> hda3: 107 sectors (53 kB), Pintos scratch (22)

> filesys: using hda2

> scratch: using hda3

> Formatting file system...done.

> Boot complete.

> Extracting ustar archive from scratch device into file system...

> Putting 'open-shared' into the file system...

> Putting 'sample.txt' into the file system...

> Erasing ustar archive...

> Executing 'open-shared':

> (open-shared) begin

> (open-shared) create "test.txt"

> (open-shared) open "test.txt"

> (open-shared) open "test.txt" again

> (open-shared) exec child thread

> (open-shared) exec child thread: FAILED

> open-shared: exit(1)

> Execution of 'open-shared' complete.

> Timer: 55 ticks

> Thread: 0 idle ticks, 54 kernel ticks, 1 user ticks

> hda2 (filesys): 138 reads, 225 writes

> hda3 (scratch): 106 reads, 2 writes

> Console: 1097 characters output

> Keyboard: 0 keys pressed

> Exception: 0 page faults

> Powering off...

#### Result
> PASS

#### Non-Trivial Potential Kernel Bugs
1. If `write()` returned some other value instead of `sizeof sample - 1`, then the test case would output `FAIL` instead.

2. If handle1 and handle2 were same instead of separate file descriptors, then the test case would output `FAIL`.
> Because if file descriptor was same, it would result `byte_cnt1` and `byte_cnt2` to be different since `read()` did not start reading where `write()` has started from (they should have started from offset = 0). `read()` will start reading from the offset `write()` has finished writing (meaning, they shared same offset).


### Conclusion

#### Tell us about your experience writing tests for Pintos.
> In order to come up with good and thorough tests, we had to go through all the existing tests and find edge cases. It was some what difficult to understand all the existing tests and see what they are clearly testing. We found out that simple file system functions such as `tell()`, `filesize()`, and `remove()` were not tested, so rather than coming up with another extensive test such as testing whether child thread writes to parent's executable, we decided to make sure simple file system functions behave like they should. That is how `seek-tell` test was created. Although we added codes to test `filesize()` as well, we forgot to git push. We couldn't test `remove()` due to failure to override built-in function.
> It was also difficult to understand `Make.tests` and find out how to add our own tests.

#### What can be improved about the Pintos testing system? (There's a lot of room for improvement.)
> `Make.tests` can be a bit more user-friendly. For example, we can add comments in places hinting where to add test directories and other additional files needed for tests.
> Writing better description of the pre-existing tests can further help users to figure out edge cases that are not covered by existing tests.

#### What did you learn from writing test cases?
> We wrote codes thinking about writing tests at same time, so we wrote codes pretending like we are covering all the edge cases. And by writing test cases, we came up with more edge cases that would further strengthen our code. And it also helps us understand better this project's objective. 
