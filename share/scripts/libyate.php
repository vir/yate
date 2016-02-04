<?php

/* libyate.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * PHP object-oriented interface library for Yate
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

    sed -i.bak -e 's/static \(function\)/\1/' libyate.php
*/

/**
 * The Yate class encapsulates the object oriented interface of PHP to Yate
 */
class Yate
{
    /** String: Type of the event */
    var $type;
    /** String: Name of the message */
    var $name;
    /** String: Return value of the message */
    var $retval;
    /** Number: Time the message was generated */
    var $origin;
    /** String: Temporarily unique message ID */
    var $id;
    /** Boolean: Was the message handled or not */
    var $handled;
    /** Array: Message parameters, $obj->params["name"]="value" */
    var $params;

    /**
     * Static function to output a string to Yate's stderr or logfile
     * @param $str String to output
     */
    static function Output($str)
    {
	global $yate_stderr, $yate_output;
	if ($str === true)
	    $yate_output = true;
	else if ($str === false)
	    $yate_output = false;
	else if ($yate_output)
	    _yate_print("%%>output:$str\n");
	else
	    fputs($yate_stderr, "$str\n");
    }

    /**
     * Static function to output a string to Yate's stderr or logfile
     *  only if debugging was enabled.
     * @param $str String to output, or boolean (true/false) to set debugging
     */
    static function Debug($str)
    {
	global $yate_stderr, $yate_debug;
	if ($str === true)
	    $yate_debug = true;
	else if ($str === false)
	    $yate_debug = false;
	else if ($yate_debug)
	    Yate::Output($str);
    }

    /**
     * Static function to get the unique argument passed by Yate at start time
     * @return First (and only) command line argument passed by Yate
     */
    static function Arg()
    {
	if (isset($_SERVER['argv'][1]))
	    return $_SERVER['argv'][1];
	return null;
    }

    /**
     * Static function to convert a Yate string representation to a boolean
     * @param $str String value to convert
     * @return True if $str is "true", false otherwise
     */
    static function Str2bool($str)
    {
	return ($str == "true") ? true : false;
    }

    /**
     * Static function to convert a boolean to a Yate string representation
     * @param $bool Boolean value to convert
     * @return The string "true" if $bool was true, "false" otherwise
     */
    static function Bool2str($bool)
    {
	return $bool ? "true" : "false";
    }

    /**
     * Static function to convert a string to its Yate escaped format
     * @param $str String to escape
     * @param $extra (optional) Character to escape in addition to required ones
     * @return Yate escaped string
     */
    static function Escape($str, $extra = "")
    {
	if ($str === true)
	    return "true";
	if ($str === false)
	    return "false";
	$str = $str . "";
	$s = "";
	$n = strlen($str);
	for ($i=0; $i<$n; $i++) {
	    $c = $str{$i};
	    if ((ord($c) < 32) || ($c == ':') || ($c == $extra)) {
		$c = chr(ord($c) + 64);
		$s .= '%';
	    }
	    else if ($c == '%')
		$s .= $c;
	    $s .= $c;
	}
	return $s;
    }

    /**
     * Static function to convert a Yate escaped string back to a plain string
     * @param $str Yate escaped string to unescape
     * @return Unescaped string
     */
    static function Unescape($str)
    {
	$s = "";
	$n = strlen($str);
	for ($i=0; $i<$n; $i++) {
	    $c = $str{$i};
	    if ($c == '%') {
		$i++;
		$c = $str{$i};
		if ($c != '%')
		    $c = chr(ord($c) - 64);
	    }
	    $s .= $c;
	}
	return $s;
    }

    /**
     * Install a Yate message handler
     * @param $name Name of the messages to handle
     * @param $priority (optional) Priority to insert in chain, default 100
     * @param $filtname (optional) Name of parameter to filter for
     * @param $filtvalue (optional) Matching value of filtered parameter
     */
    static function Install($name, $priority = 100, $filtname = "", $filtvalue = "")
    {
	$name=Yate::Escape($name);
	if ($filtname)
	    $filtname=":$filtname:$filtvalue";
	_yate_print("%%>install:$priority:$name$filtname\n");
    }

