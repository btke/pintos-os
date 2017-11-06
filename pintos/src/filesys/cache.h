#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "off_t.h"
#include "devices/block.h"

/* Initialize cache. */
void cache_init (void);

/* Invalidate the entire cache by invalidating all its entries. */
void cache_invalidate (struct block *fs_device);

/* Write entire cache to disk. */
void cache_flush (struct block *fs_device);

/* Stores the cache statistics in the corresponding argument references. */
int cache_get_stats (long long *access_count, long long *hit_count, long long *miss_count);

/* Read chunk_size bytes of data from cache starting from sector_index at position offest,
   into destination. */
void cache_read (struct block *fs_device, block_sector_t sector_index, void *destination,
                 off_t offset, int chunk_size);

/* Write chunk_size bytes of data into cache starting from sector_index at position offest,
   from source. */
void cache_write (struct block *fs_device, block_sector_t sector_index, void *source,
                  off_t offset, int chunk_size);

#endif /* filesys/block.h */
