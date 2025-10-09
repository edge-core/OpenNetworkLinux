#!/usr/bin/env python

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

# LED_CTRL_PATH = '/sys/class/leds/dx510_fan/brightness'

# LED_OFF     = 0
# LED_RED     = 10
# LED_GREEN   = 16

###############################################################################

FUNCTION_NAME = 'fan-control'
PRODUCT_NAME  = 'SYSTEM'

THERMAL_CONFIG_FILE = 'thermal_sensor_pid.config'
FAN_CONFIG_FILE = 'fan_sysfs.config'

SLEEP_TIME = 10 # Loop interval in second.
RETRY_COUNT_MAX = 3
TEMP_HISTORY_MAX = RETRY_COUNT_MAX

TEST_MODE = False
DEBUG_MODE = False
SHOW_MODE = False

NONE_VALUE = 'N/A'

###############################################################################
# Thermal sensor.
THERMAL_NUM_MAX = 0

THERMAL_INIT_VAL = None
THERMAL_INVALID_VAL = -10000

THERMAL_VAL_MIN = -40
THERMAL_VAL_MAX = 125
THERMAL_BURST_MAX = 30
THERMAL_HYSTERESIS = 3

THERMAL_FATAL_MAX = 2

THERMAL_CONFIG = {}
###############################################################################
# Fan sensor.
FAN_NUM_MAX = 4

FAN_PWM_MAX = 100
FAN_PWM_MIN = 30
FAN_PWM_DEFAULT = 50

FAN_RPM_LOW = 2500 # 10%
FAN_RPM_OFFSET_MAX = 10

FAN_ABNORMAL_MAX = 2

FAN_SYS_PATH_PREFIX = '/sys/cpld'

FAN_CONFIG = {}

FAN_NOT_PRESENT = 0

###############################################################################
# Alarm.
ALARM_STATE = {}

# Thermal alarm.
THERMAL_ALARM_INACCESS  = 'INACCESS'
THERMAL_ALARM_INVALID   = 'INVALID'
THERMAL_ALARM_BURST     = 'BURST'
THERMAL_ALARM_FATAL     = 'FATAL'
THERMAL_ALARM_MAJOR     = 'MAJOR'

THERMAL_ALARM = [THERMAL_ALARM_INACCESS, THERMAL_ALARM_INVALID, THERMAL_ALARM_BURST, THERMAL_ALARM_FATAL, THERMAL_ALARM_MAJOR]

# Fan alarm.
FAN_ALARM_ABSENT    = 'ABSENT'
FAN_ALARM_LOW       = 'LOW'
FAN_ALARM_OFFSET    = 'OFFSET'

FAN_ALARM = [FAN_ALARM_ABSENT, FAN_ALARM_LOW, FAN_ALARM_OFFSET]

###############################################################################
# LOG.
BMC_STATUS = False
DRIVER_INIT_DONE_FLAG = "/run/sonic_init_driver_done.flag"
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
    if TEST_MODE:
        time_str = time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime())
        print('%s %s: %-12s %s' %(time_str, FUNCTION_NAME, priority_name[level].upper(), msg))
    else:
        syslog.syslog(level, msg)

def DBG_LOG(msg):
    if DEBUG_MODE:
        level = syslog.LOG_DEBUG
        x = PRODUCT_NAME + ' ' + priority_name[level].upper() + ' : ' + msg
        SYS_LOG(level, x)

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

def init_thermal_config(file_name):
    # Exist ?
    if not os.path.isfile(file_name):
        SYS_LOG_CRITICAL('Not find the thermal config file "%s", exiting...' %(file_name))
        return -1, None

    # Open the file.
    try:
        file = open(file_name, 'r')
    except ImportError as e:
        SYS_LOG_CRITICAL('Can not read the thermal config file "%s", exiting...' %(file_name))
        return -1, None

    # Read the file data.
    config_data = {}
    line_num = 0
    for line in file.readlines():
        line = line.strip()
        #SYS_LOG_INFO('config_data line: %s' %(line))

        # Empty?
        if len(line) == 0:
            continue
        # Comment?
        if line[0] == '#':
            continue

        # Config.
        line_val_list = line.split()
        line_val = {}
        # name
        name                        = line_val_list[0]
        line_val[ 'name' ] = name
        # sysfs node ?
        sysfs                       = (line_val_list[1] != '0')
        line_val[ 'sysfs' ] = sysfs
        # set-point
        line_val[ 'setpoint' ]      = int(line_val_list[2])
        # MAJOR
        line_val[ 'MAJOR' ]         = int(line_val_list[3])
        # FATAL
        line_val[ 'FATAL' ]         = int(line_val_list[4])
        # Kp
        line_val[ 'kp' ]            = float(line_val_list[5])
        # Ki
        line_val[ 'ki' ]            = float(line_val_list[6])
        # Kd
        line_val[ 'kd' ]            = float(line_val_list[7])
        # sensor path, or command to get the sensor value.
        if sysfs:
            sensor_path             = line_val_list[8]
            line_val['sensor_path'] = sensor_path
            # if TEST_MODE:
            #     line_val['sensor_path'] = './test' + sensor_path
            SYS_LOG_INFO('thermal sensor "%s" sysfs path = "%s".' %(name, line_val[ 'sensor_path' ]))
        else:
            if 0: #TEST_MODE
                line_val['command'] = 'echo 50.0'
            else:
                cmd = line.partition('"')[2]
                line_val['command'] = cmd.strip('"')
            SYS_LOG_INFO('thermal sensor "%s" command = "%s".' %(name, line_val[ 'command' ]))

        # last PWM
        line_val[ 'last_pwm' ]  = FAN_PWM_DEFAULT
        # Temperature history.
        line_val[ 'temp' ] = []
        line_val[ 'history' ] = []
        # Temperature change-point
        line_val[ 'change_point' ] = None

        config_data[line_num] = line_val
        line_num += 1

    file.close()
    return 0, config_data



