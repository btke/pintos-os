Design Document for Project 3: File System
==========================================

## Group Members

* William Smith <williampsmith@berkeley.edu>
* John Seong <jwseong@berkeley.edu>
* Gera Groshev <groshevg@berkeley.edu>
* Joe Kuang <joekuang@berkeley.edu>

## __Task 1: Buffer Cache__

### Data Structures and Functions

Create `cache.c` and `cache.h` in `pintos/src/filesys`

`#define CACHE_NUM_ENTRIES 64` in `cache.c`

`#define CACHE_NUM_CHANCES 1` in `cache.c`

#### Definition of `struct cache_block` in `cache.c`
```C
struct cache_block {
  struct lock cache_block_lock;
  uint8_t data[BLOCK_SECTOR_SIZE];
  block_sector_t disk_sector_index;
  bool valid;  // false if cache_block contains garbage
  bool dirty;
  size_t chances_remaining;
};
```

#### Changes to `struct inode` in `inode.c`
```C
struct inode {
  struct list_elem elem;
  block_sector_t sector;
  int open_cnt;
  bool removed;
  int deny_write_cnt;
};
```

#### Define array of cache blocks in `cache.c`
`static struct cache_block cache[CACHE_NUM_ENTRIES];`

#### Define lock used for eviction in `cache.c`
`static struct lock cache_update_lock;`

### Algorithms

#### Changes to `filesys_init ()` in `filesys.c`
`void filesys_init(bool format)`
* Call `cache_init()`

#### Implementation of new function `cache_init ()` in `cache.c`
`void cache_init(void)`
* call `lock_init(&cache_update_lock)`
* for each `struct cache_block` in `cache`:
	* set `valid` member to false
	* call `lock_init()` on `cache_block_lock` member

#### Implementation of new function `cache_evict ()` in `cache.c`
`int cache_evict(block_sector_t sector_index)`
* declare and initialize `static size_t clock_position = 0`
* acquire `cache_update_lock`
* before checking values of any `cache_block`, acquire the lock for the `cache_block` first
* iterate through `cache` to make sure `sector_index` is not in `cache`
* if `sector_index` in cache then release `cache_update_lock` and return `(-index) - 1` for `cache` entry index
* Perform clock algorithm, loop forever:
	* first check the `valid` member and advance clock and return `cache` entry index if `valid`
	* next check `chances_remaining` == 0 and break
	* release `cache_block` lock and advance `clock_position`
* release `cache_update_lock`
* if `dirty` call `block_write()`, set `valid` to false, and return `cache` entry index

#### Implementation of new function `cache_read ()` in `cache.c`
`void cache_read(struct block *fs_device, block_sector_t sector_index, void *destination, off_t offset, int chunk_size)`
* before checking values of any `cache_block`, acquire the lock for the `cache_block` first
* iterate through `cache` to check if `sector_index` is in `cache`, if so:
	* save cache index of hit → i
	* reset `chances_remaining` to CACHE_NUM_CHANCES
	* `memcpy()` `chunk_size` bytes to `destination` from `cache[i].data` starting at `offset`
	* release `cache_block_lock`
* if `sector_index` is not in `cache`, call `cache_evict()`
  * if `cache_evict()` returns positive index i: `cache_replace()` the `cache` entry at the index
  * else `cache_evict()` returns negative index, it was a hit, so convert index to `i = -(index + 1)`
  * reset `chances_remaining` to CACHE_NUM_CHANCES
  * `memcpy()` `chunk_size` bytes to `destination` from `cache[i].data` starting at `offset`
  * release `cache_block_lock`

#### Implementation of new function `cache_write ()` in `cache.c`
`void cache_write(struct block *fs_device, block_sector_t sector_index, void *source, off_t offset, int chunk_size)`
* before checking values of any `cache_block`, acquire the lock for the `cache_block` first
* iterate through `cache` to check if `sector_index` is in `cache`, if so:
	* save cache index of hit → i
	* set `dirty` to true
	* reset `chances_remaining` to CACHE_NUM_CHANCES
	* `memcpy()` `chunk_size` bytes to `cache[i].data` starting at `offset` from `source`
	* release `cache_block_lock`
* if `sector_index` is not in `cache`, call `cache_evict()`
  * if `cache_evict()` returns positive index i: `cache_replace()` the `cache` entry at index i
  * else `cache_evict()` returns negative index, it was a hit, so convert index to `i = -(index + 1)`
  * set `dirty` to true
  * reset `chances_remaining` to CACHE_NUM_CHANCES
  * `memcpy()` `chunk_size` bytes to `cache[i].data` starting at `offset` from `source`
  * release `cache_block_lock`

