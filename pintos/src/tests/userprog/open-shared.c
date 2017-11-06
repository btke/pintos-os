/* Check if two opens read and write to the same file descriptor. */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int handle1, handle2, byte_cnt1, byte_cnt2;
  char buffer[sizeof sample];

  CHECK (create ("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK ((handle1 = open ("test.txt")) > 1, "open \"test.txt\"");
  CHECK ((handle2 = open ("test.txt")) > 1, "open \"test.txt\" again");

  byte_cnt1 = write (handle1, sample, sizeof sample - 1);
  byte_cnt2 = read (handle2, buffer, sizeof sample - 1);
  if (byte_cnt1 != sizeof sample - 1)
    fail ("write() returned %d instead of %zu", byte_cnt1, sizeof sample - 1);
  if (byte_cnt1 != byte_cnt2)
    fail ("Did not point to the same file.");

}
