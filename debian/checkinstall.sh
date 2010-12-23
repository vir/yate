#!/bin/bash
#
# (c) vir
#
# Last modified: 2010-12-23 10:33:10 +0300
#

D=debian/tmp
LIST=debian/NOT-INSTALLED-LIST

ALLFILES=`find $D -type f -print | cut -d/ -f3- | grep -v '^usr/share/doc'`


echo -n > $LIST

for f in $ALLFILES
do
# some files moved from .../client/, some not - check both
	f2=`echo $f | sed 's/usr\/lib\/yate\/client\//usr\/lib\/yate\//'`
	if [ ! -e debian/*yate*/$f -a ! -e debian/*yate*/$f2 ]
	then
		echo "$f" >> $LIST
	fi
done

echo "*** *** *** NOT PACKAGED FILES: *** *** ***"
cat $LIST
echo "~~~ ~~~ ~~~ ~~~ ~~~ ~~~ ~~~ ~~~ ~~~ ~~~ ~~~"