#### Implementation of new function `cache_replace ()` in `cache.c`
`void cache_replace(int index, sector_index)`
* ASSERT thread is holding lock
* set valid to true
* set dirty to false
* set `disk_sector_index` to `sector_index`
* set `chances_remaining` to `CACHE_NUM_CHANCES`
* call `block_read (fs_device, sector_index, &(cache[index].data))`

#### Support for `cache_read ()` and `cache_write ()`
* `block_read()` and `block_write()` will be replaced with `cache_read()` and `cache_write()` in all following functions so that all reads and writes go through cache.
	* `inode_create ()`
	* `inode_read_at ()`
	* `inode_write_at ()`
	* `partition_read()` in `partition.c`
	* `read_partition_table()` in `partition.c`
	* `fsutil_extract` in `fsutil.c`
* The `bounce` buffer will be removed in `inode_read_at ()` and `inode_write_at ()`. Instead of the if-else block, `cache_read(fs_device, sector_idx, buffer + bytes_read, sector_ofs, chunk_size)` will be called in `inode_read_at ()` and
`cache_write(fs_device, sector_idx, buffer + bytes_written, sector_ofs, chunk_size)` will be called in `inode_write_at ()`.

#### Changes to `struct inode`
* Remove `struct inode_disk data`
> The rationale here is that we do not want to incur the cache penalty of storing this metadata on memory, as this is both inefficient and would limit the number of files that can be opened simultaneously to 64. 
> Instead, we can get the data by reading the block sector of member variable `start` in the inode struct, which will return the inode_disk corresponding to that inode. We can then get the data from here directly.

#### Below are four functions that will need to be changed.

#### Changes to `byte_to_sector ()`
`static block_sector_t byte_to_sector (const struct inode *inode, off_t pos)`
* declare and malloc `struct inode_disk *data`
* read from cache into `*data`
* replace `inode->data` with `*data` and save values in `*data` to temporary variable before returning
* free `*data` before returning

#### Changes to `inode_open ()`
`struct inode *inode_open (block_sector_t sector)`
* remove `block_read (fs_device, inode->sector, &inode->data)`

#### Changes to `inode_close ()`
`void inode_close (struct inode *inode)`
* declare and malloc `struct inode_disk *data`
* read from cache into `*data`
* replace `inode->data` with `*data` and save values in `*data` to temporary variable before returning
* free `*data` before returning

#### Changes to `inode_length ()`
`off_t inode_length (const struct inode *inode)`
* declare and malloc `struct inode_disk *data`
* read from cache into `*data`
* replace `inode->data` with `*data` and `data->length` to temporary variable before returning
* free `*data` before returning

### Synchronization
> Synchronization for task 1 requires the acquisition of locks for each cache_block before checking or modifying its data. We created a `cache_block_lock` for that. In addition, the `cache_evict()` will acquire the `cache_update_lock` before modifying the cache.

### Rationale
> By having a lock for each cache block, we can synchronize access between reading and writing. The `cache_update_lock` and doing another pass to make sure there really is a miss in `cache_evict()` accounts for the edge case where if two threads end up with a cache miss for the same disk sector, they shouldn’t both be evicting from the cache, otherwise there would be duplicates. `cache_evict()` will be the only function that updates the cache and the only place where `cache_update_lock` is acquired. This allows simultaneous access to read and write to the cache while an entry is being evicted and individual `cache_block_lock` will keep the individual reads and writes synchronized. Now that data must go through the cache, `block_read()` and `block_write()` are not called directly, but instead the cache is checked first and the cache takes care of reading to and writing from the disk device.


## __Task 2: Extensible Files__

> Adding syscall for `inumber` is moved to task 3 documentation 

### Data Structures and Functions

`#define DIRECT_BLOCK_COUNT 124`

`#define INDIRECT_BLOCK_COUNT 128`

#### Changes to `struct inode_disk`
```C
struct inode_disk {
  block_sector_t direct_blocks[DIRECT_BLOCK_COUNT];
  block_sector_t indirect_block;
  block_sector_t doubly_indirect_block;

  off_t length;
  unsigned magic;
};
```
> Initial `struct inode_disk` contains `block_sector_t start` variable that indicates the first data sector and `uint32_t unused[125]` which is used as a pad to make the size of `struct inode_disk` equal to 512 bytes. Current design doesn’t allow extension of a file since a file is consisted of consecutive block sectors. In order to allow a file extension, we use direct, indirect, and doubly indirect blocks to store file and such blocks’ sector numbers are stored in the `struct inode_disk`. 

