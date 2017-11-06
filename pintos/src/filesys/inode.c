#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Block Sector Counts */
#define DIRECT_BLOCK_COUNT 123
#define INDIRECT_BLOCK_COUNT 128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t direct_blocks[DIRECT_BLOCK_COUNT];
    block_sector_t indirect_block;
    block_sector_t doubly_indirect_block;

    bool is_dir;                        /* Indicator of directory file */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
  };

/* Indirect-block structure */
struct indirect_block_sector
  {
    block_sector_t block[INDIRECT_BLOCK_COUNT];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Min function */
static inline size_t
min (size_t x, size_t y)
{
  return x < y ? x : y;
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    struct lock inode_lock;             /* Inode lock. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
  };

/* Functions replaced free_map_allocate () */
static bool inode_allocate (struct inode_disk *disk_inode, off_t length);
static bool inode_allocate_sector (block_sector_t *sector_num);
static bool inode_allocate_indirect (block_sector_t *sector_num, size_t cnt);
static bool inode_allocate_doubly_indirect (block_sector_t *sector_num, size_t cnt);

/* Functions replaced free_map_release () */
static void inode_deallocate (struct inode *inode);
static void inode_deallocate_indirect (block_sector_t sector_num, size_t cnt);
static void inode_deallocate_doubly_indirect (block_sector_t sector_num, size_t cnt);


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  block_sector_t sector = -1;
  struct inode_disk *disk_inode = get_inode_disk (inode);

  if (pos < disk_inode->length)
    {
      off_t index = pos / BLOCK_SECTOR_SIZE;

      /* direct block */
      if (index < DIRECT_BLOCK_COUNT)
        sector = disk_inode->direct_blocks[index];
      /* indirect block */
      else if (index < DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT)
        {
          /* remove direct block bias */
          index -= DIRECT_BLOCK_COUNT;

          struct indirect_block_sector indirect_block;
          cache_read (fs_device, disk_inode->indirect_block,
                      &indirect_block, 0, BLOCK_SECTOR_SIZE);
          sector = indirect_block.block[index];
        }
      /* doubly indirect block */
      else
        {
          /* remove direct and indirect block bias */
          index -= (DIRECT_BLOCK_COUNT + INDIRECT_BLOCK_COUNT);
          struct indirect_block_sector indirect_block;

          /* get doubly indirect and indirect block index */
          int did_index = index / INDIRECT_BLOCK_COUNT;
          int id_index  = index % INDIRECT_BLOCK_COUNT;

          /* Read doubly indirect block, then indirect block */
          cache_read (fs_device, disk_inode->doubly_indirect_block,
                      &indirect_block, 0, BLOCK_SECTOR_SIZE);
          cache_read (fs_device, indirect_block.block[did_index],
                      &indirect_block, 0, BLOCK_SECTOR_SIZE);

          sector = indirect_block.block[id_index];
        }
    }

  free(disk_inode);
  return sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  /* Length cannot be negative */
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->is_dir = is_dir;
      disk_inode->length = length;
      disk_inode->magic  = INODE_MAGIC;
      if (inode_allocate (disk_inode, length))
        {
          cache_write (fs_device, sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->inode_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          inode_deallocate (inode);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = min (inode_left, sector_left);

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read (fs_device, sector_idx, (void *)(buffer + bytes_read),
                  sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  /* If new size of the file is past EOF, extend file */
  if (byte_to_sector (inode, offset + size - 1) == (size_t)-1)
    {
      /* Get inode_disk */
      struct inode_disk *disk_inode = get_inode_disk (inode);

      /* Allocate more sectors */
      if (!inode_allocate (disk_inode, offset + size))
        {
          free (disk_inode);
          return bytes_written;
        }

      /* Update inode_disk */
      disk_inode->length = offset + size;
      cache_write (fs_device, inode_get_inumber (inode), (void *)disk_inode, 0, BLOCK_SECTOR_SIZE);
      free (disk_inode);
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = min (inode_left, sector_left);

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_write (fs_device, sector_idx, (void *)(buffer + bytes_written),
                   sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Reads inode_disk from disk. Make sure to free after. */
struct inode_disk *
get_inode_disk (const struct inode *inode)
{
  ASSERT (inode != NULL);
  struct inode_disk *disk_inode = malloc (sizeof *disk_inode);
  cache_read (fs_device, inode_get_inumber (inode), (void *)disk_inode, 0, BLOCK_SECTOR_SIZE);
  return disk_inode;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  ASSERT (inode != NULL);
  struct inode_disk *disk_inode = get_inode_disk (inode);
  off_t len = disk_inode->length;
  free (disk_inode);
  return len;
}

/* Returns is_dir of INODE's data. */
bool
inode_is_dir (const struct inode *inode)
{
  ASSERT (inode != NULL);
  struct inode_disk *disk_inode = get_inode_disk (inode);
  bool is_dir = disk_inode->is_dir;
  free (disk_inode);
  return is_dir;
}

/* Returns removed of INODE's data. */
bool
inode_is_removed (const struct inode *inode)
{
  ASSERT (inode != NULL);
  return inode->removed;
}

/* Attempts allocating sectors in the order of direct->indirect->d.indirect */
static bool
inode_allocate (struct inode_disk *disk_inode, off_t length)
{
  ASSERT (disk_inode != NULL);
  if (length < 0) return false;

  /* Get number of sectors needed */
  size_t i, j, num_sectors = bytes_to_sectors (length);

  /* Allocate Direct Blocks */
  j = min (num_sectors, DIRECT_BLOCK_COUNT);
  for (i = 0; i < j; i++)
    if (!inode_allocate_sector (&disk_inode->direct_blocks[i]))
      return false;
  num_sectors -= j;
  if (num_sectors == 0) return true;

  /* Allocate Indirect Block */
  j = min (num_sectors, INDIRECT_BLOCK_COUNT);
  if (!inode_allocate_indirect (&disk_inode->indirect_block, j))
    return false;
  num_sectors -= j;
  if (num_sectors == 0) return true;

  /* Allocate Doubly-Indirect Block */
  j = min (num_sectors, INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT);
  if (!inode_allocate_doubly_indirect (&disk_inode->doubly_indirect_block, j))
    return false;
  num_sectors -= j;
  if (num_sectors == 0) return true;

  /* Shouldn't go past this point */
  ASSERT (num_sectors == 0);
  return false;
}

static bool
inode_allocate_sector (block_sector_t *sector_num)
{
  static char buffer[BLOCK_SECTOR_SIZE];
  if (!*sector_num)
    {
      if (!free_map_allocate (1, sector_num))
        return false;
      cache_write (fs_device, *sector_num, buffer, 0, BLOCK_SECTOR_SIZE);
    }
  return true;
}

static bool
inode_allocate_indirect (block_sector_t *sector_num, size_t cnt)
{
  /* Allocate indirect block sector if it hasn't been */
  if (!inode_allocate_sector (sector_num))
    return false;

  /* Read in the indirect block from cache */
  struct indirect_block_sector indirect_block;
  cache_read (fs_device, *sector_num, &indirect_block, 0, BLOCK_SECTOR_SIZE);

  /* Allocate number of sectors needed */
  size_t i;
  for (i = 0; i < cnt; i++)
    if (!inode_allocate_sector (&indirect_block.block[i]))
      return false;

  /* Write to disk */
  cache_write (fs_device, *sector_num, &indirect_block, 0, BLOCK_SECTOR_SIZE);

  return true;
}

static bool
inode_allocate_doubly_indirect (block_sector_t *sector_num, size_t cnt)
{
  /* Allocate doubly-indirect block sector if it hasn't been */
  if (!inode_allocate_sector (sector_num))
    return false;

  /* Read in the indirect block from cache */
  struct indirect_block_sector indirect_block;
  cache_read (fs_device, *sector_num, &indirect_block, 0, BLOCK_SECTOR_SIZE);

  /* Allocate number of indirect blocks needed */
  size_t num_sectors, i, j = DIV_ROUND_UP (cnt, INDIRECT_BLOCK_COUNT);
  for (i = 0; i < j; i++)
    {
      num_sectors = min (cnt, INDIRECT_BLOCK_COUNT);
      if (!inode_allocate_indirect (&indirect_block.block[i], num_sectors))
        return false;
      cnt -= num_sectors;
    }

  /* Write to disk */
  cache_write (fs_device, *sector_num, &indirect_block, 0, BLOCK_SECTOR_SIZE);

  return true;
}

static void
inode_deallocate (struct inode *inode)
{
  ASSERT (inode != NULL);

  /* Get inode_disk length in bytes */
  struct inode_disk *disk_inode = get_inode_disk (inode);
  off_t length = disk_inode->length;
  if (length < 0) return;

  /* Get number of sectors needed */
  size_t i, j, num_sectors = bytes_to_sectors (length);

  /* Deallocate direct blocks */
  j = min (num_sectors, DIRECT_BLOCK_COUNT);
  for (i = 0; i < j; i++)
    free_map_release (disk_inode->direct_blocks[i], 1);
  num_sectors -= j;
  if (num_sectors == 0)
    {
      free (disk_inode);
      return;
    }

  /* Deallocate indirect block */
  j = min (num_sectors, INDIRECT_BLOCK_COUNT);
  inode_deallocate_indirect (disk_inode->indirect_block, j);
  num_sectors -= j;
  if (num_sectors == 0)
    {
      free (disk_inode);
      return;
    }

  /* Deallocate doubly indirect block */
  j = min (num_sectors, INDIRECT_BLOCK_COUNT * INDIRECT_BLOCK_COUNT);
  inode_deallocate_doubly_indirect (disk_inode->doubly_indirect_block, j);
  num_sectors -= j;

  free (disk_inode);
  ASSERT (num_sectors == 0);
}

static void
inode_deallocate_indirect (block_sector_t sector_num, size_t cnt)
{
  struct indirect_block_sector indirect_block;
  cache_read (fs_device, sector_num, &indirect_block, 0, BLOCK_SECTOR_SIZE);

  size_t i;
  for (i = 0; i < cnt; i++)
    free_map_release (indirect_block.block[i], 1);

  free_map_release (sector_num, 1);
}

static void
inode_deallocate_doubly_indirect (block_sector_t sector_num, size_t cnt)
{
  struct indirect_block_sector indirect_block;
  cache_read (fs_device, sector_num, &indirect_block, 0, BLOCK_SECTOR_SIZE);

  size_t num_sectors, i, j = DIV_ROUND_UP (cnt, INDIRECT_BLOCK_COUNT);
  for (i = 0; i < j; i++)
    {
      num_sectors = min (cnt, INDIRECT_BLOCK_COUNT);
      inode_deallocate_indirect (indirect_block.block[i], num_sectors);
      cnt -= num_sectors;
    }

  free_map_release (sector_num, 1);
}

/* Acquire the lock of INODE. */
void
inode_acquire_lock (struct inode *inode)
{
  lock_acquire (&(inode->inode_lock));
}

/* Release the lock of INODE. */
void
inode_release_lock (struct inode *inode)
{
  lock_release (&(inode->inode_lock));
}
