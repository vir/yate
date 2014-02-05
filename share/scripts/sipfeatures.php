#!/usr/bin/php -q
<?php
/**
 * sipfeatures.php
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2007-2014 Null Team
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

/* Sample script for the Yate PHP interface.
   Implements SIP SUBSCRIBE and NOTIFY for voicemail and dialog state.

   To test add in extmodule.conf

   [scripts]
   sipfeatures.php=
*/
require_once("libyate.php");
require_once("libvoicemail.php");

$next = 0;

// list of user voicemail subscriptions
$users = array();
// list of active channels (dialog) subscriptions
$chans = array();
// list of active presence subscriptions
$pres = array();

// Abstract subscription object
class Subscription {
    var $match;
    var $index;
    var $event;
    var $media;
    var $host;
    var $port;
    var $uri;
    var $from;
    var $to;
    var $callid;
    var $contact;
    var $connid;
    var $state;
    var $body = "";
    var $expire = 0;
    var $pending = false;

    // Constructor, fills internal variables and sends initial/final notifications
    function Subscription($ev,$event,$media)
    {
	$this->event = $event;
	$this->media = $media;
	$this->host = $ev->params["ip_host"];
	$this->port = $ev->params["ip_port"];
	$this->uri = $ev->params["sip_uri"];
	// allow the mailbox to be referred as vm-NUMBER (or anything-NUMBER)
	ereg(":([a-z]*-)?([^:@]+)@",$this->uri,$regs);
	$this->match = $regs[2];
	$this->index = $event .":". $this->match .":". $this->host .":". $this->port;
	Yate::Debug("Will match: " . $this->match . " index " . $this->index);
	$this->from = $ev->GetValue("sip_from");
	$this->to = $ev->GetValue("sip_to");
	if (strpos($this->to,'tag=') === false)
	    $this->to .= ';tag=' . $ev->GetValue("xsip_dlgtag");
	$this->callid = $ev->params["sip_callid"];
	$this->contact = $ev->params["sip_contact"];
	$this->connid = $ev->params["connection_id"];
	$exp = $ev->params["sip_expires"];
	if ($exp == "0") {
	    // this is an unsubscribe
	    $this->expire = 0;
	    $this->state = "terminated";
	}
	else {
	    $exp = $exp + 0;
	    if ($exp <= 0)
		$exp = 3600;
	    else if ($exp < 60)
		$exp = 60;
	    else if ($exp > 86400)
		$exp = 86400;
	    $this->expire = time() + $exp;
	    $this->state = "active";
	}
	$this->body = "";
	$this->pending = true;
    }

    // Send out a NOTIFY
    function Notify($state = false)
    {
	Yate::Debug("Notifying event " . $this->event . " for " . $this->match);
	if ($state !== false) {
	    $this->state = $state;
	    $this->pending = true;
	}
	if ($this->body == "") {
	    Yate::Output("Empty body in event " . $this->event . " for " . $this->match);
	    return;
	}
	$this->pending = false;
	$m = new Yate("xsip.generate");
	$m->id = "";
	$m->params["method"] = "NOTIFY";
	$m->params["uri"] = $this->contact;
	$m->params["host"] = $this->host;
	$m->params["port"] = $this->port;
	if (strlen($this->connid) > 0)
	    $m->params["connection_id"] = $this->connid;
	$m->params["sip_Call-ID"] = $this->callid;
	$m->params["sip_From"] = $this->to;
	$m->params["sip_To"] = $this->from;
	$m->params["sip_Contact"] = "<" . $this->uri . ">";
	$m->params["sip_Event"] = $this->event;
	$m->params["sip_Subscription-State"] = $this->state;
	$m->params["xsip_type"] = $this->media;
	$m->params["xsip_body"] = $this->body;
	$m->Dispatch();
    }

    // Add this object to the parent list
    function AddTo(&$list)
    {
	$list[$this->index] = &$this;
    }

    // Emit any pending NOTIFY
    function Flush()
    {
	if ($this->pending)
	    $this->Notify();
    }

    // Check expiration of this subscription, prepares timeout notification
    function Expire()
    {
	if ($this->expire == 0)
	    return false;
	if ($this->expire >= time())
	    return false;
	Yate::Debug("Expired event " . $this->event . " for " . $this->match);
	$this->expire = 0;
	$this->Notify("terminated;reason=timeout");
	return true;
    }

}

// Mailbox status subscription
class MailSub extends Subscription {

    function MailSub($ev) {
	parent::Subscription($ev,"message-summary","application/simple-message-summary");
    }

