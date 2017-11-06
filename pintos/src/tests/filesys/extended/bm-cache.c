/* Tests the effectiveness of the cache by doing the following:
   Writes a file to disk that takes up half of the cache size
   (this is because room must be left for the inode_disk structs
   which must be read from disk in addition to the file itself).
   Then closes and reopens the file, and invalidates the cache.
   Then reads the entire file (cold cache), keeping track of cache 
   access statistics. Then closes and reopens the file, and re-reads 
   the entire file. It then compares the access statistics from the 
   second read to the first read, and checks for an increase in the 
   cache hit rate.  */

#include <random.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "threads/fixed-point.h"

#define CACHE_NUM_ENTRIES 64
#define BLOCK_SECTOR_SIZE 512
#define BUF_SIZE (BLOCK_SECTOR_SIZE * CACHE_NUM_ENTRIES / 2)

static fixed_point_t get_hit_rate (long long num_cache_hits, long long num_cache_accesses);
static char buf[BUF_SIZE];
static long long num_cache_accesses;
static long long num_cache_hits;
static long long num_cache_misses;

fixed_point_t
get_hit_rate (long long num_cache_hits, long long num_cache_accesses)
{
  return fix_div (fix_int (num_cache_hits), fix_int (num_cache_accesses));
}

void
test_main (void)
{
  int test_fd;
  char *cache_test_file_name = "cache_test";
  CHECK (create (cache_test_file_name, 0),
    "create \"%s\"", cache_test_file_name);
  CHECK ((test_fd = open (cache_test_file_name)) > 1,
    "open \"%s\"", cache_test_file_name);

  /* Write cache_test_file of size CACHE_NUM_ENTRIES * BLOCK_SECTOR_SIZE
     to fill entire cold cache. */
  random_bytes (buf, sizeof buf);
  CHECK (write (test_fd, buf, sizeof buf) == BUF_SIZE,
   "write %d bytes to \"%s\"", (int) BUF_SIZE, cache_test_file_name);

  /* Close file and reopen */
  close (test_fd);
  msg ("close \"%s\"", cache_test_file_name);
  CHECK ((test_fd = open (cache_test_file_name)) > 1,
    "open \"%s\"", cache_test_file_name);

  /* Invalidate cache */
  invcache ();
  msg ("invcache");

   /* Get baseline cache statistics */
  CHECK (cachestat (&num_cache_accesses, &num_cache_hits,
    &num_cache_misses) == 0, "baseline cache statistics");

  /* Save baseline cache stats for comparison */
  long long base_cache_accesses = num_cache_accesses;
  long long base_cache_hits = num_cache_hits;

  /* Read full file (cold cache) */
  CHECK (read (test_fd, buf, sizeof buf) == BUF_SIZE,
   "read %d bytes from \"%s\"", (int) BUF_SIZE, cache_test_file_name);

  /* Get cache statistics */
  CHECK (cachestat (&num_cache_accesses, &num_cache_hits,
    &num_cache_misses) == 0, "cachestat");

  fixed_point_t old_hit_rate = get_hit_rate (num_cache_hits - base_cache_hits,
                              num_cache_accesses - base_cache_accesses);

  /* Re-baseline */
  base_cache_accesses = num_cache_accesses;
  base_cache_hits = num_cache_hits;

  /* Close file and reopen */
  close (test_fd);
  msg ("close \"%s\"", cache_test_file_name);
  CHECK ((test_fd = open (cache_test_file_name)) > 1,
    "open \"%s\"", cache_test_file_name);

  /* Read full file again */
  CHECK (read (test_fd, buf, sizeof buf) == BUF_SIZE,
   "read %d bytes from \"%s\"", (int) BUF_SIZE, cache_test_file_name);

  /* Get cache statistics */
  CHECK (cachestat (&num_cache_accesses, &num_cache_hits,
    &num_cache_misses) == 0, "cachestat");

  fixed_point_t new_hit_rate = get_hit_rate (num_cache_hits - base_cache_hits,
                                num_cache_accesses - base_cache_accesses);

  /* Convert to percent for display */
  int old_rate_int = fix_trunc (fix_mul (old_hit_rate, fix_int (100)));
  int new_rate_int = fix_trunc (fix_mul (new_hit_rate, fix_int (100)));

  /* Check that hit rate improved */
  CHECK (fix_compare (new_hit_rate, old_hit_rate) == 1,
    "old hit rate percent: %d, new hit rate percent: %d",
    old_rate_int, new_rate_int);

  msg ("close \"%s\"", cache_test_file_name);
  close (test_fd);
}
