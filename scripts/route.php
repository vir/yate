#!/usr/bin/php -q
<?
/* Test script for the Yate PHP interface
   To test add in extmodule.conf

   [scripts]
   test.php=
*/
require_once("libyate.php");

/* Always the first action to do */
Yate::Init();

/* Install a handler for the engine generated timer message */
//Yate::Install("engine.timer",10);
Yate::Install("chan.dtmf",10);
Yate::Install("chan.text",10);

/* Create and dispatch an initial test message */
/*$m=new Yate("test");
$m->params["param1"]="val1";
$m->retval="ret_value";
$m->Dispatch();
*/
/* The main loop. We pick events and handle them */
for (;;) {
    $ev=Yate::GetEvent();
    /* If Yate disconnected us then exit cleanly */
    if ($ev == "EOF")
        break;
    /* Empty events are normal in non-blocking operation.
       This is an opportunity to do idle tasks and check timers */
    if ($ev == "") {
//        Yate::Output("PHP event: empty");
        continue;
    }
    /* If we reached here we should have a valid object */
    switch ($ev->type) {
	case "incoming":
	//    Yate::Output("PHP Message: " . $ev->name . " id: " . $ev->id . " called: " . $ev->params["called"] . " caller: " . $ev->params["caller"]);
	  //  if ($ev->params["called"] == "1")
//	    {
//		$ev->params["response"] = "405";
//		$ev->retval = "tone/dial";
//		$ev->handled = true;
//		Yate::Output("PHP : " . $ev->params["called"] . "vias  " . $ev->params["vias"] . " cseq " . $ev->params["cseq"]);
//	    }
	    Yate::Output("PHP Message: " . $ev->name . " text: " . $ev->params["text"] . " ourcallid: " . $ev->params["ourcallid"] . "partycallid:" . $ev->params["partycallid"]);
	    $ev->handled = true;
	    /* This is extremely important.
	       We MUST let messages return, handled or not */
	    $m=new Yate("chan.text");
	    $m->params["ourcallid"]= $ev->params["partycallid"];
	    $m->params["partycallid"]= $ev->params["ourcallid"];
	    $m->params["text"] = $ev->params["text"];
	    $m->retval="ret_value";
	    $m->Dispatch();
	       
	    $ev->Acknowledge();
	    break;
	case "answer":
	    Yate::Output("PHP Answered: " . $ev->name . " id: " . $ev->id);
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
