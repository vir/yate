#!/usr/bin/php -q
<?php
/**
 * dialout-scheduler.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2008-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

/* Sample dialout scheduler script
   To use add in extmodule.conf

   dialout-scheduler.php=
*/
require_once("libyate.php");

$caller = "12345";
$callername = "Sample dialout";

/* Always the first action to do */
Yate::Init();

/* Uncomment next line to get debugging messages */
//Yate::Debug(true);

// Start routing a single call
function startRouting($called)
{
    global $caller;
    global $callername;
    Yate::Debug("Routing dialout number='$called'");
    $m = new Yate("call.route");
    $m->SetParam("caller",$caller);
    $m->SetParam("callername",$callername);
    $m->SetParam("called",$called);
    $m->Dispatch();
}

// Initiate a call once we know the target
function callInitiate($target,$ev)
{
    Yate::Debug("Initiating dialout call to '$target'");
    $m = new Yate("call.execute");
    $m->id = "";
    $m->SetParam("callto","external/nodata/dialout-dialer.php");
    $m->SetParam("direct",$target);
    $m->SetParam("caller",$ev->GetValue("caller"));
    $m->SetParam("callername",$ev->GetValue("callername"));
    $m->SetParam("called",$ev->GetValue("called"));
    $m->Dispatch();
}

// Routing failed, the number may be invalid
function routeFailure($error,$ev)
{
    $number = $ev->GetValue("called");
    Yate::Output("Failed routing dialout to '$number' with error '$error'");
}

// Check if we have any scheduled calls to start
// NOTE: You must place your real algorithm here
function checkSchedule()
{
    // pretend we get a number to dial only ever 3rd attempt
    if (rand(0,99) > 33)
	return;
    // generate a random number and route to it
    $number = rand(10000,30000);
    startRouting($number);
}

// Only install a handler for the timer message
Yate::Install("engine.timer");
// Ask Yate to restart this script if it dies unexpectedly
Yate::SetLocal("restart",true);

// The main loop. We pick events and handle them
for (;;) {
    $ev=Yate::GetEvent();
    if ($ev === false)
	break;
    if ($ev === true)
	continue;
    switch ($ev->type) {
	case "incoming":
	    // We are sure it's the timer message
	    $ev->Acknowledge();
	    // Do the processing after letting the message return
	    switch (substr($ev->GetValue("time"),-1)) {
		/* Only check when second ends in 2 or 7 */
		case "2":
		case "7":
		    checkSchedule();
		    break;
	    }
	    break;
	case "answer":
	    // Use the return of the routing message
	    if ($ev->name == "call.route") {
		if ($ev->handled && ($ev->retval != "") && ($ev->retval != "-") && ($ev->retval != "error"))
		    callInitiate($ev->retval,$ev);
		else
		    routeFailure($ev->GetValue("error"),$ev);
	    }
	    break;
	default:
	    Yate::Debug("PHP Event: " . $ev->type);
    }
}

Yate::Debug("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
