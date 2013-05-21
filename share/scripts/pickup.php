#!/usr/bin/php -q
<?php
/**
 * pickup.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2008-2012 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/* Call pickup script for the Yate PHP interface
   Add in extmodule.conf

   [scripts]
   pickup.php=PREFIX

   where PREFIX is whatever you need to dial in front of the number to pick
*/
require_once("libyate.php");

Yate::Init();
//Yate::Debug(true);

function doRoute(&$ev)
{
    global $prefix, $calls;
    $called = $ev->GetValue("called");
    // Check if called number starts with prefix
    $len = strlen($prefix);
    if (substr($called,0,$len) != $prefix)
	return;
    // Get rid of prefix and search the active calls
    $called = substr($called,$len);
    if ($called == "")
	return;
    // Have to search, cannot use number as array key because it's not unique
    $chan = array_search($called,$calls);
    Yate::Debug("For picking up '$called' found channel '$chan'");
    if ($chan == "")
	return;
    // Found! Route to it and signal success
    $ev->retval = "pickup/$chan";
    $ev->handled = true;
}

function doCdr($ev)
{
    global $calls;
    $chan = $ev->GetValue("chan");
    if ($chan == "")
	return;
    switch ($ev->GetValue("operation")) {
	case "initialize":
	    // Remember the called number for this call leg
	    $calls[$chan] = $ev->GetValue("called");
	    break;
	case "finalize":
	    // Forget about the call leg that got hung up
	    unset($calls[$chan]);
	    break;
    }
}

$calls = array();
$prefix = Yate::Arg();
if ($prefix == "") {
    // Hope this is a sensible default - else set a prefix from extmodule.conf
    $prefix = "#8";
    Yate::Output("Pickup prefix not set, using default '$prefix'");
}
Yate::SetLocal("trackparam","pickup.php");
Yate::Install("call.route",35);
Yate::Install("call.cdr",110,"direction","outgoing");
Yate::SetLocal("restart",true);

for (;;) {
    $ev=Yate::GetEvent();
    if ($ev === false)
        break;
    if ($ev === true)
        continue;
    if ($ev->type == "incoming") {
	switch ($ev->name) {
	    case "call.route":
		doRoute($ev);
		break;
	    case "call.cdr":
		doCdr($ev);
		break;
	}
	$ev->Acknowledge();
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
