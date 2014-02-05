#!/usr/bin/python
# -*- coding: iso-8859-2; -*-
"""
 Utils module for YAYPM

 Copyright (C) 2005 Maciek Kaminski

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
"""

import logging, yaypm
from twisted.internet import reactor, defer
from twisted.python import failure
from yaypm import CancellableDeferred, TCPDispatcherFactory, AbandonedException

logger = logging.getLogger('yaypm.util')

def sleep(time, until = None):
    later = None
    def canceller(*args):
        if later and later.active:
            later.cancel()

    d = CancellableDeferred(canceller)
    later = reactor.callLater(time, d.callback, None)

    if until:
        def until_callback(m):
            if later.active():
                later.cancel()
                try:
                    raise AbandonedException(m)
                except:
                    d.errback(failure.Failure())

        until.addBoth(until_callback)
    return d

def setup(start, args = [], kwargs = {},
          host = "localhost", port = 5039,
          defaultLogging = True, runreactor = True):
    try:
        import yateproxy
        from yaypm import YateLogHandler
        embedded = True
    except Exception, e:
        embedded = False

    if embedded:
        yaypm.embeddedStart(start, args, kwargs)
        if defaultLogging:
            hdlr = YateLogHandler()
            formatter = ConsoleFormatter('%(message)s')

    else:
        reactor.connectTCP(host, port,
            TCPDispatcherFactory(start, args, kwargs))

        if defaultLogging:
            hdlr = logging.StreamHandler()
            formatter = ConsoleFormatter('%(name)s %(levelname)s %(message)s')

    if defaultLogging:
        hdlr.setFormatter(formatter)
        logger = logging.getLogger()

        logger.addHandler(hdlr)
        logger.setLevel(logging.DEBUG)

        yaypm.logger.setLevel(logging.INFO)
        yaypm.logger_messages.setLevel(logging.INFO)

    if not embedded and runreactor:
        reactor.run()


class XOR(CancellableDeferred):
    def __init__(self, *deferreds):
        defer.Deferred.__init__(self)
        self.deferreds = deferreds
        self.done = False
        index = 0
        for deferred in deferreds:
            deferred.addCallbacks(self._callback, self._callback,
                                  callbackArgs=(index,True),
                                  errbackArgs=(index,False))
            index = index + 1

    def _callback(self, result, index, succeeded):
        if self.done:
            return None
        self.done = True
        i = 0
        for deferred in self.deferreds:
            if index != i:
                deferred.addErrback(lambda _: None)
                deferred.cancel()
            i = i + 1

        if succeeded:
            self.callback((index, result))
        else:
            self.errback(result)

        return None

class OR(CancellableDeferred):
    def __init__(self, *deferreds, **kwargs):
        defer.Deferred.__init__(self)
        self.deferreds = deferreds
        self.done = False
        self.errors = 0
        self.patient = kwargs.get("patient", True)

        index = 0
        for deferred in deferreds:
            deferred.addCallbacks(self._callback, self._callback,
                                  callbackArgs=(index,True),
                                  errbackArgs=(index,False))
            index = index + 1

    def _callback(self, result, index, succeeded):

        i = 0
        for deferred in self.deferreds:
            if index != i:
                deferred.addErrback(lambda _: None)
            i = i + 1

        if succeeded:
            self.callback((index, result))
            self.done = True
        else:
            if self.patient:
                self.errors = self.errors + 1
                if self.errors == len(self.deferreds):
                    self.done = True
                    self.errback(result)
            else:
                self.done = True
                self.errback(result)
        return None


class RestrictedDispatcher:

    def __init__(self, parent, restriction):
        self.parent = parent
        self.restriction = restriction

    def msg(self, name, attrs = None, retValue = None):
        return self.parent.msg(name, attrs, retValue)

    def onmsg(self, name, guard = lambda _: True,
              until = None, autoreturn = False):
        if until:
            self.restriction.addCallbacks(d.cancel, d.cancel)
            d = until
        else:
            d = self.restriction

        return self.parent.onmsg(name, guard, d, autoreturn)

    def onwatch(self, name, guard = lambda _: True, until = None):

        return self.parent.onwatch(name, guard, until)

