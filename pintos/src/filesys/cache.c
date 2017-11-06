#include <debug.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

#define CACHE_NUM_ENTRIES 64
#define CACHE_NUM_CHANCES 1

/* Cache block, each block can hold BLOCK_SECTOR_SIZE bytes of data. */
struct cache_block
  {
    struct lock cache_block_lock;
    block_sector_t disk_sector_index;
    uint8_t data[BLOCK_SECTOR_SIZE];

    bool valid;
    bool dirty;
    size_t chances_remaining;
  };

/* Array of cache entries. */
static struct cache_block cache[CACHE_NUM_ENTRIES];

/* A lock for updating cache entries. */
static struct lock cache_update_lock;

/* Used to prevent flush on uninitialized cache if shutdown occurs before cache init. */
static bool cache_initialized = false;

/* Used for cache statistics. */
static long long cache_hit_count;
static long long cache_miss_count;
static long long cache_access_count;

static struct lock cache_hit_count_lock;
static struct lock cache_miss_count_lock;
static struct lock cache_access_count_lock;

/* Initialize the cache. */
void
cache_init (void)
{
  lock_init (&cache_update_lock);
  lock_init (&cache_hit_count_lock);
  lock_init (&cache_miss_count_lock);
  lock_init (&cache_access_count_lock);

  cache_hit_count = 0;
  cache_miss_count = 0;
  cache_access_count = 0;

  /* Initialize each cache block. */
  int i;
  for (i = 0; i < CACHE_NUM_ENTRIES; i++)
    {
      cache[i].valid = false;
      lock_init (&cache[i].cache_block_lock);
    }
  cache_initialized = true;
}

/* Writes the block at index to disk. */
static void
cache_flush_block_index (struct block *fs_device, int index)
{
  ASSERT (lock_held_by_current_thread (&cache[index].cache_block_lock));
  ASSERT (cache[index].valid == true && cache[index].dirty == true);

  /* Write from data to device at disk_sector_index. */
  block_write (fs_device, cache[index].disk_sector_index, cache[index].data);
  cache[index].dirty = false;
}

/* Write entire cache to disk. */
void
cache_flush (struct block *fs_device)
{
  ASSERT (fs_device != NULL);

  /* Cache was not initialized and contains garbage so don't flush. */
  if (!cache_initialized)
    return;

  /* Write each cache block to disk */
  int i;
  for (i = 0; i < CACHE_NUM_ENTRIES; i++)
    {
      lock_acquire (&cache[i].cache_block_lock);
      if (cache[i].valid && cache[i].dirty)
        cache_flush_block_index (fs_device, i);
      lock_release (&cache[i].cache_block_lock);
    }
}

/* Invalidate the entire cache by invalidating all of its entries. */
void
cache_invalidate (struct block *fs_device)
{
  /* Cache was not initialized and contains garbage so it's already invalid. */
  if (!cache_initialized)
    return;

  lock_acquire (&cache_update_lock);
  /* Invalidate each cache entry. */
  int i;
  for (i = 0; i < CACHE_NUM_ENTRIES; i++)
    {
      lock_acquire (&cache[i].cache_block_lock);
      if (cache[i].valid && cache[i].dirty)
        cache_flush_block_index (fs_device, i);
      cache[i].valid = false;
      lock_release (&cache[i].cache_block_lock);
    }
  lock_release (&cache_update_lock);
}

/* Increment cache hit count. */
static void
cache_increment_hit_count (void)
{
  lock_acquire (&cache_hit_count_lock);
  cache_hit_count++;
  lock_release (&cache_hit_count_lock);
}

/* Increment cache miss count. */
static void
cache_increment_miss_count (void)
{
  lock_acquire (&cache_miss_count_lock);
  cache_miss_count++;
  lock_release (&cache_miss_count_lock);
}

/* Increment cache access count. */
static void
cache_increment_access_count (void)
{
  lock_acquire (&cache_access_count_lock);
  cache_access_count++;
  lock_release (&cache_access_count_lock);
}

/* Stores the cache statistics in the corresponding argument references. */
int
cache_get_stats (long long *access_count, long long *hit_count, long long *miss_count)
{
  if (access_count == NULL || hit_count == NULL || miss_count == NULL || !cache_initialized)
    return -1;

  lock_acquire (&cache_hit_count_lock);
  lock_acquire (&cache_miss_count_lock);
  lock_acquire (&cache_access_count_lock);

  *access_count = cache_access_count;
  *hit_count = cache_hit_count;
  *miss_count = cache_miss_count;

  lock_release (&cache_access_count_lock);
  lock_release (&cache_miss_count_lock);
  lock_release (&cache_hit_count_lock);

  return 0;
}

