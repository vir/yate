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
from yaypm.utils import XOR, OR, sleep #, tester
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
      ...         - ends tester thread but does not drop connection
    Example: _,1,1? - waits 1 second, sends 1, waits for 1 checkpoint.
    """

    end = [server_yate.onwatch("chan.hangup", lambda m : m["id"] == remoteid)]
    ignore_hangup = [False]

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
                    until = end[0])

            if timeout:
                logger.debug(
                    "[%d] waiting for: %ds for checkpoint: %s on: %s" % \
                             (testid, timeout, check, remoteid))
                yield XOR(check_def, sleep(timeout))
                what, _ = getResult()
                if what > 0:
                    logger.warn(
                        "[%d] timeout while waiting %d s for checkpoint: %s on: %s" % \
                        (testid, timeout, check, remoteid))
##                     client_yate.msg("call.drop", {"id": callid}).enqueue()
##                     logger.debug(
##                         "[%d] dropping connection because of timeout: %s" % (testid, remoteid))
                    raise Exception("Timeout while waiting %d s for checkpoint: %s on: %s" % \
                        (timeout, check, remoteid))
                logger.debug(
                    "[%d] checkpoint: %s on: %s passed" % \
                    (testid, check, remoteid))
            else:
                logger.debug(
                    "[%d] Waiting for checkpoint: '%s' on: %s" % \
                        (testid, check, remoteid))
                yield check_def
                getResult()
        elif "!" in step:
            check = step[:-1]
            error = server_yate.onwatch(
                    "test.checkpoint",
                    lambda m : m["id"] == remoteid and m["check"] == check,
                    until = end[0])
            end[0] = OR(end[0], error)

        elif step in ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "#", "*"]:
            logger.debug("[%d] Sending dtmf: %s to %s" % (testid, step, targetid))
            client_yate.msg(
                "chan.dtmf",
                {"id": callid, "targetid": targetid, "text": step}).enqueue()
        elif "&" in step:
            timeout = int(step[1:])
            ignore_hangup[0] = True
            logger.debug("[%d] Waiting for 'end' checkpoint after hangup for: %d s." % (testid, timeout))
            yield OR(end[0], sleep(timeout), patient = False)
            getResult()
            raise AbandonedException("Timeout while waiting for end.")
        else:
            t = 1
            if "*" in step:
                try:
                    t = int(step[2:])
                except ValueError:
                    pass
            logger.debug("[%d] Sleeping for: %d s." % (testid, t))
            yield OR(end[0], sleep(t), patient = False)
            getResult()


    def parse(toparse):
        last = None
        for t in toparse:
            if t == ",":
                def x(a, b):
                    yield go(a)
                    getResult()
                    yield go(b)
                    getResult()
                last = (x(last, parse(toparse)))
            elif t == "(":
                last = parse(toparse)
                if toparse.next() != ")":
                    raise Excepion("Unmatched bracket!")
            elif t == ")":
                toparse.back(t)
                break
            elif t == "|":
                def x(a, b):
                    yield OR(go(a), go(b))
                    r = getResult()
                last = x(last, parse(toparse))
            else:
                last = one_step(t)

        if last:
            return last
        else:
            return None

    try:
        yield go(parse(peep(tokenize(test))))
        getResult()

        yield sleep(0.1)
        getResult()
    except Exception, e:
        logger.info("[%d] dropping: %s" % (testid, callid))

        d = OR(client_yate.msg("call.drop", {"id": callid}).dispatch(),
           server_yate.msg("call.drop", {"id": remoteid}).dispatch())

        if type(e) == type(AbandonedException("")) and ignore_hangup[0]:
            yield server_yate.onwatch(
                "test.checkpoint",
                lambda m : m["id"] == remoteid and m["check"] == "end",
                until = sleep(1))
            getResult()
            logger.debug("[%d] call %s finished" % (testid, callid))
            return

        yield d
        getResult()

        logger.debug("[%d] call %s finished" % (testid, callid))

        raise e

    logger.debug("[%d] dropping: %s" % (testid, callid))
    yield client_yate.msg("call.drop", {"id": callid}).dispatch()
    getResult()
    yield server_yate.msg("call.drop", {"id": remoteid}).dispatch()
    getResult()

    logger.debug("[%d] call %s finished" % (testid, callid))