class ConsoleFormatter(logging.Formatter) :
    _level_colors  = {
      "DEBUG": "\033[22;32m", "INFO": "\033[01;34m",
      "WARNING": "\033[22;35m", "ERROR": "\033[22;31m",
      "CRITICAL": "\033[01;31m"
     };
    def __init__(self,
                 fmt = '%(name)s %(levelname)s %(message)s',
                 datefmt=None):
        logging.Formatter.__init__(self, fmt, datefmt)

    def format(self, record):
        if(ConsoleFormatter._level_colors.has_key(record.levelname)):
            record.levelname = "%s%s\033[0;0m" % \
                            (ConsoleFormatter._level_colors[record.levelname],
                             record.levelname)
        record.name = "\033[37m\033[1m%s\033[0;0m" % record.name
        return logging.Formatter.format(self, record)


class OutgoingCallException(Exception):
    """
    Raised when deferred is abandoned by until condition.

    """
    def __init__(self, cause):
        Exception.__init__(self, cause)
        self.cause = cause

def formatReason(msg):
    reason = msg["reason"] or msg["error"]
    if msg["code"]:
        reason += "(%s)" % msg["code"]
    return reason

@defer.inlineCallbacks
def outgoing(yate, target, maxcall = 30*1000,
             callto = "dumb/", formats = None,
             extra_attrs = {},
             retCallIdFast = False,
             until = None):

    logger.debug("Calling: %s" % target)

    attrs = {"callto": callto,
             "target": target,
             "maxcall": maxcall}

    if extra_attrs:
        attrs.update(extra_attrs)

    if formats:
        attrs["media_audio"] = "yes"
        attrs["formats_audio"] = formats

    execute = yate.msg("call.execute", attrs)

    if not (yield execute.dispatch()):
        raise OutgoingCallException(formatReason(execute))

    if execute["targetid"].startswith("fork/"):
        end = XOR(yate.onwatch("chan.disconnected",
                               lambda m : m["id"] == execute["id"] and m["answered"] == "false",
                               until = until),
                  yate.onwatch("chan.hangup",
                               lambda m : m["id"] == execute["id"] and m["answered"] == "false",
                               until = until),
                  yate.onwatch("chan.hangup",
                               lambda m : m["targetid"] and m["targetid"].startswith(execute["targetid"]) and m['answered'] == 'true',
                               until = until))

        end.addBoth(lambda r: r[1])
    else:
        end = yate.onwatch("chan.hangup",
                           lambda m : m["id"] == execute["targetid"],
                           until = until)

    answered = yate.onwatch(
        "call.answered",
        lambda m : m["targetid"] ==  execute["id"],
        until = end)

    def trapAbandoned(f):
        f.trap(AbandonedException)
        raise OutgoingCallException(formatReason(f.value.cause))

    answered.addErrback(trapAbandoned)

    if logger.isEnabledFor(logging.DEBUG):
        def logAnswered(msg):
            logger.debug("Answered: %s, %s", answered["id"], answered["targetid"])
            return msg

    if retCallIdFast:
        defer.returnValue((execute["targetid"], execute["id"],
                           end,
                           answered))
    else:
        logger.debug(
            "Waiting for answer: %s, %s", execute["targetid"], execute["id"])

        answered = yield answered

        defer.returnValue((answered["targetid"], answered["id"], end))


## @defer.inlineCallbacks
## def attach(yate, callid, wave, sync = False):

##     m = {"message" : "chan.attach",
##          "id": callid,
##          "source": "wave/play/%s" % wave}

##     if sync:
##         m["notify"] =  callid

##     yate.msg("chan.masquerade", m).enqueue()

##     end = yate.onwatch("chan.hangup",
##         lambda m : m["id"] == callid)

##     if sync:
##         yield yate.onmsg("chan.notify",
##                          lambda m : m["targetid"] == callid,
##                          autoreturn = True, until = end)

## @defer.inlineCallbacks
## def record(yate, callid, fileName, maxlen = 100000):
##         yate.msg("chan.masquerade",
##                  {"message": "chan.attach",
##                   "id": callid,
##                   "consumer": "wave/record/" + fileName,
##                     "maxlen": maxlen,
##                     "notify": callid}).enqueue()

##         yield yate.onmsg("chan.notify",
##                          lambda m : m["targetid"] == callid,
##                          autoreturn = True)
