#! /usr/bin/perl -w
# MD5: 18807d4a21519d196a10be4ce047d4f0
# TEST: ./rwuniq --python-file=../../tests/pysilk-plugin.py --fields=lower_port --value=bytes --sort-output ../../tests/data.rwf

use strict;
use SiLKTests;

my $rwuniq = check_silk_app('rwuniq');
my %file;
$file{data} = get_data_or_exit77('data');
$file{pysilk_plugin} = get_data_or_exit77('pysilk_plugin');
$ENV{PYTHONPATH} = $SiLKTests::testsdir.((defined $ENV{PYTHONPATH}) ? ":$ENV{PYTHONPATH}" : "");
add_plugin_dirs('/src/pysilk');

check_python_plugin($rwuniq);
my $cmd = "$rwuniq --python-file=$file{pysilk_plugin} --fields=lower_port --value=bytes --sort-output $file{data}";
my $md5 = "18807d4a21519d196a10be4ce047d4f0";

check_md5_output($md5, $cmd);