/* Find a cache entry to evict and return its index. */
static int
cache_evict (struct block *fs_device, block_sector_t sector_index)
{
  static size_t clock_position = 0;
  lock_acquire (&cache_update_lock);

  /* Check to make sure sector_index is not already in the cache. */
  int i;
  for (i = 0; i < CACHE_NUM_ENTRIES; i++)
    {
      lock_acquire (&cache[i].cache_block_lock);
      if (cache[i].valid && (cache[i].disk_sector_index == sector_index))
        {
          lock_release (&cache_update_lock);
          return (-i) - 1;
        }
      lock_release (&cache[i].cache_block_lock);
    }

  /* Perform clock algorithm to find slot to evict. */
  while (true)
    {
      i = clock_position;
      clock_position++;
      clock_position %= CACHE_NUM_ENTRIES;

      lock_acquire (&cache[i].cache_block_lock);

      if (!cache[i].valid)
        {
          lock_release (&cache_update_lock);
          return i;
        }

      if (cache[i].chances_remaining == 0)
        break;

      cache[i].chances_remaining--;
      lock_release (&cache[i].cache_block_lock);
    }
    
  /* Cache entry is valid if it got to this point. */

  lock_release (&cache_update_lock);
  /* Write dirty block back to disk. */
  if (cache[i].dirty)
    cache_flush_block_index (fs_device, i);
  cache[i].valid = false;
  return i;
}

/* Replace the cache entry at index to contain data from sector_index. */
static void
cache_replace (struct block *fs_device, int index, block_sector_t sector_index,
               bool is_whole_block_write)
{
  ASSERT (lock_held_by_current_thread (&cache[index].cache_block_lock));
  ASSERT (cache[index].valid == false);

  /* Read in and write from device at disk_sector_index to data.
     Optmization: do not read in from disk if writing a whole block. */
  if (!is_whole_block_write)
    block_read (fs_device, sector_index, cache[index].data);
  
  cache[index].valid = true;
  cache[index].dirty = false;
  cache[index].disk_sector_index = sector_index;
  cache[index].chances_remaining = CACHE_NUM_CHANCES;
}

/* Returns the index in the cache corresponding to the block holding sector_index. */
static int
cache_get_block_index (struct block *fs_device, block_sector_t sector_index,
                       bool is_whole_block_write)
{
  cache_increment_access_count ();
  /* Check if sector_index is in cache. */
  int i;
  for (i = 0; i < CACHE_NUM_ENTRIES; i++)
    {
      lock_acquire (&cache[i].cache_block_lock);
      if (cache[i].valid && (cache[i].disk_sector_index == sector_index))
        {
          /* Cache hit. */
          cache_increment_hit_count ();
          break;
        }

      lock_release (&cache[i].cache_block_lock);
    }

  /* Evict if sector_index is not in cache. */
  if (i == CACHE_NUM_ENTRIES)
    {
      /* Cache miss.
         cache_evict () acquires cache_block_lock at index i. */
      i = cache_evict (fs_device, sector_index); 
      if (i >= 0)
        {
          /* Found a block to evict. */
          cache_increment_miss_count ();
          cache_replace (fs_device, i, sector_index, is_whole_block_write);
        }
      else
        {
          /* Sector index was found in the cache so it was a cache hit. */
          cache_increment_hit_count ();
          i = - (i + 1);
        }
    }

  ASSERT (lock_held_by_current_thread (&cache[i].cache_block_lock));
  return i;
}

/* Read chunk_size bytes of data from cache starting from sector_index at position offest,
   into destination. */
void
cache_read (struct block *fs_device, block_sector_t sector_index, void *destination,
            off_t offset, int chunk_size)
{
  ASSERT (fs_device != NULL);
  ASSERT (cache_initialized == true);
  ASSERT (offset >= 0 && chunk_size >= 0);
  ASSERT ((offset + chunk_size) <= BLOCK_SECTOR_SIZE);
  
  /* cache_get_block_index () acquires cache_block_lock at index i. */
  int i = cache_get_block_index (fs_device, sector_index, false);

  ASSERT (lock_held_by_current_thread (&cache[i].cache_block_lock));
  ASSERT (cache[i].valid == true);

  memcpy (destination, cache[i].data + offset, chunk_size);
  cache[i].chances_remaining = CACHE_NUM_CHANCES;
  lock_release (&cache[i].cache_block_lock);
}

/* Write chunk_size bytes of data into cache starting from sector_index at position offest,
   from source. */
void
cache_write (struct block *fs_device, block_sector_t sector_index, void *source,
             off_t offset, int chunk_size)
{
  ASSERT (fs_device != NULL);
  ASSERT (cache_initialized == true);
  ASSERT (offset >= 0 && chunk_size >= 0);
  ASSERT ((offset + chunk_size) <= BLOCK_SECTOR_SIZE);

  /* cache_get_block_index () acquires cache_block_lock at index i. */
  int i;
  if (chunk_size == BLOCK_SECTOR_SIZE)
    i = cache_get_block_index (fs_device, sector_index, true);
  else
    i = cache_get_block_index (fs_device, sector_index, false);

  ASSERT (lock_held_by_current_thread (&cache[i].cache_block_lock));
  ASSERT (cache[i].valid == true);

  memcpy (cache[i].data + offset, source, chunk_size);
  cache[i].dirty = true;
  cache[i].chances_remaining = CACHE_NUM_CHANCES;
  lock_release (&cache[i].cache_block_lock);
}
