#!/usr/bin/php -q
<?php
/**
 * banbrutes.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2011-2014 Null Team
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

/* Brute force attack detect and ban for the Yate PHP interface
   Add in extmodule.conf

  [scripts]
  banbrutes.php=
    or
  banbrutes.php=NNN
    where NNN >= 2 is the number of failures causing a ban

  If you are using SIP proxies or clients with multiple subscriptions you will need to
   allow more failures for each since each separate transaction will fail once

  This script requires Yate to run as root or have permissions to run iptables
 */

// How many failures in a row cause a ban
$ban_failures = 10;
// In how many seconds to clear a gray host
$clear_gray = 10;
// In how many seconds to clear a blacklisted host
$clear_black = 600;
// Command to ban an address
$cmd_ban = "iptables -I INPUT -s \$addr -j DROP";
// Command to unban an address
$cmd_unban = "iptables -D INPUT -s \$addr -j DROP";


require_once("libyate.php");

$banHelp = "  banbrutes [list|unban address|debug on/off|failures NN]\r\n";

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

    function success()
    {
	if ($this->fail > 0) {
	    $this->fail = 0;
	    $this->when = time() + 2;
	}
    }

    function failed()
    {
	global $ban_failures;
	global $clear_gray;
	global $clear_black;
	if ($this->fail < 0)
	    return false;
	$this->fail++;
	if ($this->fail >= $ban_failures) {
	    $this->fail = -1;
	    $this->when = time() + $clear_black;
	    return true;
	}
	$this->when = time() + $clear_gray;
	return false;
    }

    function banned()
    {
	return $this->fail < 0;
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
    global $cmd_unban;
    if ($ok) {
	if (isset($hosts[$addr]))
	    $hosts[$addr]->success();
	return;
    }
    if (isset($hosts[$addr])) {
	if ($hosts[$addr]->failed()) {
	    $cmd = eval('return "'.$cmd_ban.'";');
	    Yate::Output("banbrutes: $cmd");
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
		Yate::Output("banbrutes: $cmd");
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
    global $ban_failures;
    global $cmd_unban;
    if ($l == "banbrutes") {
	$gray = 0;
	$banned = 0;
	foreach ($hosts as &$host) {
	    if ($host->banned())
		$banned++;
	    else
		$gray++;
	}
	$retval = "failures=${ban_failures},banned=${banned},gray=${gray}\r\n";
	return true;
    }
    else if ($l == "banbrutes list") {
	$retval = "";
	$now = time();
	foreach ($hosts as $addr => &$host) {
	    if ($retval != "")
		$retval .= ",";
	    if ($host->banned()) {
		$t = $host->when - $now;
		$retval .= "$addr=banned:${t}s";
	    }
	    else
		$retval .= "$addr=gray:".$host->fail;
	}
	$retval .= "\r\n";
	return true;
    }
    else if (strpos($l,"banbrutes unban ") === 0) {
	$addr = substr($l,16);
	if (isset($hosts[$addr])) {
	    if ($hosts[$addr]->banned()) {
		$cmd = eval('return "'.$cmd_unban.'";');
		Yate::Output("banbrutes: $cmd");
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
    else if (strpos($l,"banbrutes failures ") === 0) {
	$fail = 1 * substr($l,19);
	if ($fail > 1 && $fail <= 1000) {
	    $ban_failures = $fail;
	    return true;
	}
    }
    else if (strpos($l,"banbrutes debug ") === 0) {
	$dbg = substr($l,16);
	switch ($dbg) {
	    case "true":
	    case "yes":
	    case "on":
		Yate::Debug(true);
		return true;
	    case "false":
	    case "no":
	    case "off":
		Yate::Debug(false);
		return true;
	}
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
    else if ($l == "banbrutes") {
	oneCompletion($ev->retval,"list",$w);
	oneCompletion($ev->retval,"unban",$w);
	oneCompletion($ev->retval,"debug",$w);
	oneCompletion($ev->retval,"failures",$w);
    }
    else if ($l == "banbrutes unban") {
	foreach ($hosts as $addr => &$host) {
	    if ($host->banned())
		oneCompletion($ev->retval,$addr,$w);
	}
    }
    else if ($l == "banbrutes debug") {
	oneCompletion($ev->retval,"on",$w);
	oneCompletion($ev->retval,"off",$w);
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
// Uncomment the next line to get debugging details by default
//Yate::Debug(true);

$n = round(1 * Yate::Arg());
if ($n >= 2)
    $ban_failures = $n;

Yate::SetLocal("trackparam","banbrutes");
Yate::Watch("user.auth");
Yate::Watch("user.authfail");
Yate::Watch("engine.timer");
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
	switch ($ev->name) {
	    case "user.auth":
		$addr = $ev->GetValue("ip_host");
		if ($addr != "")
		    updateAuth($addr,$ev->handled && ($ev->retval != "-"));
		break;
	    case "user.authfail":
		$addr = $ev->GetValue("ip_host");
		if ($addr != "")
		    updateAuth($addr,false);
		break;
	    case "engine.timer":
		onTimer();
		break;
	}
    }
}

Yate::Output("banbrutes: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
