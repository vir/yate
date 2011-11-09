#! /bin/sh

# Sample log and CDR rotator that creates files based on date and hour
# Assumes Yate writes to /var/log/yate and /var/log/yate-cdr.tsv

# You can place this script in /etc/cron.hourly and not use the system logrotate
# NOTE: Files are never deleted! Periodic cleanup is required

base="/var/log"
old="$base/yate-old"
ym=`date '+%Y-%m'`
d=`date '+%d'`
h=`date '+%H'`

mkdir -p "$old/$ym/$d"
mv "$base/yate" "$old/$ym/yate-$d-$h.log"
mv "$base/yate-cdr.tsv" "$old/$ym/$d/yate-cdr-$h.tsv"

/bin/kill -HUP `/bin/cat /var/run/yate.pid`
