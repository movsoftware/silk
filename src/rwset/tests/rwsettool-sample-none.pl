#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwsettool --sample ../../tests/set1-v4.set >/dev/null

use strict;
use SiLKTests;

my $rwsettool = check_silk_app('rwsettool');
my %file;
$file{v4set1} = get_data_or_exit77('v4set1');
my $cmd = "$rwsettool --sample $file{v4set1} >/dev/null";

exit (check_exit_status($cmd) ? 1 : 0);