> There will be 124 `direct_blocks`, 1 `indirect_block`, and 1 `doubly_indirect_block` pointers due to 512 bytes constraint. The `indirect_block` will point to 128 blocks `(128 x 4 = 512)` and `doubly_indirect_block` will point to 128 `indirect_block`, resulting `128 * 128` blocks. This totals to `124 + 128 + 128 * 128 = 16636` sectors, which approximates to 8.52 MB. 

#### Definition of `struct indirect_block`
```C
struct indirect_block {
  block_sector_t blocks[INDIRECT_BLOCK_COUNT];
};
```

#### New Functions Introduced in `inode.c`
```C
static bool inode_allocate (struct inode_disk *inode_disk, off_t length);
static void inode_deallocate (struct inode *inode);
```

### Algorithms

#### Changes to `inode_create ()`
`bool inode_create (block_sector_t sector, off_t length)`
* Instead of calling `free_map_allocate ()` to allocate sectors, call `inode_allocate ()`
* Since `struct inode_disk` only requires 1 sector to contain the whole file due to direct, indirect, and doubly-indirect block pointers, we only call `cache_write ()` once on `inode_disk` instead of each sector the file is occupying. 

#### Implementation of `inode_allocate ()`
`static bool inode_allocate (struct inode_disk *inode_disk, off_t length)`
* `inode_allocate ()` will be called during two instances, once during `inode_create ()` and another time during `inode_write_at ()` when you are writing past `EOF`
* Blocks will be allocated in the order of `direct` -> `indirect` -> `doubly-indirect` depending on how much spaces are needed
* For calling `inode_allocate ()` to create a file, first calculate number of sectors needed by calling `bytes_to_sectors ()`. 
* Starting from the `direct_blocks`, allocate the blocks one by one by calling `free_map_allocate ()`. 
* When there are more sectors needed to be allocated after depleting `direct_blocks`, allocate `struct indirect_block`. 
* `struct indirect_block` will have 128 blocks. If more block sectors are needed after depleting `indirect_block`, allocate `doubly-indirect` block. 
* `doubly_indirect` block will be a `indirect_block` consisting 128 `indirect_block` pointers. Allocate amount of blocks needed.
* For calling `inode_allocate ()` to extend file, `inode_allocate ()` will check each sector and only allocate if it hasn’t been allocated before. This requires `off_t length` to be `offset + size`, meaning total size that will be resulted. 

#### Changes to `inode_close ()`
`void inode_close (struct inode *inode)`
* Since `inode` is allocated using `inode_allocate ()`, we use `inode_deallocate ()` to deallocate instead of `free_map_release ()`

#### Implementation of `inode_deallocate ()`
`static void inode_deallocate (struct inode *inode)`
* First calculate number of block sectors occupied by calling `bytes_to_sectors (inode->data.length)`
* In the order of `direct` -> `indirect` -> `doubly-indirect`, call `free_map_release ()` until all the block sectors used are freed. 

#### Changes to `byte_to_sector ()`
`static block_sector_t byte_to_sector (const struct inode *inode, off_t pos)`
* Since a file is not consisted of consecutive block sectors anymore, current `byte_to_sector ()` won’t work
* `byte_to_sector ()` will travel down to correct index of block sectors that contains byte offset `pos` within inode
* In the order of `direct` -> `indirect` -> `doubly-indirect`, check whether `pos` lies within the block

#### Changes to `inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)` to handle appending 
* Malloc `struct inode_disk_t *metadata`
* Call cache_read(), passing in metadata->data
* Check if offset > metadata->data.start + metadata->data.length
* If so, return 0 (reading past EOF should return 0 bytes)
* free metadata

#### Changes to `inode_write_at ()`
`off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset)`
* Support file extension
* If `byte_to_sector ()` returns -1, it means you are writing past `EOF`.
  * If that’s the case, call `inode_allocate (offset + size)` to allocate additional block sectors needed to accommodate space for writing to the file. 
  * Malloc `struct inode_disk_t *metadata`
  * Call cache_read(), passing in metadata->data
  * Compute zero_pad_count =  offset - (metadata->data.start + metadata->data.length)
  * If zero_pad_count > 0, calloc buffer of 0’s called `zero_padding` of size `zero_padding_size`
  * Call cache_write, passing in zero_padding as source, and offset = metadata->data.start, size = metadata->data.length


### __Changes to free_map functions to support “write-behind” behavior__

#### Changes to `free_map_allocate ()` to support “write-back” 
* Removal of first if statement entirely, which is implementing “write-through” behavior of the free_map.
* This works because all functions access the `free_map`, instead of the `free_map_file`, and we update the `free_map_file` periodically and during shutdown.

