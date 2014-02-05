#!/usr/bin/php -q
<?php
/**
 * overlapped.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2005-2014 Null Team
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

/* Simple test overlapped dialer for the Yate PHP interface
   To test add in regexroute.conf

   ... fixed-length matching rules here
   ${overlapped}yes=goto overlapped
   ^NNN$=external/nodata/overlapped.php
   ... variable or inexact matching rules here

   [overlapped]
   .*=;error=incomplete

   where NNN is the number you want to assign for the secondary dialer
*/
require_once("libyate.php");

$ourcallid = "over/" . uniqid(rand(),1);
$partycallid = "";
$state = "call";
$collect = "";
// Queued, not yet used DTMFs
$queue = "";
// Initial call.execute message used when re-routing
$executeParams = array();
// Final digit detected: no more routes allowed
$final = false;
// Don't answer the call, don't use prompts
$routeOnly = true;
// Tone language from call.execute
$lang = "";
// Desired interdigit timer
$interdigit = 0;
// Actual timer value
$timer = 0;

function setState($newstate)
{
    global $ourcallid;
    global $state;
    global $collect;
    global $routeOnly;
    global $lang;

    // are we exiting?
    if ($state == "")
	return;

    Yate::Debug("Overlapped setState('" . $newstate . "') in state: " . $state);

    // always obey a return to prompt
    if ($newstate == "prompt") {
	$state = $newstate;
	$m = new Yate("chan.attach");
	$m->params["id"] = $ourcallid;
	$m->params["source"] = "tone/dial";
	if ($lang != "")
	    $m->params["lang"] = $lang;
	$m->params["consumer"] = "wave/record/-";
	$m->params["maxlen"] = 320000;
	$m->params["notify"] = $ourcallid;
	$m->Dispatch();
	return;
    }

    if ($newstate == $state)
	return;

    if ($routeOnly) {
	$state = $newstate;
	return;
    }

    switch ($newstate) {
	case "goodbye":
	    $m = new Yate("chan.attach");
	    $m->params["id"] = $ourcallid;
	    $m->params["source"] = "tone/congestion";
	    if ($lang != "")
		$m->params["lang"] = $lang;
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 32000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "routing":
	    $m = new Yate("chan.attach");
	    $m->params["id"] = $ourcallid;
	    $m->params["source"] = "tone/noise";
	    if ($lang != "")
		$m->params["lang"] = $lang;
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 320000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "noroute":
	    $m = new Yate("chan.attach");
	    $m->params["id"] = $ourcallid;
	    $m->params["source"] = "tone/outoforder";
	    if ($lang != "")
		$m->params["lang"] = $lang;
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 32000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
    }
    $state = $newstate;
}

function routeTo($num)
{
    global $final;
    global $executeParams;
    setState("routing");
    $m = new Yate("call.route");
    $m->params = $executeParams;
    $m->params["called"] = $num;
    $m->params["overlapped"] = $final ? "no" : "yes";
    $m->Dispatch();
}

function gotNotify()
{
    global $ourcallid;
    global $state;

    Yate::Debug("Overlapped gotNotify() in state: " . $state);

    switch ($state) {
	case "prompt":
	    setState("goodbye");
	    break;
	default:
	    setState("");
	    break;
    }
}

function gotDTMF($dtmfs)
{
    global $state;
    global $collect;
    global $queue;
    global $final;
    global $timer;
    global $routeOnly;

    Yate::Debug("Overlapped gotDTMF('$dtmfs') in state: '$state' collected: '$collect' queued: '$queue'");

    $queue .= $dtmfs;
    $timer = 0;
    if ($state == "routing")
	return;
    $route = false;
    while (true) {
	$n = strlen($queue);
	if ($n < 1)
	    break;
	if ($state == "call") {
	    // First call: use all digits to route
	    $route = true;
	    if ($queue[$n - 1] != ".")
		$collect .= $queue;
	    else {
		$collect .= substr($queue,0,$n - 1);
		$final = true;
	    }
	    $queue = "";
	    break;
	}
	if ($queue[0] == "*") {
	    $queue = substr($queue,1);
	    setState("");
	    continue;
	}
	if ($queue[0] == "#") {
	    Yate::Output("Overlapped clearing already collected: '$collect'");
	    $collect = "";
	    $queue = substr($queue,1);
	    if (!$routeOnly)
		setState("prompt");
	    continue;
	}
	if (!$final) {
	    $route = true;
	    $dtmf = $queue[0];
	    $queue = substr($queue,1);
	    $final = ($dtmf == ".");
	    if (!$final) {
		$collect .= $dtmf;
		// Check for next char now
		$n = strlen($queue);
		if ($n > 0 && $queue[0] == ".") {
		    $final = true;
		    $queue = substr($queue,1);
		}
	    }
	}
	break;
    }
    if ($route) {
	if ($final)
	    Yate::Debug("Overlapped got final digit. Collected: '$collect' queued: '$queue'");
	routeTo($collect);
    }
}

