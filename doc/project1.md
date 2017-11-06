Design Document for Project 1: Threads
======================================

## __Group Members__

* William Smith <williampsmith@berkeley.edu>
* John Seong <jwseong@berkeley.edu>
* Gera Groshev <groshevg@berkeley.edu>
* Joe Kuang <joekuang@berkeley.edu>

## __Task 1: Efficient Alarm Clock__


### Data Structures and Functions

#### Sleeping List Queue Definition
```c
static struct list sleeping_list; // linked list of thread elem
```

#### Addon to thread struct, global reference wakeup time
`int64_t wakeup_time;`

#### Sleeping list insertion function
`void enqueue_sleeping_list (int64_t wakeup_time, struct thread* thread);`

#### Redefinition of timer sleep function to support non-busy wakeup
`void timer_sleep (int64_t ticks);`

#### Timer awake function to wake sleeping threads after sleep time
`static void thread_awake (void);`

#### Redefinition of timer interrupt handler. Does not busy wait.
`static void timer_interrupt (struct intr_frame *args UNUSED);`

#### List comparator function for "list_insert_ordered()", ASCENDING order.
`bool wakeup_time_less_than (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);`


### Algorithms

#### Definition of sleeping list enqueue function
`void enqueue_sleeping_list (int64_t wakeup_time, struct thread* thread)`
* Set wakeup_time struct thread field
* Call `list_insert_ordered ()` which inserts thread's list_elem elem in ascending wakeup_time manner
* Block current thread
> We use thread's list_elem elem variable since thread can belong to only one list at a time and creating another linked list of threads will be too costly (we don't want dynamically allocated data structure)

#### Redefinition of timer sleep function
`void timer_sleep (int64_t ticks)`
* Compute wakeup_time as `timer_ticks ()` + ticks
* Call `enqueue_sleeping_list ()`

#### Definition of thread awake function
`static void thread_awake (void)`
* Traverse `sleeping_list` starting from the beginning and check whether thread are poppable
	* For reference: We define poppable as `timer_tics () >= wakeup_time`
* If poppable, call `list_remove (thread)` and `thread_unblock (thread)`

#### Redefinition of timer interrupt function
`static void timer_interrupt (struct intr_frame *args UNUSED)`
* Call `timer_tick ()`
* Call `thread_awake ()`

#### Definition of wakeup time less than function
`bool wakeup_time_less_than (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);`
* Obtain thread struct pointer by calling `list_entry ()`
* Return thread a -> wakeup_time < thread b -> wakeup_time

### Synchronization
> To avoid race conditions, we have to disable interrupts at the beginning of `enqueue_sleeping_list ()` and `thread_awake ()`. We are iterating through a list with pointers, so we want to make sure interrupts are disabled. Thread is blocked once it's put into a sleeping_list and unblocked once it's popped out of the list.

### Rationale
> The given implementation avoids busy waiting by using the `thread_block ()` and `thread_unblock ()`. The data structure `sleeping_list` is maintained by inserting in sorted order. This ensures that we minimize the number of elements that need to be checked during `thread_awake()`. On average, the function will only need to peek at one.
>
> Call to `thread_awake ()` is added to `timer_interrupt ()` in order to make sure threads wake up exactly on the set wakeup_time.


## __Task 2: Priority Scheduler__


### Data Structures and Functions

#### Nested Priority Donation function
`void donate(curr_thread, dependent_thread);`

#### Changes to the following functions:
* In linked list struct: `void insert_sorted_order()`, to insert behind threads of the same priority, for
support of round robin.
* Modify `lib/kernel/list/list_sort(...)` so that it implements a stable sort.
* Modify `lib/kernel/list/list_insert_sorted(...)` so that it implements a stable insert in rear of like elements.
* Add call to `list_insert_ordered()` in `sema_down()` to replace `push_back()`
* `cond_wait()`
* `thread_unblock()` update to call `list_insert_ordered()`
* `thread_yield()` update to call `list_insert_ordered()`

#### Additions to `struct thread`:
* `int effective priority`
* `struct thread* child_thread`
* `struct lock* lock`,  to support donation so that we have a reference for waiting lock

#### Ready list struct addition
* Add struct lock* lock field to ready_list, for mutual exclusion during insert, sort, and pop.

#### Semaphore struct field addition
* Add struct lock* lock field to wait_list for semaphores, for mutual exclusion during insert, sort, and pop.
* In the case of the wait_list, this will remain as NULL always. Added for generality of sort/insert algorithms.

#### Waiting list changes (none)
* We avoid changes in waiting list implementation by instead changing the implementation of insert and sort function.

#### Small change in thread creation function
* Change `create_thread()` function to initialize effective_priority to priority.


### Algorithms

