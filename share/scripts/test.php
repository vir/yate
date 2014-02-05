#!/usr/bin/php -q
<?php
/**
 * test.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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

/* Test script for the Yate PHP interface
   To test add in extmodule.conf

   [scripts]
   test.php=
*/
require_once("libyate.php");

/* Always the first action to do */
Yate::Init();

/* Install a handler for the engine generated timer message */
Yate::Install("engine.timer",10);

/* Create and dispatch an initial test message */
$m=new Yate("test");
$m->params["param1"]="val1";
$m->retval="ret_value";
$m->Dispatch();

Yate::GetLocal("engine.version");
Yate::GetLocal("engine.nodename");
Yate::GetLocal("engine.nosuch");
Yate::SetLocal("engine.nodename","error because is readonly");
Yate::GetLocal("config.nosuch");
Yate::GetLocal("config.localsym");
Yate::GetLocal("config.localsym.h323chan.yate");
Yate::SetLocal("config.localsym.h323chan.yate","r/o error too");

/* The main loop. We pick events and handle them */
for (;;) {
    $ev=Yate::GetEvent();
    /* If Yate disconnected us then exit cleanly */
    if ($ev === false)
        break;
    /* Empty events are normal in non-blocking operation.
       This is an opportunity to do idle tasks and check timers */
    if ($ev === true) {
        Yate::Output("PHP event: empty");
        continue;
    }
    /* If we reached here we should have a valid object */
    switch ($ev->type) {
	case "incoming":
	    Yate::Output("PHP Message: " . $ev->name . " id: " . $ev->id);
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    $ev->Acknowledge();
	    break;
	case "answer":
	    Yate::Output("PHP Answered: " . $ev->name . " id: " . $ev->id);
	    break;
	case "installed":
	    Yate::Output("PHP Installed: " . $ev->name);
	    break;
	case "uninstalled":
	    Yate::Output("PHP Uninstalled: " . $ev->name);
	    break;
	case "setlocal":
	    Yate::Output("PHP Parameter: ". $ev->name . "=" . $ev->retval . ($ev->handled ? " (OK)" : " (error)"));
	    break;
	default:
	    Yate::Output("PHP Event: " . $ev->type);
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
