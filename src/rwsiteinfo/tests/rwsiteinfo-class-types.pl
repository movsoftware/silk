#! /usr/bin/perl -w
# MD5: 0a2605c86a0056c3d22f0128976c84d1
# TEST: ./rwsiteinfo --fields=class,type --site-config-file ./tests/rwsiteinfo-site.conf

use strict;
use SiLKTests;

my $rwsiteinfo = check_silk_app('rwsiteinfo');
my $cmd = "$rwsiteinfo --fields=class,type --site-config-file $SiLKTests::srcdir/tests/rwsiteinfo-site.conf";
my $md5 = "0a2605c86a0056c3d22f0128976c84d1";

check_md5_output($md5, $cmd);
