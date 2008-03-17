<?

/* libyateivr.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Object oriented IVR
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
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
		case "play":       // Play a sound file
		    $this->PlayFile($op[1],(isset($op[2]) ? $op[2] : false));
		    break;
		case "tone":       // Start playing a tone
		    $this->PlayTone($op[1]);
		    break;
		case "stop":       // Stop sound playback
		    $this->PlayStop();
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
		    $m = new Yate("chan." . $op[0]);
		    $m->id = "";
		    $m->SetParam("id",IVR::ChannelID());
		    $m->SetParam("targetid",IVR::TargetID());
		    $m->Dispatch();
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
	$this->Debug("::OnUnhandled()");
	return false;
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
	$this->OperTable("execute");
	$event->handled = true;
	return true;
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
     * @param $file Path to file to play
     * @param $clear True to clear the queue and start playing this file
     */
    function PlayFile($file, $clear = false)
    {
	if ($clear)
	    $this->playfile = array();
	$this->playfile[] = $file;
	if ($clear)
	    $this->PlayNext();
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
     * Retrive the IVR's channel ID
     * @return ID of the IVR call leg
     */
    static function ChannelID()
    {
	global $yate_ivr_channel;
	return $yate_ivr_channel;
    }

    /**
     * Retrive the target channel ID
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
     * @param $classname Name of the class to instantiate
     * @return True if registered, false if invalid class or IVR already registered
     */
    static function Register($ivrname, $classname)
    {
	global $yate_ivr_register;

	IVR::InitIVR();
	if (isset($yate_ivr_register[$ivrname])) {
	    Yate::Output("IVR: Already registered IVR '$ivrname'");
	    return false;
	}
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
	global $yate_ivr_channel;
	global $yate_ivr_target;
	global $yate_ivr_current;

	if (IVR::Jump($ivrname)) {
	    Yate::SetLocal("id",$yate_ivr_channel);
	    Yate::Install("chan.dtmf",100,"targetid",$yate_ivr_channel);
	    Yate::Install("chan.notify",100,"targetid",$yate_ivr_channel);
	    while ($yate_ivr_current !== null) {
		$ev = Yate::GetEvent();
		if ($ev === true)
		    continue;
		if ($ev === false)
		    break;
		if (($ev->type == "incoming") && ($ev->name == "call.execute"))
		    $yate_ivr_target = $ev->GetValue("id");
		IVR::EventIVR($ev);
		if ($ev && ($ev->type == "incoming")) {
		    if ($ev->handled && $ev->name == "call.execute")
			$ev->params["targetid"] = $yate_ivr_channel;
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
	global $yate_ivr_channel;
	global $yate_ivr_target;
	global $yate_ivr_register;
	global $yate_ivr_current;
	global $yate_ivr_stack;
	global $yate_ivr_files;
	if (isset($yate_ivr_register))
	    return;
	Yate::Debug("IVR::InitIVR()");
	$yate_ivr_channel = "ivr/" . uniqid(rand(),1);
	$yate_ivr_target = null;
	$yate_ivr_register = array();
	$yate_ivr_current = null;
	$yate_ivr_stack = array();
	$yate_ivr_files = "";
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
