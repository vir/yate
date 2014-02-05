#!/usr/bin/python

import sys, os, imp, logging, time, subprocess
from twisted.internet import reactor

logger = logging.getLogger('yaypm.srcmon')

def monitor():
    def modules2watch():
        modules2watch = {}
        pythonDir = sys.modules["os"].__file__.rsplit("/", 1)[0]

        for m in sys.modules.values():
            if hasattr(m, "__file__"):
                f = m.__file__
                if not f.startswith(pythonDir) and not f == '<stdin>':
                    if f.endswith("pyc"):
                        modules2watch[f[:-1]] = m
                    else:
                        modules2watch[f] = m

        return modules2watch

    def check():
        modules = modules2watch()
        watched = {}

        logger.info("Monitoring %d files." % len(modules.keys()))

        while True:
            for f in modules.keys():
                try:
                    t = os.stat(f)
                except os.error:
                    logger.info("File: '%s' possibly removed. Reloading..." % f)
                    yield True

                mtime = watched.get(f, None)
                if mtime:
                    if t.st_mtime > mtime:
                        logger.info("File: '%s' changed." % f)
                        yield False
                else:
                    watched[f] = t.st_mtime
            yield True

    def loop(t, f):
        if f():
            reactor.callLater(t, loop, t, f)
        else:
            logger.info("Reloading...")
            reactor.stop()
            os.execv(sys.argv[0], sys.argv)

    c = check()

    reactor.callLater(1, loop, 1, c.next)


if os.environ.has_key("MONITORED"):
    sys.stderr.write("Started in monitored mode.\n")
    reactor.callLater(1, monitor)
else:
    os.environ["MONITORED"] = ""

    while True:
        if not(subprocess.call(sys.argv)):
            break
        else:
            sys.stderr.write(
                "Monitored process failed(judging by exit code). " + \
                "Trying to reload.\n")
            time.sleep(3)

