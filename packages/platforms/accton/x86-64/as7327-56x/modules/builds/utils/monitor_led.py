#!/usr/bin/env python
#
# Copyright (C) 2018 Accton Technology Corporation
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# ------------------------------------------------------------------
# HISTORY:
#    mm/dd/yyyy (A.D.)
#    7/2/2018:  Jostar create for as7326-56x
# ------------------------------------------------------------------

try:
    import os
    import sys
    import syslog
    import signal
    import time
    import glob
    #import logging
    #import logging.config
except ImportError as e:
    raise ImportError('%s - required module not found' % str(e))


###############################################################################
# debug
FUNCTION_NAME = 'led-control'
PRODUCT_NAME  = 'SYSTEM'

SLEEP_TIME = 1 # Loop interval in second.
FAN_WARN_COUNT_MAX = 15

###############################################################################
# LOG.
# priorities (these are ordered)

LOG_EMERG     = 0       #  system is unusable
LOG_ALERT     = 1       #  action must be taken immediately
LOG_CRIT      = 2       #  critical conditions
LOG_ERR       = 3       #  error conditions
LOG_WARNING   = 4       #  warning conditions
LOG_NOTICE    = 5       #  normal but significant condition
LOG_INFO      = 6       #  informational
LOG_DEBUG     = 7       #  debug-level messages

'''
priority_names = {
    "alert":    LOG_ALERT,
    "crit":     LOG_CRIT,
    "critical": LOG_CRIT,
    "debug":    LOG_DEBUG,
    "emerg":    LOG_EMERG,
    "err":      LOG_ERR,
    "error":    LOG_ERR,        #  DEPRECATED
    "info":     LOG_INFO,
    "notice":   LOG_NOTICE,
    "panic":    LOG_EMERG,      #  DEPRECATED
    "warn":     LOG_WARNING,    #  DEPRECATED
    "warning":  LOG_WARNING,
}
'''
priority_name = {
    LOG_CRIT    : "critical",
    LOG_DEBUG   : "debug",
    LOG_WARNING : "warning",
    LOG_INFO    : "info",
}


def SYS_LOG(level, msg):
    syslog.syslog(level, msg)

def SYS_LOG_INFO(msg):
    level = syslog.LOG_INFO
    x = PRODUCT_NAME + ' ' + priority_name[level].upper() + ' : ' + msg
    SYS_LOG(level, x)

def SYS_LOG_WARN(msg):
    level = syslog.LOG_WARNING
    x = PRODUCT_NAME + ' ' + priority_name[level].upper() + ' : ' + msg
    SYS_LOG(level, x)

def SYS_LOG_CRITICAL(msg):
    level = syslog.LOG_CRIT
    x = PRODUCT_NAME + ' ' + priority_name[level].upper() + ' : ' + msg
    SYS_LOG(level, x)

###############################################################################

def getstatusoutput(cmd):
    if sys.version_info.major == 2:
        # python2
        import commands
        return commands.getstatusoutput( cmd )
    else:
        # python3
        import subprocess
        return subprocess.getstatusoutput( cmd )

###############################################################################

# Define
LED_DARK = 0
LED_GREEN_ON = 1
LED_AMBER_ON = 2
LED_RED_ON = 3
LED_GREEN_BLINK = 5

SYS_LED_PATH = '/sys/class/leds/accton_as7327_56x_led::sys/brightness'
SYS_LED_WDT_CLEAR_PATH = '/sys/bus/i2c/devices/157-0062/sysled_wdt_clear'

PSU1_PRESENCE_PATH = '/sys/bus/i2c/devices/1-005a/psu_present'
PSU2_PRESENCE_PATH = '/sys/bus/i2c/devices/2-0059/psu_present'
PSU1_STATUS_PATH = '/sys/bus/i2c/devices/1-005a/psu_power_good'
PSU2_STATUS_PATH = '/sys/bus/i2c/devices/2-0059/psu_power_good'
PSU_PRESENT = 1
PSU_OUT_OK = 1
FAN_PRESENT = 1

FAN_NUM_MAX = 2
FAN_STATUS_PATH = "/sys/bus/i2c/devices/157-0062/fan_present_{}"

def get_psu_status():
    try:
        psu1_cmd = 'cat '+PSU1_PRESENCE_PATH
        psu2_cmd = 'cat '+PSU2_PRESENCE_PATH
        ret, output = getstatusoutput(psu1_cmd)
        psu1_present = int(output)
        ret, output = getstatusoutput(psu2_cmd)
        psu2_present = int(output)
        if(psu1_present != PSU_PRESENT) or (psu1_present != PSU_PRESENT):
            return -1
        psu1_sta_cmd = 'cat '+PSU1_STATUS_PATH
        psu2_sta_cmd = 'cat '+PSU2_STATUS_PATH
        ret, output = getstatusoutput(psu1_sta_cmd)
        psu1_sta = int(output)
        ret, output = getstatusoutput(psu2_sta_cmd)
        psu2_sta = int(output)
        if(psu1_sta != PSU_OUT_OK) or (psu2_sta != PSU_OUT_OK):
            return -1
    except:
        return -1
    return 0

def get_fan_status():
    try:
        for index in range(1, FAN_NUM_MAX+1):
            cmd = 'cat '+FAN_STATUS_PATH.format(index)
            ret, output = getstatusoutput(cmd)
            if(ret):
                SYS_LOG_WARN(output)
            fan_status = int(output)
            if(fan_status != FAN_PRESENT):
                return -1
    except:
        return -1
    return 0

def get_sys_led():
    cmd = "cat "+SYS_LED_PATH
    ret, output = getstatusoutput(cmd)
    return int(output)

def set_sys_led(led):
    cmd = "echo {:d} > {}".format(led, SYS_LED_PATH)
    ret, output = getstatusoutput(cmd)
    return ret

def clear_sys_led_wdt():
    cmd = "echo 1 > {}".format(SYS_LED_WDT_CLEAR_PATH)
    ret, output = getstatusoutput(cmd)
    return ret

fan_warn_count = 0
def sys_led_ctrl():
    global fan_warn_count
    clear_sys_led_wdt()
    psu_status = get_psu_status()
    fan_status = get_fan_status()
    cur_led = get_sys_led()
    #SYS_LOG_INFO("psu_status = {}".format(psu_status))
    #SYS_LOG_INFO("fan_status = {}".format(fan_status))
    #SYS_LOG_INFO("cur_led = {}".format(cur_led))
    if(fan_status) :
        if (fan_warn_count < FAN_WARN_COUNT_MAX):
            fan_warn_count = fan_warn_count + 1
    else:
        fan_warn_count = 0
    if(psu_status == 0) and (fan_warn_count < FAN_WARN_COUNT_MAX):
        if(cur_led != LED_GREEN_ON):
            set_sys_led(LED_GREEN_ON)
            SYS_LOG_INFO("set sys led green")
    else:
        if(cur_led != LED_RED_ON):
            set_sys_led(LED_RED_ON)
            SYS_LOG_INFO("set sys led red on")
    return 0

def main(argv):
    # Wait 60 seconds for system to finish the initialization.
    SYS_LOG_INFO('Waiting for system finish init...')
    time.sleep(60)
    SYS_LOG_INFO('Waiting for system finish init...done.')
    SYS_LOG_INFO('Starting led monitor')
    # Loop forever, doing something useful hopefully:
    while True:
        sys_led_ctrl()
        time.sleep(SLEEP_TIME)

if __name__ == '__main__':
    main(sys.argv[1:])

