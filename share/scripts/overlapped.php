#!/usr/bin/php -q
<?
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

/* Always the first action to do */
Yate::Init();

/* Uncomment next line to get debugging messages */
// Yate::Debug(true);

/* Install handlers for the DTMF and wave EOF messages */
Yate::Install("chan.dtmf");
Yate::Install("chan.notify");

$ourcallid = "over/" . uniqid(rand(),1);
$partycallid = "";
$state = "call";
$num = "";
$collect = "";

function setState($newstate)
{
    global $ourcallid;
    global $state;
    global $collect;

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
	$m->params["consumer"] = "wave/record/-";
	$m->params["maxlen"] = 320000;
	$m->params["notify"] = $ourcallid;
	$m->Dispatch();
	return;
    }

    if ($newstate == $state)
	return;

    switch ($newstate) {
	case "goodbye":
	    $m = new Yate("chan.attach");
	    $m->params["id"] = $ourcallid;
	    $m->params["source"] = "tone/congestion";
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 32000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "routing":
	    $m = new Yate("chan.attach");
	    $m->params["id"] = $ourcallid;
	    $m->params["source"] = "tone/noise";
	    $m->params["consumer"] = "wave/record/-";
	    $m->params["maxlen"] = 320000;
	    $m->params["notify"] = $ourcallid;
	    $m->Dispatch();
	    break;
	case "noroute":
	    $m = new Yate("chan.attach");
	    $m->params["id"] = $ourcallid;
	    $m->params["source"] = "tone/outoforder";
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
    global $ourcallid;
    setState("routing");
    $m = new Yate("call.route");
    $m->params["id"] = $ourcallid;
    $m->params["called"] = $num;
    $m->params["overlapped"] = "yes";
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

function gotDTMF($dtmf)
{
    global $state;
    global $collect;

    Yate::Debug("Overlapped gotDTMF('$dtmf') in state: $state collected: '$collect'");
    switch ($dtmf) {
	case "*":
	    setState("");
	    return;
	case "#":
	    Yate::Output("Overlapped clearing already collected: '$collect'");
	    $collect="";
	    setState("prompt");
	    return;
    }

    $collect .= $dtmf;
    routeTo($collect);
}

function endRoute($callto,$ok,$err,$params)
{
    global $partycallid;
    global $num;
    global $collect;
    if ($ok && ($callto != "-") && ($callto != "error")) {
	Yate::Output("Overlapped got route: '$callto' for '$collect'");
	$m = new Yate("chan.masquerade");
	$m->params = $params;
	$m->params["message"] = "call.execute";
	$m->params["id"] = $partycallid;
	$m->params["callto"] = $callto;
	$m->params["caller"] = $num;
	$m->params["called"] = $collect;
	$m->Dispatch();
	return;
    }
    if ($err != "incomplete") {
	Yate::Output("Overlapped get error '$err' for '$collect'");
	setState("noroute");
    }
    else
	Yate::Debug("Overlapped still incomplete: '$collect'");
}

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
		    $num = $ev->GetValue("caller");
		    $autoanswer = ($ev->GetValue("called") != "off-hook");
		    $ev->handled = true;
		    // we must ACK this message before dispatching a call.answered
		    $ev->Acknowledge();
		    // we already ACKed this message
		    $ev = false;
		    if ($autoanswer) {
			$m = new Yate("call.answered");
			$m->params["id"] = $ourcallid;
			$m->params["targetid"] = $partycallid;
			$m->Dispatch();
		    }

		    setState("prompt");
		    break;

		case "chan.notify":
		    if ($ev->GetValue("targetid") == $ourcallid) {
			gotNotify();
			$ev->handled = true;
		    }
		    break;

		case "chan.dtmf":
		    if ($ev->GetValue("targetid") == $ourcallid ) {
			$dtmfs = $ev->GetValue("text");
			for ($i = 0; $i < strlen($dtmfs); $i++)
			    gotDTMF($dtmfs[$i]);
			$ev->handled = true;
		    }   
		    break;
	    }
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    if ($ev->name == "call.route")
		endRoute($ev->retval,$ev->handled,$ev->GetValue("error"),$ev->params);
	    break;
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
