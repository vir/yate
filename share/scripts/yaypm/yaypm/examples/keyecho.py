#!/usr/bin/python

from twisted.internet import reactor, defer
import logging, yaypm.utils

logger = logging.getLogger('yaypm.examples')

def route(yate):
    def on_route(route):
        callid = route["id"]
        route.ret(True, "dumb/")

        def on_execute(execute):
            yate.msg("call.answered",
                     {"id": execute["targetid"],
                      "targetid": execute["id"]}).enqueue()
            logger.debug("Call %s answered." % callid)
            def on_dtmf(dtmf):
                logger.debug("Dtmf %s received." % dtmf["text"])
                yate.msg("chan.masquerade",
                    {"message" : "chan.attach",
                     "id": dtmf["targetid"],
                     "source": "wave/play/./sounds/digits/pl/%s.gsm" % \
                     dtmf["text"]}).enqueue()
                dtmfid = dtmf["id"]
                yate.onwatch("chan.dtmf",
                    lambda m : m["id"] == dtmfid).addCallback(on_dtmf)
                dtmf.ret(True)
            dtmf = yate.onmsg("chan.dtmf",
                lambda m : m["id"] == execute["id"])
            dtmf.addCallback(on_dtmf)

        execute = yate.onwatch("call.execute",
            lambda m : m["id"] == callid)
        execute.addCallback(on_execute)
        yate.onmsg("call.route").addCallback(on_route)

    yate.onmsg("call.route",
        lambda m : m["called"] == "ivr").addCallback(on_route)

if __name__ in ["__main__"]:
    logger.setLevel(logging.DEBUG)
    yaypm.utils.setup(route)

