#! /bin/bash

# tts.sh
# This file is part of the YATE Project http://YATE.null.ro
#
# Yet Another Telephony Engine - a fully featured software PBX and IVR
# Copyright (C) 2014-2016 Null Team
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


# Shell script for the Yate external module interface
# To test add a route to: external/play/tts.sh

# Text-To-Speech using Festival http://www.cstr.ed.ac.uk/projects/festival/
# To improve voice quality please tweak Festival itself
# For non-commercial, non-military use consider adding MBROLA http://tcts.fpms.ac.be/synthesis/mbrola.html

# Parameters:
#   text=Text to speak, filename or command
#   mode=file, pipe or text (default)
#   voice=Name of Festival voice to use (optional)

# Examples using regexroute.conf
#   external/play/tts.sh;voice=kal_diphone;mode=file;text=/etc/issue.net
#   external/play/tts.sh;mode=pipe;text=date '+Today is %A, %B %e %Y, %I:%M %p'
#   external/play/tts.sh;voice=JuntaDeAndalucia_es_sf_diphone;text=El ingenioso hidalgo don Quijote de la Mancha


read
id="${REPLY#*:*}"; id="${id%%:*}"
params=":${REPLY#*:*:*:*:*:}:"
text="${params#*:text=}"; text="${text%%:*}"
mode="${params#*:mode=}"; mode="${mode%%:*}"
voice="${params#*:voice=}"; voice="${voice%%:*}"
if [ "X$text" = "X" ]; then
    echo "%%<message:$id:false::"
    exit
fi
echo "%%<message:$id:true::"
i=0
while [ "X${text:$i:1}" != "X" ]; do
    if [ "X${text:$i:1}" = "X%" ]; then
	p=$(($i+1))
	c="${text:$p:1}"
	case "X$c" in
	    XI)
		c=" "
		;;
	    Xz)
		c=":"
		;;
	    X%)
		;;
	    *)
		c=""
		;;
	esac
	p=$(($i+2))
	text="${text:0:$i}${c}${text:$p}"
    fi
    i=$(($i+1))
done
if [ "X$voice" != "X" ]; then
    voice="-eval (voice_${voice})"
fi

dd if=/dev/zero bs=8000 count=1 >&4 2>/dev/null
if [ "X$mode" = "Xfile" ]; then
    size=`stat -c '%s' "$text"`
    if [ "$size" -lt 2048 ]; then
	echo "%%>output:TTS File: $text"
	text2wave -scale 2 -F 8000 -otype raw $voice >&4 <"$text"
    fi
else
    if [ "X$mode" = "Xpipe" ]; then
	text=`/bin/bash -c "$text" 2>/dev/null`
    fi
    if [ "X$text" != "X" ]; then
	echo "%%>output:TTS Text: $text"
	echo "$text" | text2wave -scale 2 -F 8000 -otype raw $voice >&4
    fi
fi
dd if=/dev/zero bs=8000 count=6 >&4 2>/dev/null
