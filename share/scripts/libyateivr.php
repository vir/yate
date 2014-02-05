<?php

/* libyateivr.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Object oriented IVR
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
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
    WARNING: This file requires PHP 5
*/


require_once("libyate.php");

/**
 * The IVR class encapsulates an instance of IVR
 */
class IVR
{
    /** String: Name of this IVR */
    var $ivrname;

    /** Array: State operation table */
    var $optable;

    /** String: Name of the current state */
    var $curstate;

    /** Array: Name of files to play in sequence */
    var $playfile;

    /**
     * Base class constructor
     */
    function IVR()
    {
	$this->ivrname = null;
	$this->optable = array();
	$this->curstate = "";
	$this->playfile = array();
    }

    /**
     * Helper method to output a string prefixed by IVR name
     * @param $text Text to put in logs or on console
     */
    function Output($text)
    {
	if ($text{0} != ":")
	    $text = ": $text";
	Yate::Output($this->GetName() . $text);
    }

    /**
     * Helper method to output a debug string prefixed by IVR name
     * @param $text Text to put in logs or on console
     */
    function Debug($text)
    {
	if ($text{0} != ":")
	    $text = ": $text";
	Yate::Debug($this->GetName() . $text);
    }

    /**
     * Get the name of the IVR
     */
    function GetName()
    {
	return $this->ivrname;
    }

    /**
     * Get the current state of the IVR
     */
    function GetState()
    {
	return $this->curstate;
    }

    /**
     * Change the state of this IVR instance
     * @param $state New state of the IVR
     */
    function SetState($state)
    {
	$this->Debug("::SetState('$state')");
	$this->curstate = $state;
	$this->OperTable("state");
    }

    /**
     * Try and if found execute a table described operation - internal use
     * @param $state State to match
     * @param $text Event name of DTMF
     * @return True if handled
     */
    private function TryTable($state,$text)
    {
	if (isset($this->optable["$state:$text"])) {
	    $op = explode(":",$this->optable["$state:$text"]);
	    $this->Debug("Found table operation '" . $op[0] . "'");
	    switch ($op[0]) {
		case "output":     // Output a text to console or log file
		    $this->Output($op[1]);
		    break;
		case "debug":      // Output a text if debugging is enabled
		    $this->Debug($op[1]);
		    break;
		case "state":      // Change local IVR state
		    $this->SetState($op[1]);
		    break;
		case "play":       // Play one or many sound files
		    array_shift($op);
		    $this->PlayFile($op);
		    break;
		case "play_recstop":
		    array_shift($op);
		    $this->PlayRecStop($op);
		    break;
		case "recstop":
		    $this->RecStop($op[1]);
		    break;
		case "tone":       // Start playing a tone
		    $this->PlayTone($op[1]);
		    break;
		case "stop":       // Stop sound playback
		    $this->PlayStop();
		    break;
		case "dtmf":       // Emit a DTMF sequence
		    $this->PlayDTMF($op[1],(isset($op[2]) ? $op[2] : null));
		    break;
		case "jump":       // Jump to another IVR, leave this one
		    IVR::Jump($op[1],(isset($op[2]) ? $op[2] : null));
		    break;
		case "call":       // Call into another IVR, put this on stack
		    IVR::Call($op[1],(isset($op[2]) ? $op[2] : null));
		    break;
		case "leave":      // Leave this IVR, return to one on stack
		    IVR::Leave(isset($op[1]) ? $op[1] : null);
		    break;
		case "progress":   // Emit a call progress notification
		case "ringing":    // Emit a call ringing notification
		case "answered":   // Emit an answer notification
		    $m = new Yate("call." . $op[0]);
		    $m->id = "";
		    $m->SetParam("id",IVR::ChannelID());
		    $m->SetParam("targetid",IVR::TargetID());
		    $m->SetParam("cdrcreate",false);
		    $m->Dispatch();
		    break;
		case "hangup":     // Hangup the entire IVR
		    IVR::Hangup();
		    break;
		default:
		    $this->Output("Invalid table operation '" . $op[0] . "'");
		    return false;
	    }
	    return true;
	}
	return false;
    }

