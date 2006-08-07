#!/usr/bin/python
# -*- coding: iso-8859-2; -*-

from yaypm import TCPDispatcherFactory, AbandonedException
from yaypm.flow import go, logger_flow, getResult
from yaypm.utils import XOR, sleep
from twisted.internet import reactor, defer

import sys, logging, yaypm, time

logger = logging.getLogger("tester")

beepresource = "wave/play/./sounds/beep.gsm"
digitresource = "wave/play/./sounds/digits/pl/%s.gsm"

extended_calls = []

impossible_prefix = "tester_prefix"

def do_nothing(client_yate, server_yate, targetid, callid, remoteid, duration):
    logger.info("doing nothing on %s for %d s." % (callid, duration))    
    yield sleep(duration)
    getResult()
    logger.info("dropping: %s" % callid)        
    yield client_yate.msg("call.drop", {"id": callid}).dispatch()    
    getResult()

def detect_dtmf_and_do_nothing(client_yate, server_yate, targetid, callid, remoteid, duration):
    server_yate.msg("chan.masquerade",
        attrs = {"message" : "chan.detectdtmf",
                 "id": remoteid,
                 "consumer": "dtmf/"}).enqueue()
        
    logger.info("detecting dtmf on %s for %d s." % (callid, duration))    
    yield sleep(duration)
    getResult()
    logger.info("dropping: %s" % callid)        
    yield client_yate.msg("call.drop", {"id": callid}).dispatch()    
    getResult()

    
def send_dtmfs(client_yate, server_yate, targetid, callid, remoteid, dtmfs):
    # znak        - wysyla znak jako dtmf
    # connected?  - czeka checkpoint,
    # connected?x - czeka x sekund na checkpoint,
    # s:x,c:y     - robi chan attach z source s i consumer c
    # _           - przerwa 1s, _*x - x razy 1s
    # ...         - konczy watek testera, ale nie rozlacza sie
    
    end = client_yate.onwatch("chan.hangup", lambda m : m["id"] == callid)
    try:
        for text in [t.strip() for t in dtmfs.split(",")]:
            if "c:" in text or "s:" in text:
                media = text.split("|")
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
            elif text == "...":
                logger.info("call %s extended" % callid)
                extended_calls.append(callid)
                return
            elif "?" in text:
                i = text.find("?")
                check = text[:i]
                timeout = None
                if len(text) > i + 1:
                    timeout = int(text[i+1:])

                check_def = server_yate.onwatch(
                        "test.checkpoint",
                        lambda m : m["id"] == remoteid and m["check"] == check)

                if timeout:
                    logger.info(
                        "waiting for: %ds for checkpoint: %s on: %s" % \
                                 (timeout, check, remoteid))
                    yield XOR(check_def, sleep(timeout))
                    what, _ = getResult()
                    if what > 0:
                        logger.info(
                            "timeout while waiting for: %s on: %s" % \
                            (check, remoteid))
                        client_yate.msg("call.drop", {"id": callid}).enqueue()
                        logger.info(
                            "dropping connection: %s" % remoteid)                        
                        return
                else:
                    logger.info(
                        "Waiting for checkpoint: '%s' on: %s" % \
                            (check, remoteid))                
                    yield check_def
                    getResult()
                    
            elif text in ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "#", "*"]:
                logger.info("Sending dtmf: %s to %s" % (text, targetid))
                client_yate.msg(
                    "chan.dtmf",
                    {"id": callid, "targetid": targetid, "text": text}).enqueue()
            else:
                t = 1
                if "*" in text:
                    try:
                        t = int(text[2:])
                    except ValueError:
                        pass
                logger.info("Sleeping for: %d s." % t)
                yield sleep(t)
                getResult()
        yield sleep(1)
        getResult()
        logger.info("dropping: %s" % callid)        
        yield client_yate.msg("call.drop", {"id": callid}).dispatch()
        getResult()
    except AbandonedException, e:
        logger.info("call %s abandoned" % callid)
    except DisconnectedException:
        logger.info("call %s disconnected" % callid)

    logger.info("call %s finished" % callid)

def select_non_called(dfrs):
    r = []
    for d in dfrs:
        if not d.called:
            r.append(d)
    return r

