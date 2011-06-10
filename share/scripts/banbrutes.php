#!/usr/bin/php -q
<?php
/* Brute force attack detect and ban for the Yate PHP interface
   Add in extmodule.conf

  [scripts]
  banbrutes.php=
 */

// How many failures in a row cause a ban
$ban_failures = 5;
// In how many seconds to clear a gray host
$clear_gray = 10;
// In how many seconds to clear a blacklisted host
$clear_black = 600;
// Command to ban an address
$cmd_ban = "iptables -I INPUT -s \$addr -j DROP";
// Command to unban an address
$cmd_unban = "iptables -D INPUT -s \$addr -j DROP";


require_once("libyate.php");

$banHelp = "  banbrutes [unban address]\r\n";

$hosts = array();

class Host
{
    var $fail;
    var $when;

    function Host()
    {
	global $clear_gray;
	$this->fail = 1;
	$this->when = time() + $clear_gray;
    }

    function failed()
    {
	global $ban_failures;
	global $clear_gray;
	global $clear_black;
	if ($this->fail <= 0)
	    return false;
	$this->fail++;
	if ($this->fail >= $ban_failures) {
	    $this->fail = 0;
	    $this->when = time() + $clear_black;
	    return true;
	}
	$this->when = time() + $clear_gray;
	return false;
    }

    function banned()
    {
	return $this->fail <= 0;
    }

    function timer($now)
    {
	return $now >= $this->when;
    }
}

function updateAuth($addr,$ok)
{
    global $hosts;
    global $cmd_ban;
    if ($ok) {
	if (isset($hosts[$addr])) {
	    Yate::Debug("Good host: $addr");
	    unset($hosts[$addr]);
	}
	return;
    }
    if (isset($hosts[$addr])) {
	if ($hosts[$addr]->failed()) {
	    $cmd = eval('return "'.$cmd_ban.'";');
	    Yate::Output("Executing: $cmd");
	    shell_exec($cmd);
	}
    }
    else {
	Yate::Debug("New gray host: $addr");
	$hosts[$addr] = new Host();
    }
}

function onTimer()
{
    global $hosts;
    global $cmd_unban;
    $now = time();
    foreach ($hosts as $addr => &$host) {
	if ($host->timer($now)) {
	    if ($host->banned()) {
		$cmd = eval('return "'.$cmd_unban.'";');
		Yate::Output("Executing: $cmd");
		shell_exec($cmd);
	    }
	    else
		Yate::Debug("Expired host: $addr");
	    unset($hosts[$addr]);
	}
    }
}

function onCommand($l,&$retval)
{
    global $hosts;
    global $cmd_unban;
    if ($l == "banbrutes") {
	$retval = "";
	$now = time();
	foreach ($hosts as $addr => &$host) {
	    if ($retval != "")
		$retval .= ",";
	    if ($host->banned()) {
		$t = $host->when - $now;
		$retval .= "$addr=banned:$t";
	    }
	    else
		$retval .= "$addr=gray:".$host->fail;
	}
	$retval .= "\r\n";
	return true;
    }
    if (strpos($l,"banbrutes unban ") === 0) {
	$addr = substr($l,16);
	if (isset($hosts[$addr])) {
	    if ($hosts[$addr]->banned()) {
		$cmd = eval('return "'.$cmd_unban.'";');
		Yate::Output("Executing: $cmd");
		shell_exec($cmd);
		unset($hosts[$addr]);
		$retval = "Unbanned: $addr\r\n";
	    }
	    else {
		unset($hosts[$addr]);
		$retval = "Removed from gray list: $addr\r\n";
	    }
	}
	else
	    $retval = "Not banned: $addr\r\n";
	return true;
    }
    return false;
}

function oneCompletion(&$ret,$str,$part)
{
    if (($part != "") && (strpos($str,$part) !== 0))
	return;
    if ($ret != "")
	$ret .= "\t";
    $ret .= $str;
}

function onComplete(&$ev,$l,$w)
{
    global $hosts;
    if ($l == "")
	oneCompletion($ev->retval,"banbrutes",$w);
    else if ($l == "help")
	oneCompletion($ev->retval,"banbrutes",$w);
    else if ($l == "banbrutes")
	oneCompletion($ev->retval,"unban",$w);
    else if ($l == "banbrutes unban") {
	foreach ($hosts as $addr => &$host) {
	    if ($host->banned())
		oneCompletion($ev->retval,$addr,$w);
	}
    }
}

function onHelp($l,&$retval)
{
    global $banHelp;
    if ($l) {
	if ($l == "banbrutes") {
	    $retval = "${banHelp}Automatically block brute force attackers\r\n";
	    return true;
	}
	return false;
    }
    $retval .= $banHelp;
    return false;
}

Yate::Init();
// Comment the next line to get output only in logs, not in rmanager
Yate::Output(true);
// Uncomment the next line to get debugging details
//Yate::Debug(true);

Yate::Watch("user.auth");
Yate::Install("engine.timer",150);
Yate::Install("engine.command",120);
Yate::Install("engine.help",150);
Yate::SetLocal("restart",true);

for (;;) {
    $ev=Yate::GetEvent();
    if ($ev === false)
        break;
    if ($ev === true)
        continue;
    if ($ev->type == "incoming") {
	switch ($ev->name) {
	    case "engine.timer":
		onTimer();
		break;
	    case "engine.command":
		if ($ev->GetValue("line"))
		    $ev->handled = onCommand($ev->GetValue("line"),$ev->retval);
		else
		    onComplete($ev,$ev->GetValue("partline"),$ev->GetValue("partword"));
		break;
	    case "engine.help":
		$ev->handled = onHelp($ev->GetValue("line"),$ev->retval);
		break;
	}
	$ev->Acknowledge();
    }
    if ($ev->type == "answer") {
	// This is the watched user.auth
	$addr = $ev->GetValue("ip_host");
	if ($addr != "")
	    updateAuth($addr,$ev->handled && ($ev->retval != "-"));
    }
}

Yate::Output("banbrutes: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
