#!/usr/bin/python
# -*- coding: iso-8859-2; -*-
"""
 Tester module for YAYPM. Generates calls, and allows to define incall
 activity like sending DTMFs. Can be run on two separate yates to imitate
 realistic load.
 
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

from yaypm import TCPDispatcherFactory, AbandonedException
from yaypm.flow import go, logger_flow, getResult
from yaypm.utils import XOR, sleep #, tester
from twisted.internet import reactor, defer

import sys, logging, yaypm, time

logger = logging.getLogger("tester")


class peep:
    def __init__(self, g):
        self.g = g
        self.peep = None

    def __iter__(self):
        return self            
        
    def next(self):
        if self.peep:
            print "peep", self.peep
            tmp = self.peep
            self.peep = None
            return tmp
        else:
            return self.g.next()

    def back(self, v):
        self.peep = v      
    

def tokenize(text):
    a = ""
    for c in text:
        if c in ",()|":
            if a:
                yield a
            yield c
            a = ""
        else:
            a = a + c
    if a:
        yield a


def dtmf_etc(client_yate, server_yate, testid, targetid, callid, remoteid, test):
    """
    Defines incall activity. Activity is defined in a string, according
    to the following rules:
      1-9,*,#     - sends x as dtmf
      connected?  - waits for test.checkpoint message with check==connected
      connected?x - waits for test.checkpoint message with check==connected for x secs
      error!      - raise exception on test.checkpoint message with check==error
      s:x,c:y     - does chan.attach with source==s and consumer==c
      _           - 1s break, _*x - x times 1s
      timeout:t   - starts timer that drops connection after t secs
      ...         - ends tester thread but does not drop connection
    Example: _,1,1? - waits 1 second, sends 1, waits for 1 checkpoint.
    """

    end = client_yate.onwatch("chan.hangup", lambda m : m["id"] == targetid)

    def one_step(step):
        if "c:" in step or "s:" in step:
            media = step.split("|")
            consumer = None
            source = None
            for m in media:
                if m.startswith("c:"):
                    consumer = m[2:]
                elif m.startswith("s:"):
                    source = m[2:]
            if source or consumer:
                attrs = {"message": "chan.attach",
                         "id": callid}
                if source:
                    attrs["source"] = source
                if consumer:
                    attrs["consumer"] = consumer
                client_yate.msg("chan.masquerade", attrs).enqueue()
        elif step == "...":
            logger.debug("[%d] call %s extended" % (testid, callid))
            #tester.extended_calls.append(callid)
            return

        elif "?" in step:
            i = step.find("?")
            
            check = step[:i]
                           
            timeout = None
            if len(step) > i + 1:
                timeout = int(step[i+1:])

            check_def = server_yate.onwatch(
                    "test.checkpoint",
                    lambda m : m["id"] == remoteid and m["check"] == check,
                    until = end)

            if timeout:
                logger.debug(
                    "[%d] waiting for: %ds for checkpoint: %s on: %s" % \
                             (testid, timeout, check, remoteid))
                yield XOR(check_def, sleep(timeout))
                what, _ = getResult()
                if what > 0:
                    logger.debug(
                        "[%d] timeout while waiting %d s for checkpoint: %s on: %s" % \
                        (testid, timeout, check, remoteid))
                    client_yate.msg("call.drop", {"id": callid}).enqueue()
                    logger.debug(
                        "[%d] dropping connection: %s" % (testid, remoteid))
                    raise Exception("Timeout while waiting %d s for checkpoint: %s on: %s" % \
                        (timeout, check, remoteid))
            else:
                logger.debug(
                    "[%d] Waiting for checkpoint: '%s' on: %s" % \
                        (testid, check, remoteid))                
                yield check_def
                getResult()

        elif step in ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "#", "*"]:
            logger.debug("[%d] Sending dtmf: %s to %s" % (testid, step, targetid))
            client_yate.msg(
                "chan.dtmf",
                {"id": callid, "targetid": targetid, "text": step}).enqueue()
        else:
            t = 1
            if "*" in step:
                try:
                    t = int(step[2:])
                except ValueError:
                    pass
            logger.debug("[%d] Sleeping for: %d s." % (testid, t))
            yield sleep(t)
            getResult()
        
    
    def parse(toparse):
        print "parse"
        stack = []
        for t in toparse:
            print "t:", t
            if t == ",":
                def x(a, b):
                    yield go()
                    getResult()
                    yield go(b)
                    getResult()
                stack.append(x(stack.pop(), parse(toparse)))
            elif t == "(":
                stack.append(parse(toparse))
                if toparse.next() != ")":
                    raise Excepion("Unmatched bracket!")
            elif t == ")":
                toparse.back(t)
                break
            elif t == "|":
                def x(a, b):
                    yield defer.DeferredList([a, b],
                                                fireOnOneCallback=True)
                    getResult()
                stack.append(x(stack.pop(), parse(toparse)))
            else:
                stack.append(one_step(t))

        if stack:
            return stack[0]
        else:
            return None  


    yield go(parse(parse(peep(tokenize(x)))))

    getResult()
        
    yield sleep(0.1)
    getResult()
    logger.debug("[%d] dropping: %s" % (testid, callid))        
    yield client_yate.msg("call.drop", {"id": callid}).dispatch()
    getResult()

    logger.debug("[%d] call %s finished" % (testid, callid))