def load(client_yate, server_yate, target, handler, con_max, tests, monitor):

    concurrent = []
    monitored = []

    yield server_yate.installMsgHandler("call.route", prio = 50)
    getResult()
    yield client_yate.installWatchHandler("call.answered")
    getResult()
    yield client_yate.installWatchHandler("chan.hangup")
    getResult()
   
    if monitor:
        monitorid = None
        monitor_room = None

        logger.info("Creating monitoring connection.")
        
        execute = client_yae.msg("call.execute",
         {"callto": "dumb/",
          "target": monitor})
        yield execute.dispatch()
        if not getResult():
            logger.warn("can't create monitor connection on %s" % monitor)
            monitor = None
        else:
            try: 
                end = client_yate.onwatch(
                    "chan.hangup",
                    lambda m, callid = execute["id"] : m["id"] == callid)

                yield client_yate.onwatch(
                    "call.answered",
                    lambda m : m["id"] ==  execute["targetid"],
                    until = end)
                getResult()
                logger.info("Monitor connection answered.")

                dumbid = execute["id"]
                monitorid = execute["targetid"]

                execute = client_yate.msg(
                    "chan.masquerade",
                    {"message": "call.execute",
                     "id": execute["targetid"],
                     "lonely": "true",
                     "voice": "false",
                     "echo": "false",
                     "smart": "true",
                     "callto": "conf/"})
                yield execute.dispatch()
                if getResult():
                    monitor_room = execute["room"]
                    logger.debug("Monitor conference created.")
                    yield client_yate.msg("call.drop", {"id": dumbid}).dispatch()
                    getResult()
                else:
                    logger.warn("can't create monitor conference on %s" % monitor)
                    monitor = None
            except AbandonedException:
                logger.info("Monitor connection not answered.")
                monitor = None                

    for t in tests:
        if len(concurrent) >= con_max:
            concurrent = select_non_called(concurrent)
            logger.debug("waiting on concurrency limit: %d" % con_max)
            yield defer.DeferredList(concurrent, fireOnOneCallback=True)
            _, fired = getResult()
            concurrent.remove(concurrent[fired])

        route = server_yate.onmsg(
            "call.route",
            lambda m : m["driver"] != "dumb" and m["called"].find(impossible_prefix) >= 0)

        def getRemoteId(d):
            yield d
            route = getResult()
            route["called"] = route["called"].replace(impossible_prefix, "")
            remoteid = route["id"]
            route.ret(False)
            yield remoteid
            return

        remoteid_def = go(getRemoteId(route))
       
        execute = client_yate.msg(
            "call.execute",
            {"callto": "dumb/",
             "target": target % impossible_prefix})

        yield execute.dispatch()

        try:            
            if not getResult():
                route.cancel()
                raise AbandonedException("Call to: %s failed." % target)

            callid = execute["id"]        

            end = client_yate.onwatch(
                "chan.hangup",
                lambda m, callid = callid : m["id"] == callid)

            yield defer.DeferredList(
                [remoteid_def, end],
                fireOnOneCallback=True, fireOnOneErrback=True)

            result, end_first  = getResult()
            if end_first:
                raise AbandonedException("Call to: %s hungup." % target)

            logger.info("outgoing call to %s" % (callid))
        
            yield client_yate.onwatch(
                "call.answered",
                lambda m : m["targetid"] ==  callid,
                until = end)
            answered = getResult()

            targetid = execute["targetid"]

            monitoring = False

            if monitor and not monitored :
                logger.debug("monitoring: %s" % callid)
                monitored.append(callid)
                end.addCallback(
                    lambda _, targetid = targetid: client_yate.msg("call.drop", {"id": targetid}).enqueue())

                yield client_yate.msg(
                    "chan.masquerade",
                    {"message": "call.conference",
                     "id": callid,
                     "room": monitor_room}).dispatch()
                getResult()
                monitoring = True

            logger.debug("recording: %s" % str(callid))
            client_yate.msg(
                "chan.masquerade",
                {"message": "chan.attach",
                 "id": callid,
                 "source": "moh/default",
                 "consumer": "wave/record//tmp/recording%s.slin" % \
                     callid.replace("/", "-"),
                 "maxlen": 0}).enqueue()

            yield remoteid_def
            remoteid = getResult()

            result = go(handler(
                client_yate, server_yate,
                targetid,                
                callid, remoteid, t))

            if monitoring:
                result.addCallback(
                    lambda _, mon_id: monitored.remove(mon_id),
                    callid)
            concurrent.append(result)
        except AbandonedException, e:
            if not route.called:
                route.cancel()
            logger.exception("outgoing call to %s abandoned" % callid)

    yield defer.DeferredList(concurrent)            
    logger.info("Test finished!")

    if monitor and monitorid:
        logger.debug("droping monitor connection")
        yield client_yate.msg("call.drop", {"id": monitorid}).dispatch()
        getResult()
        yield sleep(1)
        getResult()
    reactor.stop()
                
def sequential_n(n, tests):
    i = 0
    l = len(tests)
    while i < n:
        yield tests[i % l]
        i = i + 1

def random_n(n, tests):
    i = 0
    while i < n:
        yield tests[random.randint(0, len(tests)-1)]
        i = i + 1

def do_load_test(
    local_addr, local_port,
    remote_addr, remote_port,
    dest, handler, con_max, tests,
    monitor = True):

    def start_client(client_yate):
        logger.debug("client started");

        def start_server(server_yate):
            logger.debug("server started");
            go(load(client_yate, server_yate,
                dest,
                handler, con_max,
                tests,
                monitor))

        server_factory = TCPDispatcherFactory(start_server)
        reactor.connectTCP(remote_addr, remote_port, server_factory)

    client_factory = TCPDispatcherFactory(start_client)
    
    reactor.connectTCP(local_addr, local_port, client_factory)