    // Update count of messages, calls voicemail library function
    function Update($ev,$id)
    {
	vmGetMessageStats($this->match,$tot,$unr);
	Yate::Debug("Messages: $unr/$tot for " . $this->match);
	if ($tot > 0) {
	    $mwi = ($unr > 0) ? "yes" : "no";
	    $this->body = "Messages-Waiting: $mwi\r\nVoice-Message: $unr/$tot\r\n";
	}
	else
	    $this->body = "Messages-Waiting: no\r\n";
	$this->pending = true;
    }

}

// SIP dialog status subscription
class DialogSub extends Subscription {

    var $version;

    function DialogSub($ev) {
	parent::Subscription($ev,"dialog","application/dialog-info+xml");
	$this->version = 0;
    }

    // Update dialog state based on CDR message parameters
    function Update($ev,$id)
    {
	$st = "";
	$dir = "";
	switch ($ev->GetValue("operation")) {
	    case "initialize":
		$st = "trying";
		break;
	    case "finalize":
		$st = "terminated";
		break;
	    default:
		switch ($ev->GetValue("status")) {
		    case "connected":
		    case "answered":
			$st = "confirmed";
			break;
		    case "incoming":
		    case "outgoing":
		    case "calling":
		    case "ringing":
		    case "progressing":
			$st = "early";
			break;
		    case "redirected":
			$st = "rejected";
			break;
		    case "destroyed":
			$st = "terminated";
			break;
		}
	}
	if ($st != "") {
	    // directions are reversed because we are talking about the remote end
	    switch ($ev->GetValue("direction")) {
		case "incoming":
		    $dir = "initiator";
		    break;
		case "outgoing":
		    $dir = "recipient";
		    break;
	    }
	    if ($dir != "")
		$dir = " direction=\"$dir\"";
	}
	else
	    $id = false;
	Yate::Debug("Dialog updated, st: '$st' id: '$id'");
	$this->body = "<?xml version=\"1.0\"?>\r\n";
	$this->body .= "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\"" . $this->version . "\" entity=\"" . $this->uri . "\" notify-state=\"full\">\r\n";
	if ($id) {
	    $uri = ereg_replace("^.*<([^<>]+)>.*\$","\\1",$this->uri);
	    $tag = $this->match;
	    $tag = " call-id=\"$id\" local-tag=\"$tag\" remote-tag=\"$tag\"";
	    $this->body .= "  <dialog id=\"$id\"$tag$dir>\r\n";
	    $this->body .= "    <state>$st</state>\r\n";
	    $this->body .= "    <remote><target uri=\"$uri\"/></remote>\r\n";
	    $this->body .= "  </dialog>\r\n";
	}
	$this->body .= "</dialog-info>\r\n";
	$this->version++;
	$this->pending = true;
	if ($id)
	    $this->Flush();
    }
}

// Presence subscription
class PresenceSub extends Subscription {

    function PresenceSub($ev) {
	parent::Subscription($ev,"presence","application/pidf+xml");
	$this->body = '<?xml version="1.0" encoding="UTF-8"?><presence xmlns="urn:ietf:params:xml:ns:pidf"></presence>';
    }

    // Update presence of users
    function Update($ev,$id)
    {
	$body = $ev->GetValue("xsip_body");
	if ($body != "") {
	    Yate::Debug("Presence: for " . $this->match);
	    $this->body = $body;
	    $this->pending = true;
	    $this->Flush();
	}
    }
}

// Update all subscriptions in a $list that match a given $key
function updateAll(&$list,$key,$ev,$id = false)
{
    if (!$key)
	return;
    $count = 0;
    foreach ($list as &$item) {
	if ($item->match == $key) {
	    $item->Update($ev,$id);
	    $count++;
	}
    }
    Yate::Debug("Updated $count subscriptions for '$key'");
}

// Flush all pending notifies for subscriptions in a $list
function flushAll(&$list)
{
    foreach ($list as &$item)
	$item->Flush();
}

// Check all subscriptions in $list for expiration, notifies and removes them
function expireAll(&$list)
{
    foreach ($list as $index => &$item) {
	if ($item->Expire()) {
	    $list[$index] = null;
	    unset($list[$index]);
	}
    }
}

// List subscriptions in $list to a $text variable
function dumpAll(&$list,&$text)
{
    foreach ($list as &$item) {
	$e = $item->expire;
	if ($e > 0)
	    $e -= time();
	$text .= $item->index . " expires in $e\r\n";
    }
}

