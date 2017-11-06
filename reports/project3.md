Final Report for Project 3: File System
=======================================

## __Changes to Task 1: Buffer Cache__

### Data Structures and Functions

#### Introduction of `cache_initialized` in `filesys/cache.c`
```C
static bool cache_initialized = false;
```
> Used in the edge case where the system shuts down and attempts to flush an uninitialized cache.

#### Introduction of static variable counters and locks used for cache statistics in `filesys/cache.c`
```C
static long long cache_hit_count;
static long long cache_miss_count;
static long long cache_access_count;

static struct lock cache_hit_count_lock;
static struct lock cache_miss_count_lock;
static struct lock cache_access_count_lock;
```

#### Introduction of cache statistics functions in `filesys/cache.c`
```C
static void cache_increment_hit_count (void);
static void cache_increment_miss_count (void);
static void cache_increment_access_count (void);
int cache_get_stats (long long *access_count, long long *hit_count, long long *miss_count);
```

#### Introduction of `cache_invalidate ()` in `filesys/cache.c`
```C
void cache_invalidate (struct block *fs_device);
```

#### Introduction of `cache_get_block_index ()` in `filesys/cache.c`
```C
static int
cache_get_block_index (struct block *fs_device, block_sector_t sector_index, bool is_whole_block_write);
```

### Algorithms

#### Additions to `cache_init ()` in `filesys/cache.c`
```C
void cache_init (void);
```
* Initializes `cache_hit_count_lock`, `cache_miss_count_lock`, and `cache_access_count_lock`.
* Sets `cache_hit_count`, `cache_miss_count`, and `cache_access_count` to 0.
* Sets `cache_initialized` to true before function returns.

#### Introduction of function `cache_flush_block_index ()` in `filesys/cache.c`
```C
static void cache_flush_block_index (struct block *fs_device, int index);
```
* Writes the data at the block at `index` back to disk at `disk_sector_index` and sets `dirty` to false.

#### Introduction of function `cache_increment_hit_count ()` in `filesys/cache.c`
```C
static void cache_increment_hit_count (void);
```
* Acquires `cache_hit_count_lock`, increments `cache_hit_count`, and releases `cache_hit_count_lock`.

#### Introduction of function `cache_increment_miss_count ()` in `filesys/cache.c`
```C
static void cache_increment_miss_count (void);
```
* Acquires `cache_miss_count_lock`, increments `cache_miss_count`, and releases `cache_miss_count_lock`.

#### Introduction of function `cache_increment_access_count ()` in `filesys/cache.c`
```C
static void cache_increment_access_count (void);
```
* Acquires `cache_access_count_lock`, increments `cache_access_count`, and releases `cache_access_count_lock`.

#### Introduction of function `cache_get_block_index ()` in `filesys/cache.c`
```C
static int
cache_get_block_index (struct block *fs_device, block_sector_t sector_index, bool is_whole_block_write);
```
* Iterates through cache to check if there is a `valid` block corresponding to `sector_index` indicating a cache hit.
* If there is no such block, then calls `cache_evict ()` and check whether it returned a positive index or a negative index indicating a cache hit.
* If `cache_evict ()` returned a positive index then it calls `cache_replace ()` to read in new disk block into cache. If `cache_evict ()` returned a negative index then `-(index + 1)` is returned.
* Calls `cache_increment_access_count ()` and either `cache_increment_hit_count ()` or `cache_increment_miss_count ()` depending on whether there is a cache hit or cache miss.

#### Additions to `cache_replace ()` in `filesys/cache.c`
```C
static void
cache_replace (struct block *fs_device, int index, block_sector_t sector_index, bool is_whole_block_write);
```
* Added optimization so that it does not read from disk if a whole block write will happen.

#### Changes to `cache_read ()` in `filesys/cache.c`
```C
void
cache_read (struct block *fs_device, block_sector_t sector_index, void *destination, off_t offset, int chunk_size);
```