#### Changes to `free_map_release ()` to support “write-back” 
* Removal of call to `bitmap_write ()`, which is implementing “write-through” behavior
* This works because all functions access the `free_map`, instead of the `free_map_file`, and we update the `free_map_file` periodically and during shutdown.

`#define FLUSH_PERIOD <some not yet determined value>`
#### Implementation of `void cache_flush ()` function in `cache.c`
* Iterate through the cache, acquiring and releasing each block on the way, and write each dirty page to disk.

#### Implementation of `void free_map_flush ()` in `free-map.c`
* Wrapper for a call to `bitmap_write (free_map, free_map_file)`. 
* It will acquire the free_map_lock, write the current state of the free_map to persistent storage, and release the free_map lock.

#### Re-implement non-busy waiting as in Project 1 design doc using timer.

#### Implementation of `void filesys_flush ()` in `filesys.c`
* call `cache_flush ()` and `free_map_flush ()`. 

#### Implementation of function `filesys_flush_periodically()`
* Loop infinitely doing the following:
	* Call `timer_sleep(FLUSH_PERIOD);`
	* Call `filesys_flush()`
* We call time_sleep() first so that when the thread is first created and run (which is in filesys_init), we do not flush an empty cache and empty free_map.

#### Additions to `filesys_init ()`
* call `thread_create(filesys_flush_peridocally, NULL)` to start the periodic flush thread
* This must be done at the end of the function, so that we are sure the cache, inode system, and free map have already been initialized.

#### Additions to `void shutdown()` in `shutdown.c`
* In very beginning, add call to `filesys_flush ()`. 
* This will handle updating persistent storage of the filesystem in the case of a shutdown, reboot, or kernel panic.
* Because this is called before the call to `filesys_done ()` in `shutdown ()`, we know that the `free_map` will still exist in memory, thereby preventing a crash.

### __Synchronization of free_map__

#### Addition of `static struct lock free_map_lock` in `free-map.c`

#### Additions to `free_map_init ()`
* Call `lock_init (free_map_lock)`
#### Additions to `free_map_flush ()`, `free_map_allocate ()`, and `free_map_release ()`
* Acquire and release the `free_map_lock`



### Synchronization
> Synchronization for task 2 requires making free map related functions thread-safe. We created a lock for free_map and the lock will be acquired/released when `free_map_allocate ()` and `free_map_release ()` is called. Since free_map is being accessed as a static variable instead of as a file, it will not go through the cache, and therefore must have its own synchronization via the lock.


### Rationale
> Having 124 direct blocks, 1 indirect block, and 1 doubly-indirect block covers just about 8 MB. Such design will result `struct inode_disk` to be exactly 512 bytes in size and since `struct inode_disk` holds sector numbers, file extension can be done easily by allocating more sectors when needed. There will be `struct indirect-block` that will hold array of 128 `block_sector_t` variables but not for doubly-indirect since doubly-indirect will be an indirect-block that holds 128 indirect-blocks. Allocating and deallocating sectors in this design might be tricky and requires a bit of thorough coding, but it should still be more efficient than other approaches. 


## __Task 3: Subdirectories__

### Data Structures and Functions

#### Additions to `struct thread` 
```C
struct thread {
  struct dir *working_dir;
};
```

#### Additions to `struct inode_disk`
```C
// Add boolean DIR to identify a directory inode
struct inode_disk {
  bool isdir;
};
```

#### Implementation of new functions to support directories
`bool inode_is_dir (struct inode *)`
 * `return inode->data.isdir`
> NEW: Returns true if inode refers to a directory

`int inode_inumber (struct inode *)`
* `return (int) inode->sector`
> NEW: Returns inode sector as the inumber

#### Addition to `inode_create ()` for directory support 
> Adds a boolean DIR parameter to identify a directory inode
`bool inode_create (block_sector_t sector, off_t length, bool isdir)`
* Set `dir` boolean for `inode_disk`
* `disk_inode->isdir = isdir`


#### Addition to `struct dir`
```C
// Add a lock to struct dir
struct dir {
  struct lock dir_lock;
}
```

// NEW: Returns a struct dir that corresponds to a DIRECTORY
#### Implementation of new function `dir_open_directory ()`
`struct dir * dir_open_directory (char * directory)`
* Tokenize directory into dir_part
* start_inode = curr_thread->working_dir OR dir_open_root()
* From start_inode, traverse each level of the directory tree through each dir_entry using dir_lookup(start_inode, dir_part, &next_inode)
* Return the last found inode if it is not removed

#### Additions to `dir_create ()`
`bool dir_create (block_sector_t sector, size_t entry_cnt)`
* Reserve the first dir entry at offset 0 after creating inode
* This will be used to store the parent directory sector

