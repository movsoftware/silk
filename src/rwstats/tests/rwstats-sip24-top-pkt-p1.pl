#! /usr/bin/perl -w
# MD5: d772133f1a95ac50fd6196ea81ffdf15
# TEST: ./rwstats --sip=24 --values=packets --percentage=1 --top --ipv6-policy=ignore ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwstats = check_silk_app('rwstats');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "$rwstats --sip=24 --values=packets --percentage=1 --top --ipv6-policy=ignore $file{data}";
my $md5 = "d772133f1a95ac50fd6196ea81ffdf15";

check_md5_output($md5, $cmd);
