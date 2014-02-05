#!/usr/bin/php -q
<?php
/**
 * queue_out.php
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

/*
 * Queue outbound calls (distributed to operators)
 * The queue module will create one instance every time it tries to send a
 *  call from queue to an available operator.
 *
 * To use add in queues.conf:
 * [channels]
 * outgoing=external/nodata/queue_out.php
 */
require_once("libyate.php");

$ourcallid = "q-out/" . uniqid(rand(),1);
$partycallid = "";
$caller = "";
$prompt = "";
$queue = "";

/* Always the first action to do */
Yate::Init();

/* Uncomment next line to get debugging messages */
//Yate::Debug(true);

Yate::SetLocal("trackparam","queue_out.php");
Yate::SetLocal("id",$ourcallid);
Yate::SetLocal("disconnected","true");

Yate::Install("call.answered",40);
Yate::Install("chan.disconnected",20,"id",$ourcallid);

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
		case "call.execute":
		    $partycallid = $ev->GetValue("notify");
		    $caller = $ev->GetValue("caller");
		    $prompt = $ev->GetValue("prompt");
		    $queue = $ev->GetValue("queue");
		    $ev->handled=true;
		    Yate::Install("chan.hangup",80,"id",$partycallid);
		    // create call leg to operator
		    $m = new Yate("call.execute");
		    $m->params["id"] = $ourcallid;
		    $m->params["caller"] = $caller;
		    $m->params["called"] = $ev->GetValue("called");
		    $m->params["callto"] = $ev->GetValue("direct");
		    $m->params["billid"] = $ev->GetValue("billid");
		    $m->params["maxcall"] = $ev->GetValue("maxcall");
		    $m->params["cdrtrack"] = "false";
		    $m->Dispatch();
		    // check if queued call still exists
		    $m = new Yate("chan.locate");
		    $m->params["id"] = $partycallid;
		    $m->Dispatch();
		    break;
		case "call.answered":
		    if ($ev->GetValue("id") == $partycallid) {
			// call was picked up from queue
			$ev->Acknowledge();
			Yate::SetLocal("reason","pickup");
			exit();
		    }
		    if ($ev->GetValue("targetid") != $ourcallid)
			break;
		    $ev->params["targetid"] = $partycallid;
		    $ev->Acknowledge();
		    // connect operator call leg directly to incoming one
		    $m = new Yate("chan.connect");
		    $m->id = "";
		    $m->params["id"] = $ev->GetValue("id");
		    $m->params["targetid"] = $partycallid;
		    $ev = false;
		    $m->Dispatch();
		    break;
		case "chan.disconnected":
		    // operator hung up or did not answer
		    if ($ev->GetValue("reason")) {
			$ev->name = "chan.hangup";
			$ev->params["notify"] = $partycallid;
			$ev->params["queue"] = $queue;
			$ev->params["cdrtrack"] = "false";
		    }
		    break;
		case "chan.hangup":
		    // caller hung up while in queue
		    exit();
	    }
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    Yate::Debug("PHP Answered: " . $ev->name . " id: " . $ev->id);
	    if (!$ev->handled) {
		if ($ev->name == "call.execute") {
		    // call leg to operator didn't even start
		    Yate::Output("Failed to start queue '$queue' call leg to: " . $ev->GetValue("callto"));
		    $m = new Yate("chan.hangup");
		    $m->id = "";
		    $m->params["notify"] = $partycallid;
		    $m->params["queue"] = $queue;
		    $m->params["cdrtrack"] = "false";
		    $m->Dispatch();
		}
		else if ($ev->name == "chan.locate") {
		    // caller hung up before we installed the hangup handler
		    Yate::Output("Call $partycallid from '$caller' exited early from '$queue'");
		    Yate::SetLocal("reason","nocall");
		    exit();
		}
	    }
	    break;
	default:
	    Yate::Debug("PHP Event: " . $ev->type);
    }
}

Yate::Debug("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
