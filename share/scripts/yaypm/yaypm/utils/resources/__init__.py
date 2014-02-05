import logging
from yaypm.utils import XOR
from twisted.internet import defer
from random import random

logger = logging.getLogger("yaypm.resources")

class Resource:
    def _match(self, *args):
        raise NotImplementedError("Abstract Method!")

    @defer.inlineCallbacks
    def play(self, yate, callid, targetid,
             stopOnDTMF = False, until = None,
             override = False, *args):

        files = self._match(*args)

        if not until:
            until = yate.onwatch("chan.hangup",
                               lambda m : m["id"] == callid)

        for f in files:
            logger.debug("on %s %s: %s", targetid,
                         "overiding" if override else "playing",
                         f)

            nid = f + str(random())

            m = yate.msg("chan.masquerade",
                         {"message": "chan.attach",
                          "id": targetid,
                          "override" if override else "source": f,
                          "notify": nid})
            yield m.dispatch()

            if stopOnDTMF:
                dtmf, notify = yield XOR(
                    yate.onmsg("chan.notify",
                               lambda m : m["targetid"] == nid,
                               autoreturn = True,
                               until = until),
                    yate.onwatch("chan.dtmf",
                                 lambda m : m["id"] == callid,
                                until = until))
                if dtmf:
                    defer.returnValue(notify)
            else:
                notify = yield yate.onwatch("chan.notify",
                                 lambda m : m["targetid"] == nid,
                                 until = until)

            if notify["reason"] != "eof":
                break

    def override(self, yate, callid,
                 stopOnDTMF=False, until = None, *args):

        return Resource.play(self, yate, callid, callid, stopOnDTMF,
                             until, True, *args)

class StaticResource(Resource):
    def __init__(self, attach, desc = None):
        self.attach = attach
        if not desc:
            self.desc = attach

    def _match(self, *args):
        return [self.attach]

class ConcatenationResource(Resource):
    def __init__(self, *args):
        self.resources = []
        current = None
        current_args = None
        for arg in args:
            if isinstance(arg, Resource):
                if current:
                    self.resources.append((current, current_args))
                current = arg
                current_args = []
            else:
                if not current:
                    raise WrongValue("Argument without Resource!")
                current_args.append(arg)
        if current:
            self.resources.append((current, current_args))

    def _match(self, *args):
        result = []
        for resource, res_args in self.resources:
            result.extend(resource._match(*(args[i] for i in res_args)))
        return result