#### Before
* Iterates through cache to check if there is a cache hit.
* If there it was a cache miss, then calls `cache_evict ()`.
* If `cache_evict ()` returned positive index then calls `cache_replace ()` to read in new disk block into cache.
* If `cache_evict ()` returned negative index indicating a cache hit then uses `-(index + 1)`.

#### After
* Calls `cache_get_block_index ()` to get the index of the cache block.

#### Changes to `cache_write ()` in `filesys/cache.c`
```C
void
cache_write (struct block *fs_device, block_sector_t sector_index, void *source, off_t offset, int chunk_size);
```

#### Before
* Iterates through cache to check if there is a cache hit.
* If there it was a cache miss, then calls `cache_evict ()`.
* If `cache_evict ()` returned positive index then calls `cache_replace ()` to read in new disk block into cache.
* If `cache_evict ()` returned negative index indicating a cache hit then uses `-(index + 1)`.

#### After
* Calls `cache_get_block_index ()` to get the index of the cache block.

#### Additions to support `invcache ()` syscall

##### In `lib/user/syscall.c`
```C
void invcache (void);
```
* Userprog syscall to flush dirty cache blocks to disk and invalidate the entire cache.

##### In `lib/syscall-nr.h`, addition of macro `SYS_INVCACHE` to `enum`

##### In `userprog/syscall.c`
```C
static void syscall_invcache (uint32_t *args UNUSED, uint32_t *eax UNUSED);
```
* Calls `cache_invalidate (fs_device)`.

##### In `filesys/cache.c`
```C
void cache_invalidate (struct block *fs_device);
```
* Iterates through each cache block, flushes blocks that are both valid and dirty to disk, and invalidates each block by setting `valid` to false.
* Acquires and releases `cache_update_lock` and each cache blocks' corresponding `cache_block_lock` before using and modifying each cache block and its metadata.

#### Additions to support `cachestat ()` syscall

##### In `lib/user/syscall.c`
```C
int cachestat (const long long *access_count, const long long *hit_count, const long long *miss_count);
```
* Userprog getter syscall to get `cache_access_count`, `cache_hit_count`, and `cache_miss_count`.

##### In `lib/syscall-nr.h`, addition of macro `SYS_CACHESTAT` to `enum`

##### In `userprog/syscall.c`
```C
static void syscall_cachestat (uint32_t *args UNUSED, uint32_t *eax UNUSED);
```
* Checks that args are valid and then calls `cache_get_stats (…)`.
* Sets `*eax` to -1 if args are invalid, otherwise to return value from function call.

##### In `filesys/cache.c`
```C
int cache_get_stats (long long *access_count, long long *hit_count, long long *miss_count);
```
* Sets `*access_count` to `cache_access_count`, and `*hit_count` to `cache_hit_count`, and `*miss_count` to `cache_miss_count`.
* Returns 0 on success and -1 on failure.

### Synchronization
> Added locks to synchronize cache access, cache hit, and cache miss counters to prevent race conditions.

## __Changes to Task 2: Extensible Files__

### Data Structures and Functions

#### Changes to `struct inode` from `inode.c`
> `struct inode_disk data` is removed from the `struct inode` since it is no longer needed and `struct inode_disk` can be obtained by calling the function `get_inode_disk ()`. `struct lock inode_lock` is added to ensure the synchronization for reading and writing to the same file.

##### Before
```C
struct inode
  {
     struct list_elem elem;
     block_sector_t sector;
     int open_cnt;
     bool removed;
     int deny_write_cnt;
     struct inode_disk data;
   };
```

##### After
```C
struct inode
  {
    struct list_elem elem;
    block_sector_t sector;
    int open_cnt;
    struct lock inode_lock;
    bool removed;
    int deny_write_cnt;
  };
```

#### Introduction of helper functions for `inode_allocate ()` and `inode_deallocate ()`

