#! /bin/bash

# yatemon.sh
# This file is part of the YATE Project http://YATE.null.ro
#
# Yet Another Telephony Engine - a fully featured software PBX and IVR
# Copyright (C) 2010-2014 Null Team
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


# Use: yatemon.sh [-p pid]
# Displays a history of Yate's memory and thread CPU usage
# You will need a very wide console

pid=`pidof yate 2>/dev/null`

if [ "X$1" = "X-p" ]; then
    pid="$2"
fi

if ! kill -0 "$pid" 2> /dev/null; then
    echo "Invalid PID: $pid" >&2
    exit 1
fi

top -p "$pid" -H -d 5 -b | gawk --assign pid=$pid '
function clearStats() {
    time = "";
    vmm = "";
    rss = "";
    threads["main()"] = 0;
    for (t in threads)
	threads[t] = 0;
}

function printHead()
{
    ccount = length(threads);
    head = 1;
    l1 = sprintf(fmt,"Time","|","Virt","|","Res");
    l2 = sprintf(fmt,"","|","","|","");
    l3 = sprintf(fmt,"--------","+","------","+","------");
    for (t in threads) {
	l1 = l1 sprintf(hfmt,"|",substr(t,1,8));
	l2 = l2 sprintf(hfmt,"|",substr(t,9,8));
	l3 = l3 sprintf(hfmt,"+","--------");
    }
    print "\n" l3 "\n" l1 "\n" l2 "\n" l3;
}

BEGIN {
    ccount = 0;
    offs = -1;
    head = 1;
    fmt = "%8s%1s%6s%1s%6s";
    hfmt = "%1s%-8s";
    tfmt = "%1s%8s";
    headlines = 24;
    clearStats();
}

/^top - / {
    time=substr($0,7,8);
}

/^ *[0-9]+ / {
    if (offs < 0) {
	offs = 0;
	while (substr($0,offs+6,1) >= "0" && substr($0,offs+6,1) <= "9")
	    offs++;
    }
    tid = substr($0,1,offs+5);
    gsub(/ */,"",tid);
    cpu = substr($0,offs+41,5);
    gsub(/ */,"",cpu);
    if (tid == pid) {
	vmm = substr($0,offs+23,6);
	gsub(/ */,"",vmm);
	rss = substr($0,offs+29,6);
	gsub(/ */,"",rss);
	name = "main()";
    }
    else {
	name = substr($0,offs+62);
	gsub(/ *$/,"",name);
	gsub(/^ */,"",name);
    }
    if (name in threads)
	threads[name] += cpu;
    else
	threads[name] = cpu;
}

/^$/ {
    if (vmm != "") {
	if (head++ >= headlines || length(threads) != ccount)
	    printHead();
	buf = sprintf(fmt,time,"|",vmm,"|",rss);
	for (t in threads)
	    buf = buf sprintf(tfmt,"|",threads[t]);
	print buf;
	fflush();
	clearStats();
    }
}
'
