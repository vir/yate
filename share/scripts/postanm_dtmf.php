#!/usr/bin/php -q
<?php
/* Post answer DTMF sender
   Set in extmodule.conf:

   [scripts]
   postanm_dtmf.php=

   Parameters handled in call.answered:
   postanm_dtmf: boolean: Handle the message. Defaults to true
   postanm_dtmf_text: string: Tones to send. The message will be ignored if text is empty
   postanm_dtmf_outbound: boolean: Send DTMFs to called (true) or caller (false). Defaults to false
   postanm_dtmf_delay: integer: Interval to delay the tones in seconds. Defaults to 0
*/
require_once("libyate.php");

$param = "postanm_dtmf";
$calls = array();
$tones = array();
$defDelay = 0;                           // Default delay value

// Always the first action to do
Yate::Init();
// Uncomment next line to get debugging messages
Yate::Debug(true);

function getBoolValue($str,$defVal)
{
    if (empty($str))
	return $defVal;
    return Yate::Str2bool($str);
}

// Set/unset array value
function setupCall($key,$set,$interval = 0,$text = "")
{
    global $param;
    global $calls;
    global $tones;

    Yate::Debug($param . ": setupCall(" . $key . "," . $set . ")");
    if ($set) {
	$calls[$key] = $interval;
	$tones[$key] = $text;
    }
    else {
	unset($calls[$key]);
	unset($tones[$key]);
    }
}

// Masquerade a chan.dtmf
function sendTones($id,$text)
{
    global $param;

    Yate::Debug($param . ": sendTones(" . $id ."," . $text . ")");
    $m = new Yate("chan.masquerade");
    $m->params["id"] = $id;
    $m->params["message"] = "chan.dtmf";
    $m->params["text"] = $text;
    $m->Dispatch();
}

// Handle call.answered
function callAnswered($ev,$text)
{
    global $param;
    global $defDelay;

    $evId = $ev->GetValue("id");
    $evPeerId = $ev->GetValue("peerid");
    $outbound = getBoolValue($ev->getValue($param . "_outbound"),false);
    $tmp = $ev->getValue($param . "_delay");
    $delay = $defDelay;
    if (!empty($tmp)) {
	$val = (int)$tmp;
	if ($val >= 0)
	    $delay = $val;
    }
    Yate::Debug($param . ": Handling " . $ev->name . " text=" . $text . " id=" . $evId . " peerid=" . $evPeerId . " outbound=" . $outbound . " delay=" . $delay);
    if ($outbound) {
	if (empty($evPeerId))
	    return;
	$evId = $evPeerId;
    }
    else if (empty($evId))
	return;
    if ($delay > 0)
	setupCall($evId,true,$delay,$text);
    else
	sendTones($evId,$text);
}

function checkCalls()
{
    global $calls;
    global $tones;

    foreach ($calls as $key => $call) {
	$calls[$key]--;
	if ($calls[$key] <= 0) {
	    sendTones($key,$tones[$key]);
	    setupCall($key,false);
	}
    }
}

Yate::Watch("call.answered");
Yate::Install("chan.hangup");
Yate::Install("engine.timer");
// Restart if terminated
Yate::SetLocal("restart",true);

Yate::Debug($param . ": Starting");

/* The main loop. We pick events and handle them */
for (;;) {
    $ev=Yate::GetEvent();
    /* If Yate disconnected us then exit cleanly */
    if ($ev === false)
        break;
    /* Empty events are normal in non-blocking operation.
       This is an opportunity to do idle tasks and check timers */
    if ($ev === true) {
        Yate::Debug($param . ": empty event");
        continue;
    }
    switch ($ev->type) {
	case "incoming":
	    if ($ev->name == "engine.timer")
		checkCalls();
	    else if ($ev->name == "chan.hangup") {
		$id = $ev->getValue("id");
		if (!empty($id))
		    setupCall($id,false);
	    }
	    $ev->Acknowledge();
	    break;
	case "answer":
	    if ($ev->name == "call.answered") {
		$text = "";
		if (getBoolValue($ev->getValue($param),true))
		    $text = $ev->getValue($param . "_text");
		if (!empty($text))
		    callAnswered($ev,$text);
	    }
	    break;
	case "watched":
	    Yate::Debug($param . ": Watching " . $ev->name);
	    break;
	case "installed":
	    Yate::Debug($param . ": Installed " . $ev->name);
	    break;
	case "uninstalled":
	    Yate::Debug($param . ": Uninstalled " . $ev->name);
	    break;
	case "setlocal":
	    Yate::Debug($param . ": Parameter ". $ev->name . "=" . $ev->retval . ($ev->handled ? " (OK)" : " (error)"));
	    break;
	default:
	    Yate::Debug($param . ": Event " . $ev->type);
    }
}

Yate::Debug($param . ": Terminated");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