##### Helper functions for `inode_allocate ()`
> For readability and simplicity, we separated implementations for allocating each block, indirect block, and doubly indirect block. `inode_allocate_sector ()` allocates the sector, `inode_allocate_indirect ()` allocates the indirect block and also allocates amount of `size_t cnt` sectors by calling `inode_allocate_sector ()`, and `inode_allocate_doubly_indirect ()` allocates the doubly indirect block and also calls `inode_allocate_indirect ()` to allocate amount of `size_t cnt` indirect blocks and so on.

```C
static bool inode_allocate (struct inode_disk *disk_inode, off_t length);
static bool inode_allocate_sector (block_sector_t *sector_num);
static bool inode_allocate_indirect (block_sector_t *sector_num, size_t cnt);
static bool inode_allocate_doubly_indirect (block_sector_t *sector_num, size_t cnt);
```

##### Helper functions for `inode_deallocate ()`
> Similar to allocate functions above.

```C
static void inode_deallocate (struct inode *inode);
static void inode_deallocate_indirect (block_sector_t sector_num, size_t cnt);
static void inode_deallocate_doubly_indirect (block_sector_t sector_num, size_t cnt);
```

### Algorithms

#### Introduction of function `get_inode_disk ()` in `inode.c`
```C
struct inode_disk *get_inode_disk (const struct inode *inode);
```
* Reads in `inode_disk` by accessing `inode->sector` from the disk and return
> Since we have removed `struct inode_disk data` from the `struct inode`, we need a function to obtain the `inode_disk`.

#### Introduction of function `inode_is_removed ()`
```C
bool inode_is_removed (const struct inode *inode);
```
* Returns `inode->removed`

#### Introduction of `inode_acquire_lock ()` and `inode_release_lock ()`
```C
void inode_acquire_lock (struct inode *inode);
void inode_release_lock (struct inode *inode);
```
* Acquires and releases inode lock
* Since we don’t have a global file lock anymore, we need to handle synchronization for read and writing to the same file, and we handled it by adding a lock to inode. This allows serialized read and write to the same file.

#### Changes to `inode_read_at ()`
```C
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset);
```

##### Before
* Malloc `struct inode_disk_t *metadata`
* Call `cache_read ()`, passing in `metadata->data`
* Check if `offset > metadata->data.start + metadata->data.length`
* If so, return 0 (reading past EOF should return 0 bytes)
* free metadata

##### After
* We call `cache_read ()` by passing in `buffer` directly instead of getting `inode_disk`
> Checking for reading past EOF is already implemented and won’t read past EOF.

#### Changes to `inode_write_at ()`
```C
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset);
```

##### Before
* Support file extension
* If `byte_to_sector ()` returns -1, it means you are writing past `EOF`.
  * If that’s the case, call `inode_allocate (offset + size)` to allocate additional block sectors needed to accommodate space for writing to the file.
  * Malloc `struct inode_disk_t *metadata`
  * Call cache_read(), passing in metadata->data
  * Compute zero_pad_count =  offset - (metadata->data.start + metadata->data.length)
  * If zero_pad_count > 0, calloc buffer of 0’s called `zero_padding` of size `zero_padding_size`
  * Call cache_write, passing in zero_padding as source, and offset = metadata->data.start, size = metadata->data.length

##### After
* Support file extension
* If `byte_to_sector ()` returns -1, it means you are writing past `EOF`.
  * If that’s the case, call `inode_allocate (offset + size)` to allocate additional block sectors needed to accommodate space for writing to the file.
  * Call `get_inode_disk ()` to obtain `inode_disk`
  * Call `cache_write ()` to update the disk

> Zero padding implementation was not needed since it was already handled.

#### Additions to support `diskstat ()` syscall

##### In `lib/user/syscall.c`
```C
int diskstat (const long long *read_count, const long long *write_count);
```
* Userprog getter syscall to get `fs_device->read_cnt` and `fs_device->write_cnt`.

