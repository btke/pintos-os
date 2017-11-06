# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek-tell) begin
(seek-tell) create "seek-tell.txt"
(seek-tell) open "seek-tell.txt"
(seek-tell) seek "seek-tell.txt"
(seek-tell) tell "seek-tell.txt"
(seek-tell) end
seek-tell: exit(0)
EOF
pass;
