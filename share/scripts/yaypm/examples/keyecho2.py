#!/usr/bin/python
from twisted.internet import reactor, defer
from yaypm import TCPDispatcherFactory, AbandonedException
from yaypm.flow import go, getResult
import logging, yaypm.utils

logger = logging.getLogger('yaypm.examples')

def ivr(yate, callid):
    try:
        end = yate.onwatch("chan.hangup", lambda m : m["id"] == callid)

        yield yate.onwatch("call.execute",
            lambda m : m["id"] == callid,
            until = end)
        execute = getResult()

        targetid = execute["targetid"]

        yate.msg("call.answered",
                 {"id": targetid,
                  "targetid": callid}).enqueue()

        logger.debug("Call %s answered." % callid)

        while True:
            yield yate.onmsg(
                "chan.dtmf",
                lambda m : m["id"] == callid,
                end)
            dtmf = getResult()

            logger.debug("Dtmf %s received." % dtmf["text"])

            yate.msg("chan.masquerade",
                {"message" : "chan.attach",
                 "id": targetid,
                 "source": "wave/play/./sounds/digits/pl/%s.gsm" % \
                 dtmf["text"]}).enqueue()

            dtmf.ret(True)

    except AbandonedException, e:
        logger.debug("Call %s abandoned." % callid)

def route(yate):
    while True:
        yield yate.onmsg("call.route", lambda m : m["called"] == "ivr")
        route = getResult()
        go(ivr(yate, route["id"]))
        route.ret(True, "dumb/")

if __name__ in ["__main__", "__embedded_yaypm_module__"]:
    logger.setLevel(logging.DEBUG)
    yaypm.utils.setup(lambda yate: go(route(yate)))