    /**
     * Perform table described operations
     * @return True if operation was executed from table
     */
    function OperTable($text)
    {
	if ($this->TryTable($this->curstate,$text) || $this->TryTable("",$text))
	    return true;
	if (strlen($text) != 1)
	    return false;
	return $this->TryTable($this->curstate,".") || $this->TryTable("",".");
    }

    /**
     * Method called when this IVR is entered by jump or call
     * @param $state Initial state requested by the caller IVR
     */
    function OnEnter($state)
    {
	$this->Debug("::OnEnter('$state')");
	$this->OperTable("enter");
    }

    /**
     * Method called when leaving this IVR by jump or return
     */
    function OnLeave()
    {
	$this->Debug("::OnLeave()");
    }

    /**
     * Method called when returning to this IVR from a called one
     * @param $retval Value returned by the called IVR
     */
    function OnReturn($retval)
    {
	$this->Debug("::OnReturn('$retval')");
    }

    /**
     * Method called when this IVR receives an event
     * @param $event Reference to received event
     * @return True if the event was handled, false if undesired
     */
    function OnEvent(&$event)
    {
	$this->Debug("::OnEvent(" . $event->type . " '" . $event->name . "')");
	switch ($event->type) {
	    case "incoming":
		return $this->OnMessage($event->name,$event);
	    case "answer":
		return $this->OnDispatch($event->name,$event);
	    case "setlocal":
		return $this->OnSetting($event->name,$event->retval,$event->handled);
	}
	return false;
    }

    /**
     * Method called on stacked IVRs when the current one didn't process an event
     * @param $event Reference to received event
     * @return True if the event was handled, false if undesired
     */
    function OnUnhandled(&$event)
    {
	$this->Debug("::OnUnhandled(" . $event->type . " '" . $event->name . "')");
	if ($event->type != "incoming")
	    return false;
	if ($event->name != "call.execute" && $event->name != "chan.disconnected")
	    return false;
	return $this->OnMessage($event->name,$event);
    }

    /**
     * Method called when a message from Yate arrives at this IVR
     * @param $name Name of the message
     * @param $event Reference to incoming message event
     * @return True if the event was handled, false if undesired
     */
    function OnMessage($name, &$event)
    {
	$this->Debug("::OnMessage('$name')");
	switch ($name) {
	    case "call.execute":
		return $this->OnExecute($event);
	    case "chan.dtmf":
		return $this->OnDTMF($event->GetValue("text"));
	    case "chan.notify":
		$notify = $event->GetValue("event");
		if (($notify !== null) && ($notify != "wave"))
		    return $this->OnNotify($notify,$event);
		if ($event->GetValue("reason") == "replaced")
		    return true;
		return $this->PlayNext() || $this->OnEOF();
	}
	return false;
    }

    /**
     * Method called when a message generated by this IVR has finished dispatching
     * @param $name Name of the message
     * @param $event Reference to message answered event
     * @return True if the event was handled, false if undesired
     */
    function OnDispatch($name, &$event)
    {
	$this->Debug("::OnDispatch('$name')");
	return false;
    }

    /**
     * Method called after getting or setting a module or engine parameter
     * @param $name Name of the parameter
     * @param $value Current value of the parameter
     * @param $ok True if th operation was successfull, false if invalid
     */
    function OnSetting($name, $value, $ok)
    {
	$this->Debug("::OnSetting('$name','$value'," . Yate::Bool2str($ok) . ")");
	return false;
    }

    /**
     * Method called on the call.execute event. This is the first event
     *  received and only one IVR will get a chance to handle it
     * @param $event Reference to received event
     * @return True if the event was handled, false if undesired
     */
    function OnExecute(&$event)
    {
	$this->Debug("::OnExecute()");
	if ($this->OperTable("execute")) {
	    $event->handled = true;
	    return true;
	}
	return false;
    }