#### Nested Priority Donation
`void donate(struct thread* curr_thread, struct thread* blocking_thread)`
* If in MLFQS mode, return.
* Initialize variable `max_priority` to max(dependent_thread->eff_priority, curr_thread->eff_priority)
* Create a traversal pointer, initially equal to the current thread pointer
* Set `blocking_thread->eff_priority` to `max_priority`
* Loop while `blocking_thread != NULL && curr_thread->eff_priority > blocking_thread->eff_priority`
	* set `blocking_thread->eff_priority = curr_thread->eff_priority`
	* set `current_thread = blocking_thread`
	* set `blocking_thread = blocking_thread->waiting_lock->holder  // go to next link`
	* update `max_priority` as done above during initialization.
	* call `list_sort()` on waitlist
* if any priority in readylist was changed, call `list_sort()` on `ready_list`

#### Linked list sort modification
* Simply modify `lib/kernel/list/list_sort(...)` algorithm so that it is a "stable" sort. Aquires list-> lock before and releases after sort.

#### Insert algorithm modification
* Simply modify `lib/kernel/list/list_insert_sorted(...)` algorithm so that it is a "stable" insert.
* Aquires `list->lock` before and releases after insertion.

#### Choosing the next thread to run
* No changes need to be made to this function. Why?
	* We simply pop from the first thread from the ready or waiting list as code already does.
	* This is fine because we are maintaining a sorted descending ordering of the list (by effective priority), with internal sort for round robin, elsewhere in the code.

#### Acquiring a Lock
* In `sema_down()`, replace `list_push_back()` with `insert_sorted_order()`, in order to maintain sort.

#### Releasing a Lock
* In `sema_up()`, add logic to revert the current thread’s (child) effective priority to its original priority.  
* There is no need to modify the parent threads, as there are two cases:
	* The dependent thread (parent) has a higher effective priority, or
	* The dependent thread has a lower effective priority.
* In both cases, the child releasing the lock does not affect the parent’s effective priority.

#### Computing the effective priority
* Effective priority is determined when a thread calls lock_acquire()
* At this point it will propagate its current effective priority to the lock holder thread and update as needed.
* This process continues "recursively" (really, implementation is iterative to preserve size of the stack frame).
* Traversal terminates when the holder thread no longers has a dependency (child).

#### Priority scheduling for semaphores and locks
* When inserting threads into waiting lists, place the elem in descending order using list_insert_ordered().
* This list is now maintained such that it will be sorted by effective priority.
* This guarantees that when the list is popped, the highest priority thread is chosen first.
* Furthermore, by calling `list_sort()` during execution of `donate()`, we maintain ordering after dynamic priority updtes within both the thread readylist and the semaphore waiting list.

#### Priority scheduling for condition variables
* Use the same method as above.

#### Changing thread's priority
* A thread’s effective priority is only modified when donate() is called by the thread attempting to acquire a held lock.
* At this point, effective priority of all threads fighting over the lock becomes the max of their effective priorities.


### Synchronization
> Shared resources that are handled during this section include the semaphore waitlist, readylist, and sleeping_list.
> We synchronize these by adding a lock field to each of the definition of these list structs.
> We aquire the lock before modification of the list, and after completion of the modification.

### Rationale
> For implementation of the ready and waiting list queues: By maintaining ready/waiting lists in sorted order,
> we can simply keep the current method of popping the lists to determine the thread with the highest effective
> priority. Because we treat insertion as a LIFO queue, where threads of the same priority are internally sorted
> by last run time, we are able to maintain a round robin schedule by simply popping frome the sorted list, while
> avoiding saving runtimes and things of that nature. We considered several other options, including creating
> an array of size 64 of pointers to linked lists, where the i’th linked list would be a LIFO queue of threads
> of the i’th priority, in round robin order. However, this would require a minimum of 256 bytes of memory just
> for storing the pointers, which in the case of the waiting lists for semaphores, might be mostly empty (wasted
> space), and there would be one of these for each semaphore, which could take up a considerable amount of space.
> This might be unacceptable if the resource is in the kernel stack or static memory, as memory is scarce.
>
> Another considered alternative was similar to the previous, except we would have a linked list of max size 64,
> which would link to “priority nodes.” If there exists no threads of a given priority, then there would be no
> node for that priority. The existing nodes would point to their own linked lists, which would be a LIFO queue
> for round robin. This would get rid of the wasted spaceproblem, but would prove to be complex in implementation.
> Our chosen implementation would be correct, but would require additional time complexity in traversing the
> list for insertion, and requires a custom implementation of insert_in_sorted_order().
>
> For implementation of nested priority donation: We considered a recursive solution but realized that the stack
> grow unbounded in the case of a long chain of lock dependencies, and therefore considered a solution that could be
> implemented iteratively. The proposed solution of saving pointers to the dependent thread was simplistic and
> straightforward, but then required us to create a reference to the lock that a thread is waiting on within the
> threrad struct, in order to always have a reference to the next child when following the dependency chain
> (since the lock already has a reference to its owner).


