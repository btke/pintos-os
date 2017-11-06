# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(bm-cache) begin
(bm-cache) create "cache_test"
(bm-cache) open "cache_test"
(bm-cache) write 16384 bytes to "cache_test"
(bm-cache) close "cache_test"
(bm-cache) open "cache_test"
(bm-cache) invcache
(bm-cache) baseline cache statistics
(bm-cache) read 16384 bytes from "cache_test"
(bm-cache) cachestat
(bm-cache) close "cache_test"
(bm-cache) open "cache_test"
(bm-cache) read 16384 bytes from "cache_test"
(bm-cache) cachestat
(bm-cache) old hit rate percent: 65, new hit rate percent: 98
(bm-cache) close "cache_test"
(bm-cache) end
EOF
pass;