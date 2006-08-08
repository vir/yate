#!/usr/bin/python
from twisted.internet import reactor, defer
from yaypm import TCPDispatcherFactory, AbandonedException
from yaypm.flow import go, getResult

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

        print "Call %s answered." % callid

        while True:
            yield yate.onmsg(
                "chan.dtmf",
                lambda m : m["id"] == callid,
                end)
            dtmf = getResult()

            print "Dtmf %s received." % dtmf["text"]

            yate.msg("chan.masquerade",
                {"message" : "chan.attach",
                 "id": targetid,
                 "source": "wave/play/./sounds/digits/pl/%s.gsm" % \
                 dtmf["text"]}).enqueue()

            dtmf.ret(True)

    except AbandonedException, e:
        print "Call %s abandoned." % callid

def route(yate):
    while True:
        yield yate.onmsg("call.route", lambda m : m["called"] == "ivr")
        route = getResult()
        go(ivr(yate, route["id"]))
        route.ret(True, "dumb/")

def start(yate):
    go(route(yate))

if __name__ == '__main__':
    f = TCPDispatcherFactory(start)
    reactor.connectTCP("localhost", 5039, f)
    reactor.run()
