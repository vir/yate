"""
 yaypm.py

 YAYPM - Yet Another Yate(http://yate.null.ro/) Python Module,
 uses Twisted 2.0 (http://twistedmatrix.com/projects/core/).

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

import sys

from twisted.internet import reactor, defer
from twisted.protocols.basic import LineReceiver
from twisted.internet.protocol import Protocol, ClientFactory
from twisted.python import failure
from threading import Thread, Event
import logging, imp, time, random, traceback, string

try:
    import yateproxy
    class YateLogHandler(logging.Handler):
        def __init__(self):
            import yateproxy
            logging.Handler.__init__(self)
            self.setFormatter(logging.Formatter('%(message)s'))

        def emit(self, record):
            try:
                yateproxy.debug("pymodule(%s)" % record.name,
                                record.levelno,
                                self.format(record))
            except:
                self.handleError(record)

except Exception:
    pass

#main_logger = logging.getLogger('yaypm')
logger = logging.getLogger('yaypm.internals')
logger_messages = logging.getLogger('yaypm.messages')

_HANDLER_TYPE_MSG=0
_HANDLER_TYPE_WCH=1

_DEFAULT_HANDLER_PRIO=100

_MSG_TYPE_DSCS = ("message", "watch")

def embeddedStart(*args):
    """
    Stub, redefined later.
    """
    raise RuntimeError("Embeded Dispatcher should be run only from pymodule!")

class AbandonedException(Exception):
    """
    Raised when deferred is abandoned by until condition.

    """
    def __init__(self, cause):
        self.cause = cause
        Exception.__init__(self, cause)

class DisconnectedException(Exception):
    """
    Raised when TCPDispatcher is disconnected.

    """
    pass

class CancelledError(Exception):
    """
    Raised when ...

    """
    pass

class CancellableDeferred(defer.Deferred):
    """
    Deferred that can be cancelled.
    """
    def __init__(self, canceller=None):
        defer.Deferred.__init__(self)
        self.canceller = canceller
        self.cancelled = False

    def cancel(self, *args, **kwargs):
        canceller = self.canceller
        if not self.called:
            self.cancelled = True
            if canceller:
                canceller(self, *args, **kwargs)
            if not self.called:
               self.errback(CancelledError())
        elif isinstance(self.result, CancellableDeferred):
            # Waiting for another deferred -- cancel it instead
            self.result.cancel()
        #else:
            # Called and not waiting for another deferred
            #raise defer.AlreadyCalledError

class Dispatcher:
    """
    Message dispatcher. Registers and fires message deferreds.
    """

    def __init__(self):
        self.handlers = {}

    def _autoreturn(self, m):
        m.ret(True)
        return m

    def _register_handler(self, name, hdlr_type, guard, until, autoreturn):
        """
        Register message/watch handler.
        """

        d = CancellableDeferred(
            lambda d, m = None : self._cancelHandler(m, name, hdlr_type, d))

        key = (name, hdlr_type)
        handlers = self.handlers.get(key, {})
        if not handlers:
            self.handlers[key] = handlers
        handlers[d] = guard

        if until:
            def until_callback(m):
                d.cancel(m)
                return m
            until.addBoth(until_callback)
        if autoreturn:
            d.addCallback(self._autoreturn)

        return d

    def is_handler_installed(name, hdlr_type):
        return self.handlers.get((name, hdlr_type), None) != None

    def _register_and_install_handler(
        self, name, hdlr_type, guard, until, autoreturn = False):

        """
        Register message/watch handler. Install appropriate handler in yate.
        """

        handler = self.handlers.get((name, hdlr_type), None)

        if handler == None:
            if logger_messages.isEnabledFor(logging.DEBUG):
                logger.debug(
                    "No handler registered in Yate for %s: %s",\
                    _MSG_TYPE_DSCS[hdlr_type], name)
            if hdlr_type == _HANDLER_TYPE_MSG:
                d = self.installMsgHandler(name)
            else:
                d = self.installWatchHandler(name)

            if d:
                d.addCallback(
                    lambda _: self._register_handler(name, hdlr_type, guard, until, autoreturn))
                return d

        return self._register_handler(
            name, hdlr_type, guard, until, autoreturn)

    def _fireHandlers(self, m, hdlr_type):
        """
        Fire message/watch handlers.
        """

        if logger_messages.isEnabledFor(logging.DEBUG):
            logger.debug(
                "searching for handlers of %s: %s",
                _MSG_TYPE_DSCS[hdlr_type], m.getName())

        key = (m.getName(), hdlr_type)

        handlers = self.handlers.get(key, None)
        if not handlers:
            return False

        done = False
        to_check = handlers.keys()

        for d in to_check:
            guard = handlers.get(d, None)
            if guard and guard(m):
                done = True
                if logger_messages.isEnabledFor(logging.DEBUG):
                    logger.debug(
                        "firing handler for %s: %s on %s",
                        _MSG_TYPE_DSCS[hdlr_type], m.getName(), str(d))

                del handlers[d]
                d.callback(m)
                if hdlr_type == _HANDLER_TYPE_MSG:
                    break

        if logger_messages.isEnabledFor(logging.DEBUG):
            result = "Active handlers:\n" + "-"*80
            for (name, type), handlers in self.handlers.iteritems():
                result = result + "\n%s \"%s\": %d handler(s)" % \
                         (_MSG_TYPE_DSCS[type], name, len(handlers))
            result = result + "\n" + "-"*80
            logger_messages.debug(result)

        return done

    def _removeHandler(self, name, hdlr_type, d2remove):
        """
        Remove message/watch handler
        """

        key = (name, hdlr_type)
        if self.handlers.has_key(key):
            del self.handlers[key][d2remove]

    def _cancelHandler(self, m, name, hdlr_type, d):
        """
        Cancel YAYPM deferred.
        """
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug("Canceling: %s", name)
        self._removeHandler(name, hdlr_type, d)
        try:
            raise AbandonedException(m)
        except:
            d.errback(failure.Failure())

    def installMsgHandler(name, prio = 100):
        """
        Install YATE message handler. Abstract. Implementation dependent.
        """
        raise NotImplementedError("Abstract Method!")

    def installWatchHandler(name):
        """
        Install YATE watch handler. Abstract. Implementation dependent.
        """
        raise NotImplementedError("Abstract Method!")

    def msg(self, name, attrs = None, retValue = None):
        """
        Create message. Abstract. Implementation dependent.
        """
        raise NotImplementedError("Abstract Method!")

    def onmsg(self, name, guard = lambda _: True,
              until = None, autoreturn = False):
        """
        Create deferred that will fire on specified message.
        """
        return self._register_and_install_handler(
            name, _HANDLER_TYPE_MSG, guard, until, autoreturn)

    def onwatch(self, name, guard = lambda _: True, until = None):
        """
        Create deferred that will fire on specified watch.
        """
        return self._register_and_install_handler(
            name, _HANDLER_TYPE_WCH, guard, until)

class AbstractMessage:
    """
    Message interface.
    """

    def __init__(self):
        raise NotImplementedError("Abstract Method!")

    def getName(self):
        raise NotImplementedError("Abstract Method!")

    def __getitem__ (self, key):
        raise NotImplementedError("Abstract Method!")

    def __setitem__ (self, key, value):
        raise NotImplementedError("Abstract Method!")

    def setRetValue(self, value):
        raise NotImplementedError("Abstract Method!")

    def getRetValue(self):
        raise NotImplementedError("Abstract Method!")

    def ret(self, handled=True, retValue=None):
        raise NotImplementedError("Abstract Method!")

def _checkIfIsAlive(method):
    """
    Guards EmbeddedDispatcher MessageProxy methods against calling
    in wrong states.
    """

    def wrapper(self, *args, **kwargs):
        if self._gone:
            raise RuntimeError(
                "Message has been dispatched/enqueued or returned and can't be touched anymore!")
        else:
            return method(self, *args, **kwargs)
    return wrapper


class EmbeddedDispatcher(Dispatcher):
    """
    Dispatcher that works with embedded pymodule.
    """
    class _MessageProxy(AbstractMessage):
        """
        YATE message wrapper. Notice that python object lifespan may extend
        beyond YATE message lifespan. That is why checkIfIsAlive decorator is
        used. This can be also solved by making a message copy. At the moment
        I think that it is better not to cheat to much.
        """
        def __init__(self, yatemsg, event = None, result = None):
            self._yatemsg = yatemsg
            self._event = event
            self._result = result
            self._gone = False # sent or returned

        @_checkIfIsAlive
        def getName(self):
            return yateproxy.message_getName(self._yatemsg)

        @_checkIfIsAlive
        def __getitem__ (self, key):
            return yateproxy.message_getValue(self._yatemsg, key)

        @_checkIfIsAlive
        def __setitem__ (self, key, value):
            yateproxy.message_setParam(self._yatemsg, key, value)

        @_checkIfIsAlive
        def setRetValue(self, value):
            return yateproxy.message_setRetValue(self._yatemsg, value)

        @_checkIfIsAlive
        def getRetValue(self):
            return yateproxy.message_getRetValue(self._yatemsg)

        @_checkIfIsAlive
        def ret(self, handled=True, retValue=None):
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug("Retuning(%s): %s", str(handled), str(self))

            if not self._result or not self._event:
                raise RuntimeError("Can't return own message!")
            self._result[0] = handled
            if retValue != None:
                self.setRetValue(retValue)
            self._event.set()
            self._gone = True

        def _dispatchAndSignal(self, d):
            result = yateproxy.message_dispatch(self._yatemsg)
            reactor.callFromThread(d.callback, result)

        @_checkIfIsAlive
        def dispatch(self):
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug("Dispatching: %s", str(self))
            if self._result or self._event:
                raise RuntimeError("Can't dispatch incomming message!")
            d = defer.Deferred()
            reactor.callInThread(self._dispatchAndSignal, d)
            return d

        @_checkIfIsAlive
        def enqueue(self):
            if not self._gone:
                if logger.isEnabledFor(logging.DEBUG):
                    logger.debug("Enqueing: %s", str(self))
                self._gone = True
                yateproxy.message_enqueue(self._yatemsg)
            else:
                raise RuntimeError("Message already sent!")

        @_checkIfIsAlive
        def __iter__(self):
            def keys():
                for i in range(0, yateproxy.message_getLength(self._yatemsg)):
                    key = yateproxy.message_getKeyByIndex(self._yatemsg, i)
                    if key:
                        yield key
            return keys()

        @_checkIfIsAlive
        def __str__(self):
            result = self.getName()
            for k in self:
                result = result + ", " + "%s=%s" % (k, self[k])
            return result

    def __init__(self, interpreter, scripts, timeout = 1):
        """
        Create a dispatcher. Execute script in another thread. It should
        run Twisted reactor.
        """
##         hdlr = YateLogHandler()
##         root_logger = logging.getLogger()

##         root_logger.setLevel(logging.INFO)
##         root_logger.addHandler(hdlr)

##         logger_messages.setLevel(logging.INFO)

        Dispatcher.__init__(self)

        self._timeout = timeout

        global embeddedStart
        embeddedStart = lambda start, args, kwargs: reactor.callLater(0, start, self, *args, **kwargs)

        for i, script in enumerate(scripts):
            try:
                name = "__embedded_yaypm_module__%d" % i
                yateproxy.debug("yaypm",
                                logging.DEBUG,
                                "Loading script: %s as %s" % (script, name))
                imp.load_source(name, script)
            except Exception:
                yateproxy.debug("pymodule(%s)" % "yaypm",
                                logging.ERROR,
                                "Exception while loading: %s\n%s\n%s" %
                                (script,
                                 sys.exc_info()[1],
                                 string.join(traceback.format_tb(sys.exc_info()[2]))))
                yateproxy.debug("pymodule(%s)" % "yaypm",
                                logging.WARN,
                                "Reactor thread not started!")
                self.thread = None
                return

        self.thread = Thread(
            name = "pymodule script thread",
            target = reactor.run,
            kwargs = {"installSignalHandlers": 0})

        self.interpreter = interpreter

        self.thread.start()

        yateproxy.debug("pymodule(%s)" % "yaypm",
                        logging.INFO,
                        "Reactor thread started!")

    def installMsgHandler(self, name, prio = 100):
        """
        Install Pymodule message handler.
        """
        key = (name, _HANDLER_TYPE_MSG)
        self.handlers[key] = self.handlers.get(key, {})
        yateproxy.installMsgHandler(self.interpreter, name, prio)
        return None

        #d = CancellableDeferred()
        #d.callback(True)
        #return d

    def installWatchHandler(self, name):
        """
        Install Pymodule watch handler.
        """
        key = (name, _HANDLER_TYPE_WCH)
        self.handlers[key] = self.handlers.get(key, {})
        yateproxy.installWatchHandler(self.interpreter, name)
        return None

        #d = CancellableDeferred()
        #d.callback(True)
        #return d

    def msg(self, name, attrs = None, retValue = None):
        """
        Create YATE message wrapped with MessageProxy.
        """

        m = EmbeddedDispatcher._MessageProxy(
            yateproxy.message_create(name, str(retValue)))

        if attrs:
            for key, value in attrs.iteritems():
                m[key] = str(value)
        return m

    def _stop(self):
        """
        Stop dispatcher by stopping reactor.
        """

        reactor.callFromThread(reactor.stop)

        if self.thread:
            while self.thread.isAlive():
                self.thread.join(1)

    def _fireHandlersAndSignal(self, m, hdlr_type):
        """
        Fire message handlers. Signal completion.
        """
        try:
            if not self._fireHandlers(m, hdlr_type):
                m._event.set()
        except:
            logger.exception("exception while firing handlers!")

    def _timeoutHandler(self, m):
        if not m._event.isSet():
            logger.warn("Message %s not returned in %d sec!", m, self._timeout)

    def _enqueEmbeddedMessage(self, yateMessage):
        """
        Fire message handlers in reactor. Wait for completion.
        """

        result = [False]
        event = Event()
        m = EmbeddedDispatcher._MessageProxy(yateMessage, event, result)

        if self._timeout:
            reactor.callLater(self._timeout, self._timeoutHandler, m)
        reactor.callFromThread(self._fireHandlersAndSignal, m, _HANDLER_TYPE_MSG)
        event.wait()
        return result[0]

    def _enqueEmbeddedWatch(self, yateMessage, handled):
        """
        Fire watch handlers by reactor. It is a copy not the original message.
        It is better than making this call synchronous.
        """
        reactor.callFromThread(
            self._fireHandlers,
            EmbeddedDispatcher._MessageProxy(yateMessage), _HANDLER_TYPE_WCH)


def escape(str, extra = ":"):
    """
    ExtModule protocol escape.
    """
    str = str + ""
    s = ""
    n = len(str)
    i = 0
    while i < n:
        c = str[i]
        if( ord(c) < 32 ) or (c == extra):
            c = chr(ord(c) + 64)
            s = s + "%"
        elif( c == "%" ):
            s = s + c
        s = s + c
        i = i + 1
    return s

def unescape(str):
    """
    ExtModule protocol unescape.
    """
    s = ""
    n = len(str)
    i = 0
    while i < n:
        c = str[i]
        if c == "%":
            i = i + 1
            c = str[i]
            if c != "%":
                c = chr(ord(c) - 64)
        s = s + c
        i = i + 1
    return s


class TCPDispatcher(Dispatcher, LineReceiver):
    """
    Dispatcher that works over ExtMod protocol.
    """

    class _TCPMessage(AbstractMessage):
        """
        Message interface.
        """

        def __init__(self, dispatcher,
                     mid = None,
                     name = None,
                     timestamp = None,
                     retvalue = None,
                     attrs = None,
                     returned = False):

            self._name = name
            self._dispatcher = dispatcher

            if timestamp:
                self._timestamp = timestamp
            else:
                self._timestamp = long(time.time())

            if mid:
                self._mid = mid
            else:
                self._mid = str(self._timestamp) + str(random.randrange(1, 10000, 1))

            self._returned = returned
            self._retvalue = retvalue

            self._attrs = attrs

        def __str__(self):
            result = self.getName()
            for k in self:
                result = result + ", " + "%s=%s" % (k, self[k])
            return result

        def _format_attrs(self):
            """
            Format message attributes.
            """
            result = ""
            for name, value in self._attrs.iteritems():
                if type(value) == type(u""):
                    v = value.encode('raw_unicode_escape')
                elif type(value) != type(""):
                    v = str(value)
                else:
                    v = value
                result = result + ":" + name + "=" + escape(v)
            return result


        def _format_message_response(self, returned):

            result = "%%%%<message:%s:%s" % (self._mid, str(returned).lower())

            result = result + ":" + escape(self._name)

            if self._retvalue:
                result = result + ":" + escape(str(self._retvalue))
            else:
                result = result + ":"
            result = result + self._format_attrs()

            return result

        def _format_message(self):
            """
            %%>message:<id>:<time>:<name>:<retvalue>[:<key>=<value>...]
            """

            result = "%%%%>message:%s:%s" % (self._mid, str(self._timestamp))
            result = result + ":" + escape(self._name)
            result = result + ":"
            if self._retvalue:
                result = result + escape(self._retvalue)
            return result + self._format_attrs() + "\n"

        def getName(self):
            return self._name

        def __getitem__ (self, key):
            return self._attrs.get(key, None)

        def __setitem__ (self, key, value):
            self._attrs[key] = value

        def __iter__(self):
            return self._attrs.__iter__()

        def setRetValue(self, value):
            self._retvalue = value

        def getRetValue(self):
            return self._retvalue

        def ret(self, handled=True, retValue=None):
            if retValue:
                self.setRetValue(retValue)
            resp = self._format_message_response(handled)
            if logger_messages.isEnabledFor(logging.DEBUG):
                logger_messages.debug("sending resp.:\n" + resp)
            self._dispatcher.transport.write(resp + "\n")
            self._returned = True

        def _cancelResponse(self, d, m):
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug("Canceling: %s", self);
            del self.waiting[self._mid]
            try:
                raise AbandonedException("Abandoned by: %s" % m)
            except:
                d.errback(failure.Failure())

        def dispatch(self):
            d = CancellableDeferred(self._cancelResponse)
            self._dispatcher.waiting[self._mid] = (self, d)
            line = self._format_message()
            self._dispatcher.transport.write(line)
            if logger_messages.isEnabledFor(logging.DEBUG):
                logger_messages.debug("sending: " + line[:-1])
            return d

        def enqueue(self):
            self.dispatch()

    def _parse_attrs(self, values):
        values = values.split(":")
        if values:
            attrs = {}
            for key, value in [x.split("=", 1) for x in values]:
                attrs[key] = unescape(value)
            return attrs
        else:
            return None

    def _messageReceived(self, values):
        """
        Message parser.
        """
        values = values.split(':', 4)

        m = TCPDispatcher._TCPMessage(
            self,
            mid = unescape(values[0]),
            timestamp = values[1],
            name = unescape(values[2]),
            retvalue = None,
            attrs = None)

        l = len(values)
        if l > 3: m.setRetValue(unescape(values[3]))
        if l > 4: m._attrs = self._parse_attrs(values[4])

        if not self._fireHandlers(m, _HANDLER_TYPE_MSG):
            m.ret(False)

    def _watchReceived(self, values):
        """
        Watch parser.
        """

        w = TCPDispatcher._TCPMessage(
            self,
            mid = unescape(values[0]),
            returned = values[1],
            name = unescape(values[2]),
            retvalue = unescape(values[3]),
            attrs = self._parse_attrs(values[4]))

##        if w.getName() in ["test.checkpoint"]:
##            logger.warn("received checkpoint: " + w["check"])
##         if w.getName() in ["chan.hangup"]:
##             logger.warn("chan.hangup: " + w["id"])

        self._fireHandlers(w, _HANDLER_TYPE_WCH)

    def _messageResponse(self, values):
        """
        Message response parser.
        """

        mid = values[0]
        if self.waiting.has_key(mid):
            m, d = self.waiting[mid]
            l = len(values)
            if len(values) > 3:
                m.setRetValue(unescape(values[3]))
            if len(values) > 4:
                m._attrs = self._parse_attrs(values[4])
            del self.waiting[mid]

            if logger_messages.isEnabledFor(logging.DEBUG):
                logger_messages.debug("received response:\n" + str(m))

            d.callback(values[1] == "true")
        else:
            if logger.isEnabledFor(logging.WARN):
                logger.warn("Response to unknown message: %s", str(values))

    def _watchOrResponseReceived(self, values):
        values = values.split(':', 4)
        if values[0] == "":
            self._watchReceived(values)
        else:
            self._messageResponse(values)

    def _watchResponse(self, values):
        """
        Watch handler install response parser.
        """

        values = values.split(':')
        wid = "watch-" + values[0]
        if self.waiting.has_key(wid):
            _, d = self.waiting[wid]
            del self.waiting[wid]
            if unescape(values[1]) in ("ok", "true"):
                key = (values[0], _HANDLER_TYPE_WCH)
                self.handlers[key] = self.handlers.get(key, {})
                d.callback(True)
            else:
                logger.warn("Can't install handler for: %s", str(values[0]))
                d.errback(failure.Failure(
                    Exception("Can't install handler for: %s", str(values[0]))))
        else:
            if logger.isEnabledFor(logging.WARN):
                logger.warn("Response to unknown message: %s", str(values))


    def _installResponse(self, values):
        """
        Message handler install response parser.
        """

        values = values.split(':')
        mid = "install-" + values[1]
        if self.waiting.has_key(mid):
            _, d = self.waiting[mid]
            del self.waiting[mid]
            if unescape(values[2]) in ("ok", "true"):
                key = (values[1], _HANDLER_TYPE_MSG)
                self.handlers[key] = self.handlers.get(key, {})
                d.callback(True)
            else:
                logger.warn("Can't install handler for: %s", str(values[1]))
                d.errback(failure.Failure(
                    "Can't install handler for: %s" % str(values[1])))
        else:
            if logger.isEnabledFor(logging.WARN):
                logger.warn("Response to unknown message: %s", str(values))

    def _setlocalResponse(self, values):
        """
        Set local response parser.
        """
        values = values.split(':')

        name, value, success = values

        if success:
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug("Local %s set to: %s", name, value)
        else:
            if logger.isEnabledFor(logging.WARN):
                logger.warn("Local %s not set to: %s", name, value)

    def __init__(self, connected, args = [], kwargs = {},
                 reenter = True, selfwatch = True):
        """
        Create a dispatcher.
        """

        Dispatcher.__init__(self)

        self.delimiter='\n'
        self.reenter = reenter
        self.selfwatch = selfwatch
        self.connectedFunction = connected
        self.args = args
        self.kwargs = kwargs
        self.afterFirstLine = False
        self.waiting = {}
        self.parsers = {"%%>message": self._messageReceived,
                        "%%<message": self._watchOrResponseReceived,
                        "%%<install": self._installResponse,
                        "%%<watch": self._watchResponse,
                        "%%<setlocal": self._setlocalResponse}
#                        "%%<uninstall": self._uninstallResponse,
#                        "%%<unwatch": self._unwatchResponse}

    def connectionMade(self):
        self.transport.write(
            "%%%%>setlocal:reenter:%s\n" % str(self.reenter).lower())
        self.transport.write(
            "%%%%>setlocal:selfwatch:%s\n" % str(self.selfwatch).lower())
        self.connectedFunction(self, *self.args, **self.kwargs)

    def connectionLost(self, reason):
        logger.info("Connection lost: %s", reason.getErrorMessage());
        for (m, (_, d)) in self.waiting.items():
            try:
                raise DisconnectedException()
            except:
                d.errback(failure.Failure())
        self.waiting.clear()
##         try:
##             reactor.stop()
##         except:
##             pass

    def lineReceived(self, line):
        if logger_messages.isEnabledFor(logging.DEBUG):
            logger_messages.debug("received line:\n%s", line);

        if line == "":
            raise Exception("Can't build message from empty string!")
        line = line.rstrip()
        if line.startswith("Error in:"):
            raise Exception(line)

        kind, rest = line.split(':', 1)

        parser = self.parsers.get(kind, None)
        if parser:
            parser(rest)
        else:
            raise Exception("Can't interpret line: " + line)

    def installMsgHandler(self, name, prio = 100):
        """
        Install Pymodule message handler.
        """
        d = CancellableDeferred(
            lambda d, m = None : self._cancelHandler(m, name, _HANDLER_TYPE_MSG, d))

        logger_messages.debug("installing %s..." % name);

        key = "install-" + name

        if self.waiting.has_key(key):
            if logger_messages.isEnabledFor(logging.DEBUG):
                logger_messages.debug("install of %s already sent...", name);
            _, otherd = self.waiting[key]
            d.chainDeferred(otherd)
            self.waiting[key] = (None, d)
        else:
            self.waiting[key] = (None, d)

            line = "%%%%>install:%d:%s\n" % (prio, name)

            if logger_messages.isEnabledFor(logging.DEBUG):
                logger_messages.debug("sending:\n%s", str(line[:-1]));

            self.transport.write(line)

        return d

    def installWatchHandler(self, name):
        """
        Install Pymodule watch handler.
        """
        d = CancellableDeferred(
            lambda d, m = None : self._cancelHandler(m, name, _HANDLER_TYPE_WCH, d))

        key = "watch-" + name

        if self.waiting.has_key(key):
            _, otherd = self.waiting[key]
            d.chainDeferred(otherd)
            self.waiting[key] = (None, d)
        else:
            self.waiting[key] = (None, d)

            line = "%%%%>watch:%s\n" % name

            if logger_messages.isEnabledFor(logging.DEBUG):
                logger_messages.debug("sending:\n%s", str(line[:-1]));

            self.transport.write(line)

        return d

    def msg(self, name, attrs = None, retValue = None):
        return TCPDispatcher._TCPMessage(
            self, name = name, retvalue = retValue, attrs = attrs)

class TCPDispatcherFactory(ClientFactory):
    def __init__(self, connected,
                 args = [], kwargs = {},
                 reenter = True, selfwatch = True):
        self.connected = connected
        self.args = args
        self.kwargs = kwargs
        self.reenter = reenter
        self.selfwatch = selfwatch

    def startedConnecting(self, connector):
        logger.info("Connecting...")

    def buildProtocol(self, addr):
        logger.info("Connected.")
        return TCPDispatcher(
            self.connected,
            self.args, self.kwargs,
            self.reenter,
            self.selfwatch)

    def clientConnectionLost(self, connector, reason):
        logger.info("clientConnectionLost")
        pass

    def clientConnectionFailed(self, connector, reason):
        reactor.stop()

class Formatter(logging.Formatter) :
    _level_colors  = {
      "DEBUG": "\033[22;32m", "INFO": "\033[01;34m",
      "WARNING": "\033[22;35m", "ERROR": "\033[22;31m",
      "CRITICAL": "\033[01;31m"
     };
    def __init__(self,
                 fmt = '%(name)s %(levelname)s %(message)s',
                 datefmt=None):
        logging.Formatter.__init__(self, fmt, datefmt)

    def format(self, record):
        if(Formatter._level_colors.has_key(record.levelname)):
            record.levelname = "%s%s\033[0;0m" % \
                            (Formatter._level_colors[record.levelname],
                             record.levelname)
        record.name = "\033[37m\033[1m%s\033[0;0m" % record.name
        return logging.Formatter.format(self, record)