    /**
     * Uninstall a Yate message handler
     * @param $name Name of the messages to stop handling
     */
    static function Uninstall($name)
    {
	$name=Yate::Escape($name);
	_yate_print("%%>uninstall:$name\n");
    }

    /**
     * Install a Yate message watcher
     * @param $name Name of the messages to watch
     */
    static function Watch($name)
    {
	$name=Yate::Escape($name);
	_yate_print("%%>watch:$name\n");
    }

    /**
     * Uninstall a Yate message watcher
     * @param $name Name of the messages to stop watching
     */
    static function Unwatch($name)
    {
	$name=Yate::Escape($name);
	_yate_print("%%>unwatch:$name\n");
    }

    /**
     * Changes a local module parameter
     * @param $name Name of the parameter to modify
     * @param $value New value to set in the parameter
     */
    static function SetLocal($name, $value)
    {
	if (($value === true) || ($value === false))
	    $value = Yate::Bool2str($value);
	$name=Yate::Escape($name);
	$value=Yate::Escape($value);
	_yate_print("%%>setlocal:$name:$value\n");
    }

    /**
     * Asynchronously retrieve a local module parameter
     * @param $name Name of the parameter to read
     */
    static function GetLocal($name)
    {
	Yate::SetLocal($name, "");
    }

    /**
     * Constructor. Creates a new outgoing message
     * @param $name Name of the new message
     * @param $retval (optional) Default return
     * @param $id (optional) Identifier of the new message
     */
    function Yate($name, $retval = "", $id = "")
    {
	if ($id == "")
	    $id=uniqid(rand(),1);
	$this->type="outgoing";
	$this->name=$name;
	$this->retval=$retval;
	$this->origin=time();
	$this->handled=false;
	$this->id=$id;
	$this->params=array();
    }

    /**
     * Retrieve the value of a named parameter
     * @param $key Name of the parameter to retrieve
     * @param $defvalue (optional) Default value to return if parameter is not set
     * @return Value of the $key parameter or $defvalue
     */
    function GetValue($key, $defvalue = null)
    {
	if (isset($this->params[$key]))
	    return $this->params[$key];
	return $defvalue;
    }

    /**
     * Set a named parameter
     * @param $key Name of the parameter to set
     * @param $value Value to set in the parameter
     */
    function SetParam($key, $value)
    {
	if (($value === true) || ($value === false))
	    $value = Yate::Bool2str($value);
	$this->params[$key] = $value;
    }

    /**
     * Fill the parameter array from a text representation
     * @param $parts A numerically indexed array with the key=value parameters
     * @param $offs (optional) Offset in array to start processing from
     */
    function FillParams(&$parts, $offs = 0)
    {
	$n = count($parts);
	for ($i=$offs; $i<$n; $i++) {
	    $s=$parts[$i];
	    $q=strpos($s,'=');
	    if ($q === false)
		$this->params[Yate::Unescape($s)]=NULL;
	    else
		$this->params[Yate::Unescape(substr($s,0,$q))]=Yate::Unescape(substr($s,$q+1));
	}
    }

    /**
     * Dispatch the message to Yate for handling
     * @param $message Message object to dispatch
     */
    function Dispatch()
    {
	if ($this->type != "outgoing") {
	    Yate::Output("PHP bug: attempt to dispatch message type: " . $this->type);
	    return;
	}
	$i=Yate::Escape($this->id);
	$t=0+$this->origin;
	$n=Yate::Escape($this->name);
	$r=Yate::Escape($this->retval);
	$p="";
	$pa = array(&$p);
	array_walk($this->params, "_yate_message_walk", $pa);
	_yate_print("%%>message:$i:$t:$n:$r$p\n");
	$this->type="dispatched";
    }

    /**
     * Acknowledge the processing of a message passed from Yate
     * @param $handled (optional) True if Yate should stop processing, default false
     */
    function Acknowledge()
    {
	if ($this->type != "incoming") {
	    Yate::Output("PHP bug: attempt to acknowledge message type: " . $this->type);
	    return;
	}
	$i=Yate::Escape($this->id);
	$k=Yate::Bool2str($this->handled);
	$n=Yate::Escape($this->name);
	$r=Yate::Escape($this->retval);
	$p="";
	$pa = array(&$p);
	array_walk($this->params, "_yate_message_walk", $pa);
	_yate_print("%%<message:$i:$k:$n:$r$p\n");
	$this->type="acknowledged";
    }

