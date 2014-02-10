#!/usr/bin/php -q
<?php
/**
 * voicemail.php
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

/* Simple voicemail manipulation for the Yate PHP interface
   To use add in regexroute.conf

   ^NNN$=external/nodata/voicemail.php

   where NNN is the number you want to assign to handle voicemail

   Sample prompt sound files can be found at:
   http://yate.null.ro/tarballs/yate-sounds.tar.gz
*/
require_once("libyate.php");
require_once("libvoicemail.php");

/* Always the first action to do */
Yate::Init();

/* Uncomment next line to get output in rmanager, not just in logs */
//Yate::Output(true);

/* Uncomment next line to get debugging messages */
//Yate::Debug(true);

$ourcallid = "voicemail/" . uniqid(rand(),1);
$partycallid = "";
$state = "call";
$dir = "";
$mailbox = "";
$collect_user = "";
$collect_pass = "";
$files = array();
$current = 0;

/* Ask the user to enter number */
function promptUser()
{
    global $collect_user;
    global $vm_base;
    $collect_user = "";
    $m = new Yate("chan.attach");
    $m->params["source"] = "wave/play/$vm_base/usernumber.slin";
    $m->Dispatch();
}

/* Ask for the password */
function promptPass()
{
    global $collect_pass;
    global $vm_base;
    $collect_pass = "";
    $m = new Yate("chan.attach");
    $m->params["source"] = "wave/play/$vm_base/password.slin";
    $m->Dispatch();
}

/* Perform machine status transitions */
function setState($newstate)
{
    global $ourcallid;
    global $partycallid;
    global $state;
    global $mailbox;
    global $vm_base;
    global $dir;
    global $files;
    global $current;

    // are we exiting?
    if ($state == "")
	return;

    Yate::Debug("setState('$newstate') state: $state");

    // always obey a return to prompt
    switch ($newstate) {
	case "prompt":
	    $state = $newstate;
	    $m = new Yate("chan.attach");
	    $m->params["source"] = "wave/play/$vm_base/menu.slin";
	    $m->Dispatch();
	    $m = new Yate("chan.attach");
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 320000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    return;
	case "listen":
	    $state = $newstate;
	    if (vmSetMessageRead($mailbox,$files[$current])) {
		$m = new Yate("user.update");
		$m->id = "";
		$m->params["user"] = $mailbox;
		$m->Dispatch();
	    }
	    $f = $dir . "/" . $files[$current];
	    $m = new Yate("chan.attach");
	    if (is_file("$f"))
		$m->params["source"] = "wave/play/$f";
	    else
		$m->params["source"] = "wave/play/$vm_base/deleted.slin";
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 100000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    return;
    }

    if ($newstate == $state)
	return;

    $st = strpos($newstate,",");
    $st = ($st !== false) ? substr($newstate,0,$st) : $newstate;
    switch ($st) {
	case "user":
	    promptUser();
	    $m = new Yate("chan.attach");
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 160000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "pass":
	    promptPass();
	    $m = new Yate("chan.attach");
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 160000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "record":
	    $m = new Yate("chan.attach");
	    $m->params["source"] = "wave/play/-";
	    $m->params["consumer"] = "wave/record/$dir/greeting.slin";
	    $m->params["maxlen"] = 80000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "play":
	    $m = new Yate("chan.attach");
	    if (is_file("$dir/greeting.slin"))
		$m->params["source"] = "wave/play/$dir/greeting.slin";
	    else
		$m->params["source"] = "wave/play/$vm_base/nogreeting.slin";
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 100000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "goodbye":
	    $mailbox = "";
	    $m = new Yate("chan.attach");
	    $m->params["source"] = "tone/congestion";
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 32000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	default:
	    if (substr($st,0,5) == "play:") {
		$m = new Yate("chan.attach");
		$m->params["source"] = "wave/play/$vm_base/" . substr($st,5) . ".slin";
		$m->params["consumer"] = "wave/record/-";
		$m->params["notify"] = $ourcallid;
		$m->Dispatch();
	    }
    }
    $state = $newstate;
}

/* Check if the maibox exists, create if not, scan voicemail files */
function initUser()
{
    global $vm_base;
    global $dir;
    global $mailbox;
    global $files;
    vmInitMessageDir($mailbox);
    vmGetMessageFiles($mailbox,$files);
    $dir = "$vm_base/$mailbox";
    Yate::Output("found " . count($files) . " file entries for mailbox $mailbox");
    setState("prompt");
}

/* Transition to password entering state if user is not empty else exit */
function checkUser()
{
    global $collect_user;
    if ($collect_user == "")
	setState("goodbye");
    else
	setState("pass");
}

/* Transition to authentication state if password is not empty else exit */
function checkPass()
{
    global $collect_user;
    global $collect_pass;
    if ($collect_pass == "")
	setState("goodbye");
    else {
	setState("auth");
	$m = new Yate("user.auth");
	$m->params["username"] = $collect_user;
	$m->Dispatch();
    }
}

