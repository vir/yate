#!/usr/bin/python
# -*- coding: iso-8859-2; -*-
"""
 flow.py

 Flow submodule for YAYPM.

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

from twisted.internet import reactor, defer
import types

import sys, logging, random, time, types, traceback, yaypm

logger_flow = logging.getLogger('yaypm.flow')

current_result = None

def logFailure(f):
    if f.type!=yaypm.AbandonedException:
        if logger_flow.isEnabledFor(logging.WARN):
            logger_flow.warn("Exception in flow: %s" % str(f))
    return f

class Result:
    def __init__(self, result = None, failure = None):
        self.result = result
        self.failure = failure

    def getResult(self):
        current_result = None
        if self.failure:
            if self.failure.value:
                raise self.failure.value
            else:
                raise Exception(self.failure)
        else:
            return self.result

def getResult():
    global current_result
    if current_result:
        return current_result.getResult()
    else:
        None

class Flow:
    def step(self):
        d = None
        try:
            d = self.fun_todo.next()
            if isinstance(d, defer.Deferred):
                d.addCallbacks(self.callback, self.errback)
                d.addErrback(logFailure)
                d.addErrback(self.unhandled_exception)
            else:
                self.return_with.callback(d)
        except StopIteration:
            if d:
                self.return_with.callback(d)
            self.return_with.callback(None)

    def __init__(self, fun_todo, return_with):
        self.fun_todo = fun_todo
        self.return_with = return_with
        global current_result
        current_result = None
        self.step()

    def callback(self, result):
        global current_result
        current_result = Result(result)
        self.step()

    def errback(self, failure):
        global current_result
        current_result = Result(failure = failure)
        self.step()

    def unhandled_exception(self, failure):
        self.return_with.errback(failure)

def go(fun_todo):
    return_with = defer.Deferred()
    if isinstance(fun_todo, types.GeneratorType):
        Flow(fun_todo, return_with)
    else:
        reactor.callLater(0, return_with.callback, fun_todo)
    return return_with

