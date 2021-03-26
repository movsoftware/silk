#! /usr/bin/perl -w
# MD5: 56c889edf22d4853edba107f39877beb
# TEST: cat ../../tests/data.rwf | ./rwtotal --sport --skip-zero

use strict;
use SiLKTests;

my $rwtotal = check_silk_app('rwtotal');
my %file;
$file{data} = get_data_or_exit77('data');
my $cmd = "cat $file{data} | $rwtotal --sport --skip-zero";
my $md5 = "56c889edf22d4853edba107f39877beb";

check_md5_output($md5, $cmd);
