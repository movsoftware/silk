#! /usr/bin/perl -w
# MD5: multiple
# TEST: multiple
# RCSIDENT("$SiLK: rwsettool-union-data-v4v6.pl 62a32b7637cc 2018-03-16 17:30:43Z mthomas $")

use strict;
use SiLKTests;

my $NAME = $0;
$NAME =~ s,.*/,,;

# find the apps we need.  this will exit 77 if they're not available
my $rwset = check_silk_app('rwset');
my $rwsetbuild = check_silk_app('rwsetbuild');
my $rwsetcat = check_silk_app('rwsetcat');
my $rwsettool = check_silk_app('rwsettool');
my $rwsort = check_silk_app('rwsort');
my $rwsplit = check_silk_app('rwsplit');

# switches for every rwset and rwsettool invocation
my $common_args = '--compression=none --invocation-strip';

# find the data files we use as sources, or exit 77
my %file;
$file{data} = get_data_or_exit77('data');
$file{v6data} = get_data_or_exit77('v6data');

# verify that required features are available
check_features(qw(ipset_v6));

# create our tempdir
my $tmpdir = make_tempdir();

# basename for files that get created in $tmpdir
my $basename = 'v4v6';

# result of running MD5 on /dev/null
my $empty_md5 = 'd41d8cd98f00b204e9800998ecf8427e';

# result of running MD5 on "echo 0"
my $zero_md5 = '897316929176464ebc9ad085f31e7284';

my ($cmd, $md5);

#### CREATE FLOW FILES

$cmd = "$rwsort --fields=stime,dur,proto,sport,dport $file{data} $file{v6data}"
    ." | $rwsplit --basename=$tmpdir/$basename --flow-limit=10000";
check_md5_output($empty_md5, $cmd);


#### CREATE AND UNION THE IPSETS

# create initial empty IPsets
my %sets = (
    sip => "$tmpdir/$basename-sip.set",
    dip => "$tmpdir/$basename-dip.set",
    any => "$tmpdir/$basename-any.set",
    );
for my $s (keys %sets) {
    $cmd = "cat /dev/null | $rwsetbuild $common_args - $sets{$s}";
    check_md5_output($empty_md5, $cmd);
}


# loop over the flow files
opendir T, $tmpdir
    or die "$NAME: Cannot opendir($tmpdir): $!\n";
for my $f (readdir T) {
    next unless $f =~ /^$basename\.\d+\.rwf$/;

    my %oldsets = %sets;

    for my $s (keys %sets) {
        # this is the new output file
        $sets{$s} = "$tmpdir/$f.$s.union";

        # create an IPset and union it with the rollup of all IPsets
        $cmd = "$rwset $common_args --$s=- $tmpdir/$f"
            ." | $rwsettool $common_args --output-path=$sets{$s}"
            ." --union - $oldsets{$s}";
        check_md5_output($empty_md5, $cmd);

        # remove the new set from the old set, giving an empty set
        $cmd = "$rwsettool $common_args --difference $oldsets{$s} $sets{$s}"
            ." | $rwsetcat --count";
        check_md5_output($zero_md5, $cmd);

        # intersect the new set with the old set; result should be
        # identical to the old set
        $cmd = "$rwsetcat --cidr-blocks=1 $oldsets{$s}";
        compute_md5(\$md5, $cmd);
        $cmd = "$rwsettool $common_args --intersect $sets{$s} $oldsets{$s}"
            ." | $rwsetcat --cidr-blocks=1";
        check_md5_output($md5, $cmd);
        $cmd = "$rwsettool $common_args --intersect $oldsets{$s} $sets{$s}"
            ." | $rwsetcat --cidr-blocks=1";
        check_md5_output($md5, $cmd);
    }

    unless ($ENV{SK_TESTS_SAVEOUTPUT}) {
        unlink "$tmpdir/$f", values %oldsets;
    }
}
closedir T;


#### CHECK THE HASHES

my %md5s = (
    sip => '44f858b9c100e7853717434b693f6d58',
    dip => '077092fd76d9233cebd0dbaecfb97ad0',
    any => '73c7d64fa3b6e9796571cc2491d7c040',
    );
for my $s (keys %sets) {
    $md5 = $md5s{$s};
    $cmd = "$rwsetcat --cidr --ip-format=map-v4 $sets{$s}";
    check_md5_output($md5, $cmd);
}
