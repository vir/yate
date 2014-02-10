"""
-----------------------------------------------------------------------
 libyate.py
 This file is a contribute of nexlab ( http://www.nexlab.it )
 for the YATE Project ( http://YATE.null.ro ).

 libyate.py is Copyright (C) 2005 Nexlab S.r.l
 Author: Franco (nextime) Lanza <nextime@nexlab.it>
 Version: 0.3

 Python interface library for Yate

 Yet Another Telephony Engine - a fully featured software PBX and IVR
 is Copyright (C) 2004 Null Team

 Thanks to:
 - Dana ( my love who understand me when at 5:00 am i'm writing this piece of code )
 - Diana and Paoul ( Null Team, creators of the YATE project itself )
 - markit ( this guy it's so insistent to point my attenction to yate ... )
 - all people of FSFE and all free software developers ( OS filosofy roks! )
 - debian project ( i love this GNU\Linux distro )

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
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

------------------------------------------------------------------------

Changelog:
----------

version 0.3:
	- added threading support.
	- modified test.py example application to do an example of how to use also the threaded version.

version 0.2:
	- rewriting with asyncore support and better use of OO python power.
	- added a test.py application as an example of how to use this library.

version 0.1:
	- first version, it's based on a 1-to-1 port of the libyate.php library.


TODO:
-----
- here i will write few lines of documentation for the use of the lib
- add support for audio channels ( fd 4 and 5 )

"""

# various needed import

import sys

try:
	import asyncore, asynchat, random, time, string
except:
	sys.stdout.write("YATE PYTHON LIBRARY:\n\n")
	sys.stderr.write("You need the following python modules installed:\n\n1- asyncore\n2- asynchat\n3- random\n4- time\n5- string\n6- threading\n")
	sys.exit(1)


# internal class to make use of asyncore python module
class YateInit(asynchat.async_chat):
	""" internal use only! """

	def __init__(self, fd, handler):
		asynchat.async_chat.__init__(self)
		self.set_terminator("\n")
		self.in_buffer = ''
		self._incoming = []
		self.set_file(fd)
		self.handler = handler

	def set_file(self, fd):
		self._fileno = fd
		self.socket = asyncore.file_wrapper(fd)
		if fd == 0:
			self.add_channel()

	def handle_connect(self):
		pass

	def collect_incoming_data(self, data):
		self.in_buffer = self.in_buffer + data

	def found_terminator(self):
		if self._fileno == 0:
			datain = self.in_buffer
			self.in_buffer = ''
			if (datain != '\n') and ( datain != ''):
				self.handler(datain)

	def handle_close(self):
	#	self.write("CLOSE\n")
		self.close()

	def write(self, str):
		self.send(str)