def init_fan_config(file_name):
    # Exist ?
    if not os.path.isfile(file_name):
        SYS_LOG_CRITICAL('Not find the fan config file "%s", exiting...' %(file_name))
        return -1, None

    # Open the file.
    try:
        file = open(file_name, 'r')
    except ImportError as e:
        SYS_LOG_CRITICAL('Can not read the fan config file "%s", exiting...' %(file_name))
        return -1, None


    fan_config = {}
    prefix = ''
    # if TEST_MODE:
    #     prefix = './test' + prefix


    # Read the file data.
    config_data = {}
    line_num = 0
    for line in file.readlines():
        line = line.strip()
        #SYS_LOG_INFO('config_data line: %s' %(line))

        # Empty?
        if len(line) == 0:
            continue
        # Comment?
        if line[0] == '#':
            continue

        # Config.
        line_val_list = line.split()
        line_val = {}
        # name
        line_val[ 'name' ]                          =               line_val_list[0]
        # sysfs node
        line_val[ 'sysfs_path' ]                    =  prefix     + line_val_list[1]
        sysfs_path = line_val[ 'sysfs_path' ]
        # present
        line_val[ 'sysfs_path_present' ]            =  sysfs_path + line_val_list[2] if line_val_list[2] != NONE_VALUE else None
        # status
        line_val[ 'sysfs_status' ]                  =  sysfs_path + line_val_list[3]
        # pwm
        line_val[ 'sysfs_path_pwm_0' ]              =  sysfs_path + line_val_list[4]
        line_val[ 'sysfs_path_pwm_1' ]              = (sysfs_path + line_val_list[5]) if line_val_list[5] != NONE_VALUE else None
        # speed
        line_val[ 'sysfs_path_speed_0' ]            =  sysfs_path + line_val_list[6]
        line_val[ 'sysfs_path_speed_1' ]            =  sysfs_path + line_val_list[7]
        # speed target
        line_val[ 'sysfs_path_speed_target_0' ]     =  sysfs_path + line_val_list[8]
        line_val[ 'sysfs_path_speed_target_1' ]     =  sysfs_path + line_val_list[9]
        # speed tolerance
        line_val[ 'sysfs_path_speed_tolerance_0' ]  =  sysfs_path + line_val_list[10]
        line_val[ 'sysfs_path_speed_tolerance_1' ]  =  sysfs_path + line_val_list[11]

        line_val[ 'present' ]                   = True
        line_val[ 'rpm_0' ]                     = 0
        line_val[ 'rpm_1' ]                     = 0

        config_data[line_num] = line_val
        line_num += 1

    config_data['present_count'] = 0
    config_data['abnormal_count'] = 0
    config_data['abnormal_list'] = []

    file.close()
    return 0, config_data



def init_data():
    global THERMAL_NUM_MAX
    global THERMAL_CONFIG, FAN_CONFIG

    prefix = '/lib/platform-config/current/onl/bin/'
    if TEST_MODE:
        prefix = './'

    # Init thermal config.
    thermal_config_file = prefix + THERMAL_CONFIG_FILE
    ret, THERMAL_CONFIG = init_thermal_config(thermal_config_file)
    if ret != 0:
        return ret
    THERMAL_NUM_MAX = len(THERMAL_CONFIG)
    SYS_LOG_INFO('THERMAL_CONFIG:')
    for index in range(THERMAL_NUM_MAX):
        SYS_LOG_INFO('%s' %(THERMAL_CONFIG[index]))

    fan_config_file = prefix + FAN_CONFIG_FILE
    ret, FAN_CONFIG = init_fan_config(fan_config_file)
    if ret != 0:
        return ret
    SYS_LOG_INFO('FAN_CONFIG:')
    SYS_LOG_INFO('%s' %(FAN_CONFIG))

    return 0

def main(argv):
    # Init
    ret = init_data()
    if ret != 0:
        SYS_LOG_CRITICAL('Fail to init program data, %d, exiting...' %(ret))
        exit(ret)

    while True:
        SYS_LOG_INFO("Thermal policy is executing.............")
        time.sleep(SLEEP_TIME)


if __name__ == '__main__':
    main(sys.argv)


