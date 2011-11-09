#!/usr/bin/perl

use strict;
use warnings;

my $verbose = @ARGV;

open CL, "< debian/changelog" or die;
my $firstline = <CL>;
$firstline =~/^.*?\([\d\.]+vir(\d{10})\)/i or die;
my $lastrev = $1;
close CL;
print "Found last seen revision $lastrev\n";

my $yatever;
open CF, "< configure.in" or die;
while(<CF>) {
	chomp;
	if(/^\s*AC_INIT\(Yate, ([\d\.]+)\)/) {
		$yatever = $1;
		last;
	}
}
close CF;

my($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime(time);
my $suff = sprintf("%04d%02d%02d%02d", 1900+$year, 1+$mon, $mday, 1);

++$suff while $suff le $lastrev;

my $newver="$yatever.vir$suff";
print "New version: $newver\n";
my $rc = system qw( debchange -b --preserve --distribution UNRELEASED --newversion ), "$newver";
if($rc == 0) {
	exec "git commit -m 'New debian package $newver' debian/changelog";
}



