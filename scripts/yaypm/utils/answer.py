#!/usr/bin/python
# -*- coding: iso-8859-2; -*-

from twisted.internet import reactor, defer

import yaypm, logging
from random import randint
from yaypm import AbandonedException, TCPDispatcherFactory
from yaypm.flow import go, getResult
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

        yield yate.onwatch(
            "call.execute",
            lambda m : m["id"] == callid, until = end)
        execute = getResult()

        targetid = execute["targetid"]

        yield sleep(1)
        getResult()

        yate.msg("call.drop",
                 {"id": targetid, "reason": "noanswer"}).enqueue()

    def answered(yate, route):
        callid = route["id"]
        route.ret(True, answered_target)

        logger.info("[%s, %s] Call answered.", called, callid)

        end = yate.onwatch(
                "chan.hangup",
                lambda m : m["id"] == callid)

        yield yate.onwatch(
            "call.execute",
            lambda m : m["id"] == callid, until = end)    

        execute = getResult()

        targetid = execute["targetid"]

        if answered_target.startswith("dumb"):           
            yate.msg("call.answered",
                     {"id": targetid,
                      "targetid": callid}).enqueue()

        if answered_handler:
            go(answered_handler(yate, called, callid, targetid))
        else:
            yield sleep(2)
            getResult()            
            yate.msg("call.drop", {"id": targetid}).enqueue()

    def busy(yate, route):
        callid = route["id"]
        target = "answer-busy-%s" % callid
        route.ret(True, target)

        logger.info("[%s, %s] Call busy.", called, callid)

        end = yate.onwatch(
                "chan.hangup",
                lambda m : m["id"] == callid)

        yield yate.onmsg(
            "call.execute",
            lambda m : m["callto"] == target, until = end)

        execute = getResult()

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
        yield yate.onmsg(
            "call.route", lambda m: handlers.has_key(m["called"]))
        route = getResult()

        go(handlers[route["called"]](yate, route))

if __name__ in ["__main__", "__embedded_yaypm_module__"]:
    def start(yate):
        go(answer(yate, "1234"))

    logger.setLevel(logging.DEBUG)

    yaypm.utils.setup(start)