    /**
     * Method called when a file finished playing, by default it starts playing
     *  the next in queue
     */
    function OnEOF()
    {
	$this->Debug("::OnEOF()");
	$this->OperTable("eof");
    }

    /**
     * Method called on named notifications
     * @param $name Name of the notification (parameter "event" in message)
     * @param $event Reference to notification message event
     * @return True if the notification was handled, false if undesired
     */
    function OnNotify($name, &$event)
    {
	if ($name == "dtmf")
	    return $this->OnDTMF($event->GetValue("text"));
	$this->Debug("::OnNotify('$name')");
	return $this->OperTable($name);
    }

    /**
     * Method called for each DTMF received for this IVR
     * @param $tone Received key press, 0-9, A-D or F(lash)
     * @return True if the key was handled, false if undesired
     */
    function OnDTMF($key)
    {
	$this->Debug("::OnDTMF('$key')");
	return $this->OperTable($key);
    }


    /**
     * Play a wave file or add it to the queue
     * @param $file1,$file2,... Path to files to play
     * @param $clear Optional - true to clear the queue and start playing now
     */
    function PlayFile()
    {
	$args = func_get_args();
	$n = count($args);
	if ($n == 1 && is_array($args[0])) {
	    $args = $args[0];
	    $n = count($args);
	}
	if ($n < 1)
	    return;
	$clear = false;
	$last = $args[$n - 1];
	if ($last === false)
	    $n--;
	if (($last === true) || ($last == "clear")) {
	    $n--;
	    $clear = true;
	}
	if ($n == 1 && is_array($args[0])) {
	    $args = $args[0];
	    $n = count($args);
	}
	if ($clear)
	    $this->playfile = array();
	for ($i = 0; $i < $n; $i++)
	    $this->playfile[] = $args[$i];
	if ($clear)
	    $this->PlayNext();
    }

    /**
     * Stop recording then play a wave file or add it to the queue
     * @param $file1,$file2,... Path to files to play
     * @param $clear Optional - true to clear the queue and start playing now
     */
    function PlayRecStop()
    {
	$args = func_get_args();
	$this->RecStop();
	call_user_func_array(array($this, "PlayFile"), $args);
    }

    /**
     * Stop recording
     * @param $maxlen Maximum number of octets that should be transferred. Default NULL
     */
    function RecStop($maxlen=null)
    {
	$m = new Yate("chan.attach");
	$m->id = "";
	$m->SetParam("consumer","wave/record/-");
	$m->SetParam("single",true);
	if($maxlen) {
	    $m->SetParam("maxlen", $maxlen);
	    $m->SetParam("notify", IVR::ChannelID());
	}
	$m->Dispatch();
    }

    /**
     * Start playing a tone, clear the file queue
     * @param $tone Name of the tone to play
     */
    function PlayTone($tone)
    {
	$this->playfile = array();
	$m = new Yate("chan.attach");
	$m->id = "";
	$m->SetParam("source","tone/$tone");
	$m->SetParam("single",true);
	$m->Dispatch();
    }

    /**
     * Play next file from the play queue
     * @return True if play started, false if queue was empty
     */
    function PlayNext()
    {
	$file = array_shift($this->playfile);
	if ($file === null)
	    return false;
	$m = new Yate("chan.attach");
	$m->id = "";
	$m->SetParam("notify",IVR::ChannelID());
	$m->SetParam("source","wave/play/$file");
	$m->SetParam("single",true);
	$m->Dispatch();
	return true;
    }

    /**
     * Stop the file playback and clear the play queue
     */
    function PlayStop()
    {
	$this->playfile = array();
	$m = new Yate("chan.attach");
	$m->id = "";
	$m->SetParam("source","wave/play/-");
	$m->SetParam("single",true);
	$m->Dispatch();
    }

    /**
     * Emit a DTMF to the other end
     * @param $dtmf DTMF tones to play
     * @param $method Method to emit the DTMF, null to use technology default
     */
    function PlayDTMF($dtmf, $method = null)
    {
	$m = new Yate("chan.dtmf");
	$m->id = "";
	$m->SetParam("id",IVR::ChannelID());
	$m->SetParam("targetid",IVR::TargetID());
	$m->SetParam("text",$dtmf);
	if ($method !== null)
	    $m->SetParam("method",$method);
	$m->Dispatch();
    }