##### In `lib/syscall-nr.h`, addition of macro `SYS_DISKSTAT` to `enum`

##### In `userprog/syscall.c`
```C
static void syscall_diskstat (uint32_t *args UNUSED, uint32_t *eax UNUSED);
```
* Checks that args are valid and then calls `block_get_stats (fs_device, …)`.
* Sets `*eax` to -1 if args are invalid, otherwise to return value from function call.

##### In `devices/block.c`
```C
int block_get_stats (struct block *block, long long *read_count, long long *write_count);
```
* Sets `*read_count` to `block->read_cnt` and `*write_count` to `block->write_cnt`.
* Returns 0 on success and -1 on failure.


### Synchronization
> We have removed the global file lock from Project 2 and now allows serialized read and write to the same file by adding a inode lock. Removing the global file lock ensures that 2 operations acting on different disk sectors, different files, and different directories can run simultaneously.

## __Changes to Task 3: Subdirectories__

### Data Structures and Functions

#### Changes to `struct inode_disk`
* `DIRECT_BLOCK_COUNT` changed from 124 to 123 in order to add the boolean variable `is_dir`.

##### Before
```C
struct inode_disk {
  block_sector_t direct_blocks[DIRECT_BLOCK_COUNT];
  block_sector_t indirect_block;
  block_sector_t doubly_indirect_block;

  off_t length;
  unsigned magic;
};
```

##### After
```C
struct inode_disk {
  block_sector_t direct_blocks[DIRECT_BLOCK_COUNT];
  block_sector_t indirect_block;
  block_sector_t doubly_indirect_block;

  bool is_dir;
  off_t length;
  unsigned magic;
};
```

#### Removal of `inode_inumber ()` from design since it's unnecessary
* Another function `inode_get_inumber ()` exists that serves the same purpose.

#### Addition of functions to acquire and release a dir lock
```C
void dir_acquire_lock (struct dir *dir);
void dir_release_lock (struct dir *dir);
```

* Added `dir_acquire_lock ()` and `dir_release_lock ()` to handle synchronized operations with dir

### Algorithms

#### Function added to tokenize a string
```C
static int get_next_part (char part[NAME_MAX + 1], const char **srcp);
```
* Extracts a file name part from SRCP into PART, and updates SRCP so that the next call will return
the next file name part. Returns 1 if successful, 0 at end of string, -1 for a too-long file name part.

#### Function added to split a path into its directory and filename
```C
bool split_directory_and_filename (const char *path, char *directory, char *filename);
```

* Given a full PATH, extract the DIRECTORY and FILENAME into the provided pointers.

### Synchronization
> No changes in synchronization designs.

## __Reflection__
* Gera implemented task 1 and helped with code style and formatting.
* John implemented task 2 and helped implement and debug task 3.
* William implemented task 2 and student testing.
* Joe implemented task 3.
* Each member who implemented his parts was responsible for corresponding design doc, debugging, and final report.
* This project went very well with excellent team effort. The cache was implemented as soon as the design doc was completed in order for later tasks that depend on it to be able to use it as soon as possible. There was a collaborative effort in designing the doc and tests and writing the reports. As far as improvements that could be made, none because this was one of the best team efforts.

## __Student Testing Report__

### Test 1: Benchmark Cache

#### Description
> The test "Benchmark Cache", located in `bm-cache.c`, tests the effectiveness of the cache by doing the following: Writes a file to disk that takes up half of the cache size (this is because room must be left for the inode_disk structs which must be read from disk in addition to the file itself). Then closes and reopens the file, and invalidates the cache. Then reads the entire file (cold cache), keeping track of cache access statistics. Then closes and reopens the file, and re-reads the entire file. It then compares the access statistics from the second read to the first read, and checks for an increase in the cache hit rate. This test is meant to ensure that the cache yields a large hit rate given reasonable conditions.

#### Overview
1. Create a file called `cache_test` of length 0 using `create()` and check for success.
> Expected output: create "cache_test"

