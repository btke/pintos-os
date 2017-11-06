/* Check if seek and tell works properly */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int handle, byte_cnt;

  CHECK (create ("seek-tell.txt", sizeof sample - 1), "create \"seek-tell.txt\"");
  CHECK ((handle = open ("seek-tell.txt")) > 1, "open \"seek-tell.txt\"");

  byte_cnt = write (handle, sample, sizeof sample - 1);
  if (byte_cnt != sizeof sample - 1)
    fail ("write() returned %d instead of %zu", byte_cnt, sizeof sample - 1);

  msg ("seek \"seek-tell.txt\"");
  seek (handle, sizeof sample - 10);
  CHECK ((tell (handle)) == sizeof sample - 10, "tell \"seek-tell.txt\"");
}
