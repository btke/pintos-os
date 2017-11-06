Final Report for Project 1: Threads
===================================

## Changes to Task 1: Efficient Alarm Clock

Rather than introducing a new lock for synchronization, we used thread_block()/thread_unblock().
This simplified the enqueue_sleeping_list function.

We also did not add a new list_elem in thread and used the original elem for usage in sleeping_list.

Introduced a comparator function for removing threads from sleeping_list.

## Changes to Task 2: Priority Scheduler

A complete redesign for task 2:

#### Data Structures and Functions
New members in struct thread:
    busy - Lock that this thread is waiting on
    locks - List of locks held by this thread
    initial_priority - Base priority

New members in struct lock:
    elem - Used for locks lists

Existing function changes in thread.c:
init_thread() - Initialize new variables for struct thread
next_thread_to_run() - Use list_max to return the highest priority thread
                        from ready_list
thread_set_priority() - Yield thread after setting priority
thread_create() - Yield thread after thread_unblock() call

Function changes in synch.c:
sema_up() - Use list_max to get the highest priority thread from waiters list
lock_acquire() - Handle donations, push lock to locks list, set busy lock
lock_release() - Remove lock from thread's locks list, reset priority to initial
                priority and update with donation if necessary
cond_signal() - Use list_max to get the semaphore that contains the highest
                priority waiter


#### Algorithms
bool priority_less (struct list_elem *a, struct list_elem *b, void *);
    - Comparator for a list with thread elem(s)
    - Returns true if priority of A < priority of B

bool lock_less (struct list_elem *, struct list_elem *, void *);
    - Comparator for a list with lock elem(s)
    - Returns true if sema of lock A contains higher priority waiters than sema
        of lock B
    - Returns false if sema of lock B has no waiters
    - Returns true if sema of lock A has no waiters and sema of lock B has waiters

bool cond_less (struct list_elem *, struct list_elem *, void *);
    - Comparator for a list with cond elem(s)
    - Returns true if sema of cond A contains higher priority waiters than sema
        of cond B
    - Returns false if sema of cond B has no waiters
    - Returns true if sema of cond A has no waiters and sema of cond B has waiters

###### Choosing the next thread to run
Use list_max() with the appropriate comparators

###### Acquiring a Lock
Prior to sema_down() -
    if not mlfqs:
        donation = current_thread.priority
        next = lock
        while next is not NULL:
            if next.holder.priority < donation:
                next.holder.priority = donation
                next = next.holder.busy
            else
                break

After sema_down() -
    lock.holder = current_thread
    if not mlfqs:
        Append lock to current_thread.locks
        current_thread.busy = NULL

###### Releasing a Lock
Prior to sema_up() -
    lock.holder = NULL
    if not mlfqs:
        Remove lock from current_thread.locks
        current_thread.priority = current_thread.initial_priority
        if current_thread.locks is not empty:
            check = Highest priority of all waiters in the semaphores of locks
            current_thread.priority = max(check, current_thread.priority)

###### Computing the effective priority
When mlfqs is off, change thread priority solely through donations. Keep the
    initial priority for donation resets.

###### Priority scheduling for semaphores and locks
Use list_max() with appropriate comparators

###### Priority scheduling for condition variables
Use list_max() with appropriate comparators

###### Changing thread's priority
A thread's priority is only changed in lock_acquire and lock_release; when
    donation happens and when priority is reset.

#### Synchronization
Use interrupt disables at certain memory accesses (list manipulations, etc) to
    prevent race conditions

#### Rationale
In nested donations, donations occur only when a thread tries to acquire a lock
    that is currently held by another thread. Thus, in lock_acquire, the current
    thread donates to the next thread (lock holder). If the next thread is also
    waiting on a second lock, the donation propagates to the second lock holder.
    This recursively occurs until a thread with no busy lock is reached.
    - thread->busy allows this recursive donation

When a lock is released, the thread priority is reset (first to initial priority).
    We then determine the highest priority of possible waiters by iterating through
    each lock held by the current thread, and checking the priorities of each waiter
    within the semaphores of each lock. This allows us to retain any high priorities
    donated from waiting threads.
    - thread->initial_priority stores the thread's original priority
    - thread->locks holds the locks held by the current thread

## Changes to Task 3: Multi-level Feedback Queue Scheduler (MLFQS)

Update thread MFLQS priority void thread_update_mlfqs_priority(void); Update the priority of every thread priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)

Increment recent_cpu void increment_recent_cpu(void); Increment recent_cpu by 1 for the running thread, unless the idle thread is running.

Add to thread_init() Initialized initial_thread’s recent_cpu to 0 and nice to 0, and load_avg to 0 in thread_init(void). Inherit recent_cpu and nice in thread_create().

## Reflection

John worked on Task 1, Joe and William worked on Task 2, and Gera worked on Task 3. We chose this
split because priority scheduler was a larger portion of the project in terms of time spent.

Overall, the project went well and we completed everything on time. Preparing the same way for the
next project should be effective.