2. Open `cache_test` using `open()` and check for success.
> Expected output: open "cache_test"

3. Populate a buffer of size `BLOCK_SECTOR_SIZE * CACHE_NUM_ENTRIES / 2` with random bytes using `random_bytes()`

4. Write the buffer contents to `cache_test` using `write`, and ensure `write` writes the entire buffer (`write` returns 16384 bytes).
> Expected output: write 16384 bytes to "cache_test"

5. Close and reopen `cache_test` by calling `close()` and `open()`. Test for successful open.
> Expected output: close "cache_test"
> Expected output: open "cache_test"

6. Invalidate the cache by calling `invcache()`, a syscall implemented for cache testing purposes. Cache is now cold.
> Expected output: invcache

7. Get and save baseline cache statistics by calling `cachestat()`, a syscall implemented for querying cache utilization information (cache hits, cache misses, cache accessses).
> Expected output: baseline cache statistics

8. Read `cache_test` in entirety using `read()` and ensure `BLOCK_SECTOR_SIZE * CACHE_NUM_ENTRIES / 2` bytes are read.
> Expected output: read 16384 bytes from "cache_test"

9. Get and save new cache statistics by calling `cachestat()`.
> Expected output: cachestat

10. Call `get_hit_rate()` (helper function to calculate hit rate by dividing hit count by access count) on the difference between new cache stats and baseline cache stats (this gives the number of new hits, misses, and accesses since the cache was invalidated). Save return value as `old_hit_rate`.

11. Rebaseline cache statistics by setting baseline cache stats to new cache stats.

12. Close and reopen `cache_test` by calling `close()` and `open()`.
> Expected output: close "cache_test"
> Expected ouptut: open "cache_test"

13. Read `cache_test` in entirety using `read()` and ensure `BLOCK_SECTOR_SIZE * CACHE_NUM_ENTRIES / 2` bytes are read.
> Expected output: read 16384 bytes from "cache_test"

14. Get and save new cache statistics by calling `cachestat()`.
> Expected output: cachestat

15. Call `get_hit_rate()` on the difference between new cache stats and baseline cache stats. Save return value as `new_hit_rate`.

16. Convert old and new hit rates to percentages for display (cannot format specify Pintos `fixed_point_t`)

17. Compare `old_hit_rate` to `new_hit_rate`, and ensure that `new_hit_rate` is higher. (`new_hit_rate` should be almost 100% by design for an efficient cache due to a file size that would not require evictions, but the call to `CHECK ()` only enforces that the hit rate has improved).
> Expected output: old hit rate percent: 65, new hit rate percent: 98

18. Close `cache_test`
> Expected output: close "cache_test"

#### Output
> Copying tests/filesys/extended/bm-cache to scratch partition...

> Copying tests/filesys/extended/tar to scratch partition...

> qemu -hda /tmp/3uVs28dg20.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null

> PiLo hda1

> Loading...........

> Kernel command line: -q -f extract run bm-cache

> Pintos booting with 4,088 kB RAM...

> 382 pages available in kernel pool.

> 382 pages available in user pool.

> Calibrating timer...  419,020,800 loops/s.

> hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"

> hda1: 187 sectors (93 kB), Pintos OS kernel (20)

> hda2: 246 sectors (123 kB), Pintos scratch (22)

> hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"

> hdb1: 4,096 sectors (2 MB), Pintos file system (21)

> filesys: using hdb1

> scratch: using hda2

> Formatting file system...done.

> In bitmap_read()

> Boot complete.

> Extracting ustar archive from scratch device into file system...

> Putting 'bm-cache' into the file system...

> Putting 'tar' into the file system...

> Erasing ustar archive...

> Executing 'bm-cache':

> (bm-cache) begin

> (bm-cache) create "cache_test"

> (bm-cache) open "cache_test"

> (bm-cache) write 16384 bytes to "cache_test"

