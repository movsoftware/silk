#! /usr/bin/perl -w
# STATUS: ERR
# TEST: ./rwgeoip2ccmap

use strict;
use SiLKTests;

my $rwgeoip2ccmap = check_silk_app('rwgeoip2ccmap');
check_features(qw(stdin_tty));
my $cmd = "$rwgeoip2ccmap";

exit (check_exit_status($cmd) ? 1 : 0);