    /**
     * Retrieve the IVR's channel ID, initialize it if required
     * @return ID of the IVR call leg
     */
    static function ChannelID()
    {
	global $yate_ivr_channel;
	if (!isset($yate_ivr_channel))
	    $yate_ivr_channel = "ivr/" . uniqid(rand(),1);
	return $yate_ivr_channel;
    }

    /**
     * Set the IVR's channel ID, must be called early
     * @param $id Desired ID of the IVR call leg
     * @return True if the new ID was set successfully, false if already set
     */
    static function SetChannelID($id)
    {
	global $yate_ivr_channel;
	if (($id != "") && !isset($yate_ivr_channel)) {
	    $yate_ivr_channel = $id;
	    return true;
	}
	return false;
    }

    /**
     * Retrieve the target channel ID
     * @return ID of the call leg that called into the IVR
     */
    static function TargetID()
    {
	global $yate_ivr_target;
	return $yate_ivr_target;
    }

    /**
     * Jump to another IVR, leave the current one
     * @param $ivrname Name of the IVR to jump to
     * @param $state Desired initial state of the new IVR, null to use default
     * @return True if jumped to a new IVR, false if it doesn't exist
     */
    static function Jump($ivrname, $state = null)
    {
	global $yate_ivr_current;

	$obj = IVR::CreateIVR($ivrname);
	if ($obj === null)
	    return false;

	if ($yate_ivr_current !== null)
	    $yate_ivr_current->OnLeave();
	$yate_ivr_current = $obj;
	$yate_ivr_current->OnEnter($state);
	return true;
    }

    /**
     * Call another IVR, current one is placed on the stack
     * @param $ivrname Name of the IVR to call to
     * @param $state Desired initial state of the new IVR
     * @return True if called to a new IVR, false if it doesn't exist
     */
    static function Call($ivrname, $state = null)
    {
	global $yate_ivr_current;
	global $yate_ivr_stack;

	$obj = IVR::CreateIVR($ivrname);
	if ($obj === null)
	    return false;

	if ($yate_ivr_current !== null)
	    array_unshift($yate_ivr_stack, $yate_ivr_current);
	$yate_ivr_current = $obj;
	$yate_ivr_current->OnEnter($state);
	return true;
    }

    /**
     * Leave this IVR and return to the caller on the stack.
     * If the stack is empty hang up the channel
     * @param $retval Value to return to the caller
     * @return True if returned OK, false if stack was empty and we're hanging up
     */
    static function Leave($retval = null)
    {
	global $yate_ivr_current;
	global $yate_ivr_stack;

	if ($yate_ivr_current === null)
	    return false;
	$yate_ivr_current->OnLeave();
	$yate_ivr_current = array_shift($yate_ivr_stack);
	if ($yate_ivr_current === null)
	    return false;
	$yate_ivr_current->OnReturn($retval);
	return true;
    }

    /**
     * Change the state of the current IVR.
     * @param $state New state of the IVR
     */
    static function State($state)
    {
	global $yate_ivr_current;

	if ($yate_ivr_current !== null)
	    $yate_ivr_current->SetState($state);
    }

    /**
     * Register an IVR by its class name
     * @param $ivrname Name of the IVR to register
     * @param $classname Name of the class to instantiate, defaults to name of IVR
     * @return True if registered, false if invalid class or IVR already registered
     */
    static function Register($ivrname, $classname = null)
    {
	global $yate_ivr_register;

	IVR::InitIVR();
	if (isset($yate_ivr_register[$ivrname])) {
	    Yate::Output("IVR: Already registered IVR '$ivrname'");
	    return false;
	}
	if ($classname === null)
	    $classname = $ivrname;
	if (!class_exists($classname)) {
	    Yate::Output("IVR: Inexistent class '$classname' for IVR '$ivrname'");
	    return false;
	}
	$yate_ivr_register[$ivrname] = $classname;
	return true;
    }

