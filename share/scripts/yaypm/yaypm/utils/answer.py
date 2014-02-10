#!/usr/bin/python
# -*- coding: iso-8859-2; -*-
"""
 Answer module for YAYPM. Adds extenstions that results in answered,
 unanswered, busy, notfound calls. There is also an extenstion that
 behaves randomly. Good for automated testing.

 Copyright (C) 2006 Maciek Kaminski

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
from twisted.internet import reactor, defer

import yaypm, logging
from random import randint
from yaypm import AbandonedException, TCPDispatcherFactory
from yaypm.utils import RestrictedDispatcher, sleep

logger = logging.getLogger('yaypm.answer')

def answer(yate, called,
           answered_handler = None,
           answered_target = "moh/default",
           extensions = {"answered": "1",
                      "unanswered": "2",
                      "busy": "3",
                      "notfound": "4",
                      "random": "5"}):

    def unanswered(yate, route):
        callid = route["id"]
        route.ret(True, "dumb/")

        logger.info("[%s, %s] Call unanswered.", called, callid)

        end = yate.onwatch(
                "chan.hangup",
                lambda m : m["id"] == callid)

        execute = yield yate.onwatch(
            "call.execute",
            lambda m : m["id"] == callid, until = end)

        targetid = execute["targetid"]

        yield sleep(1)

        yate.msg("call.drop",
                 {"id": targetid, "reason": "noanswer"}).enqueue()

    def answered(yate, route):
        callid = route["id"]
        route.ret(True, answered_target)

        logger.info("[%s, %s] Call answered.", called, callid)

        end = yate.onwatch(
                "chan.hangup",
                lambda m : m["id"] == callid)

        execute = yield yate.onwatch(
            "call.execute",
            lambda m : m["id"] == callid, until = end)

        targetid = execute["targetid"]

        if answered_target.startswith("dumb"):
            yate.msg("call.answered",
                     {"id": targetid,
                      "targetid": callid}).enqueue()

        if answered_handler:
            answered_handler(yate, called, callid, targetid)
        else:
            yield sleep(2)
            yate.msg("call.drop", {"id": targetid}).enqueue()

    def busy(yate, route):
        callid = route["id"]
        target = "answer-busy-%s" % callid
        route.ret(True, target)

        logger.info("[%s, %s] Call busy.", called, callid)

        end = yate.onwatch(
                "chan.hangup",
                lambda m : m["id"] == callid)

        execute = yield yate.onmsg(
            "call.execute",
            lambda m : m["callto"] == target, until = end)

        execute["reason"] = "busy"
        execute.ret(False)

    def notfound(yate, route):
        route.ret(False)
        logger.info("[%s, %s] Number not found.", called, route["id"])

    def random(yate, route):
        handlers = [answered, unanswered, busy, notfound]
        h = handlers[randint(0, len(handlers)-1)]
        go(h(yate, route))

    called_aswered = "%s%s" % (called, extensions["answered"])
    called_unanswered = "%s%s" % (called, extensions["unanswered"])
    called_busy = "%s%s" % (called, extensions["busy"])
    called_notfound = "%s%s" % (called, extensions["notfound"])
    called_random = "%s%s" % (called, extensions["random"])

    handlers = {called_aswered: answered,
                called_unanswered: unanswered,
                called_busy: busy,
                called_notfound: notfound,
                called_random: random}

    logger.info("[%s] Answer will handle calls to: %s",
                called, str(handlers.keys()))

    while True:
        route = yield yate.onmsg(
            "call.route", lambda m: handlers.has_key(m["called"]))

        handlers[route["called"]](yate, route)

if __name__ in ["__main__", "__embedded_yaypm_module__"]:
    def start(yate):
        answer(yate, "1234")

    logger.setLevel(logging.DEBUG)
    yaypm.utils.setup(start)