/* Check the system known password agains the user enterd one */
function checkAuth($pass)
{
    global $collect_user;
    global $collect_pass;
    global $mailbox;
//    Yate::Debug("checking passwd if '$collect_pass' == '$pass'");
    if ($collect_pass == $pass) {
	$mailbox = $collect_user;
	initUser();
    }
    else
	setState("goodbye");
    $collect_pass = "";
}

/* Handle EOF of wave files */
function gotNotify($reason)
{
    global $ourcallid;
    global $partycallid;
    global $state;

    Yate::Debug("gotNotify('$reason') state: $state");
    if ($reason == "replaced")
	return;

    $st = strpos($state,",");
    if ($st !== false) {
	setState(substr($state,$st+1));
	return;
    }
    switch ($state) {
	case "goodbye":
	    setState("");
	    break;
	case "record":
	case "play":
	case "listen":
	    setState("prompt");
	    break;
	case "user":
	    checkUser();
	    break;
	case "pass":
	    checkPass();
	    break;
	default:
	    setState("goodbye");
	    break;
    }
}

/* Play the n-th voicemail file */
function listenTo($n)
{
    global $files;
    global $current;
    if (($n < 0) || ($n >= count($files)))
	return;
    $current = $n;
    setState("listen");
}

/* Handle DTMFs after successfully logging in */
function navigate($text)
{
    global $state;
    global $current;
    switch ($text) {
	case "0":
	    listenTo(0);
	    break;
	case "7":
	    listenTo($current-1);
	    break;
	case "8":
	    listenTo($current);
	    break;
	case "9":
	    listenTo($current+1);
	    break;
	case "1":
	    setState("play:record,play:beep,record");
	    break;
	case "2":
	    setState("play");
	    break;
	case "3":
	    setState("");
	    break;
	case "*":
	    setState("prompt");
	    break;
    }
}

/* Handle all DTMFs here */
function gotDTMF($text)
{
    global $state;
    global $mailbox;
    global $collect_user;
    global $collect_pass;

    Yate::Debug("gotDTMF('$text') state: $state");

    switch ($state) {
	case "user":
	    if ($text == "*") {
		promptUser();
		return;
	    }
	    if ($text == "#")
		checkUser();
	    else
		$collect_user .= $text;
	    return;
	case "pass":
	    if ($text == "*") {
		promptPass();
		return;
	    }
	    if ($text == "#")
		checkPass();
	    else
		$collect_pass .= $text;
	    return;
    }
    if ($mailbox == "")
	return;

    navigate($text);
}

/* Install filtered handlers for the wave end and dtmf notify messages */
Yate::Install("chan.dtmf",100,"targetid",$ourcallid);
Yate::Install("chan.notify",100,"targetid",$ourcallid);

/* The main loop. We pick events and handle them */
while ($state != "") {
    $ev=Yate::GetEvent();
    /* If Yate disconnected us then exit cleanly */
    if ($ev === false)
	break;
    /* No need to handle empty events in this application */
    if ($ev === true)
	continue;
    /* If we reached here we should have a valid object */
    switch ($ev->type) {
	case "incoming":
	    switch ($ev->name) {
		case "call.execute":
		    $mailbox = $ev->GetValue("user");
		    $untrusted = Yate::Str2bool($ev->GetValue("untrusted"));
		    $partycallid = $ev->GetValue("id");
		    $ev->params["targetid"] = $ourcallid;
		    $ev->handled = true;
		    /* We must ACK this message before dispatching a call.answered */
		    $ev->Acknowledge();
		    /* Prevent a warning if trying to ACK this message again */
		    $ev = false;

		    /* Signal we are answering the call */
		    $m = new Yate("call.answered");
		    $m->params["id"] = $ourcallid;
		    $m->params["targetid"] = $partycallid;
		    $m->Dispatch();

		    /* If the user is unknown we need to identify and authenticate */
		    if ($mailbox == "")
			setState("user");
		    else if ($untrusted) {
			$collect_user = $mailbox;
			setState("pass");
		    }
		    else
			initUser();
		    break;

		case "chan.notify":
		    gotNotify($ev->GetValue("reason"));
		    $ev->handled = true;
		    break;

		case "chan.dtmf":
		    $text = $ev->GetValue("text");
		    for ($i = 0; $i < strlen($text); $i++)
			gotDTMF($text[$i]);
		    $ev->handled = true;
		    break;
	    }
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    // Yate::Debug("PHP Answered: " . $ev->name . " id: " . $ev->id);
	    if ($ev->name == "user.auth")
		checkAuth($ev->retval);
	    break;
	case "installed":
	    // Yate::Debug("PHP Installed: " . $ev->name);
	    break;
	case "uninstalled":
	    // Yate::Debug("PHP Uninstalled: " . $ev->name);
	    break;
	default:
	    // Yate::Output("PHP Event: " . $ev->type);
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
