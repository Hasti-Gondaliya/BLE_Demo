#!/usr/bin/python
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import absolute_import, print_function, unicode_literals
from pydbus import SystemBus
from optparse import OptionParser, make_option
from gi.repository import GLib

import time
import dbus
import pydbus
import rpi_central
import bluezutils
import dbus.mainloop.glib
import speech_recognition as sr

try:
  from gi.repository import GObject
except ImportError:
  import gobject as GObject

def input_loop():
	command = ""
	try:
		while command != "quit": # loop until the command is quit
			r = sr.Recognizer()
			speech = sr.Microphone(device_index=1)
			with speech as source:
				print("say something!…")
				audio = r.adjust_for_ambient_noise(source)
				audio = r.listen(source)
			try:
				command = r.recognize_google(audio, language = 'en-US')

				print("---------->You said: " + command)
			except sr.UnknownValueError:
				print("Google Speech Recognition could not understand audio")
			except sr.RequestError as e:
				print("Could not request results from Google Speech Recognition service; {0}".format(e))
			# command = input("Enter a command: ") # get the command from the user

			if command == "connect": 
				rpi_central.connect() # call the connect function
			elif command == "show": 
				for address in rpi_central.device_address:
					print(address) # call the disconnect function
			elif command == "hello": 
				rpi_central.discovery(1) # call the disconnect function
			elif command == "stop disc": 
				rpi_central.discovery(0) # call the disconnect function
				print("====> Discovered devices: <====")
				for address in rpi_central.device_address:
					print(address) # call the disconnect function
				print("================================\n")
			elif command == "disconnect": 
				rpi_central.disconnect() # call the disconnect function
			elif command == "temp": 
				rpi_central.read_temp() # Report temperature
			elif command == "LED": 
				rpi_central.read_led() # Report current led status
			elif command == "ON": 
				rpi_central.write_led("1") # Report current led status
			elif command == "OFF": 
				rpi_central.write_led(0) # Report current led status
			elif command == "write": 
				# print("device_address.append("00:11:22:33:44:55") # append a dummy address")
				print("device_address") # print the updated device address
			elif command == "quit": 
				print("Quitting the program") # print a message
				raise SystemExit("Terminating the program")
			else: # if the command is not recognized
				print("Invalid command. Please use connect, disconnect, read, write, or quit.")
			mainloop.run()
	except KeyboardInterrupt:
		input_loop()
	except SystemExit:
		mainloop.quit()

if __name__ == '__main__':
	
	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
	bus = dbus.SystemBus()
	pybus = pydbus.SystemBus()

	om = dbus.Interface(bus.get_object("org.bluez", "/"),
				"org.freedesktop.DBus.ObjectManager")
	om.connect_to_signal("connected", rpi_central.properties_changed)
	objects = om.GetManagedObjects()

	for path, interfaces in objects.items():
		if "org.bluez.Device1" in interfaces:
			rpi_central.devices[path] = interfaces["org.bluez.Device1"]

	# Run the main loop
	mainloop = GObject.MainLoop()

try:
    input_loop()
except KeyboardInterrupt:
	mainloop.quit()