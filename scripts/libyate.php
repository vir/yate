<?

/* libyate.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * PHP object-oriented interface library for Yate
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
    function Output($str)
    {
	global $yate_stderr;
	fputs($yate_stderr, $str . "\n");
    }

    /**
     * Static replacement error handler that outputs plain text to stderr
     * @param $errno Error code
     * @param $errstr Error text
     * @param $errfile Name of file that triggered the error
     * @param $errline Line number where the error occured
     */
    function ErrorHandler($errno, $errstr, $errfile, $errline)
    {
	$str = " [$errno] $errstr in $errfile line $errline\n";
	switch ($errno) {
	    case E_USER_ERROR:
		Yate::Output("PHP fatal:" . $str);
		exit(1);
		break;
	    case E_WARNING:
	    case E_USER_WARNING:
		Yate::Output("PHP error:" . $str);
		break;
	    case E_NOTICE:
	    case E_USER_NOTICE:
		Yate::Output("PHP warning:" . $str);
		break;
	    default:
		Yate::Output("PHP unknown error:" . $str);
	}
    }

    /**
     * Static function to convert a Yate string representation to a boolean
     * @param $str String value to convert
     * @return True if $str is "true", false otherwise
     */
    function Str2bool($str)
    {
	return ($str == "true") ? true : false;
    }

    /**
     * Static function to convert a boolean to a Yate string representation
     * @param $bool Boolean value to convert
     * @return The string "true" if $bool was true, "false" otherwise
     */
    function Bool2str($bool)
    {
	return $bool ? "true" : "false";
    }

    /**
     * Static function to convert a string to its Yate escaped format
     * @param $str String to escape
     * @param $extra (optional) Character to escape in addition to required ones
     * @return Yate escaped string
     */
    function Escape($str, $extra = "")
    {
	$s = "";
	$n = strlen($str);
	for ($i=0; $i<$n; $i++) {
	    $c = $str{$i};
	    if ((ord($c) < 32) || ($c == $extra)) {
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
    function Unescape($str)
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
     */
    function Install($name, $priority = 100)
    {
	$name=Yate::Escape($name);
	print "%%>install:$priority:$name\n";
    }

    /**
     * Uninstall a Yate message handler
     * @param $name Name of the messages to stop handling
     */
    function Uninstall($name)
    {
	$name=Yate::Escape($name);
	print "%%>uninstall:$name\n";
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
     * Fill the parameter array from a text representation
     * @param $parts A numerically indexed array with the key=value parameters
     * @param $offs (optional) Offset in array to start processing from
     */
    function FillParams(&$parts, $offs = 0)
    {
	$n = count($parts);
	for ($i=$offs; $i<$n; $i++) {
	    $s=explode('=',$parts[$i]);
	    $this->params[Yate::Unescape($s[0])]=Yate::Unescape($s[1]);
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
	$i=Yate::Escape($this->id,':');
	$t=0+$this->origin;
	$n=Yate::Escape($this->name,':');
	$r=Yate::Escape($this->retval,':');
	$p="";
	array_walk(&$this->params, "_yate_message_walk", &$p);
	print "%%>message:$i:$t:$n:$r$p\n";
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
	$i=Yate::Escape($this->id,':');
	$k=Yate::Bool2str($this->handled);
	$n=Yate::Escape($this->name,':');
	$r=Yate::Escape($this->retval,':');
	$p="";
	array_walk(&$this->params, "_yate_message_walk", &$p);
	print "%%<message:$i:$k:$n:$r$p\n";
	$this->type="acknowledged";
    }

    /**
     * This static function processes just one input line.
     * It must be called in a loop to keep messages running. Or else.
     * @return "EOF" if we should exit, "" if we should keep running,
     *  or an Yate object instance
     */
    function GetEvent()
    {
	global $yate_stdin;
	if (feof($yate_stdin))
	    return "EOF";
	$line=fgets($yate_stdin,4096);
	if ($line == false)
	    return "";
	$line=str_replace("\n", "", $line);
	if ($line == "")
	    return "";
	$ev="";
	$part=explode(":", $line);
	switch ($part[0]) {
	    case "%%>message":
		/* incoming message str_id:int_time:str_name:str_retval[:key=value...] */
		$ev=new Yate(Yate::Unescape($part[3]),Yate::Unescape($part[4]),Yate::Unescape($part[1]));
		$ev->type="incoming";
		$ev->origin=0+$part[2];
		$ev->FillParams(&$part,5);
		break;
	    case "%%<message":
		/* message answer str_id:bool_handled:str_name:str_retval[:key=value...] */
		$ev=new Yate(Yate::Unescape($part[3]),Yate::Unescape($part[4]),Yate::Unescape($part[1]));
		$ev->type="answer";
		$ev->handled=Yate::Str2bool($part[2]);
		$ev->FillParams(&$part,5);
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
	    default:
		Yate::Output("PHP parse error: " . $line);
	}
	return $ev;
    }

    /**
     * This static function initializes globals in the PHP Yate External Module.
     * It should be called before any other method.
     * @param $async (optional) True if asynchronous, polled mode is desired
     */
    function Init($async = false)
    {
	global $yate_stdin, $yate_stdout, $yate_stderr;
	$yate_stdin = fopen("php://stdin","r");
	$yate_stdout = fopen("php://stdout","w");
	$yate_stderr = fopen("php://stderr","w");
	flush();
	set_error_handler("Yate::ErrorHandler");
	ob_implicit_flush(1);
	if ($async && function_exists("stream_set_blocking"))
	    stream_set_blocking($yate_stdin,false);
    }

}

/* Internal function */
function _yate_message_walk($item, $key, &$result)
{
    $result .= ':' . Yate::Escape($key,':') . '=' . Yate::Escape($item,':');
}

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
