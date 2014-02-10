#!/usr/bin/php -q
<?php
/**
 * detector.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
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

/* Script to automatically initiate fax receiving
   To test add in extmodule.conf

   [scripts]
   detector.php=
*/
require_once("libyate.php");

$detHelp = "  detect [on|off|chanid]\r\n";

// set to true in the next line to work from startup
$detect = false;

function onCommand($l,&$retval)
{
    global $detect;
    if ($l == "detect") {
	$retval = $detect ? "active" : "inactive";
	$retval = "Fax detection on new calls is $retval\r\n";
	return true;
    }
    if (strpos($l,"detect ") === 0) {
	$d = substr($l,7);
	if ($d == "on") {
	    $detect = true;
	    $retval = "Detection activated on all new calls\r\n";
	    return true;
	}
	if ($d == "off") {
	    $detect = false;
	    $retval = "Detection on new calls disabled\r\n";
	    return true;
	}
	$m = new Yate("chan.masquerade");
	$m->id = ""; // don't notify about message result
	$m->params["message"] = "chan.record";
	$m->params["id"] = $d;
	$m->params["call"] = "tone/";
	$m->Dispatch();
	$retval = "Starting detection on $d\r\n";
	return true;
    }
    return false;
}

function oneCompletion(&$ret,$str,$part)
{
    if (($part != "") && (strpos($str,$part) !== 0))
	return;
    if ($ret != "")
	$ret .= "\t";
    $ret .= $str;
}

function onComplete(&$ev,$l,$w)
{
    if ($l == "")
	oneCompletion($ev->retval,"detect",$w);
    else if ($l == "help")
	oneCompletion($ev->retval,"detect",$w);
    else if ($l == "detect") {
	oneCompletion($ev->retval,"on",$w);
	oneCompletion($ev->retval,"off",$w);
	$ev->params["complete"] = "channels";
    }
}

function onHelp($l,&$retval)
{
    global $detHelp;
    if ($l) {
	if ($l == "detect") {
	    $retval = "${detHelp}Activate, deactivate or query status of fax detection\r\n";
	    return true;
	}
	return false;
    }
    $retval .= $detHelp;
    return false;
}

function onExecute($id,$ev)
{
    if (!$id)
	return;
    if (strpos($id,"fax/") === 0)
	return;
    if (strpos($ev->GetValue("callto"),"fax/") === 0)
	return;
    $num = $ev->GetValue("billid") . "_" .$ev->GetValue("caller") . "-" . $ev->GetValue("called");
    $m = new Yate("chan.masquerade");
    $m->id = ""; // don't notify about message result
    $m->params["message"] = "chan.attach";
    $m->params["id"] = $id;
    $m->params["sniffer"] = "tone/*";
    $m->params["fax_divert"] = "fax/receive/spool/$num.tif";
    $m->Dispatch();
    Yate::Output("Starting detection on $id");
}

function onFax($id)
{
    Yate::Output("Fax call detected on $id");
    $m = new Yate("chan.masquerade");
    $m->id = ""; // don't notify about message result
    $m->params["message"] = "call.execute";
    $m->params["id"] = $id;
    // FIXME: generate an unique name for each call
    $m->params["callto"] = "fax/receive/spool/fax-rx.tif";
    $m->params["reason"] = "fax";
    $m->Dispatch();
}

/* Always the first action to do */
Yate::Init();

Yate::SetLocal("trackparam","detector.php");
Yate::Install("engine.command",85);
Yate::Install("engine.help",125);
Yate::Install("call.execute",25);
Yate::Install("call.fax",25);

/* Create and dispatch an initial test message */
/* The main loop. We pick events and handle them */
for (;;) {
    $ev=Yate::GetEvent();
    /* If Yate disconnected us then exit cleanly */
    if ($ev === false)
        break;
    /* Empty events are normal in non-blocking operation.
       This is an opportunity to do idle tasks and check timers */
    if ($ev === true) {
        continue;
    }
    /* If we reached here we should have a valid object */
    switch ($ev->type) {
	case "incoming":
	    switch ($ev->name) {
		case "engine.command":
		    if ($ev->GetValue("line"))
			$ev->handled = onCommand($ev->GetValue("line"),$ev->retval);
		    else
			onComplete($ev,$ev->GetValue("partline"),$ev->GetValue("partword"));
		    break;
		case "engine.help":
		    $ev->handled = onHelp($ev->GetValue("line"),$ev->retval);
		    break;
		case "call.execute":
		    if ($detect && $ev->GetValue("callto"))
			onExecute($ev->GetValue("id"),$ev);
		    break;
		case "call.fax":
		    onFax($ev->GetValue("id"));
		    break;
	    }
	    $ev->Acknowledge();
	    break;
	case "answer":
	    Yate::Debug("PHP Answered: " . $ev->name . " id: " . $ev->id);
	    break;
	case "installed":
	    Yate::Debug("PHP Installed: " . $ev->name);
	    break;
	case "uninstalled":
	    Yate::Debug("PHP Uninstalled: " . $ev->name);
	    break;
	default:
	    Yate::Output("PHP Event: " . $ev->type);
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
