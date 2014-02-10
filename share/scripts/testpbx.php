#!/usr/bin/php -q
<?php
/**
 * testpbx.php
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

/* Dialer scheduler and PBX/IVR tester
   Global script - start it from rmanager with: external start testpbx.php
*/
require_once("libyate.php");


// edit these to fit your needs:
$numberCalled = "122";
$sipGateway = "192.168.0.1";
// and the sequences of DTMFs to randomly choose from:
$codes = array(
    "*0*0",
    "*0124#124*##*3",
    "*0124**3"
);


$calls = array();
$nextCallTime = 0;

function runDtmfTimer($when)
{
    global $calls;
    foreach ($calls as $id => $state) {
	if ($state == "")
	    continue;
	$arr = explode(":",$state);
	if ($when < $arr[0])
	    continue;
	$key = substr($arr[1],0,1);
	$state = substr($arr[1],1);
	if ($key == "H") {
	    Yate::Debug("DROP: $id");
	    $calls[$id] = "";
	    $m = new Yate("chan.drop");
	    $m->id = "";
	    $m->params["id"] = $id;
	    $m->Dispatch();
	    return;
	}
	Yate::Debug("DTMF: $id '$key' '$state'");
	if ($state == "")
	    $state = (rand(60,180)+$when).":H";
	else
	    $state = (rand(1,3)+$when).":$state";
	$calls[$id] = $state;
	$m = new Yate("chan.dtmf");
	$m->id = "";
	$m->params["targetid"] = $id;
	$m->params["text"] = $key;
	$m->params["method"] = "inband";
	$m->Dispatch();
    }
}

function runCallTimer($when)
{
    global $nextCallTime;
    global $numberCalled;
    global $sipGateway;
    if ($when < $nextCallTime)
	return;
    $nextCallTime = $when + rand(1,10);
    $m = new Yate("call.execute");
    $m->id = "";
    $m->params["cdrtrack"] = "false";
    $m->params["callto"] = "tone/noise";
    $m->params["called"] = $numberCalled;
    $m->params["caller"] = rand(210000000,799999999);
    $m->params["direct"] = "sip/sip:$numberCalled@$sipGateway";
    $m->Dispatch();
}

function onStartup($ev,$id)
{
    global $calls;
    if (substr($id,0,4) != "sip/")
	return;
    $calls[$id] = "";
    Yate::Debug("STARTUP: $id");
}

function onHangup($ev,$id)
{
    global $calls;
    if (substr($id,0,4) != "sip/")
	return;
    if (!isset($calls[$id]))
	return;
    unset($calls[$id]);
    Yate::Debug("HANGUP: $id");
}

function onAnswer($ev,$id,$when)
{
    global $calls;
    global $codes;
    if (substr($id,0,4) != "sip/")
	return;
    if (!isset($calls[$id]))
	return;
    $keys = $codes[rand(0,count($codes)-1)];
    $when += rand(2,10);
    $calls[$id] = "$when:$keys";
    Yate::Debug("ANSWER: $id '$keys'");
}

Yate::Init();
Yate::Debug(true);
Yate::Install("engine.timer");
Yate::Install("chan.startup",50);
Yate::Install("chan.hangup",50);
Yate::Install("call.answered",50);

/* The main loop. We pick events and handle them */
for (;;) {
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
		case "engine.timer":
		    $ev->Acknowledge();
		    runDtmfTimer($ev->origin);
		    runCallTimer($ev->origin);
		    $ev = false;
		    break;
		case "chan.startup":
		    onStartup($ev,$ev->GetValue("id"));
		    break;
		case "chan.hangup":
		    onHangup($ev,$ev->GetValue("id"));
		    break;
		case "call.answered":
		    onAnswer($ev,$ev->GetValue("id"),$ev->origin);
		    break;
	    }
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    // Yate::Debug("PHP Answered: " . $ev->name . " id: " . $ev->id);
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
