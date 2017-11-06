/* This test checks for block write optimization, wherein
   a write syscall is made of size BLOCK_SECTOR_SIZE. In this
   case, the block should not first be read from the block device
   in order to edit and write back, but instead should be completely
   overwritten without a read. The test creates a buffer of size
   200 * BLOCK_SECTOR_SIZE, and writes it to disk. It then invalidates
   all entries of the cache, and saves the current read and write 
   counts, then rewrites the file. It then checks how many additional 
   reads and writes are incurred. The additional write count should 
   be high (comparable to the number of blocks, since the cache is cold), 
   while the additional read count should be very low (only occuring 
   from the initial read of the inode_disk, reading in the indirect_inodes 
   once the file becomes long enough, and accounting for inode eviction). */
   
#include <random.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "threads/fixed-point.h"

#define BLOCK_SECTOR_SIZE 512
#define BUF_SIZE (BLOCK_SECTOR_SIZE * 200)

static char buf[BUF_SIZE];
static long long num_disk_reads;
static long long num_disk_writes;

void
test_main (void)
{
  int test_fd;
  char *opt_write_file_name = "opt_write_test";
  CHECK (create (opt_write_file_name, 0), 
    "create \"%s\"", opt_write_file_name);
  CHECK ((test_fd = open (opt_write_file_name)) > 1, 
    "open \"%s\"", opt_write_file_name);

  /* Write opt_write_test file of size BLOCK_SECTOR_SIZE * 200 */
  random_bytes (buf, sizeof buf);
  CHECK (write (test_fd, buf, sizeof buf) == BUF_SIZE,
   "write %d bytes to \"%s\"", (int) BUF_SIZE, opt_write_file_name);

  /* Invalidate cache */
  invcache ();
  msg ("invcache");

   /* Get baseline disk statistics */
  CHECK (diskstat (&num_disk_reads, &num_disk_writes) == 0, 
  	"baseline disk statistics");

  /* Save baseline disk stats for comparison */
  long long base_disk_reads = num_disk_reads;
  long long base_disk_writes = num_disk_writes;

  /* Write opt_write_test file of size BLOCK_SECTOR_SIZE * 200 */
  random_bytes (buf, sizeof buf);
  CHECK (write (test_fd, buf, sizeof buf) == BUF_SIZE,
   "write %d bytes to \"%s\"", (int) BUF_SIZE, opt_write_file_name);

   /* Get new disk statistics */
  CHECK (diskstat (&num_disk_reads, &num_disk_writes) == 0, 
  	"get new disk statistics");

  /* Check that hit rate improved */
  CHECK (num_disk_reads - ((long long ) 10) <= base_disk_reads, 
    "old reads: %lld, old writes: %lld, new reads: %lld, new writes: %lld", 
  	base_disk_reads, base_disk_writes, num_disk_reads, num_disk_writes);

  msg ("close \"%s\"", opt_write_file_name);
  close (test_fd);
}




