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

impossible_prefix = "_aaaa_tester_prefix"

def do_nothing(client_yate, server_yate, testid, targetid, callid, remoteid, duration):
    logger.debug("[%d] doing nothing on %s for %d s." % (testid, callid, duration))
    yield sleep(duration)
    getResult()
    logger.debug("[%d] dropping: %s" % (testid, callid))
    yield client_yate.msg("call.drop", {"id": callid}).dispatch()
    getResult()

def detect_dtmf_and_do_nothing(client_yate, server_yate, testid, targetid, callid, remoteid, duration):
    server_yate.msg("chan.masquerade",
        attrs = {"message" : "chan.detectdtmf",
                 "id": remoteid,
                 "consumer": "dtmf/"}).enqueue()
        
    logger.debug("[%d] detecting dtmf on %s for %d s." % (testid, callid, duration))
    yield sleep(duration)
    getResult()
    logger.debug("[%d] dropping: %s" % (testid, callid))
    yield client_yate.msg("call.drop", {"id": callid}).dispatch()
    getResult()

    
def send_dtmfs(client_yate, server_yate, testid, targetid, callid, remoteid, dtmfs):
    # dtmfs syntax
    # 1-9,*,#     - sends x as dtmf
    # connected?  - waits for test.checkpoint message with check==connected
    # connected?x - waits for test.checkpoint message with check==connected for x secs
    # s:x,c:y     - does chan.attach with source==s and consumer==c
    # _           - 1s break, _*x - x times 1s
    # timeout:t   - starts timer that drops connection after t secs
    # ...         - ends tester thread but does not drop connection
    

    end = client_yate.onwatch("chan.hangup", lambda m : m["id"] == targetid)

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
            logger.debug("[%d] call %s extended" % (testid, callid))
            extended_calls.append(callid)
            return
##         elif text.startswith("timeout:"):
##             timeout = int(text[len("timeout:"):])
##             drop = client_yate.msg("call.drop",
##                             {"id": targetid, "reason": "timeout"})
##             logger.debug(
##                 "[%d] Setting absolute timeout to: %d s" % (testid, timeout))
##             reactor.callLater(timeout, drop.enqueue)
        elif "?" in text:
            i = text.find("?")
            check = text[:i]
            timeout = None
            if len(text) > i + 1:
                timeout = int(text[i+1:])

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

        elif text in ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "#", "*"]:
            logger.debug("[%d] Sending dtmf: %s to %s" % (testid, text, targetid))
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
            logger.debug("[%d] Sleeping for: %d s." % (testid, t))
            yield sleep(t)
            getResult()
    yield sleep(1)
    getResult()
    logger.debug("[%d] dropping: %s" % (testid, callid))        
    yield client_yate.msg("call.drop", {"id": callid}).dispatch()
    getResult()
##     except AbandonedException, e:
##         logger.debug("[%d] call %s abandoned" % (testid, callid))
##     except DisconnectedException:
##         logger.debug("[%d] call %s disconnected" % (testid, callid))

    logger.debug("[%d] call %s finished" % (testid, callid))

def select_non_called(dfrs):
    r = []
    for d in dfrs:
        if not d.called:
            r.append(d)
    return r

def load(client_yate, server_yate, target, handler, con_max, tests, monitor):

    concurrent = []
    monitored = []

    successes, failures = {}, {}

    def success(_, t, start):
        successes[t] = time.time() - start 

    def failure(f, t, start):
        failures[t] = (time.time() - start, f)

    yield server_yate.installMsgHandler("call.route", prio = 50)
    getResult()
    yield client_yate.installWatchHandler("call.answered")
    getResult()
    yield client_yate.installWatchHandler("chan.hangup")
    getResult()
   
    if monitor:
        monitorid = None
        monitor_room = None

        logger.debug("Creating monitoring connection.")
        
        execute = client_yate.msg("call.execute",
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
                logger.debug("Monitor connection answered.")

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
                logger.debug("Monitor connection not answered.")
                monitor = None                

    count = 0

    for t in tests:
        concurrent = select_non_called(concurrent)        
        if len(concurrent) >= con_max:
            logger.debug("waiting on concurrency limit: %d" % con_max)
            yield defer.DeferredList(concurrent, fireOnOneCallback=True)
            _, fired = getResult()
            concurrent.remove(concurrent[fired])

        count = count + 1

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
             "target": target % impossible_prefix,
             "maxcall": 1000})

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

            logger.debug("[%d] outgoing call to %s" % (count, callid))
        
            yield client_yate.onwatch(
                "call.answered",
                lambda m : m["targetid"] ==  callid,
                until = end)
            answered = getResult()

            targetid = execute["targetid"]

            monitoring = False

            if monitor and not monitored :
                logger.debug("[%d] monitoring: %s" % (count, callid))
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

            logger.debug("[%d] recording: %s" % (count, str(callid)))
            client_yate.msg(
                "chan.masquerade",
                {"message": "chan.attach",
                 "id": callid,
#                 "source": "moh/default",
                 "source": "tone/silence",                 
                 "consumer": "wave/record//tmp/recording%s.slin" % \
                     callid.replace("/", "-"),
                 "maxlen": 0}).enqueue()

            yield remoteid_def
            remoteid = getResult()

            logger.debug(
                "[%d] running test with local=(%s, %s) remote=%s" %
                (count, targetid, callid, remoteid))

            start = time.time()

            result = go(handler(
                client_yate, server_yate,
                count,
                targetid,                
                callid, remoteid, t))

            result.addCallbacks(success, failure,
                                callbackArgs=(count, start),
                                errbackArgs=(count, start))

            if monitoring:
                result.addCallback(
                    lambda _, mon_id: monitored.remove(mon_id),
                    callid)
                
            concurrent.append(result)
        except AbandonedException, e:
            if not route.called:
                route.cancel()
            logger.warn("[%d] outgoing call to %s abandoned" % (count, callid))

    logger.debug(
        "Waiting for %d tests to finish" % len(select_non_called(concurrent)))
    yield defer.DeferredList(concurrent)
    logger.info("Test finished!")

    if monitor and monitorid:
        logger.debug("droping monitor connection")
        yield client_yate.msg("call.drop", {"id": monitorid}).dispatch()
        getResult()
        yield sleep(1)
        getResult()

    logger.debug("stopping reactor!")        
    reactor.stop()

    logger.info("-"*80)    
    logger.info("Summary")
    logger.info("-"*80)
    logger.info("Tests: %d" % (len(successes) + len(failures)))
    if successes:
        logger.info("-"*80)        
        logger.info("Successes: %d" % len(successes))
        logger.info("Avg time: %.2f s" %
                    (reduce(lambda x, y: x + y, successes.values(), 0) /
                     len(successes)))
    if failures:
        logger.info("-"*80)        
        logger.info("Failures: %d" % len(failures))
        sumt = 0
        for tid, (t, f) in failures.iteritems():
            logger.info("%d, %s %s" % (tid, str(f.type), f.getErrorMessage()))
            sumt = sumt + t
        logger.info("Avg time: %.2f s" % (sumt/len(failures)))

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
    monitor = None):

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