// SIP SUBSCRIBE handler
function onSubscribe($ev)
{
    global $users;
    global $chans;
    global $pres;
    $event = $ev->GetValue("sip_event");
    $accept = $ev->GetValue("sip_accept");
    if (($event == "message-summary") && ($accept == "application/simple-message-summary" || $accept == "")) {
	$s = new MailSub($ev);
	$s->AddTo($users);
	Yate::Debug("New mail subscription for " . $s->match);
    }
    else if (($event == "dialog") && ($accept == "application/dialog-info+xml" || $accept == "")) {
	$s = new DialogSub($ev);
	$s->AddTo($chans);
	Yate::Debug("New dialog subscription for " . $s->match);
    }
    else if (($event == "presence") && ($accept == "application/pidf+xml" || $accept == "")) {
	$s = new PresenceSub($ev);
	$s->AddTo($pres);
	Yate::Debug("New presence subscription for " . $s->match);
    }
    else
	return false;
    $s->Update($ev,false);
    $s->Flush();
    // Return expires in OK
    $exp = $s->expire - time();
    if ($exp < 0)
	$exp = 0;
    $ev->params["osip_Expires"] = strval($exp);
    return true;
}

// SIP PUBLISH handler
function onPublish($ev)
{
    global $pres;
    updateAll($pres,$ev->GetValue("caller"),$ev);
    return true;
}

// User status (mailbox) update handler
function onUserUpdate($ev)
{
    global $users;
    updateAll($users,$ev->GetValue("user"),$ev);
}

// Channel status (dialog) handler, not currently used
function onChanUpdate($ev)
{
    global $chans;
    updateAll($chans,$ev->GetValue("id"),$ev);
}

// CDR handler for channel status update
function onCdr($ev)
{
    global $chans;
    updateAll($chans,$ev->GetValue("external"),$ev,$ev->GetValue("chan"));
}

// Timer message handler, expires and flushes subscriptions
function onTimer($t)
{
    global $users;
    global $chans;
    global $pres;
    global $next;
    if ($t < $next)
	return;
    // Next check in 15 seconds
    $next = $t + 15;
    // Yate::Debug("Expiring aged subscriptions at $t");
    expireAll($users);
    expireAll($chans);
    expireAll($pres);
    // Yate::Debug("Flushing pending subscriptions at $t");
    flushAll($users);
    flushAll($chans);
    flushAll($pres);
}

// Command message handler
function onCommand($l,&$retval)
{
    global $users;
    global $chans;
    global $pres;
    if ($l == "sippbx list") {
	$retval .= "Subscriptions:\r\n";
	dumpAll($users,$retval);
	dumpAll($chans,$retval);
	dumpAll($pres,$retval);
	return true;
    }
    return false;
}

/* Always the first action to do */
Yate::Init();

Yate::Output(true);
Yate::Debug(true);

Yate::SetLocal("restart",true);

Yate::Install("sip.subscribe");
Yate::Install("sip.publish");
Yate::Install("user.update");
Yate::Install("chan.update");
Yate::Install("call.cdr");
Yate::Install("engine.timer");
Yate::Install("engine.command");

/* Create and dispatch an initial test message */
/* The main loop. We pick events and handle them */
for (;;) {
    $ev=Yate::GetEvent();
    /* If Yate disconnected us then exit cleanly */
    if ($ev === false)
        break;
    /* Empty events are normal in non-blocking operation.
       This is an opportunity to do idle tasks and check timers */
    if ($ev === true) {
        continue;
    }
    /* If we reached here we should have a valid object */
    switch ($ev->type) {
	case "incoming":
	    switch ($ev->name) {
		case "engine.timer":
		    $ev->Acknowledge();
		    onTimer($ev->GetValue("time"));
		    $ev = false;
		    break;
		case "engine.command":
		    $ev->handled = onCommand($ev->GetValue("line"),$ev->retval);
		    break;
		case "sip.subscribe":
		    $ev->handled = onSubscribe($ev);
		    break;
		case "sip.publish":
		    $ev->handled = onPublish($ev);
		    break;
		case "user.update":
		    $ev->Acknowledge();
		    onUserUpdate($ev);
		    $ev = false;
		    break;
		case "chan.update":
		    $ev->Acknowledge();
		    onChanUpdate($ev);
		    $ev = false;
		    break;
		case "call.cdr":
		    $ev->Acknowledge();
		    onCdr($ev);
		    $ev = false;
		    break;
	    }
	    if ($ev)
		$ev->Acknowledge();
	    break;
	case "answer":
	    // Yate::Debug("PHP Answered: " . $ev->name . " id: " . $ev->id);
	    break;
	case "installed":
	    // Yate::Debug("PHP Installed: " . $ev->name);
	    break;
	case "uninstalled":
	    // Yate::Debug("PHP Uninstalled: " . $ev->name);
	    break;
	default:
	    Yate::Output("PHP Event: " . $ev->type);
    }
}

Yate::Output("PHP: bye!");

/* vi: set ts=8 sw=4 sts=4 noet: */
?>
