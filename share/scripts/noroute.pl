#!/usr/bin/perl
#######################################################
#
# noroute.pl is an demo script module for Yate using Yate.pm
# Copyright Anthony Minessale II <anthmct@yahoo.com>
# This Module is released under the GNU Public License
# If you find this software useful, donations are welcome
# at paypal:anthmct@yahoo.com
#
$|=1;
use lib 'scripts/';
use Yate;
use Data::Dumper;

sub demo($) {
  my $message = shift;
  printf STDERR "hello time is %s and binded is %s i'll return undef so the next module gets a chance....\n",
     $message->param("time"),$message->header("binded");
#     return ("true");
  return undef;
}

sub demo2($) {
  my $message = shift;
  printf STDERR "hello again time is %s and binded is %s I'll return false to put and end to it.\n",
    $message->param("time"),$message->header("binded");
  return ("true","tone/dial");
}

my $message = new Yate();
$message->install("engine.timer",\&demo);
$message->install("call.route",\&demo2);
$message->listen();
