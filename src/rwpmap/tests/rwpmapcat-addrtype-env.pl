#! /usr/bin/perl -w
# MD5: 045c3a9fd792d585bec7047b42e2618b
# TEST: ./rwpmapcat --no-cidr --address-types

use strict;
use SiLKTests;

my $rwpmapcat = check_silk_app('rwpmapcat');
my %file;
$file{address_types} = get_data_or_exit77('address_types');
$ENV{SILK_ADDRESS_TYPES} = "$SiLKTests::PWD/$file{address_types}";
$ENV{SILK_COUNTRY_CODES} = "$SiLKTests::PWD/$file{fake_cc}";
my $cmd = "$rwpmapcat --no-cidr --address-types";
my $md5 = "045c3a9fd792d585bec7047b42e2618b";

check_md5_output($md5, $cmd);
