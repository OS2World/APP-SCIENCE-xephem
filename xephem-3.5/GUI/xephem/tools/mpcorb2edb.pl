#!/usr/bin/perl
# convert MPCORB.DAT to 2 .edb files.
# Usage: [-f] <base>
#   if -f then use ftp to get the script from harvard, else read it from stdin.
#   <base> is a prefix used in naming the generated .edb files.
# Two files are created:
#   <base>.edb contains only those asteroids which might ever be brighter
#      than $dimmag (set below);
#   <base>_dim.edb contains the remaining asteroids.
#
# mpcorb.dat is a service of the Minor Planet Center,
# http://cfa-www.harvard.edu/cfa/ps/mpc.html.
#
# Copyright (c) 2000 Elwood Downey
# 16 Mar 1999: first draft
# 17 Mar 1999: change output filename
#  4 Apr 2000: update arg handling and support new MPC file format.
#  6 Oct 2000: add -f

# grab RCS version
$ver = '$Revision: 1.7 $';
$ver =~ s/\$//g;

# setup cutoff mag
$dimmag = 13;			# dimmest mag to be saved in "bright" file

# set site and file in case of -f
$MPCSITE = "cfa-ftp.harvard.edu";
$MPCFTPDIR = "/pub/MPCORB";
$MPCFILE = "MPCORB.DAT";
$MPCZIPFILE = "MPCORB.ZIP";

# immediate output
$| = 1;

# crack args.
# when thru here $fnbase is prefix name and $srcfd is handle to $MPCFILE.
if (@ARGV == 2) {
    &usage() unless @ARGV[0] eq "-f";
    &fetch();
    open SRCFD, $MPCFILE or die "$MPCFILE: $?\n";
    $srcfd = SRCFD;
    $fnbase = $ARGV[1];
} elsif (@ARGV != 1) {
    &usage();
} else {
    &usage() if @ARGV[0] =~ /^-/;
    $srcfd = STDIN;
    $fnbase = $ARGV[0];
}

# create output files prefixed with $fnbase
$brtfn = "$fnbase.edb";		# name of file for bright asteroids
open BRT, ">$brtfn" or die "Can not create $brtfn\n";
$dimfn = "$fnbase"."_dim.edb";# name of file for dim asteroids
open DIM, ">$dimfn" or die "Can not create $dimfn\n";
print "Creating $brtfn and $dimfn..\n";

# build some common boilerplate
($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = gmtime;
$year += 1900;
$mon += 1;
$from = "# Data is from ftp://cfa-ftp.harvard.edu/pub/MPCORB/MPCORB.DAT\n";
$what = "# Generated by mpcorb2edb.pl $ver, (c) 2000 Elwood Downey\n";
$when = "# Processed $year-$mon-$mday $hour:$min:$sec UTC\n";

# add boilerplate to each file
print BRT "# Asteroids ever brighter than $dimmag.\n";
print BRT $from;
print BRT $what;
print BRT $when;
print DIM "# Asteroids never brighter than $dimmag.\n";
print DIM $from;
print DIM $what;
print DIM $when;

# process each mpcorb.dat entry
while (<$srcfd>) {
    chomp();
    if (/^-----------/) {
	$sawstart = 1;
	next;
    }
    next unless ($sawstart);

    # build the name
    $name = &s(167, &min(length(),194));
    $name =~ s/[\(\)]//g;
    $name =~ s/^ *//;
    $name =~ s/ *$//;
    next if ($name eq "");

    # gather the orbital params
    $i = &s(60,68) +  0;
    $O = &s(49,57) +  0;
    $o = &s(38,46) +  0;
    $a = &s(93,103) + 0;
    $e = &s(71,79) +  0;
    $M = &s(27,35) +  0;
    $H = &s(9,13) +   0;
    $G = &s(15,19) +  0;

    $cent = &s(21,21);
    $TY = &s(22,23) + 0;
    $TY += 1800 if ($cent =~ /I/i);
    $TY += 1900 if ($cent =~ /J/i);
    $TY += 2000 if ($cent =~ /K/i);
    $TM = &mpcdecode (&s(24,24)) + 0;
    $TD = &mpcdecode (&s(25,25)) + 0;

    # decide whether it's ever bright
    $per = $a*(1 - $e);
    $aph = $a*(1 + $e);
    if ($per < 1.1 && $aph > .9) {
	$fd = BRT;	# might be in the back yard some day :-)
    } else {
	$maxmag = $H + 5*&log10($per*&absv($per-1));
	$fd = $maxmag > $dimmag ? DIM : BRT;
    }

    # print
    print $fd "$name";
    print $fd ",e,$i,$O,$o,$a,0,$e,$M,$TM/$TD/$TY,2000.0,$H,$G\n";
}

# remove fetched files, if any
unlink $MPCFILE;
unlink $MPCZIPFILE;

exit;

# like substr($_,first,last), but one-based.
sub s
{
    substr ($_, $_[0]-1, $_[1]-$_[0]+1);
}

# return log base 10
sub log10
{
    .43429*log($_[0]);
}

# return absolute value
sub absv
{
    $_[0] < 0 ? -$_[0] : $_[0];
}

# return decoded value
sub mpcdecode
{
    my $x = $_[0];
    $x =~ /\d/ ? $x : sprintf "%d", 10 + ord ($x) - ord ("A");
}

# return min of two values
sub min
{
    $_[0] < $_[1] ? $_[0] : $_[1];
}

# print usage message then die
sub usage
{
    my $base = $0;
    $base =~ s#.*/##;
    print "Usage: $base [-f] <base>\n";
    print "$ver\n";
    print "Purpose: convert $MPCFILE to 2 .edb files.\n";
    print "Options:\n";
    print "  -f: first ftp $MPCFILE from $MPCSITE, else read from stdin\n";
    print "Creates two files:\n";
    print "  <base>.edb:     all asteroids ever brighter than $dimmag\n";
    print "  <base>_dim.edb: all asteroids never brighter than $dimmag\n";

    exit 1;
}

# get and unzip the data
sub fetch
{
    # transfer
    print "Getting $MPCFTPDIR/$MPCZIPFILE from $MPCSITE...\n";
    open ftp, "|ftp -n $MPCSITE" or die "Can not ftp $MPCSITE";
    print ftp "user anonymous xephem\@clearskyinstitute.com\n";
    print ftp "binary\n";
    print ftp "cd $MPCFTPDIR\n";
    print ftp "get $MPCZIPFILE\n";
    close ftp;

    # extract into current dir
    print "Decompressing $MPCZIPFILE...\n";
    !system "unzip -j $MPCZIPFILE" or die "$MPCZIPFILE: unzip failed\n";
    -s $MPCFILE or die "$MPCFILE: failed to create from unzip $MPCZIPFILE\n";
}

# For RCS Only -- Do Not Edit
# @(#) $RCSfile: mpcorb2edb.pl,v $ $Date: 2001/08/15 05:35:44 $ $Revision: 1.7 $ $Name:  $