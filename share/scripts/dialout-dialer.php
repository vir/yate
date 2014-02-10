#!/usr/bin/php -q
<?php
/**
 * dialout-dialer.php
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

/* Dialout IVR for the Yate PHP interface
   It will be started from dialout-scheduler.php
*/
require_once("libyate.php");

/* Always the first action to do */
Yate::Init();

/* Uncomment next line to get debugging messages */
//Yate::Debug(true);

$prompt = "";
//$prompt = "wave/play/sounds/dialoutprompt.au";

$running = true;
$done = false;
$ourcallid = "dialout/" . uniqid(rand(),1);
$partycallid = "";

$caller = "";
$callername = "";
$called = "";

// Initialization code called after the called party presses a key
function startRealJob()
{
    global $partycallid;
    global $caller;
    global $called;

    // NOTE: put your state init code here
}

// Got a DTMF
function gotDTMF($text)
{
    global $done;
    if (!$done) {
	$done = true;
	startRealJob();
	return;
    }

    // NOTE: put your DTMF handling code here
}

// Call failed
function callFailed($error,$reason = "")
{
    global $running;
    global $done;
    if ($done)
	return;
    if ($error == "")
	$error = $reason;
    if ($error == "Normal Clearing")
	$error = "";
    Yate::Debug("callFailed: '$error'");
    $running = false;
    $done = true;
    // NOTE: You must place your real handling code here
}

// Succeeded call.execute, prepare for more activity
function callExecuting($peerid)
{
    global $ourcallid;
    global $partycallid;
    Yate::Debug("callExecuting: '$peerid'");
    $partycallid = $peerid;
    // Install handlers for the wave end and dtmf notify messages
    Yate::Install("chan.dtmf",90,"peerid",$ourcallid);
    Yate::Install("call.answered",90,"peerid",$ourcallid);
    Yate::Install("chan.notify",90,"id",$ourcallid);
}

// Called party answered, ask it to press a number key to check it's human
function callAnswered()
{
    global $ourcallid;
    global $prompt;
    global $done;
    Yate::Debug("callAnswered");
    if ($prompt != "") {
	$m = new Yate("chan.attach");
	$m->id = "";
	$m->SetParam("id",$ourcallid);
	$m->SetParam("source",$prompt);
	$m->SetParam("single",true);
	$m->SetParam("notify",$ourcallid);
	$m->Dispatch();
    }
    else
	$done = true;
}

Yate::SetLocal("id",$ourcallid);
Yate::SetLocal("disconnected","true");
Yate::Install("chan.disconnected",90,"id",$ourcallid);

// The main loop. We pick events and handle them
while ($running) {
    $ev=Yate::GetEvent();
    if ($ev === false)
	break;
    if ($ev === true)
	continue;
    switch ($ev->type) {
	case "incoming":
	    switch ($ev->name) {
		case "call.execute":
		    $caller = $ev->GetValue("caller");
		    $callername = $ev->GetValue("callername");
		    $called = $ev->GetValue("called");
		    $ev->handled = true;

		    // Dispatch outgoing call.execute before acknowledging this one
		    $m = new Yate("call.execute");
		    $m->SetParam("id",$ourcallid);
		    $m->SetParam("callto",$ev->GetValue("direct"));
		    $m->SetParam("caller",$caller);
		    $m->SetParam("callername",$callername);
		    $m->SetParam("called",$called);
		    // No need to track us, this is an utility channel
		    $m->SetParam("cdrtrack",false);
		    // Active DTMF detector on outgoing call leg
		    $m->SetParam("tonedetect_out",true);
		    $m->Dispatch();
		    break;
		case "call.answered":
		    callAnswered();
		    $ev->handled = true;
		    break;
		case "chan.notify":
		    callFailed("noanswer");
		    $ev->handled = true;
		    break;
		case "chan.dtmf":
		    gotDTMF($ev->GetValue("text"));
		    $ev->handled = true;
		    break;
		case "chan.disconnected":
		    callFailed($ev->GetValue("reason"));
		    /* Put it back running to avoid double destruction... */
		    $running = true;
		    $ev->handled = true;
		    break;
	    }
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    if ($ev->name == "call.execute") {
		if ($ev->handled) {
		    callExecuting($ev->GetValue("peerid"));
		    continue;
		}
		callFailed($ev->GetValue("error"),$ev->GetValue("reason"));
	    }
	    break;
	default:
	    Yate::Debug("PHP Event: " . $ev->type);
    }
}

Yate::Debug("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
