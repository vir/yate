#!/usr/bin/python

"""
 pbx.py

 Copyright (C) 2005 Maciek Kaminski

 Very poor man's pbx.

"""

from yaypm import TCPDispatcherFactory, AbandonedException
from yaypm.utils import sleep, XOR, ConsoleFormatter
from yaypm.flow import go, getResult
from twisted.internet import reactor

import logging, yaypm, time

logger = logging.getLogger('pbx')

def blind_transfer(yate, callid, targetid, transferto, returnto):
    try:
        yate.msg(
            "chan.masquerade",
            {"message" : "call.execute",
             "id": targetid, "callto": transferto}).enqueue()

        end = yate.onmsg(
             "chan.hangup",
             lambda m : m["id"] == targetid,
            autoreturn = True)

        notanswered =  yate.onmsg(
            "chan.disconnected",
            lambda m : m["id"] == targetid,
            until = end)

        answered =  yate.onwatch(
            "call.answered",
            lambda m : m["targetid"] == targetid,
            until = end)

        yield XOR(answered, notanswered)
        what, m = getResult()

        if what == 0:
            logger.debug("Blind transfer to: %s done" % transferto)
            m.ret(False)
            return
        else:
            logger.debug(
                "Blind transfer to: %s failed. Returning to %s" % \
                (transferto, returnto))

            route = yate.msg("call.route",
                             {"called": returnto},
                             until = end)
            yield route.dispatch()

            if not getResult():
                logger.debug("Can't return to: %s" % returnto)
                m.ret(False)
                return

            yate.msg("chan.masquerade",
                     {"message" : "call.execute",
                      "id": m["id"],
                      "callto": route.getRetValue(),
                      "called": returnto}).enqueue()
            yate.ret(m, True)

    except AbandonedException, e:
        logger.debug(
            "Blind transfer to: %s failed. Peer has disconnected" % \
            transferto)

def supervised_transfer(yate, callid, targetid, transferto, returnto):
    pass

def pbx(yate, callid, targetid, callto, called):
    # Pbx function provides dtmf interface to pbx functions

    logger.debug("Pbx for %s, %s  started" % (callto, callid))
    try:
        # run until hangup:
        end = yate.onwatch(
            "chan.hangup",
            lambda m : m["id"] ==callid)


        while True:
            last_time = time.time()
            ext = ""
            while True:
                yield yate.onmsg(
                    "chan.dtmf",
                    lambda m : m["id"] == callid,
                    end,
                    autoreturn = True)
                getResult()
                text = dtmf["text"]
                dtmf.ret(False)
                current_time = time.time()
                if last_time - current_time > 3:
                    ext = text
                else:
                    # * or # initializes transfer
                    if text in ["#", "*"]:
                        break
                    else:
                        ext += text

            # Let routing module resolve the extension

            route = yate.msg("call.route",
                     {"called": ext})
            yield route.dispatch()

            if not getResult():
                # Abandon transfer in case of bad extension
                logger.debug(
                    "Can't route extension: %s. Abandoning transfer." % ext)
                continue
            else:
                print route
                ext = route.getRetValue()

            if(text in ["*"]):
                logger.debug(
                    "doing supervised transfer on %s to %s." % (callid, ext))
                go(supervised_transfer(yate, callid, targetid, ext, called))
                break
            else:
                logger.debug(
                    "Blind transfer on %s to %s." % (callid, ext))
                go(blind_transfer(
                    yate, callid, targetid, ext, called))
        logger.debug("Pbx for %s finished" % callid)
    except AbandonedException, e:
        logger.debug("Pbx for %s abandoned" % callid)


def main(yate, called):
    # Main function will start pbx function for connections comming to a
    # given extension.

    logger.debug("Watching for calls to: %s" % called)

    while True:
        # Notice that message watches have to be used here to get
	# call.execute attributes after message is handled:

        yield yate.onwatch("call.execute", lambda m : m["called"] == called)
        execute = getResult()

        end = yate.onwatch(
            "chan.hangup",
            lambda m : m["id"] == execute["targetid"])

        answered = yate.onwatch(
            "call.answered",
            lambda m : m["id"] == execute["targetid"] , end)

        answered.addCallbacks(
            lambda m, yate, callto: go(pbx(yate, m["id"], m["targetid"], callto, called)),
            lambda m: None,
            [yate, execute["callto"]])

if __name__ == '__main__':
       hdlr = logging.StreamHandler()
       formatter = ConsoleFormatter('%(name)s %(levelname)s %(message)s')
       hdlr.setFormatter(formatter)

       yaypm.logger.addHandler(hdlr)
       #yaypm.logger.setLevel(logging.DEBUG)
       yaypm.flow.logger_flow.setLevel(logging.DEBUG)
       #yaypm.logger_messages.setLevel(logging.DEBUG)
       yaypm.logger_messages.setLevel(logging.INFO)

       logger.setLevel(logging.DEBUG)
       logger.addHandler(hdlr)

       f = TCPDispatcherFactory(lambda yate: go(main(yate, "maciejka")))
       reactor.connectTCP("localhost", 5039, f)
       reactor.run()
