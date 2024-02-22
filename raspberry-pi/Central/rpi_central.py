import bluezutils
import pydbus
import dbus
import binascii

device_address = []
TEMPERATURE_UUID = '0x2a6e'

BLUEZ_SERVICE_NAME = 'org.bluez'
DBUS_OM_IFACE =      'org.freedesktop.DBus.ObjectManager'
DBUS_PROP_IFACE =    'org.freedesktop.DBus.Properties'

GATT_SERVICE_IFACE = 'org.bluez.GattService1'
GATT_CHRC_IFACE =    'org.bluez.GattCharacteristic1'

ESS_SERVICE_UUID = '0000181a-0000-1000-8000-00805f9b34fb'
TEMPERATURE_UUID = '00002a6e-0000-1000-8000-00805f9b34fb'

CUS_SERVICE_UUID = '00001234-0000-1000-8000-00805f9b34fb'
CUS_LED_UUID = '00005678-0000-1000-8000-00805f9b34fb'

# The objects that we interact with.
ess_msrmt_chrc = None
custom_led_chrc = None

devices = {}

def as_int(value):
    """Create integer from bytes"""
    return int.from_bytes(value, byteorder='little')

def generic_error_cb(error):
    print('D-Bus call failed: ' + str(error))
    # mainloop.quit()

def temp_read_val_cb(value):
    print('Temperature: ' + (value[0]))

def led_read_val_cb(value):
    if (int(value[0])) == 0 :
        print("Light Status: OFF")
    elif (int(value[0])) == 1 :
        print("Light Status: ON")

def read_temp():
	bus.add_signal_receiver(temp_read_val_cb, signal_name="temperature")
	ess_msrmt_chrc[0].ReadValue({}, reply_handler=temp_read_val_cb,
                                    error_handler=generic_error_cb,
                                    dbus_interface=GATT_CHRC_IFACE)

def read_led():
	custom_led_chrc[0].ReadValue({}, reply_handler=led_read_val_cb,
                                    error_handler=generic_error_cb,
                                    dbus_interface=GATT_CHRC_IFACE)
	
def write_led(value):
	print(value)
	data_hex = "01"
	data_bytes = bytes.fromhex(data_hex)
	custom_led_chrc[0].WriteValue(data_bytes, dbus.Array(signature='y'))

def process_chrc(chrc_path):
    chrc = bus.get_object(BLUEZ_SERVICE_NAME, chrc_path)
    chrc_props = chrc.GetAll(GATT_CHRC_IFACE,
                             dbus_interface=DBUS_PROP_IFACE)

    uuid = chrc_props['UUID']

    if uuid == TEMPERATURE_UUID:
        global ess_msrmt_chrc
        ess_msrmt_chrc = (chrc, chrc_props)
    elif uuid == CUS_LED_UUID:
        global custom_led_chrc
        custom_led_chrc = (chrc, chrc_props)
    else:
        print('Unrecognized characteristic: ' + uuid)

    return True

def process_ble_service(service_path, chrc_paths):
	service = bus.get_object(BLUEZ_SERVICE_NAME, service_path)
	service_props = service.GetAll(GATT_SERVICE_IFACE,
									dbus_interface=DBUS_PROP_IFACE)

	uuid = service_props['UUID']

	# Process the characteristics.
	for chrc_path in chrc_paths:
		process_chrc(chrc_path)

	global ble_service
	ble_service = (service, service_props, service_path)

	return True

def get_characteristic_path():
	global bus
	bus = dbus.SystemBus()
	chrcs = []
	om = dbus.Interface(bus.get_object("org.bluez", "/"),
			"org.freedesktop.DBus.ObjectManager")
	objects = om.GetManagedObjects()

	# List characteristics found
	for path, interfaces in objects.items():
		if GATT_CHRC_IFACE not in interfaces.keys():
			continue
		chrcs.append(path)

		# List sevices found
	for path, interfaces in objects.items():
		if GATT_SERVICE_IFACE not in interfaces.keys():
			continue

		chrc_paths = [d for d in chrcs if d.startswith(path + "/")]
		process_ble_service(path, chrc_paths)

def disconnect():
	bus = pydbus.SystemBus()
	for address in device_address:
		device_path = f"/org/bluez/hci0/dev_{address.replace(':', '_')}"

		# Get the device object
		device = bus.get('org.bluez', device_path)

		# Disconnect from the device
		device.Disconnect()
		device_address.remove(address)
		print(address + " disconnected")
	return

def connect():
	bus = pydbus.SystemBus()
	for address in device_address:
		device_path = f"/org/bluez/hci0/dev_{address.replace(':', '_')}"

		# Get the device object
		device = bus.get('org.bluez', device_path)

		# Connect to the device
		device.Connect()
		print(address + " connected")

		get_characteristic_path()

def print_normal(address, properties):
	print("[ " + address + " ]")

	for key in properties.keys():
		value = properties[key]
		if type(value) is dbus.String:
			value = str(value).encode('ascii', 'replace')
		if (key == "Class"):
			print("    %s = 0x%06x" % (key, value))
		else:
			print("    %s = %s" % (key, value))
	print()
	properties["Logged"] = True

def skip_dev(old_dev, new_dev):
	if not "Logged" in old_dev:
		return False
	if "Name" in old_dev:
		return True
	if not "Name" in new_dev:
		return True
	return False

def properties_changed(interface, changed, invalidated, path):
	if interface != "org.bluez.Device1":
		return

	if path in devices:
		dev = devices[path]

		devices[path] = dict(list(devices[path].items()) + list(changed.items()))
	else:
		devices[path] = changed

	if "Address" in devices[path]:
		address = devices[path]["Address"]
		if address not in device_address:
			device_address.append(address)
			print_normal(address, devices[path])
			connect()
	
	else:
		address = "<unknown>"

def discovery(action):
	bus = dbus.SystemBus()
	bus.add_signal_receiver(properties_changed,
			dbus_interface = "org.freedesktop.DBus.Properties",
			signal_name = "PropertiesChanged",
			arg0 = "org.bluez.Device1",
			path_keyword = "path")
	adapter = bluezutils.find_adapter()

	if action == 1:
		scan_filter = {'UUIDs': [ESS_SERVICE_UUID]} # ESS service UUID
		adapter.SetDiscoveryFilter(scan_filter)
		
		adapter.StartDiscovery()
		print("Start Discovery")
	elif action == 0:
		print("Stop Discovery")
		adapter.StopDiscovery()