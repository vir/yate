import logging, yaypm
from twisted.internet import reactor, defer
from twisted.python import failure
from yaypm import CancellableDeferred, TCPDispatcherFactory


def sleep(time, until = None):
    d = CancellableDeferred()
    if until:
        def until_callback(m):
            d.cancel(m)
            return m
        until.addBoth(until_callback)
    
    reactor.callLater(time, lambda : not d.cancelled and d.callback(None))
    return d

def setup(start, args = [], kwargs = {},
          host = "localhost", port = 5039,
          defaultLogging = True, runreactor = True):
    try:
        import yateproxy
        embedded = True
    except Exception:
        embedded = False

    if embedded:
        yaypm.embeddedStart(start, args, kwargs)
    else:
        reactor.connectTCP(host, port,
            TCPDispatcherFactory(start, args, kwargs))

        if defaultLogging:
            hdlr = logging.StreamHandler()
            formatter = ConsoleFormatter('%(name)s %(levelname)s %(message)s')                                

            hdlr.setFormatter(formatter)
            logger = logging.getLogger()

            logger.addHandler(hdlr)
            logger.setLevel(logging.DEBUG)

            yaypm.logger.setLevel(logging.INFO)
            yaypm.logger_messages.setLevel(logging.INFO)

    if not embedded and runreactor:
        reactor.run()

    
class XOR(CancellableDeferred):
    def __init__(self, *deferreds):
        defer.Deferred.__init__(self)
        self.deferreds = deferreds
        self.done = False
        index = 0
        for deferred in deferreds:
            deferred.addCallbacks(self._callback, self._callback,
                                  callbackArgs=(index,True),
                                  errbackArgs=(index,False))
            index = index + 1

    def _callback(self, result, index, succeeded):
        if self.done:
            return None
        self.done = True
        i = 0
        for deferred in self.deferreds:
            if index != i:
                deferred.addErrback(lambda _: None)
                deferred.cancel()
            i = i + 1

        if succeeded:
            self.callback((index, result))
        else:
            self.errback(failure.Failure())

        return None

class RestrictedDispatcher:

    def __init__(self, parent, restriction):
        self.parent = parent
        self.restriction = restriction
   
    def msg(self, name, attrs = None, retValue = None):
        return self.parent.msg(name, attrs, retValue)
    
    def onmsg(self, name, guard = lambda _: True,
              until = None, autoreturn = False):
        if until:
            self.restriction.addCallbacks(d.cancel, d.cancel)
            d = until
        else:
            d = self.restriction
            
        return self.parent.onmsg(name, guard, d, autoreturn)

    def onwatch(self, name, guard = lambda _: True, until = None):

        return self.parent.onwatch(name, guard, until)

class ConsoleFormatter(logging.Formatter) :
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
        if(ConsoleFormatter._level_colors.has_key(record.levelname)):
            record.levelname = "%s%s\033[0;0m" % \
                            (ConsoleFormatter._level_colors[record.levelname],
                             record.levelname)
        record.name = "\033[37m\033[1m%s\033[0;0m" % record.name
        return logging.Formatter.format(self, record)    
