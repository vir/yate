#!/bin/sh

# mkpatch.sh
# This file is part of the YATE Project http://YATE.null.ro
#
# Yet Another Telephony Engine - a fully featured software PBX and IVR
# Copyright (C) 2005-2014 Null Team
#
# This software is distributed under multiple licenses;
# see the COPYING file in the main directory for licensing
# information for this specific distribution.
#
# This use of this software may be subject to additional restrictions.
# See the LEGAL file in the main directory for details.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


# Use:
#    cd base-source-directory
#    mkpatch >file.patch
# Before editing files create copies with .orig extension
#    cp -p somefile.c somefile.c.orig

find . -name '*.orig' -exec echo diff -u {} {} \; | sed 's+\.orig$++' | /bin/sh
