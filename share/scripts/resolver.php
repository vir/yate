#!/usr/bin/php -q
<?php
/**
 * resolver.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2009-2014 Null Team
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

/* Caching resolver script for the Yate PHP interface
   Needs php pear Net_DNS http://pear.php.net/package/Net_DNS

   Intercepts call.execute and turns hostnames into IP addresses.
   Example: sip/sip:123@example.com:5060 -> sip/sip:123@10.0.0.1:5060

   If multiple A records are returned a random one will be picked from cache
   for each call. The TTL of each returned address is capped.

   When SIP calls fail with error 408 (No gateway response) the address to
   which the failed call was sent to is removed from cache.

   To use add in extmodule.conf

   [scripts]
   resolver.php=
*/
require_once("libyate.php");
require_once("Net/DNS.php");

// Always the first action to do
Yate::Init();
// Uncomment next line to see debug messages
Yate::Debug(true);

$maxttl = 120; // limit to the TTL we accept, in seconds
$cache = array();
$resolver = new Net_DNS_Resolver();
// The following can be commented out to use defaults
$resolver->retry = 3; // resolver retry, in seconds
$resolver->retrans = 2; // resolver repeat count

class Cached {
    var $address;
    var $expires;

    function Cached($addr,$ttl)
    {
	$this->address = $addr;
	$this->expires = $ttl + time();
    }
}

function resolve($host)
{
    global $resolver, $cache, $maxttl;

    if (isset($cache[$host])) {
	Yate::Debug("Found host $host in cache, checking...");
	$t = time();
	$changed = false;
	foreach ($cache[$host] as $idx => $cached) {
	    if ($t >= $cached->expires) {
		Yate::Debug("Expiring address " . $cached->address);
		unset($cache[$host][$idx]);
		$changed = true;
	    }
	}
	$c = count($cache[$host]);
	if ($c != 0) {
	    if ($changed)
		$cache[$host] = array_values($cache[$host]);
	    return $cache[$host][rand(0,$c-1)]->address;
	}
	Yate::Debug("Expiring entire host $host");
	unset($cache[$host]);
    }

    Yate::Debug("Resolving host $host");
    $pkt = $resolver->query($host,'A','IN');
    if (!$pkt)
	return false;
    $entries = array();
    foreach ($pkt->answer as $rr) {
	$ttl = $rr->ttl;
	if ($ttl <= 0 || $ttl > $maxttl)
	    $ttl = $maxttl;
	$entries[] = new Cached($rr->address,$ttl);
    }
    $c = count($entries);
    Yate::Debug("Caching $c addresses for host $host");
    $cache[$host] = $entries;
    return $entries[rand(0,$c-1)]->address;
}

function invalidate($address)
{
    global $cache;
    foreach($cache as $host => &$entries) {
	$changed = false;
	foreach ($entries as $idx => $cached) {
	    if ($cached->address == $address) {
		Yate::Output("Invalidating $address of host $host");
		unset($entries[$idx]);
		$changed = true;
	    }
	}
	if (count($entries) == 0) {
	    Yate::Debug("Invalidating entire host $host");
	    unset($cache[$host]);
	}
	else if ($changed)
	    $cache[$host] = array_values($cache[$host]);
    }
}

Yate::SetLocal("trackparam","resolver.php");
Yate::Install("call.execute",80);
Yate::Install("chan.hangup",120,"cause_sip","408");
Yate::SetLocal("restart",true);

function _disable_warnings_handler($errno, $errstr, $errfile, $errline)
{
    switch ($errno) {
	case E_USER_ERROR:
	case E_WARNING:
	case E_USER_WARNING:
	    _yate_error_handler($errno, $errstr, $errfile, $errline);
	default:
	    if (!strpos($errfile,'Net/DNS'))
		_yate_error_handler($errno, $errstr, $errfile, $errline);
    }
}
// Net_DNS has static call issues so disable its many warnings...
set_error_handler("_disable_warnings_handler");

/* The main loop. We pick events and handle them */
for (;;) {
    $ev=Yate::GetEvent();
    if ($ev === false)
        break;
    if ($ev === true)
        continue;
    if ($ev->type == "incoming") {
	switch ($ev->name) {
	    case "call.execute":
		$callto = $ev->GetValue("callto");
		$parts = array();
		// Separate host part from called resource
		if (eregi('^([a-z]+/)([0-9a-z]+:)?([^@:]+@)?([0-9a-z.-]+)(.*)$',$callto,$parts)) {
		    $host = $parts[4];
		    // Check if it's not a dotted IPv4
		    if (ereg('[a-z-]',$host)) {
			$callto = resolve($host);
			if ($callto) {
			    Yate::Debug("Resolved '$host' to '$callto'");
			    $callto = $parts[1] . $parts[2] . $parts[3] . $callto . $parts[5];
			    $ev->SetParam("callto",$callto);
			}
		    }
		}
		break;
	    case "chan.hangup":
		if ($ev->GetValue("status") == "outgoing") {
		    $address = $ev->GetValue("address");
		    $parts = array();
		    if (ereg('^([0-9.]+)(:[0-9]+)?',$address,$parts))
			invalidate($parts[1]);
		}
		break;
	}
	$ev->Acknowledge();
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