> (bm-cache) close "cache_test"

> (bm-cache) open "cache_test"

> (bm-cache) invcache

> (bm-cache) baseline cache statistics

> (bm-cache) read 16384 bytes from "cache_test"

> (bm-cache) cachestat

> (bm-cache) close "cache_test"

> (bm-cache) open "cache_test"

> (bm-cache) read 16384 bytes from "cache_test"

> (bm-cache) cachestat

> (bm-cache) old hit rate percent: 65, new hit rate percent: 98

> (bm-cache) close "cache_test"

> (bm-cache) end

> bm-cache: exit(0)

> Execution of 'bm-cache' complete.

> Timer: 76 ticks

> Thread: 0 idle ticks, 69 kernel ticks, 7 user ticks

> hdb1 (filesys): 73 reads, 530 writes

> hda2 (scratch): 245 reads, 2 writes

> Console: 1509 characters output

> Keyboard: 0 keys pressed

> Exception: 0 page faults

> Powering off...

#### Result
> PASS

#### Non-Trivial Potential Kernel Bugs
1. If `write()` or `read()` returned some other value instead of `BLOCK_SECTOR_SIZE * CACHE_NUM_ENTRIES / 2`, then the test case would output `FAIL` instead. (This could be caused by, perhaps, file extension not working correctly, since the file is initialized to size 0).

2. If the cache clock replacement algorithm did not first evict invalid entries before evicting valid, but non-recently used entries, then the performance in the second read would mirror the performance of the first read (after cache was invalidated). This would cause the two cache hit rates to be equal, thereby causing the `CHECK` at step 17 to fail, hence causing the test to fail.

### Test 2: Optimized Writes

#### Description
> The test "Optimized Writes", contained in `opt-writes.c`, checks for block write optimization, wherein a write syscall is made of size `BLOCK_SECTOR_SIZE`. In this case, the block should not first be read from the block device in order to edit and write back, but instead should be completely overwritten without a read. The test creates a buffer of size `200 * BLOCK_SECTOR_SIZE`, and writes it to disk. It then invalidates all entries of the cache, and saves the current read and write  counts, then rewrites the file. It then checks how many additional  reads and writes are incurred. The additional write count should be high (comparable to the number of blocks, since the cache is cold), while the additional read count should be very low (only occuring from the initial read of the inode_disk, reading in the indirect_inodes once the file becomes long enough, and accounting for inode eviction). The additional persistence test is not needed and is provided for consistency with the testing framework. It simply calls `PASS`.

#### Overview
1. Create a file called `opt_write_test` of length 0 using `create()` and check for success.
> Expected output: create "opt_write_test"

2. Open `op_test_write` using `open()` and check for success.
> Expected output: open "opt_write_test"

3. Populate a buffer of size `200 * BLOCK_SECTOR_SIZE` with random bytes using `random_bytes()`

4. Write the buffer contents to `opt_write_test` using `write`, and ensure `write` writes the entire buffer (`write` returns 102400 bytes).
> Expected output: write 102400 bytes to "opt_write_test"

5. Invalidate the cache by calling `invcache`, a new syscall implemented for testing purposes. Ensure syscall returns successfully.
> Expected output: invcache

6. Get baseline disk statistics by calling `diskstat()`, a new syscall implemented for testing disk utilization. Ensure syscall returns successfully.
> Expected output: baseline disk statistics

7. Repopulate buffer with `200 * BLOCK_SECTOR_SIZE` new random bytes and write to `opt_write_test`.
> Expected output: write 102400 bytes to "opt_write_test"

8. Get new disk statistics by calling `diskstat()` and ensure syscall returns successfully.
> Expected output: get new disk statistics

9. Compare baseline read count to new read count by calling `CHECK ()`. Ensure no more than 10 new reads have been incurred (`num_disk_reads - 10 <= base_disk_reads`).
> Expected output: old reads: 38, old writes: 887, new reads: 44, new writes: 1233

