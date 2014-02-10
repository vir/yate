<?php

/* libyatechan.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Linear-like program flow channel interface library for Yate
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
    WARNING: This file is for PHP 5
    To modify it for PHP 4 use the following command (needs sed version 4)

    sed -i.bak -e 's/static \(function\)/\1/' libyatechan.php
*/


require_once("libyate.php");

/**
 * The YateChan class encapsulates a Yate channel
 */
class YateChan
{
    /** String: Unique channel identifier */
    var $localid;

    /** String: Identifier of the channel we are connected to */
    var $targetid;

    /** String: The tones currently collected */
    var $collect;

    /** String: The tones that make RunEvent return immediately */
    var $breaktones;

    /** Integer: Maximum number of tones we should collect */
    var $maxtones;

    /** Boolean: True if the channel script is exiting */
    var $exiting;

    /**
     * This method is called internally to keep the module running.
     * It processes the events and returns if a notification is received,
     *  a specific tone comes in or a number of tones are collected.
     */
    static function RunEvents()
    {
	global $chan_instance;
	if ($chan_instance->exiting)
	    return;
	$loop = true;
	while ($loop) {
	    $ev=Yate::GetEvent();
	    if ($ev === true)
		continue;
	    if ($ev === false) {
		$chan_instance->exiting = true;
		break;
	    }
	    if ($ev->type == "incoming") {
		if ($ev->name == "call.execute") {
		    $chan_instance->targetid = $ev->params["id"];
		    $ev->params["targetid"] = $chan_instance->localid;
		    $ev->handled = true;
		    $ev->Acknowledge();
		    break;
		}
		if ($ev->params["targetid"] == $chan_instance->localid) {
		    switch ($ev->name) {
			case "chan.notify":
			    $loop = false;
			    break;
			case "chan.dtmf":
			    $t = $ev->params["text"];
			    $chan_instance->collect .= $t;
			    if (($chan_instance->maxtones > 0) &&
				(strlen($chan_instance->collect) >= $chan_instance->maxtones))
				$loop = false;
			    else {
				for ($i = 0; $i < strlen($t); $i++) {
				    if (strstr($chan_instance->breaktones,$t[$i])) {
					$loop = false;
					break;
				    }
				}
			    }
			    break;
		    }
		    $ev->handled = true;
		}
		$ev->Acknowledge();
	    }
	}
    }

    /**
     * Record audio to a file
     * @param $file (optional) Name of the file to record or "-" for no file
     * @param $maxlen (optional)Maximum bumber of bytes to be written to file
     * @param $wait (optional) True to block until finishes, false to continue
     */
    static function RecordFile($file = "-", $maxlen = 0, $wait = true)
    {
	global $chan_instance;
	if ($chan_instance->exiting)
	    return;
	$m = new Yate("chan.attach");
	$m->params["consumer"] = "wave/record/$file";
	$m->params["notify"] = $chan_instance->localid;
	if ($maxlen != 0)
	    $m->params["maxlen"] = $maxlen;
	$m->Dispatch();
	if ($wait)
	    YateChan::RunEvents();
    }

    /**
     * Play audio from a file
     * @param $file (optional) Name of the file to play or "-" for no file
     * @param $wait (optional) True to block until finishes, false to continue
     */
    static function PlayFile($file = "-", $wait = true)
    {
	global $chan_instance;
	if ($chan_instance->exiting)
	    return;
	$m = new Yate("chan.attach");
	$m->params["source"] = "wave/play/$file";
	$m->params["notify"] = $chan_instance->localid;
	$m->Dispatch();
	if ($wait)
	    YateChan::RunEvents();
    }

    /**
     * Play a standard tone
     * @param $file (optional) Name of the tone to play
     * @param $wait (optional) True to block until tones are received, false to continue
     */
    static function PlayTone($tone = "dial", $wait = true)
    {
	global $chan_instance;
	if ($chan_instance->exiting)
	    return;
	$m = new Yate("chan.attach");
	$m->params["source"] = "tone/$tone";
	$m->Dispatch();
	if ($wait)
	    YateChan::RunEvents();
    }

    /**
     * Set the tones and/or collected length that cause end collecting tones
     * @param $btones (optional) A string containing all tones that stop collectiong
     * @param $ntones (optional) Maximum number of tones we attempt to collect
     */
    static function SetBreakTones($btones = "", $ntones = 0)
    {
	global $chan_instance;
	$chan_instance->breaktones = $btones;
	$chan_instance->maxtones = $ntones;
    }

    /**
     * Flush all collected tones from the buffer
     */
    static function FlushTones()
    {
	global $chan_instance;
	$chan_instance->collect = "";
    }

    /**
     * Constructor. Creates a new channel object
     * @param $prefix Prefix used for the unique channel identifier
     */
    function YateChan($prefix)
    {
	$this->localid = $prefix . "/" . uniqid(rand(),1);
	$this->targetid = "";
	$this->collect = "";
	$this->breaktones = "";
	$this->maxtones = 0;
	$this->exiting = false;
    }

    /**
     * This static function initializes globals in the PHP Yate Channel Module.
     * It should be called before any other method.
     * It will call Yate::Init internally.
     * @param $prefix (optional) Prefix used for the unique channel identifier
     * @param $async (optional) True if asynchronous, polled mode is desired
     * @param $addr Hostname to connect to or UNIX socket path
     * @param $port TCP port to connect to, zero to use UNIX sockets
     */
    static function Init($prefix = "extchan", $async = false, $addr = "", $port = 0)
    {
	global $chan_instance;
	Yate::Init($async,$addr,$port,"channel");
	$chan_instance = new YateChan($prefix);
	YateChan::RunEvents();
	if ($chan_instance->exiting)
	    return;
	Yate::Install("chan.dtmf");
	Yate::Install("chan.notify");
    }

}

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