## __Task 3: Multi-level Feedback Queue Scheduler (MLFQS)__


### Data Structures and Functions

#### Addition of variable to track load average
`fixed_point_t load_avg;`

#### Modification of thread struct
* Add variables in thread struct:
	* `fixed_point_t recent_cpu`, initialized to 0
	* `int nice;`

#### Recent CPU update function
`void recent_cpu_update(void);`
* Calculate new `recent_cpu` time for all threads, and updates associated thread fields.
* Calculated as `recent_cpu = (2 * load_avg)/(2 * load_avg + 1) * recent_cpu + nice`
* As spec says, may have to compute coefficient separetely to avoid overflow.

#### Recent CPU getter function
`int tread_get_recent_cpu(void);`

#### Load average update function
`void load_avg_update(void);`
* Calculate new system-wide load average.
* Calculated as `load_avg = (59/60) * load_avg + (1/60) * ready_threads`

#### Load average getter function
`fixed_point_t get_load_avg(void);`
* Simply return `100 * load_average`.

### Algorithms

#### Modification to `timer_interrupt()` for calculation of load_avg and recent_cpu
* In `timer_interrupt()`, check `if (thread_mlfqs && timer_ticks() % 100 == 0)`.
* If so, call `load_avg_update()` and `recent_cpu_update()`, which updates these values for all the threads.
* Otherwise, increment recent_cpu for the currently running thread.

#### Calculate priority
* In `timer_interrupt()`, check `if (thread_mlfqs && timer_ticks() % 4 == 0)`.
* If so, call a function to update priority.

#### Update to `thread_get_priority()`
* If thread_mlfqs is enabled, return original priority.
* Else, return effective priority

#### Implementation of `fixed_point_t get_recent_cpu()`
* Simply return `recent_cpu` member variable, which should already be calculated and accurate at calling.


### Synchronization
> All the calculations and sortings are done in timer_interrupt() where interrupt is disabled.
> Therefore, synchronization is not a problem.


### Rationale
> By running all computation in the `timer_interrupt` thread, we are able to avoid extra locks.
> This comes with the extra consequence that interrupts are globally disabled, however this is to be
> desired since we do not want any other threads to preempt this computation so that we can have accurate priorities.


## Additional Questions


### Question 1

> In the main thread, initialize a lock and a sema with value 0. Then create three threads in order A, B, C
> to run three separate functions. Create the threads such that the main thread has lower base priority
> than thread A, which has lower base priority than B, which has lower base priority than C. As soon as A
> is created, it will acquire the lock. Then it will attempt to sema down, getting blocked. At this point,
> thread B is created and attempts to sema down, getting blocked. Finally, thread C is created and tries to
> acquire the lock, but gets blocked and donates its priority to A. Now all threads are blocked, except the
> main thread. The main thread will sema up. This should unblock thread A, which will sema down and print,
> then release then lock, and C would acquire the lock, sema up, then release the lock and finish. Then B will
> sema down, print and finish. Then A will finish and finally main will finish. However with the incorrect
> implementation, where base priority is used, when the main thread performs sema up, thread B will be
> unblocked and will sema down, print and finish. Then the main thread will finish and thread A and C will
> deadlock where A is blocked on sema down and C is blocked on trying to acquire the lock that A holds.
> Order of termination should be thread C, thread B, thread A, and then the main thread. However, with an
> incorrect implementation there will be deadlock. Debugging and testing can be done by checking the order
> of the printed messages.

> __Expected Output:__
>	“A sema down\n”, “C finished\n”, “B sema down\n”, “B finished\n”, “A finished\n”, “Main finished\n”

> __Actual Output:__
> 	“B sema down\n”, “B finished\n”, “Main thread finished\n”
>
> 	__Deadlock occurs between threads A and C. A is blocked on sema down and C is blocked on trying to
> 	acquire the lock that A holds.__



### Question 2

timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | thread to run
------------|------|------|------|------|------|------|--------------
 0          |   0  |   0  |   0  |  63  |  61  |  59  | A
 4          |   4  |   0  |   0  |  62  |  61  |  59  | A
 8          |   8  |   0  |   0  |  61  |  61  |  59  | B
12          |   8  |   4  |   0  |  61  |  60  |  59  | A
16          |  12  |   4  |   0  |  60  |  60  |  59  | B
20          |  12  |   8  |   0  |  60  |  59  |  59  | A
24          |  16  |   8  |   0  |  59  |  59  |  59  | C
28          |  16  |   8  |   4  |  59  |  59  |  58  | B
32          |  16  |  12  |   4  |  59  |  58  |  58  | A
36          |  20  |  12  |   4  |  58  |  58  |  58  | C


### Question 3
> The ambiguity was choosing which thread of the same priority to run first.
> We used the round robin rule to determine the next running thread.
