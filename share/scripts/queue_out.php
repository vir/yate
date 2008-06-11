#!/usr/bin/php -q
<?
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
$prompt = "";
$queue = "";

/* Always the first action to do */
Yate::Init();

Yate::SetLocal("id",$ourcallid);
Yate::SetLocal("disconnected","true");

Yate::Install("call.answered",50);
Yate::Install("chan.hangup");
Yate::Install("chan.disconnected",20);

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
		    $prompt = $ev->GetValue("prompt");
		    $queue = $ev->GetValue("queue");
		    $ev->handled=true;
		    $m = new Yate("call.execute");
		    $m->params["id"] = $ourcallid;
		    $m->params["caller"] = $ev->GetValue("caller");
		    $m->params["called"] = $ev->GetValue("called");
		    $m->params["callto"] = $ev->GetValue("direct");
		    $m->params["maxcall"] = $ev->GetValue("maxcall");
		    $m->params["cdrtrack"] = "false";
		    $m->Dispatch();
		    break;
		case "call.answered":
		    if ($ev->GetValue("targetid") == $ourcallid) {
			$ev->params["targetid"] = $partycallid;
			$ev->Acknowledge();
			$m = new Yate("chan.connect");
			$m->params["id"] = $ev->GetValue("id");
			$m->params["targetid"] = $partycallid;
			$ev = false;
			$m->Dispatch();
		    }
		    break;
		case "chan.notify":
		    if ($ev->GetValue("targetid") == $ourcallid) {
			Yate::Uninstall("chan.notify");
		    }
		    break;
		case "chan.disconnected":
		    if (($ev->GetValue("id") == $ourcallid) && $ev->GetValue("reason")) {
			$ev->name = "chan.hangup";
			$ev->params["notify"] = $partycallid;
			$ev->params["queue"] = $queue;
			$ev->params["cdrtrack"] = "false";
		    }
		    break;
		case "chan.hangup":
		    if ($ev->GetValue("id") == $partycallid)
			exit();
	    }
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    Yate::Output("PHP Answered: " . $ev->name . " id: " . $ev->id);
	    if (($ev->name == "call.execute") && !$ev->handled) {
		Yate::Output("Failed to start queue call leg to: " . $ev->GetValue("callto"));
		$m = new Yate("chan.hangup");
		$m->id = "";
		$m->params["notify"] = $partycallid;
		$m->params["queue"] = $queue;
		$m->params["cdrtrack"] = "false";
		$m->Dispatch();
	    }
	    break;
	case "installed":
	    Yate::Output("PHP Installed: " . $ev->name);
	    break;
	case "uninstalled":
	    Yate::Output("PHP Uninstalled: " . $ev->name);
	    break;
	default:
	    Yate::Output("PHP Event: " . $ev->type);
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
