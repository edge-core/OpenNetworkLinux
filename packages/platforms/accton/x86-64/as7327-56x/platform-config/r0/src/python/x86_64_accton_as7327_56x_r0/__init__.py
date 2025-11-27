from onl.platform.base import *
from onl.platform.accton import *

import os.path
import time

class OnlPlatform_x86_64_accton_as7327_56x_r0(OnlPlatformAccton,
                                              OnlPlatformPortConfig_48x25_8x100):

    PLATFORM='x86-64-accton-as7327-56x-r0'
    MODEL="AS7327-56X"
    SYS_OBJECT_ID=".7327.56"

    def add_path(self, bin_path):
        path = os.environ['PATH']
        path_exist = False
        for sub_path in path.split(":"):
            if bin_path == sub_path:
                path_exist = True

        if(not path_exist):
            new_path = bin_path + ":" + path
            os.environ['PATH'] = new_path

    def baseconfig(self):
        self.insmod('optoe')
        self.insmod('at24_as7327_56x')
        for m in [ 'fpga', 'cpld', 'psu', 'leds' ]:
            self.insmod("x86-64-accton-as7327-56x-%s.ko" % m)

        self.new_i2c_device('pca9548', 0x70, 0)

        self.new_i2c_devices([
            # inititate LM75
            ('lm75', 0x4a, 7),
            ('lm75', 0x4b, 7),
            ('lm75', 0x4c, 7),
        ])

        self.new_i2c_devices([
            # initialize CPLD
            ('as7327_56x_cpld1', 0x62, 157),
            ('as7327_56x_cpld2', 0x64, 158),
        ])

        self.new_i2c_devices([
            # initiate PSU-1
            ('as7327_56x_psu1', 0x5a, 1),

            # initiate PSU-2
            ('as7327_56x_psu2', 0x59, 2),
        ])

        sfp_map = [
            101,102,103,104,
            105,106,107,108,
            109,110,111,112,
            113,114,115,116,
            117,118,119,120,
            121,122,123,124,
            125,126,127,128,
            129,130,131,132,
            133,134,135,136,
            137,138,139,140,
            141,142,143,144,
            145,146,147,148,
            149,150,151,152,
            153,154,155,156
        ]

        for i in range(0, len(sfp_map)):
            if i < 48: # initialize SFP+ port 1~48
                self.new_i2c_device('optoe2', 0x50, sfp_map[i])
            else: # initialize QSFP port 49~56
                self.new_i2c_device('optoe1', 0x50, sfp_map[i])

            subprocess.call('echo port%d > /sys/bus/i2c/devices/%d-0050/port_name' % (i+1, sfp_map[i]), shell=True)

        self.new_i2c_device('accton_24c64', 0x50, 0)
        self.new_i2c_device('accton_24c64', 0x51, 6)
        self.new_i2c_device('accton_24c64', 0x55, 6)

        bin_path = "/lib/platform-config/current/onl/bin"
        self.add_path(bin_path)
        sbin_path = "/sbin"
        self.add_path(sbin_path)
        print("PATH={}".format(os.environ['PATH']))

        # Wait for ready of drivers intialization
        time.sleep(5)
        # Thermal policy executes overhere
        os.system("sudo /usr/bin/python -u {}/fan_monitor.py &".format(bin_path))
        os.system("sudo /usr/bin/python -u {}/monitor_led.py &".format(bin_path))

        return True