    /**
     * Get the name of the current IVR
     * @return Name of the IVR, null if none running
     */
    static function Current()
    {
	global $yate_ivr_current;

	if (isset($yate_ivr_current) && ($yate_ivr_current !== null))
	    return $yate_ivr_current->GetName();
	return null;
    }

    /**
     * Hang up from the IVR side
     */
    static function Hangup()
    {
	global $yate_ivr_current;

	IVR::CleanupIVR();
	$yate_ivr_current = null;
    }

    /**
     * Run the IVR system
     * @param $ivrname Name of the initial IVR to start
     */
    static function Run($ivrname)
    {
	global $yate_ivr_target;
	global $yate_ivr_current;

	if (IVR::Jump($ivrname)) {
	    $init_id = true;
	    while ($yate_ivr_current !== null) {
		$ev = Yate::GetEvent();
		if ($ev === true)
		    continue;
		if ($ev === false)
		    break;
		if (($ev->type == "incoming") && ($ev->name == "call.execute")) {
		    $yate_ivr_target = $ev->GetValue("id");
		    IVR::SetChannelID($ev->GetValue("ivrchanid"));
		}
		IVR::EventIVR($ev);
		if ($init_id && ($yate_ivr_current !== null)) {
		    $init_id = false;
		    Yate::SetLocal("id",IVR::ChannelID());
		    Yate::Install("chan.dtmf",100,"targetid",IVR::ChannelID());
		    Yate::Install("chan.notify",100,"targetid",IVR::ChannelID());
		}
		if ($ev && ($ev->type == "incoming")) {
		    if ($ev->handled && $ev->name == "call.execute")
			$ev->SetParam("targetid",IVR::ChannelID());
		    $ev->Acknowledge();
		}
	    }
	    IVR::CleanupIVR();
	}
    }

    /**
     * Create an IVR instance by name, used internally only
     * @param $ivrname Name of the IVR to create
     * @return Newly created IVR object
     */
    private static function CreateIVR($ivrname)
    {
	global $yate_ivr_register;

	IVR::InitIVR();
	if (isset($yate_ivr_register[$ivrname])) {
	    $obj = new $yate_ivr_register[$ivrname];
	    $obj->ivrname = $ivrname;
	    return $obj;
	}
	Yate::Output("IVR: Requested unknown IVR '$ivrname'");
	return null;
    }

    /**
     * Cleanup the remaining IVR objects on stack, used internally only
     */
    private static function CleanupIVR()
    {
	global $yate_ivr_current;
	global $yate_ivr_stack;

	do {
	    if ($yate_ivr_current !== null)
		$yate_ivr_current->OnLeave();
	    $yate_ivr_current = array_shift($yate_ivr_stack);
	} while ($yate_ivr_current !== null);
    }

    /**
     * Run an event through the current IVR and the call stack
     * @param $event Reference to event to run through the IVR stack
     * @return True if the event was handled, false if not
     */
    private static function EventIVR(&$event)
    {
	global $yate_ivr_current;
	global $yate_ivr_stack;

	if ($yate_ivr_current === null)
	    return false;
	if ($yate_ivr_current->OnEvent($event))
	    return true;
	foreach ($yate_ivr_stack as &$obj)
	    if ($obj->OnUnhandled($event))
		return true;
	return false;
    }

    /**
     * Initialize the IVR system if not already initialized, used internally
     */
    private static function InitIVR()
    {
	global $yate_ivr_target;
	global $yate_ivr_register;
	global $yate_ivr_current;
	global $yate_ivr_stack;
	global $yate_ivr_files;
	if (isset($yate_ivr_register))
	    return;
	Yate::Debug("IVR::InitIVR()");
	$yate_ivr_target = null;
	$yate_ivr_register = array();
	$yate_ivr_current = null;
	$yate_ivr_stack = array();
	$yate_ivr_files = "";
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
