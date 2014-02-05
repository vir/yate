#!/usr/bin/python
"""
-----------------------------------------------------------------------
 test.py
 This file is a contribute of nexlab ( http://www.nexlab.it )
 for the YATE Project ( http://YATE.null.ro ).

 test.py is a really small and minimal script to test and understand
 the libyate.py library.
 It simply install "engine.timer" and intercept messages from
 yate and log them to stderr for 10 times, after that the script
 uninstall engine.timer and exit.

 libyate.py is Copyright (C) 2005 Nexlab S.r.l
 Author: Franco (nextime) Lanza <nextime@nexlab.it>

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
"""
from libyate import Yate

"""
Python is Object Oriented language, so, define a "yourapp" class

"""

class YateApp:
	""" initialize the object """
	def __init__(self):
			# call the Yate object
			self.app = Yate()
			# declare the event intercetion method
			self.app.__Yatecall__ = self.retenv

	# this function will manage the imput from Yate.
	# param "d" is the Yate.type variable for incoming message
	def retenv(self, d):
		if d == "":
			self.app.Output("PYTHON event: empty")
		elif d == "incoming":
			self.app.Output("PYTHON message: " +  self.app.name + " id: " + self.app.id)
			self.app.Acknowledge()
			self.count = self.count + 1
		elif d == "answer":
			self.app.Output("PYTHON Answered: " +  self.app.name + " id: " + self.app.id)
		elif d == "installed":
			self.app.Output("PYTHON Installed: " + self.app.name )
		elif d == "uninstalled":
			self.app.Output("PYTHON Uninstalled: " + self.app.name )
		else:
			self.app.Output("PYTHON event: " + self.app.type )


	# clean shutdown of our application...
	def uninstall(self):
			self.app.Uninstall("engine.timer")

	def main(self):
		# create and dispatch an initial test message to Yate
		self.app.Yate("test")
		self.app.retval = "ret_value"
		self.app.params = [ ['param1', 'value1'], ['param2', 'value2'] ]
		self.app.Dispatch()

		# reset the counter
		self.count = 0

		# install engine.timer
		self.app.Install("engine.timer", 10)

		# start the main loop
		while True:
			# check for events
			self.app.flush()

			# uninstall engine.timer after 5 messages from Yate
			if self.count == 5:
				self.uninstall()
				break

		# ok, now a second example with a threaded asyncore loop!
		self.count = 0
		self.app.Install("engine.timer", 10)
		self.app.th_loop()

		# wow, amazing, the loop still running and we can continue in the execution!
		# dispatch a final test message after the main loop exit
		self.app.Yate("final_test")
		self.app.retval = "ret_value"
		self.app.params = [ ['param1', 'value1'] ]
		self.app.Dispatch()

		# now wait for 10 message from yate, and then uninstall the engine.timer and exit
		while self.count != 10:
			pass
		self.uninstall()
		self.app.th_stop()
		# bye!
		self.app.Output("PYTHON: Bye!")

		# close file descriptors
		self.app.close()

# execution start here!
if __name__ == '__main__':
	a = YateApp()
	a.main()