    /**
     * This static function processes just one input line.
     * It must be called in a loop to keep messages running. Or else.
     * @return false if we should exit, true if we should keep running,
     *  or an Yate object instance. Remember to use === and !== operators
     *  when comparing against true and false.
     */
    static function GetEvent()
    {
	global $yate_stdin, $yate_socket, $yate_buffer;
	if ($yate_socket) {
	    $line = @socket_read($yate_socket,$yate_buffer,PHP_NORMAL_READ);
	    // check for error
	    if ($line == false)
		return false;
	    // check for EOF
	    if ($line === "")
		return false;
	}
	else {
	    if ($yate_stdin == false)
		return false;
	    // check for EOF
	    if (feof($yate_stdin))
		return false;
	    $line=fgets($yate_stdin,$yate_buffer);
	    // check for async read no data
	    if ($line == false)
		return true;
	}
	$line=str_replace("\n", "", $line);
	if ($line == "")
	    return true;
	$ev=true;
	$part=explode(":", $line);
	switch ($part[0]) {
	    case "%%>message":
		/* incoming message str_id:int_time:str_name:str_retval[:key=value...] */
		$ev=new Yate(Yate::Unescape($part[3]),Yate::Unescape($part[4]),Yate::Unescape($part[1]));
		$ev->type="incoming";
		$ev->origin=0+$part[2];
		$ev->FillParams($part,5);
		break;
	    case "%%<message":
		/* message answer str_id:bool_handled:str_name:str_retval[:key=value...] */
		$ev=new Yate(Yate::Unescape($part[3]),Yate::Unescape($part[4]),Yate::Unescape($part[1]));
		$ev->type="answer";
		$ev->handled=Yate::Str2bool($part[2]);
		$ev->FillParams($part,5);
		break;
	    case "%%<install":
		/* install answer num_priority:str_name:bool_success */
		$ev=new Yate(Yate::Unescape($part[2]),"",0+$part[1]);
		$ev->type="installed";
		$ev->handled=Yate::Str2bool($part[3]);
		break;
	    case "%%<uninstall":
		/* uninstall answer num_priority:str_name:bool_success */
		$ev=new Yate(Yate::Unescape($part[2]),"",0+$part[1]);
		$ev->type="uninstalled";
		$ev->handled=Yate::Str2bool($part[3]);
		break;
	    case "%%<watch":
		/* watch answer str_name:bool_success */
		$ev=new Yate(Yate::Unescape($part[1]));
		$ev->type="watched";
		$ev->handled=Yate::Str2bool($part[2]);
		break;
	    case "%%<unwatch":
		/* unwatch answer str_name:bool_success */
		$ev=new Yate(Yate::Unescape($part[1]));
		$ev->type="unwatched";
		$ev->handled=Yate::Str2bool($part[2]);
		break;
	    case "%%<connect":
		/* connect answer str_role:bool_success */
		$ev=new Yate(Yate::Unescape($part[1]));
		$ev->type="connected";
		$ev->handled=Yate::Str2bool($part[2]);
		break;
	    case "%%<setlocal":
		/* local parameter answer str_name:str_value:bool_success */
		$ev=new Yate(Yate::Unescape($part[1]),Yate::Unescape($part[2]));
		$ev->type="setlocal";
		$ev->handled=Yate::Str2bool($part[3]);
		break;
	    case "%%<quit":
		if ($yate_socket) {
		    socket_close($yate_socket);
		    $yate_socket = false;
		}
		else if ($yate_stdin) {
		    fclose($yate_stdin);
		    $yate_stdin = false;
		}
		return false;
	    case "Error in":
		/* We are already in error so better stay quiet */
		break;
	    default:
		Yate::Output("PHP parse error: " . $line);
	}
	return $ev;
    }

