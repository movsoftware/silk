#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: flowcap-netflowv5-v4.pl 90396a06eb67 2015-08-05 22:05:43Z mthomas $")

use strict;
use SiLKTests;

my $NAME = $0;
$NAME =~ s,.*/,,;

# set envvar to run app under valgrind when SK_TESTS_VALGRIND is set
check_silk_app('flowcap');

# find the apps we need.  this will exit 77 if they're not available
my $rwcat = check_silk_app('rwcat');
my $rwsort = check_silk_app('rwsort');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# create our tempdir
my $tmpdir = make_tempdir();

# send data to this port and host
my $host = '127.0.0.1';
my $port = get_ephemeral_port($host, 'udp');

# create the sensor.conf
my $sensor_conf = "$tmpdir/sensor.conf";
my $sensor_conf_text = <<EOF;
probe P0 netflow-v5
    protocol udp
    listen-on-port $port
    listen-as-host $host
end probe
EOF
make_config_file($sensor_conf, \$sensor_conf_text);

# the command that wraps flowcap
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/flowcap-daemon.py",
                     ($ENV{SK_TESTS_VERBOSE} ? "--verbose" : ()),
                     ($ENV{SK_TESTS_LOG_DEBUG} ? "--log-level=debug" : ()),
                     "--pdu 40000,$host,$port",
                     "--limit=40000",
                     "--basedir=$tmpdir",
                     "--daemon-timeout=120",
                     "--",
                     "--sensor-conf=$sensor_conf",
                     "--max-file-size=100k",
                     "--clock-time=2"
    );

# run it and check the MD5 hash of its output
check_md5_output('f6e9b35dc226f9975c8c14d9ab332bd7', $cmd);

# path to the directory holding the output files
my $data_dir = "$tmpdir/destination";
die "$NAME: ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# check for zero length files in the directory
opendir D, "$data_dir"
    or die "$NAME: ERROR: Unable to open directory $data_dir: $!\n";
for my $f (readdir D) {
    next if (-d "$data_dir/$f") || (0 < -s _);
    warn "$NAME: WARNING: Zero length files in $data_dir\n";
    last;
}
closedir D;

# create a command to sort all files in the directory and output them
# in a standard form.
$cmd = ("find $data_dir -type f -print "
        ." | $rwcat --xargs "
        ." | $rwsort --fields=stime,sip "
        ." | $rwcat --byte-order=little --compression-method=none"
        ." --ipv4-output");

exit check_md5_output('4ac59f73c7d70e777982e9907952c9a3', $cmd);
