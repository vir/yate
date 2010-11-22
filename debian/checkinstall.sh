#!/bin/bash
#
# (c) vir
#
# Last modified: 2009-06-25 16:07:50 +0400
#

D=debian/tmp
LIST=debian/NOT-INSTALLED-LIST

ALLFILES=`find $D -type f -print | cut -d/ -f3- | grep -v '^usr/share/doc' | sed 's/usr\/lib\/yate\/client\//usr\/lib\/yate\//'`

echo -n > $LIST

for f in $ALLFILES
do
	if [ ! -e debian/*yate*/$f ]
	then
		echo "$f" >> $LIST
	fi
done