function timerTick()
{
    global $state;
    global $final;
    global $interdigit;
    global $timer;
    global $collect;
    global $queue;

    if ($interdigit <= 0)
	return;

    if ($timer++ >= $interdigit) {
	$timer = 0;
	Yate::Debug("Overlapped timeout in state: '$state' collected: '$collect' queued: '$queue'");
	if ($state == "routing")
	    return;
	$interdigit = 0;
	$final = true;
	$collect .= $queue;
	routeTo($collect);
    }
}

function endRoute($callto,$ok,$err,$params)
{
    global $partycallid;
    global $collect;
    global $final;
    global $queue;
    global $routeOnly;
    global $state;

    if ($ok && ($callto != "-") && ($callto != "error")) {
	Yate::Output("Overlapped got route: '$callto' for '$collect'");
	$m = new Yate("chan.masquerade");
	$m->params = $params;
	$m->params["message"] = "call.execute";
	$m->params["complete_minimal"] = true;
	$m->params["id"] = $partycallid;
	$m->params["callto"] = $callto;
	$m->Dispatch();
	if (strlen($queue)) {
	    // Masquerade the remaining digits
	    // TODO: wait for call.execute to be processed to do that?
	    $d = new Yate("chan.masquerade");
	    $d->params["message"] = "chan.dtmf";
	    $d->params["id"] = $partycallid;
	    $d->params["tone"] = $queue;
	    $d->Dispatch();
	}
	return;
    }
    if ($final) {
	Yate::Output("Overlapped got final error '$err' for '$collect'");
	Yate::SetLocal("reason",$err);
	setState("");
    }
    else if ($err != "incomplete") {
	Yate::Output("Overlapped got error '$err' for '$collect'");
	Yate::SetLocal("reason",$err);
	setState("");
	$final = true;
    }
    else {
	Yate::Debug("Overlapped still incomplete: '$collect'");
	if ($routeOnly)
	    setState("noroute");
	else
	    // Don't use setState: we don't want to change the prompt
	    $state = "prompt";
	// Check if got some other digits
	if ($queue != "")
	    gotDTMF("");
    }
}


/* Always the first action to do */
Yate::Init();

/* Comment next line to send output straight to log (no rmanager) */
Yate::Output(true);

/* Uncomment next line to get debugging messages */
// Yate::Debug(true);

/* Set tracking name for all installed handlers */
Yate::SetLocal("trackparam","overlapped.php");

/* Install filtered handlers for the DTMF and wave EOF messages */
Yate::Install("chan.dtmf",95,"targetid",$ourcallid);
Yate::Install("chan.notify",95,"targetid",$ourcallid);

/* The main loop. We pick events and handle them */
while ($state != "") {
    $ev=Yate::GetEvent();
    /* If Yate disconnected us then exit cleanly */
    if ($ev === false)
	break;
    if ($ev === true)
	continue;
    /* If we reached here we should have a valid object */
    switch ($ev->type) {
	case "incoming":
	    switch ($ev->name) {
		case "call.execute":
		    $partycallid = $ev->GetValue("id");
		    $ev->params["targetid"] = $ourcallid;
		    $routeOnly = !Yate::Str2bool($ev->getValue("accept_call"));
		    $interdigit = 1 * $ev->GetValue("interdigit",$interdigit);
		    $autoanswer = false;
		    $callednum = "";
		    $lang = $ev->getValue("lang");
		    if ($routeOnly) {
			$callednum = $ev->GetValue("called");
			if ($callednum == "off-hook")
			    $callednum = "";
		    }
		    else
			$autoanswer = ($ev->GetValue("called") != "off-hook");
		    $executeParams =  $ev->params;
		    $ev->handled = true;
		    // we must ACK this message before dispatching a call.answered
		    $ev->Acknowledge();
		    // we already ACKed this message
		    $ev = false;
		    if ($interdigit > 0)
			Yate::Watch("engine.timer");
		    if ($autoanswer) {
			$m = new Yate("call.answered");
			$m->params["id"] = $ourcallid;
			$m->params["targetid"] = $partycallid;
			$m->Dispatch();
		    }
		    // Route initial called number
		    if (strlen($callednum))
			gotDTMF($callednum);

		    if (!$routeOnly)
			setState("prompt");
		    break;

		case "chan.notify":
		    gotNotify();
		    $ev->handled = true;
		    break;

		case "chan.dtmf":
		    gotDTMF($ev->GetValue("text"));
		    $ev->handled = true;
		    break;
	    }
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    switch ($ev->name) {
		case "call.route":
		    endRoute($ev->retval,$ev->handled,$ev->GetValue("error","noroute"),$ev->params);
		    break;

		case "engine.timer":
		    timerTick();
		    break;
	    }
	    break;
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
