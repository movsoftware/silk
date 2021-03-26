#! /usr/bin/perl -w
# MD5: 9570d11206a18ac99e3df75c1fa233b0
# TEST: ./rwuniq --fields=sport --values=distinct:sip,distinct:dip --sort-output ../../tests/data-v6.rwf

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{v6data} = get_data_or_exit77('v6data');
check_features(qw(ipv6));
my $cmd = "$rwuniq --fields=sport --values=distinct:sip,distinct:dip --sort-output $file{v6data}";
my $md5 = "9570d11206a18ac99e3df75c1fa233b0";

check_md5_output($md5, $cmd);