#### Additions to `dir_add ()`
> Add boolean isdir to parameter to identify whether the file is a directory
`bool dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool, isdir)`
* If it is a directory, update the first entry of the file struct dir to reflect the parent struct dir

#### Additions to `dir_lookup ()`
> Account “..” and “.” in directory name
`bool dir_lookup (const struct dir *dir, const char *name, struct inode **inode)`
* “..”: Check first dir entry for parent dir
* “.”: Use current dir

#### Changes to `filesys_create ()`
> Add a boolean DIR parameter to identify a directory inode
`bool filesys_create (const char *name, off_t initial_size, bool isdir)`
* Separate directory and file name
* `dir = dir_open_directory ()`
* Call create and add functions with `isdir` bool (for `inode` and `dir` structs)

#### Changes to `filesys_open ()`
`struct file * filesys_open (const char *name)`
* Separate directory and file name

#### Changes in `process.c/h`
```C
// Add parent’s working directory to struct load_synch
struct load_synch {
  Struct dir *parent_working_dir;
}
```

#### Changes to `start_process ()`
`static void start_process (void *load_info_);`
* Access parent’s working dir and set current thread’s working dir

#### Changes to `process_exit ()`
`void process_exit (void)`
* Close current thread’s working dir

### Algorithms

#### Implementation of `sys_chdir ()`
`bool sys_chdir (const char *dir)`
* Set `thread_current()->working_dir = dir` if it is valid.

#### Implementation of `sys_mkdir ()`
`bool sys_mkdir (const char *dir)`
* Use `filesys_create()`
* Directory parsing handled internally in `filesys_create`

#### Implementation of `readdir ()`
`bool readdir (int fd, char *name)`
* `struct file *file = get_file(thread_current(), fd);`
* `struct inode *inode = file_get_inode(file)`
* Use `dir_readdir()`
* Check if inode is a valid directory `isdir()`

#### Implementation of `isdir ()`
`bool isdir (int fd)`
* `struct file *file = get_file(thread_current(), fd);`
* `struct inode *inode = file_get_inode(file)`
* Use `inode_isdir()`

#### Implementation of `inumber ()`
`int inumber (int fd)`
* `struct file *file = get_file(thread_current(), fd);`
* `struct inode *inode = file_get_inode(file)`
* `return inode_inumber()`

### Synchronization
> An individual lock is associated with each dir struct. This lock will be used whenever thread safe operations is performed with the dir. As each dir is associated with a different level in the directory tree, this allows us to ensure mutual exclusion only on operations that modify the same dir.

### Rationale
> Each tokenized part in the directory path is associated with its own inode and dir structs. By treating each level in the directory tree as a separate dir, we can recursively traverse the “tree” and simplify the logic in accessing different files/directories. Also, by reserving the first dir entry as a holder for the parent directory sector, it allows us to easily handle the “..” case in parsing the directory tree.

## Additional Questions
> write behind functionality is already implemented in our project in task 2, therefore our given implementation is what we would use. Please see task 2 for details, but to summarize, we would re-implement the timer from Project 1 that does not busy wait. In filesys_init(), we would call thread_create(), passing in a helper function called filesys_flush_periodically(), which would infinitely loop, calling `timer_sleep(FLUSH_PERIOD)`, and then calling filesys_flush(), which is another function we would implement, which would iterate through the cache and write back all dirty pages to disk. In addition, we would write back our free_map (since we removed file writing of the free_map during updates, so that it has a “write-back” semantics).

> For read ahead functionality, we would do the following. Implement a helper function, called inode_read_at_asynch(), which will have the same exact behavior as inode_read_at(), except with added functionality to predict the next block that the user process will want to read. As an important note, inode_read_at_asynch() cannot call inode_read_at(), despite the similar code, as doing so would cause an infinite recursion where the call to inode_read_at() calls inode_read_at_asynch(), which calls inode_read_at(), etc. In calls to inode_read_at(), we would add a call to thread_create(), where we pass in the function inode_read_at_asynch() for the child thread to execute, and passing into the *aux the necessary arguments, most notable of which would be offset. The offset should be the original offset passed into inode_read_at() + 512, so that the call to byte_to_sector() will return the next contiguous block sector in the file. Regarding the other arguments, we will malloc a buffer of size 1 that will be the destination buffer, and we will free it at the end, and the size argument will also be 1. Therefore, the function will retrieve the next block in the file and place it in the cache, and then write 1 byte into the destination buffer for ease of debugging (instead of dealing with 0’s and nulls) and subsequently free the buffer.