class Yate:

	""" internal methods """

	type = ''
	name = ''
	retval = ''
	origin = ''
	id = ''
	handled = ''
	params = []

	# initialize the file descriptors to communicate with yate
	# ( internal use )
	def __init__(self):
		self.si = YateInit(0, self)
		self.so = YateInit(1, self)
		self.se = YateInit(2, self)

	# static function to intercept incoming message from yate
	# ( internal use )
	def __call__(self, data):
		self.NotifyEvent(data)

	# static function to parse incoming message from yate
	# ( internal use )
	""" Do a better and most precise parsing, funcking guy! """
	def parse_incoming_data(self, data):

		""" TODO: add feof check to respond with EOF when necessary """

		#if data == EOF:
		#	sys.stderr.write("antani\n")
		if data == None:
			return ''
		data = string.replace(data, "\n", "")
		if data == '':
			return ''
		part = string.split(data, ":")
		if part[0] == "%%>message":
			# incoming message str_id:int_time:str_name:str_retval[:key=value...]
			self.Yate(self.Unescape(part[3]), self.Unescape(part[4]), self.Unescape(part[1]))
			self.type = "incoming"
			self.origin = 0 + int(part[2])
			self.params = self.FillParams(part, 5)
		elif part[0] == "%%<message":
			# message answer str_id:bool_handled:str_name:str_retval[:key=value...]
			self.Yate(self.Unescape(part[3]), self.Unescape(part[4]), self.Unescape(part[1]))
			self.type = "answer"
			self.handled = self.Str2bool(part[2])
			self.params = self.FillParams(part, 5)
		elif part[0] == "%%<install":
			# install answer num_priority:str_name:bool_success
			self.Yate(self.Unescape(part[2]), "", 0+int(part[1]))
			self.type = "installed"
			self.handled = self.Str2bool(part[3])
		elif part[0] == "%%<uninstall":
			# uninstall answer num_priority:str_name:bool_success
			self.Yate(self.Unescape(part[2]), "", 0+int(part[1]))
			self.type = "uninstalled"
			self.handled = self.Str2bool(part[3])
		elif part[0] == "Error in":
			# We are already in error so better stay quiet
			pass
		else:
			self.Output("PYTHON parse error: " + data)
		return self.type

	# function to notify an event to the user application
	# ( internal use )
	def NotifyEvent(self, data):
		udata = self.parse_incoming_data(data)
		#self.se.write(data + "\n")
		self.__Yatecall__(udata)
	# function to convert params lists to escaped string form ready
	# to use in a message to engine
	def List2str(self, params = []):
		r = ''
		n = len(params)
		if n > 0:
			c = 0
			while c < n:
				r = r + ":" + self.Escape(params[c][0]) + "=" + self.Escape(params[c][1])
				c = c + 1
		return r

	# asyncore.loop() wrapper for retrocompatibility with python prior to
	# 2.4, this is needed because python 2.3 and prior asyncore.loop
	# cant use the "count=1" parameter that we need to programmate exit
	# from the loop.
	def __loop__(self, timeout=30.0, use_poll=False, map=None, count=None):
		poll_fun = asyncore.poll
		if map is None:
			map = asyncore.socket_map

		if count is None:
			while map:
				poll_fun(timeout, map)

		else:
			while map and count > 0:
				poll_fun(timeout, map)
				count = count - 1

	# loop executed in separate thread
	def __th_loop__(self, exit_cond):
		self.exit_condition = exit_cond
		while not self.exit_condition:
			self.flush()

	# function to create a new thread with an asyncore loop
	def th_loop(self):
		try:
			import threading
			self.exit_condition = []
			threading.Thread(target=self.__th_loop__, args=(self.exit_condition,)).start()
		except:
			self.Output("PYTHON ERROR: You need the threading python module to use threaded asyncore.loop()")

	# stop a threaded loop
	def th_stop(self):
		self.exit_condition.append(None)

	# function to be wrapped out on the user application.
	# this is here only to advise the error if the function
	# isn't wrapped
	def __Yatecall__(self, data):
		self.se.write("PYTHON ERROR: Hey dude! You MUST redeclare the __Yatecall__() in your app!\n")

	""" following methods are called from the user application """

	# do the asyncore loop! User MUST even call this at the end of his application
	# I love pool(), so, i use it, you can use select() instead simply using asyncore.loop()
	# without the "True" parameter.
	def flush(self):
		if sys.version >= "2.4":
			asyncore.loop(0, True, count=1)
		else:
			self.__loop__(0, count=1)

	# static function to clean close the asyncore.loop
	def close(self):
		asyncore.close_all()

   # static function to output a string to Yate's stderr or logfile
   # @param $str String to output
	def Output(self, str):
		self.se.write(str + '\n')

	# Static replacement error handler that outputs plain text to stderr
   # @param $errno Error code
   # @param $errstr Error text
   # @param $errfile Name of file that triggered the error
   # @param $errline Line number where the error occured
	""" TODO: rewrite this function to handle python exceptions? """
	def ErrorHandler(self, errno, errstr, errfile, errline):
		str = ' [' + errno + '] ' + errstr + ' in ' + errfile + ' line ' + errline + '\n'
		if errno == 'E_USER_ERROR':
			self.Output('Python FATAL: %s' % str)
			sys.exit(1)
		elif (errno == 'E_WARNING') or (errno == 'E_USER_WARNING'):
			self.Output('Python ERROR: %s' % str)
		elif (errno == 'E_NOTICE') or (errno == 'E_USER_NOTICE'):
			self.Output('Python WARNING: %s' % str)
		else:
			self.Output('Python Unknown ERROR: %s' % str)

   # Static function to convert a Yate string representation to a boolean
   # @param $str String value to convert
   # @return True if $str is "true", false otherwise
	def Str2bool(self, str):
		if str == "true":
			return True
		else:
			return False

   # Static function to convert a boolean to a Yate string representation
   # @param $bool Boolean value to convert
   # @return The string "true" if $bool was true, "false" otherwise
	def Bool2str(self, bool):
		if bool == True:
			return "true"
		else:
			return "false"


   # Static function to convert a string to its Yate escaped format
   # @param $str String to escape
   # @param $extra (optional) Character to escape in addition to required ones
   # @return Yate escaped string
	def Escape(self, str, extra = ""):
		str = str + ""
		s = ""
		n = len(str)
		i = 0
		while i < n:
			c = str[i]
			if( ord(c) < 32 ) or (c == ":") or (c == extra):
				c = chr(ord(c) + 64)
				s = s + "%"
			elif( c == "%" ):
				s = s + c
			s = s + c
			i = i + 1
		return s


   # Static function to convert a Yate escaped string back to a plain string
   # @param $str Yate escaped string to unescape
   # @return Unescaped string
	def Unescape(self, str):
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

   # Install a Yate message handler
   # @param $name Name of the messages to handle
   # @param $priority (optional) Priority to insert in chain, default 100
	def Install(self, name, priority = "100"):
		name = self.Escape(name)
		initstr = "%%>install"
		self.so.write("%s:%s:%s\n" % ( initstr, priority, name ))
		#self.se.write("%s:%s:%s\n" % ( initstr, priority, name ))
		self.flush()

   # Uninstall a Yate message handler
   # @param $name Name of the messages to stop handling
	def Uninstall(self, name):
		initstr = "%%>uninstall"
		name = self.Escape(name)
		self.so.write("%s:%s\n" % ( initstr, name))
		#self.se.write("%s:%s\n" % ( initstr, name))
		self.flush()

   # Constructor. Creates a new outgoing message
   # @param $name Name of the new message
   # @param $retval (optional) Default return
   # @param $id (optional) Identifier of the new message
	def Yate(self, name, retval = "", id = ""):
		if id == "":
			rand = random.Random()
			rand.seed(time.time())
			if sys.version >= "2.3":
				id = "PY-" + "%x" % int(int (rand.random() * time.time() * 10000000) * int(rand.random() * 1000000000000))
			else:
				id = "PY-" + "%x" % int(int (rand.random() * time.time())) + "%x" % int(int (rand.random() * time.time()))
		self.type = "outgoing"
		self.name = name
		self.retval = retval
		self.origin = int(time.strftime("%s"))
		self.handled = 'true'
		self.id = id
		self.params = []

	# Fill the parameter array from a text representation
	# @param $parts A numerically indexed array with the key=value parameters
	# @param $offs (optional) Offset in array to start processing from
	def FillParams(self, parts, offs = 0):
		n = len(parts)
		r = ''
		if n > 0:
			r = []
		c = 0
		while c < n:
			if c >= offs:
				s = string.split(parts[c], "=")
				r.append( [ self.Unescape(s[0]), self.Unescape(s[1]) ] )
			c = c + 1
		return r

	# Dispatch the message to Yate for handling
	# @param $message Message object to dispatch
	def Dispatch(self):
		if self.type != "outgoing":
			self.Output("Python bug: attempt to dispatch message type: " + self.type)
			return
		i = self.Escape(self.id, ':')
		t = str(0 + self.origin)
		n = self.Escape(self.name, ':')
		r = self.Escape(self.retval, ':')
		p = self.List2str(self.params)
		self.so.write('%%>message:' + i + ':' + t + ':' + n + ':' + r + p + '\n')
		#self.se.write('%%>message:' + i + ':' + t + ':' + n + ':' + r + p + '\n')
		self.type = "dispatched"
		self.flush()


	# Acknowledge the processing of a message passed from Yate
	# @param $handled (optional) True if Yate should stop processing, default false
	def Acknowledge(self):
		if self.type != "incoming":
			self.Output("PYTHON bug: attempt to acknowledge message type: " + self.type )
			return
		i = self.Escape(self.id, ':')
		k = self.Bool2str(self.handled)
		n = self.Escape(self.name, ':')
		r = self.Escape(self.retval, ':')
		p = self.List2str(self.params)
		self.so.write('%%<message:' + i + ':' + k + ':' + n + ':' + r + p + '\n')
		#self.se.write('%%<message:' + i + ':' + k + ':' + n + ':' + r + p + '\n')
		self.type = "acknowledged"
		self.flush()

# vi: set ts=3 sw=4 sts=4 noet
# EOF!
