#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
    struct lock dir_lock;
  };

/* A single directory entry. */
struct dir_entry
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  bool success = inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
  if (success)
    {
      struct dir *curr_dir = dir_open (inode_open (sector));
      struct dir_entry e;
      e.inode_sector = sector;
      e.in_use = false;

      /* Acquire dir lock */
      dir_acquire_lock (curr_dir);
      if (inode_write_at (dir_get_inode (curr_dir), &e, sizeof (e), 0) != sizeof (e))
        success = false;

      /* Release dir lock */
      dir_release_lock (curr_dir);
      dir_close (curr_dir);
    }
  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      lock_init (&dir->dir_lock);
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL;
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir)
{
  ASSERT (dir != NULL);
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir)
{
  ASSERT (dir != NULL);
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = sizeof e; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    {
      if (e.in_use && !strcmp (name, e.name))
        {
          if (ep != NULL)
            *ep = e;
          if (ofsp != NULL)
            *ofsp = ofs;
          return true;
        }
    }

  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (strcmp (name, "..") == 0)
    {
      inode_read_at (dir->inode, &e, sizeof e, 0);
      *inode = inode_open (e.inode_sector);
    }
  else if (strcmp (name, ".") == 0)
    *inode = inode_reopen (dir->inode);
  else if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Acquire dir_lock. */
  dir_acquire_lock (dir);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    goto done;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  if (is_dir)
    {
      bool parent_success = true;
      struct dir_entry e_;
      struct dir *curr_dir = dir_open (inode_open (inode_sector));
      if (curr_dir == NULL)
        goto done;

      /* Acquire curr_dir lock. */
      dir_acquire_lock (curr_dir);

      e_.in_use = false;
      e_.inode_sector = inode_get_inumber (dir_get_inode (dir));
      if (inode_write_at (curr_dir->inode, &e_, sizeof e_, 0) != sizeof e_)
        parent_success = false;

      /* Release curr_dir lock. */
      dir_release_lock (curr_dir);
      dir_close (curr_dir);

      if (!parent_success)
        goto done;
    }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.

     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = sizeof e; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  /* Release dir lock. */
  dir_release_lock (dir);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Acquire dir lock. */
  dir_acquire_lock (dir);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;
  else if (inode_is_dir(inode))
  {
    struct dir *dir_remove = dir_open(inode);
    struct dir_entry e_remove;
    off_t ofs_remove;

    bool empty = true;
    for (ofs_remove = sizeof e_remove;
        inode_read_at (dir_remove->inode, &e_remove, sizeof e_remove, ofs_remove) == sizeof e_remove;
        ofs_remove += sizeof e_remove)
      if (e_remove.in_use)
        {
          empty = false;
          break;
        }
    dir_close(dir_remove);

    if (!empty)
      goto done;
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  /* Release dir lock. */
  dir_release_lock (dir);
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  /* Acquire dir lock */
  dir_acquire_lock (dir);

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          dir_release_lock (dir);
          return true;
        }
    }

  /* Release dir lock */
  dir_release_lock (dir);
  return false;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp)
{
  const char *src = *srcp;
  char *dst = part;

  /* Skip leading slashes. If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0')
    {
      if (dst < part + NAME_MAX)
        *dst++ = *src;
      else
        return -1;
      src++;
    }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Given a full PATH, extract the DIRECTORY and FILENAME into the provided pointers. */
bool
split_directory_and_filename (const char *path, char *directory, char *filename)
{
  if (strlen (path) == 0)
    return false;

  if (path[0] == '/')
    *directory++ = '/';

  int status;
  char token[NAME_MAX + 1], prev_token[NAME_MAX + 1];
  token[0] = '\0';
  prev_token[0] = '\0';

  while ((status = get_next_part (token, &path)) != 0)
    {
      if (status == -1)
        return false;

      int prev_length = strlen (prev_token);
      if (prev_length > 0)
        {
          memcpy (directory, prev_token, sizeof (char) * prev_length);
          directory[prev_length] = '/';
          directory += prev_length + 1;
        }
      memcpy (prev_token, token, sizeof (char) * strlen (token));
      prev_token[strlen (token)] = '\0';
    }

  *directory = '\0';
  memcpy (filename, token, sizeof (char) * (strlen (token) + 1));
  return true;
}

/* Opens and returns the dir of DIRECTORY. */
struct dir *
dir_open_directory (const char *directory)
{
  struct thread *curr_thread = thread_current ();

  /* Absolute path */
  struct dir *curr_dir;
  if (directory[0] == '/' || curr_thread->working_dir == NULL)
    curr_dir = dir_open_root ();
  /* Relative path */
  else
    curr_dir = dir_reopen (curr_thread->working_dir);

  /* Tokenize each directory */
  char dir_token[NAME_MAX + 1];
  while (get_next_part (dir_token, &directory) == 1)
    {
      /* Lookup directory from current directory */
      struct inode *next_inode;
      if (!dir_lookup (curr_dir, dir_token, &next_inode))
        {
          dir_close (curr_dir);
          return NULL;
        }

      /* Open directory from inode received above */
      struct dir *next_dir = dir_open (next_inode);

      /* Close current directory and assign next directory as current */
      dir_close (curr_dir);
      if (!next_dir)
        return NULL;
      curr_dir = next_dir;
    }

  /* Return the last found inode if it is not removed */
  if (!inode_is_removed (dir_get_inode (curr_dir)))
    return curr_dir;

  dir_close (curr_dir);
  return NULL;
}

/* Acquire the lock of DIR. */
void
dir_acquire_lock (struct dir *dir)
{
  lock_acquire (&(dir->dir_lock));
}

/* Release the lock of DIR. */
void
dir_release_lock (struct dir *dir)
{
  lock_release (&(dir->dir_lock));
}
