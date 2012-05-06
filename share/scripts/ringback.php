#!/usr/bin/php -q
<?php
/**
 * ringback.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2010-2012 Null Team
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

/* Ringback provider for the Yate PHP interface
   Add in extmodule.conf

   [scripts]
   ringback.php=RINGBACK

   where RINGBACK is a wave or autorepeat temporary tone resource like:
      tone/*ring  (this is the default)
      wave/play/path/to/custom.au
*/
require_once("libyate.php");

// A fixed format is needed as we can't know what the called will offer
$mediafmt = "mulaw";

Yate::Init();
//Yate::Debug(true);

$ringback = Yate::Arg();
if ($ringback == "")
    $ringback = "tone/*ring";

Yate::Install("call.ringing",50);
Yate::Watch("call.ringing");
Yate::SetLocal("restart",true);

for (;;) {
    $ev=Yate::GetEvent();
    if ($ev === false)
        break;
    if ($ev === true)
        continue;
    $id = $ev->GetValue("peerid");
    if ($ev->type == "incoming") {
	if ($ev->GetValue("earlymedia") == "false" &&
		$ev->GetValue("rtp_forward") != "yes") {
	    Yate::Debug("Preparing fake $mediafmt ringback to $id");
	    $ev->SetParam("earlymedia",true);
	    $ev->SetParam("formats",$mediafmt);
	    $ev->SetParam("ringback",$ringback);
	}
	$ev->Acknowledge();
    }
    else if ($ev->type == "answer") {
	if ($ev->handled) {
	    $ring = $ev->GetValue("ringback");
	    if ($id != "" && $ring != "") {
		Yate::Debug("Faking ringback $ring to $id");
		$m = new Yate("chan.masquerade");
		$m->id = "";
		$m->SetParam("message","chan.attach");
		$m->SetParam("id",$id);
		$m->SetParam("replace",$ring);
		$m->SetParam("autorepeat",true);
		$m->SetParam("single",true);
		$m->Dispatch();
	    }
	}
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
