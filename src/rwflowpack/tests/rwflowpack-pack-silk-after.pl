#! /usr/bin/perl -w
#
#
# RCSIDENT("$SiLK: rwflowpack-pack-silk-after.pl 04bb49edf544 2018-05-31 18:25:33Z mthomas $")

use strict;
use SiLKTests;
use File::Find;

my $rwflowpack = check_silk_app('rwflowpack');

# find the apps we need.  this will exit 77 if they're not available
my $rwcat = check_silk_app('rwcat');
my $rwsetbuild = check_silk_app('rwsetbuild');

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');

# prefix any existing PYTHONPATH with the proper directories
check_python_bin();

# set the environment variables required for rwflowpack to find its
# packing logic plug-in
add_plugin_dirs('/site/twoway');

# Skip this test if we cannot load the packing logic
check_exit_status("$rwflowpack --sensor-conf=$srcdir/tests/sensor77.conf"
                  ." --verify-sensor-conf")
    or skip_test("Cannot load packing logic");

# create our tempdir
my $tmpdir = make_tempdir();

# Generate the IPset files
my %ipset = (
    internal => "$tmpdir/internal.set",
    external => "$tmpdir/external.set",
    null     => "$tmpdir/null.set",
    );
my %blocks = (
    internal => '192.168.x.x',
    external => '10.0.0.0/8',
    null     => '172.16.0.0/13',
    );

for my $k (keys %ipset) {
    my $cmd = "echo '$blocks{$k}' | $rwsetbuild - '$ipset{$k}'";
    check_md5_output('d41d8cd98f00b204e9800998ecf8427e', $cmd);
}

# Generate the sensor.conf file
my $sensor_conf = "$tmpdir/sensor-templ.conf";
{
    my $regex = join "|", keys %ipset;
    # undef record separator to slurp all of <DATA> into variable
    local $/;
    my $sensor_conf_text = <DATA>;
    $sensor_conf_text =~ s,\$\{($regex)\},$ipset{$1},g;
    make_config_file($sensor_conf, \$sensor_conf_text);
}

# the command that wraps rwflowpack
my $cmd = join " ", ("$SiLKTests::PYTHON $srcdir/tests/rwflowpack-daemon.py",
                     ($ENV{SK_TESTS_VERBOSE} ? "--verbose" : ()),
                     ($ENV{SK_TESTS_LOG_DEBUG} ? "--log-level=debug" : ()),
                     "--sensor-conf=$sensor_conf",
                     "--copy-after $file{data}:incoming",
                     "--limit=501876",
                     "--basedir=$tmpdir",
                     "--",
                     "--polling-interval=5",
    );

# run it and check the MD5 hash of its output
check_md5_output('a78a286719574389a972724d761c931e', $cmd);


# the following directories should be empty
verify_empty_dirs($tmpdir, qw(error incoming incremental sender));

# path to the data directory
my $data_dir = "$tmpdir/root";
die "ERROR: Missing data directory '$data_dir'\n"
    unless -d $data_dir;

# number of files to find in the data directory
my $expected_count = 0;
my $file_count = 0;

# read in the MD5s for every packed file we expect to find.  Although
# we are packing IPv4 data, whether we write IPv4 or IPv6 files
# depends on how SiLK was compiled.  In the packed IPv4 files, bytes
# are stored as a byte/packet ratio, and due to rounding the "bytes"
# value in the IPv4 and IPv6 files may differ.  Thus, we read in
# separate MD5 sums for each.
my %md5_map;
my $md5_file = $0;
# the MD5 sums should be the same as those for the
# rwflowpack-pack-silk.pl test
$md5_file =~ s/-after//;

if ($SiLKTests::SK_ENABLE_IPV6) {
    $md5_file .= "-ipv6.txt";
}
else {
    $md5_file .= "-ipv4.txt";
}

open F, $md5_file
    or die "ERROR: Cannot open $md5_file: $!\n";
while (my $lines = <F>) {
    my ($md5, $path) = split " ", $lines;
    $md5_map{$path} = $md5;
    ++$expected_count;
}
close F;

# find the files in the data directory and compare their MD5 hashes
File::Find::find({wanted => \&check_file, no_chdir => 1}, $data_dir);

# did we find all our files?
if ($file_count != $expected_count) {
    die "ERROR: Found $file_count files in root; expected $expected_count\n";
}

# successful!
exit 0;


# this is called by File::Find::find.  The full path to the file is in
# the $_ variable
sub check_file
{
    # skip anything that is not a file
    return unless -f $_;
    my $path = $_;
    # set $_ to just be the file basename
    s,^.*/,,;
    die "ERROR: Unexpected file $path\n"
        unless $md5_map{$_};
    ++$file_count;

    # do the MD5 sums match?
    check_md5_output($md5_map{$_}, ("$rwcat --ipv4-output --byte-order=little"
                                    ." --compression-method=none $path"));
}

__END__
# the sensor.conf file for this test
probe P0 silk
    poll-directory ${incoming}
end probe

group internal
    ipsets ${internal}
end group
group external
    ipsets ${external}
end group

sensor S0
    silk-probes P0
    internal-ipsets  @internal
    external-ipsets  @external
    null-ipset       "${null}"
end sensor
