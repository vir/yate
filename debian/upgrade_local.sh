#!/bin/sh
#
# (c) vir
#
# Last modified: 2013-04-15 15:10:46 +0400
#

DIR=../
ARCH=i386
INSTALLED=`dpkg -l | awk '/^ii/ && /yate/  { print $2 }'`
VERSION=`dpkg-parsechangelog | awk '/^Version:/ { print $2 }'`

LINE='sudo dpkg -i'
for p in $INSTALLED
do
	LINE="${LINE} ${DIR}${p}_${VERSION}_${ARCH}.deb"
done

echo "Executing: ${LINE}"
exec ${LINE}