    /**
     * This static function initializes globals in the PHP Yate External Module.
     * It should be called before any other method.
     * @param $async (optional) True if asynchronous, polled mode is desired
     * @param $addr Hostname to connect to or UNIX socket path
     * @param $port TCP port to connect to, zero to use UNIX sockets
     * @param $role Role of this connection - "global" or "channel"
     * @return True if initialization succeeded, false if failed
     */
    static function Init($async = false, $addr = "", $port = 0, $role = "", $buffer = 8192)
    {
	global $yate_stdin, $yate_stdout, $yate_stderr;
	global $yate_socket, $yate_buffer, $yate_debug, $yate_output;
	$yate_debug = false;
	$yate_stdin = false;
	$yate_stdout = false;
	$yate_stderr = false;
	$yate_output = false;
	$yate_socket = false;
	if ($buffer < 2048)
	    $buffer = 2048;
	else if ($buffer > 65536)
	    $buffer = 65536;
	$yate_buffer = $buffer;
	if ($addr) {
	    $ok = false;
	    if (!function_exists("socket_create")) {
		$yate_stderr = fopen("php://stderr","w");
		Yate::Output("PHP sockets missing, initialization failed");
		return false;
	    }
	    if ($port) {
		$yate_socket = @socket_create(AF_INET,SOCK_STREAM,SOL_TCP);
		$ok = @socket_connect($yate_socket,$addr,$port);
	    }
	    else {
		$yate_socket = @socket_create(AF_UNIX,SOCK_STREAM,0);
		$ok = @socket_connect($yate_socket,$addr,$port);
	    }
	    if (($yate_socket === false) || !$ok) {
		$yate_socket = false;
		$yate_stderr = fopen("php://stderr","w");
		Yate::Output("Socket error, initialization failed");
		return false;
	    }
	    $yate_output = true;
	}
	else {
	    $yate_stdin = fopen("php://stdin","r");
	    $yate_stdout = fopen("php://stdout","w");
	    $yate_stderr = fopen("php://stderr","w");
	    $role = "";
	    flush();
	}
	set_error_handler("_yate_error_handler");
	ob_implicit_flush(1);
	if ($async && function_exists("stream_set_blocking") && $yate_stdin)
	    stream_set_blocking($yate_stdin,false);
	if ($role)
	    _yate_print("%%>connect:$role\n");
	return true;
    }

    /**
     * Send a quit command to the External Module
     * @param close Set to true to close the communication immediately
     */
    static function Quit($close = false)
    {
	global $yate_stdin, $yate_socket;
	_yate_print("%%>quit\n");
	if ($close) {
	    if ($yate_socket) {
		socket_close($yate_socket);
		$yate_socket = false;
	    }
	    else if ($yate_stdin) {
		fclose($yate_stdin);
		$yate_stdin = false;
	    }
	}
    }

}

/* Internal error handler callback - output plain text to stderr */
function _yate_error_handler($errno, $errstr, $errfile, $errline)
{
    $str = " [$errno] $errstr in $errfile line $errline\n";
    switch ($errno) {
	case E_USER_ERROR:
	    Yate::Output("PHP fatal: $str");
	    exit(1);
	    break;
	case E_WARNING:
	case E_USER_WARNING:
	    Yate::Output("PHP error: $str");
	    break;
	case E_NOTICE:
	case E_USER_NOTICE:
	    Yate::Output("PHP warning: $str");
	    break;
	default:
	    Yate::Output("PHP unknown error: $str");
    }
}

/* Internal function */
function _yate_print($str)
{
    global $yate_stdout, $yate_socket;
    if ($yate_socket)
	@socket_write($yate_socket, $str);
    else if ($yate_stdout)
	fputs($yate_stdout, $str);
}

/* Internal function */
function _yate_message_walk($item, $key, &$result)
{
    // workaround to pass by reference the 3rd parameter to message_walk:
    // the reference is placed in an array and the array is passed by value
    // taken from: http://hellsgate.online.ee/~glen/array_walk2.php
    $result[0] .= ':' . Yate::Escape($key,'=') . '=' . Yate::Escape($item);
}

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
