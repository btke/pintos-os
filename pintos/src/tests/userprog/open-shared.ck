# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(open-shared) begin
(open-shared) create "test.txt"
(open-shared) open "test.txt"
(open-shared) open "test.txt" again
(open-shared) end
open-shared: exit(0)
EOF
pass;
