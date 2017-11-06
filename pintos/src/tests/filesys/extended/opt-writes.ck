# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(opt-writes) begin
(opt-writes) create "opt_write_test"
(opt-writes) open "opt_write_test"
(opt-writes) write 102400 bytes to "opt_write_test"
(opt-writes) invcache
(opt-writes) baseline disk statistics
(opt-writes) write 102400 bytes to "opt_write_test"
(opt-writes) get new disk statistics
(opt-writes) old reads: 38, old writes: 887, new reads: 44, new writes: 1233
(opt-writes) close "opt_write_test"
(opt-writes) end
EOF
pass;