10. Close `opt_write_test` by calling `close()` and ensure success.
> Expected output: close "opt_write_test"

#### Output

> Copying tests/filesys/extended/opt-writes to scratch partition...

> Copying tests/filesys/extended/tar to scratch partition...

> qemu -hda /tmp/aDPy8XEE5t.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null

> PiLo hda1

> Loading...........

> Kernel command line: -q -f extract run opt-writes

> Pintos booting with 4,088 kB RAM...

> 382 pages available in kernel pool.

> 382 pages available in user pool.

> Calibrating timer...  419,020,800 loops/s.

> hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"

> hda1: 187 sectors (93 kB), Pintos OS kernel (20)

> hda2: 239 sectors (119 kB), Pintos scratch (22)

> hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"

> hdb1: 4,096 sectors (2 MB), Pintos file system (21)

> filesys: using hdb1

> scratch: using hda2

> Formatting file system...done.

> In bitmap_read()

> Boot complete.

> Extracting ustar archive from scratch device into file system...

> Putting 'opt-writes' into the file system...

> Putting 'tar' into the file system...

> Erasing ustar archive...

> Executing 'opt-writes':

> (opt-writes) begin

> (opt-writes) create "opt_write_test"

> (opt-writes) open "opt_write_test"

> (opt-writes) write 102400 bytes to "opt_write_test"

> (opt-writes) invcache

> (opt-writes) baseline disk statistics

> (opt-writes) write 102400 bytes to "opt_write_test"

> (opt-writes) get new disk statistics

> (opt-writes) old reads: 38, old writes: 887, new reads: 44, new writes: 1233

> (opt-writes) close "opt_write_test"

> (opt-writes) end

> opt-writes: exit(0)

> Execution of 'opt-writes' complete.

> Timer: 96 ticks

> Thread: 0 idle ticks, 69 kernel ticks, 27 user ticks

> hdb1 (filesys): 44 reads, 1295 writes

> hda2 (scratch): 238 reads, 2 writes

> Console: 1405 characters output

> Keyboard: 0 keys pressed

> Exception: 0 page faults

> Powering off...

#### Result
> PASS

#### Non-Trivial Potential Kernel Bugs
1. If `write()` or `read()` returned some other value instead of `BLOCK_SECTOR_SIZE * 200`, then the test case would output `FAIL` instead. (This could be caused by, perhaps, file extension not working correctly, since the file is initialized to size 0).

2. If the syscall to `wtite()` was implemented in such a way that `BLOCK_SECTOR_SIZE` writes were not optimized, then, in the final call to `write()`, since the cache was invalidated between calls, the filesystem would need to read in every block one by one, making the additional read count increase by at least 200, thereby causing the `CHECK ()` at step 9 to fail, and hence causing the test to fail.

### Conclusion

#### Tell us about your experience writing tests for Pintos.
> In order to implement some of the testing functionality required of benchmarking the cache and the disk, we had to implement some additional syscalls.
> Getting the perl files to work correctly proved to be a bit tricky, as we were getting bugs initially due to our persistence .ck files having a call to `PASS` with no other code other than importing `TESTS`. We fixed it by removing the import statement.

#### What can be improved about the Pintos testing system? (There's a lot of room for improvement.)
> Perhaps a short guide could be provided for navigating through the perl scripts so that we understand how the tests work more. This would help during debugging immensely.

#### What did you learn from writing test cases?
> We wrote test cases assuming that certain lines would be candidates for failure, while others were tested and would almost definitely work. We found through running our own test cases that sometimes the failures come from unexpected places, for example pieces of code that we already assumed were solid. This taught a valuable lesson in software engineering in general. You should not test assuming that if code passes the test cases, it is bug-free. Just because it passes does not mean there are not undiscovered bugs. Therefore the value of covering all reasonable, and perhaps even some unreasonable, possibilities cannot be understated.
