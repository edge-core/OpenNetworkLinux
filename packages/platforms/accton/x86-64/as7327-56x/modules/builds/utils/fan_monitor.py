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
FAN_NUM_MAX = 2

FAN_PWM_MAX = 100
FAN_PWM_MIN = 30
FAN_PWM_DEFAULT = 50

FAN_RPM_LOW = 2500 # 10%
FAN_RPM_OFFSET_MAX = 10

FAN_ABNORMAL_MAX = 2

FAN_SPEED_CTRL_PATH = "/sys/bus/i2c/devices/157-0062/fan_duty_cycle_percentage"
FAN_WDT_CONFIG_PATH = "/sys/bus/i2c/devices/157-0062/fan_wdt_count"
FAN_WDT_CLEAR_PATH = "/sys/bus/i2c/devices/157-0062/fan_wdt_clear"

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

def check_debug_flag():
    path_prefix = '/run/'
    if TEST_MODE:
        path_prefix = './test/'
    return os.path.isfile(path_prefix + "fan-control.debug")

def check_show_flag():
    path_prefix = '/run/'
    if TEST_MODE:
        path_prefix = './test/'
    return os.path.isfile(path_prefix + "fan-control.show")

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

        line_val[ 'present' ]                   = True

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


def raising_alarm(sensor, alarm, value = None):
    sensor_alarm = {}
    if ALARM_STATE.get(sensor) == None:
        ALARM_STATE[sensor] = { alarm : { 'raise' : 0, 'clear' : 0, 'value' : value }}
    else:
        if ALARM_STATE[sensor].get(alarm) == None:
            ALARM_STATE[sensor][alarm] = { 'raise' : 0, 'clear': 0, 'value' : value }
    ALARM_STATE[sensor][alarm]['raise'] += 1
    ALARM_STATE[sensor][alarm]['value']  = value
    ALARM_STATE[sensor][alarm]['clear']  = 0

    # clear other alarm.
    alarm_raise_count = ALARM_STATE[sensor][alarm]['raise']
    if alarm_raise_count >= RETRY_COUNT_MAX:
        for every_alarm in ALARM_STATE[sensor].keys():
            if every_alarm != alarm:
                ALARM_STATE[sensor].pop(every_alarm)

    return alarm_raise_count


def clearing_alarm(sensor, alarm, value = None):
    if ALARM_STATE.get(sensor) == None:
        return 0
    if ALARM_STATE[sensor].get(alarm) == None:
        return 0
    if ALARM_STATE[sensor][alarm]['raise'] <= 0:
        return 0

    ALARM_STATE[sensor][alarm]['clear'] += 1
    ALARM_STATE[sensor][alarm]['value']  = None
    if ALARM_STATE[sensor][alarm]['raise'] < RETRY_COUNT_MAX:
        ALARM_STATE[sensor][alarm]['raise'] = 0
    else:
        if ALARM_STATE[sensor][alarm]['clear'] >= RETRY_COUNT_MAX:
            ALARM_STATE[sensor][alarm]['raise'] = 0
    return ALARM_STATE[sensor][alarm]['clear']


def get_alarm_count(sensor, alarm):
    if ALARM_STATE.get(sensor) == None:
        return 0
    if ALARM_STATE[sensor].get(alarm) == None:
        return 0
    return ALARM_STATE[sensor][alarm]['raise']

def get_sensor_alarm(sensor):
    alarm_list = []
    if ALARM_STATE.get(sensor) != None:
        for alarm in ALARM_STATE[sensor].keys():
            if ALARM_STATE[sensor][alarm]['raise'] >= RETRY_COUNT_MAX:
                alarm_list.append(alarm)
    return alarm_list

def check_thermal_sensor_alarm(index, temp, alarm, raising, RETRY_LIMIT = RETRY_COUNT_MAX, PWM_PROPOSAL = FAN_PWM_MAX):
    pwm = None
    old_pwm = FAN_PWM_MIN

    sensor = THERMAL_CONFIG[index]
    sensor_name = sensor['name']
    # Old PWM.
    last_pwm = sensor['last_pwm']

    if raising:
        count = raising_alarm(sensor_name, alarm, temp)
        if count > 0 and count <= RETRY_LIMIT:
            SYS_LOG_INFO('sensor "%s" is going to raise "%s"(%s), retrying count %d' %(sensor_name, alarm, str(temp), count))
            # Need more retry, so not to change PWM.
            pwm = last_pwm
        if count >= RETRY_LIMIT:
            if count == RETRY_LIMIT:
                '''
                # Customer log specification.
                if alarm == THERMAL_ALARM_FATAL:
                    SYS_LOG_CRITICAL('TempRisingCriticalAlarm Report:The temperature of [%s] exceeded the critical limit.(currentValue(%s), thresholdValue(%s))' \
                            %(sensor_name, str(temp), str(sensor[alarm])))
                elif alarm == THERMAL_ALARM_MAJOR:
                    SYS_LOG_WARN('TempRisingWarningAlarm Report:The temperature of [%s] exceeded the upper limit.(currentValue(%s), thresholdValue(%s))'
                            %(sensor_name, str(temp), str(sensor[alarm])))
                '''
                SYS_LOG_WARN('sensor "%s" raise alarm "%s"' %(sensor_name, alarm))
                if PWM_PROPOSAL == FAN_PWM_MAX:
                    SYS_LOG_CRITICAL('propose to set MAX PWM')
            pwm = PWM_PROPOSAL
    else:
        count = clearing_alarm(sensor_name, alarm, temp)
        if count > 0 and count <= RETRY_LIMIT:
            pwm = last_pwm
            SYS_LOG_INFO('sensor "%s" is going to clear "%s"(%s), retrying count %d' %(sensor_name, alarm, str(temp), count))
        if count >= RETRY_LIMIT:
            if count == RETRY_LIMIT:
                '''
                # Customer log specification.
                if alarm == THERMAL_ALARM_FATAL:
                    SYS_LOG_WARN('TempRisingCriticalAlarm Cancel:The temperature of [%s] exceeded the critical limit.(currentValue(%s), thresholdValue(%s))' \
                            %(sensor_name, str(temp), str(sensor[alarm])))
                elif alarm == THERMAL_ALARM_MAJOR:
                    SYS_LOG_WARN('TempRisingWarningAlarm Cancel:The temperature of [%s] exceeded the upper limit.(currentValue(%s), thresholdValue(%s))'
                            %(sensor_name, str(temp), str(sensor[alarm])))
                '''
                SYS_LOG_WARN('sensor "%s" clear alarm "%s"(%s)' %(sensor_name, alarm, str(temp)))
            pwm = None
    return pwm


def check_thermal_sensor_value(index, temp):
    sensor = THERMAL_CONFIG[index]
    sensor_name = sensor['name']

    pwm = None
    pwm_all = []

    # INACCESS ?
    alarm = THERMAL_ALARM_INACCESS
    raise_alarm = (temp == None)
    if raise_alarm and sensor_name == 'SFP':
        # SFP may be inaccessible when system is booting up.
        return FAN_PWM_MIN
    pwm = check_thermal_sensor_alarm(index, temp, alarm, raise_alarm)
    if raise_alarm:
        DBG_LOG('sensor "%s" hit alarm "%s", value is "%s", propose PWM "%s"' %(sensor_name, alarm, str(temp), str(pwm)))
        return pwm
    if pwm != None:
        pwm_all.append(pwm)

    # INVALID ?
    alarm = THERMAL_ALARM_INVALID
    raise_alarm = (temp < THERMAL_VAL_MIN or temp > THERMAL_VAL_MAX)
    pwm = check_thermal_sensor_alarm(index, temp, alarm, raise_alarm)
    if raise_alarm:
        DBG_LOG('sensor "%s" hit alarm "%s", value is "%s", propose PWM "%s"' %(sensor_name, alarm, str(temp), str(pwm)))
        return pwm
    if pwm != None:
        pwm_all.append(pwm)

    # BURST ?
    alarm = THERMAL_ALARM_BURST
    history = sensor['temp']
    #DBG_LOG('sensor "%s" history value %s' %(sensor_name, history))
    if len(history) >= TEMP_HISTORY_MAX:
        old_temp_max = max(history)
        old_temp_min = min(history)
        raise_alarm = (old_temp_max > old_temp_min + THERMAL_BURST_MAX)
        pwm = check_thermal_sensor_alarm(index, temp, alarm, raise_alarm)
        if raise_alarm:
            DBG_LOG('sensor "%s" hit alarm "%s", value is "%s", propose PWM "%s"' %(sensor_name, alarm, str(temp), str(pwm)))
            return pwm
        if pwm != None:
            pwm_all.append(pwm)

    # FATAL
    alarm_fatal = THERMAL_ALARM_FATAL
    raise_alarm = (temp >= sensor[alarm_fatal])
    check_thermal_sensor_alarm(index, temp, alarm_fatal, raise_alarm, PWM_PROPOSAL = None)
    if raise_alarm:
        DBG_LOG('sensor "%s" hit alarm "%s", value is "%s"' %(sensor_name, alarm_fatal, str(temp)))
    else:
        # MAJOR
        alarm_major = THERMAL_ALARM_MAJOR
        raise_alarm = (temp >= sensor[alarm_major] and temp < sensor[alarm_fatal])
        check_thermal_sensor_alarm(index, temp, alarm_major, raise_alarm, PWM_PROPOSAL = None)
        if raise_alarm:
            DBG_LOG('sensor "%s" hit alarm "%s", value is "%s"' %(sensor_name, alarm_major, str(temp)))

    # Return the MAX PWM proposal.
    if len(pwm_all) > 0:
        pwm = max(pwm_all)
    else:
        pwm = None

    return pwm


def check_thermal_sensor_fatal():
    fatal_thermal = []
    alarm = 'FATAL'
    for index in range(THERMAL_NUM_MAX):
        thermal = THERMAL_CONFIG[index]
        thermal_name = thermal['name']
        count = get_alarm_count(thermal_name, alarm)
        if count >= RETRY_COUNT_MAX:
            fatal_thermal.append(thermal_name)
            DBG_LOG('sensor "%s" hit alarm "%s" count %d' %(thermal_name, alarm, count))
    if len(fatal_thermal) >= THERMAL_FATAL_MAX:
        while True:
            SYS_LOG_CRITICAL('resetting board since thermal sensor %s hit "%s" alarm!' %(fatal_thermal, alarm))
            if TEST_MODE:
                break
            time.sleep(3)
            # reset board...
            ret, output = getstatusoutput('sudo reboot')
            SYS_LOG_CRITICAL('%d, "%s"' %(ret, output))
            time.sleep(300)


def get_thermal_sensor_value(index):
    temp = None
    name = THERMAL_CONFIG[index]['name']

    # Get the thermal sensor.
    cmd = 'timeout 3s '
    if THERMAL_CONFIG[index]['sysfs']:
        sys_path = THERMAL_CONFIG[index]['sensor_path']
        cmd = cmd + 'cat %s' %(sys_path)
    else:
        cmd = cmd + THERMAL_CONFIG[index]['command']

    ret, output = getstatusoutput(cmd)
    DBG_LOG('read sensor "%s" output: "%s".' %(name, output))
    temp = None
    if ret != 0 or len(output) == 0:
        DBG_LOG('fail to read sensor "%s".' %(name))
    else:
        try:
            temp = int(float(output))
        except:
            temp = 0

        temp /= 1000

        # History, but may be invalid.
        THERMAL_CONFIG[index][ 'temp' ].insert(0, temp)
        if len(THERMAL_CONFIG[index][ 'temp' ]) > TEMP_HISTORY_MAX:
            old_temp = THERMAL_CONFIG[index][ 'temp' ].pop()
        DBG_LOG('sensor "%s" history value = %s' %(name, THERMAL_CONFIG[index][ 'temp' ]))
    return temp


CUSTOMER_FAN_ALARM_LOG = {
    FAN_ALARM_ABSENT :      ['0x10002001', 'Report:The fan module was pluged in/out.(Status=0 0-out 1-in.)',
                                           'Cancel:The fan module was pluged in/out.(Status=1 0-out 1-in.)'],
    FAN_ALARM_LOW    :      ['0x10002002', 'Report:The fan module failed.(ErrCode=1, Reason=The Fan module status abnormal.)',
                                           'Cancel:The fan module failed.(ErrCode=0, Reason=The Fan module status abnormal.)'],
    FAN_ALARM_OFFSET :      ['0x10002002', 'Report:The fan module failed.(ErrCode=2, Reason=The Fan module status abnormal.)',
                                           'Cancel:The fan module failed.(ErrCode=0, Reason=The Fan module status abnormal.)'],
    'FAN_ABNORMAL_LIST' :   ['Report:The fan module failed(Name={}, Reason= Two or more fans are faulty.)',
                                           'Cancel:The fan module failed(Name={}, Reason= Two or more fans are faulty.)' ],
}

def check_fan_sensor_alarm(index, alarm, raising):
    sensor = FAN_CONFIG[index]
    sensor_name = sensor['name']

    if raising:
        count = raising_alarm(sensor_name, alarm)
        if count <= RETRY_COUNT_MAX:
            SYS_LOG_INFO('sensor "%s" is going to raise "%s", retrying count %d' %(sensor_name, alarm, count))
        if count >= RETRY_COUNT_MAX:
            if count == RETRY_COUNT_MAX:
                SYS_LOG_WARN('sensor "%s" raise alarm "%s"' %(sensor_name, alarm))

                '''
                # Customer log specification.
                SYS_LOG_WARN('%s|%s %s' %(CUSTOMER_FAN_ALARM_LOG[alarm][0], sensor_name, CUSTOMER_FAN_ALARM_LOG[alarm][1]))
                '''
    else:
        count = clearing_alarm(sensor_name, alarm)
        if count > 0 and count <= RETRY_COUNT_MAX:
            SYS_LOG_INFO('sensor "%s" is going to clear "%s", retrying count %d' %(sensor_name, alarm, count))
        if count >= RETRY_COUNT_MAX:
            if count == RETRY_COUNT_MAX:
                SYS_LOG_WARN('sensor "%s" clear alarm "%s"' %(sensor_name, alarm))

                '''
                # Customer log specification.
                SYS_LOG_WARN('%s|%s %s' %(CUSTOMER_FAN_ALARM_LOG[alarm][0], sensor_name, CUSTOMER_FAN_ALARM_LOG[alarm][2]))
                '''
    raised = (get_alarm_count(sensor_name, alarm) >= RETRY_COUNT_MAX)
    return raised


def check_fan_state(index, alarm , raising):
    abnormal = False
    if raising:
        if check_fan_sensor_alarm(index, alarm, True):
            # alarm raised.
            abnormal = True
    else:
        abnormal = check_fan_sensor_alarm(index, alarm, False)
    return abnormal


def check_all_fan_state():
    abnormal_fan = []
    for index in range(FAN_NUM_MAX):
        name = FAN_CONFIG[index]['name']
        present = FAN_CONFIG[index]['present']

        raise_alarm = False
        abnormal = False
        # ABSENT
        if not raise_alarm:
            alarm = FAN_ALARM_ABSENT
            raise_alarm = not present
            if check_fan_state(index, alarm, raise_alarm):
                abnormal = True

        if abnormal:
            abnormal_fan.append(name)

    return abnormal_fan


def get_fan_alarm_count(alarm):
    total_count = 0
    for index in range(FAN_NUM_MAX):
        name = FAN_CONFIG[index]['name']
        if get_alarm_count(name, alarm) >= RETRY_COUNT_MAX:
            total_count += 1
    return total_count


def get_fan_pwm():
    val = None
    cmd = "cat %s" %(FAN_SPEED_CTRL_PATH)
    ret, output = getstatusoutput(cmd)
    if (ret != 0) or (len(output) == 0):
        val = 0
    else:
        val = int(output)
    return val


def set_fan_pwm(pwm):
    cmd = 'echo %d > %s' %(pwm, FAN_SPEED_CTRL_PATH)
    #SYS_LOG_INFO('setting %s PWM to %d by command "%s".' %(name, pwm, cmd))
    ret, output = getstatusoutput(cmd)
    if ret != 0:
        SYS_LOG_WARN('fail to set PWM to %d, %s.' %(pwm, output))
    return ret


def check_fan_pwm(pwm_expected):
    match = True
    pwm = get_fan_pwm()
    if pwm != pwm_expected:
        match = False
    return match


def get_fan_present(index):
    present = True
    fan = FAN_CONFIG[index]

    val = None
    cmd = "cat %s" %(fan['sysfs_path_present'])
    ret, output = getstatusoutput(cmd)
    if (ret != 0) or (len(output) == 0):
        val = 0
    else:
        val = int(output)

    if val == 0:
        present = False

    if present == False:
        # Not present.
        fan['present'] = False
        DBG_LOG('FAN%d is absent' %(index + 1))

    return present


def check_fan_sensor():
    global FAN_CONFIG
    pwm_proposal = FAN_PWM_MIN
    present_count = 0

    for index in range(FAN_NUM_MAX):
        fan = FAN_CONFIG[index]
        # present ?
        present = get_fan_present(index)
        if present == False:
            # Not present.
            fan['present'] = False
            DBG_LOG('FAN%d is absent' %(index + 1))
            continue
        # present.
        fan['present'] = True
        present_count += 1

    FAN_CONFIG['present_count'] = present_count
    DBG_LOG('FAN present count = [%d]' %(present_count))

    abnormal_fan_list = check_all_fan_state()
    abnormal_count = len(abnormal_fan_list)
    DBG_LOG('FAN abnormal count = [%d]' %(abnormal_count))


    FAN_CONFIG['abnormal_list']  = abnormal_fan_list
    FAN_CONFIG['abnormal_count'] = abnormal_count

    return pwm_proposal


def calc_pid_pwm(thermal_index, temp):
    global THERMAL_CONFIG
    sensor = THERMAL_CONFIG[thermal_index]
    name = sensor['name']

    Kp          = sensor['kp']
    Ki          = sensor['ki']
    Kd          = sensor['kd']
    setpoint    = sensor['setpoint']
    last_pwm    = sensor['last_pwm']
    change_point_temp = sensor['change_point']

    # Record real valid temperature history.
    THERMAL_CONFIG[thermal_index][ 'history' ].insert(0, temp)
    if len(THERMAL_CONFIG[thermal_index][ 'history' ]) > TEMP_HISTORY_MAX:
        old_temp = THERMAL_CONFIG[thermal_index][ 'history' ].pop()
    DBG_LOG('sensor "%s" valid history value = %s' %(name, THERMAL_CONFIG[thermal_index][ 'history' ]))

    last_temp1  = temp
    if len(sensor['history']) > 1:
        last_temp1  = sensor['history'][1]
    last_temp2  = temp
    if len(sensor['history']) > 2:
        last_temp2  = sensor['history'][2]

    P = Kp * (temp - last_temp1)
    I = Ki * (temp - setpoint)
    D = Kd * (temp + last_temp2 - 2 * last_temp1)
    pwm = int(last_pwm + P + I + D)

    if temp < setpoint:
        # Force the thermal sensor not to affect PWM as long as its temperature is below "setpoint".
        pwm = FAN_PWM_MIN

    # Hysteresis operation.
    hyst_action = 'hyst'
    if change_point_temp == None:
        # init state.
        change_point_temp = temp + THERMAL_HYSTERESIS + 1
    if (pwm <= last_pwm) and ((change_point_temp - THERMAL_HYSTERESIS) <= temp <= change_point_temp):
        # Hyst state
        DBG_LOG('%s: pwm = [%d] last_pwm = [%d] change_point = [%d] P = [%d] I = [%d] D = [%d]' \
                %(hyst_action, pwm, last_pwm, change_point_temp, P, I, D))
        # Not change PWM under Hyst state.
        pwm = last_pwm
    else:
        # Non-Hyst state
        hyst_action = 'calc'
        THERMAL_CONFIG[thermal_index]['change_point'] = temp
        DBG_LOG('%s: pwm = [%d] last_pwm = [%d] change_point = [%d] P = [%d] I = [%d] D = [%d]' \
                %(hyst_action, pwm, last_pwm, change_point_temp, P, I, D))

    pwm = min(pwm, FAN_PWM_MAX)
    pwm = max(pwm, FAN_PWM_MIN)
    THERMAL_CONFIG[thermal_index]['last_pwm'] = pwm
    return pwm


def set_all_fan_pwm(pwm, sensor):
    if check_fan_pwm(pwm):
        # No need to set the same PWM again.
        return
    SYS_LOG_INFO('setting ALL FAN PWM to %d, thermal sensor %s.' %(pwm, sensor))
    set_fan_pwm(pwm)


def show_raised_alarm():
    if not SHOW_MODE:
        return

    SYS_LOG_INFO( "----------------------------------------------------------------------" )
    SYS_LOG_INFO("")
    SYS_LOG_INFO( "%-16s %-16s %-24s %-12s" %('Raised alarm:', 'MODULE', 'ALARM', 'VALUE') )

    print_sensor = None
    for sensor in ALARM_STATE.keys():
        for alarm in ALARM_STATE[sensor].keys():
            raised_count = ALARM_STATE[sensor][alarm]['raise']
            if raised_count < RETRY_COUNT_MAX:
                continue
            if( print_sensor != sensor ):
                SYS_LOG_INFO("%-16s %-16s %-24s [%s]" %('', sensor, alarm, raised_count))
            else:
                SYS_LOG_INFO("%-16s %-16s %-24s [%s]" %('', '', alarm, raised_count))
            print_sensor = sensor
    SYS_LOG_INFO("")
    SYS_LOG_INFO( "----------------------------------------------------------------------" )


def thermal_pid_fan_policy():
    pwm_sensor = []
    max_thermal_pwm = FAN_PWM_MIN
    for index in range(THERMAL_NUM_MAX):
        # Read the thermal sensor value.
        temp = get_thermal_sensor_value(index)

        # Check thermal sensor value.
        thermal_pwm = check_thermal_sensor_value(index, temp)
        # Check FATAL thermal sensor.

        pwm = FAN_PWM_MIN
        if thermal_pwm != None:
            # No need to calculate the PID value.
            pwm = thermal_pwm
        else:
            # PID
            pwm = calc_pid_pwm(index, temp)

        # Record the sensor with the max PWM.
        if pwm > max_thermal_pwm:
            max_thermal_pwm = pwm
            pwm_sensor = [THERMAL_CONFIG[index]['name'], str(temp)]
        elif pwm == max_thermal_pwm:
            pwm_sensor.append([THERMAL_CONFIG[index]['name'], str(temp)])

    # Reset the board once two(or more) thermal sensors hit alarm 'FATAL'.
    check_thermal_sensor_fatal()

    # Check the FAN sensor
    fan_sensor_pwm = check_fan_sensor()

    # Set FMEA status.
    #set_all_fmea_fan_status()

    # Choose the highest value.
    pwm_proposal = max(max_thermal_pwm, fan_sensor_pwm)
    DBG_LOG('propose NEW PWM = [%d]' %(pwm_proposal))

    pwm_proposal = min(FAN_PWM_MAX, pwm_proposal)
    pwm_proposal = max(FAN_PWM_MIN, pwm_proposal)

    # Set PWM.
    set_all_fan_pwm(pwm_proposal, pwm_sensor)

    # Show all alarms by now.
    show_raised_alarm()

# time_out 0-42 sec
def set_fan_timeout(time_out):
    if(time_out > 42):
        time_out = 42
    timeout_count = int(float(time_out)/0.67) & 0x3f
    cmd = "echo {} > {}".format(timeout_count, FAN_WDT_CONFIG_PATH)
    status, output = getstatusoutput(cmd)
    if status:
        print(output)

def clear_fan_count():
    clear_cmd = "echo 1 > {}".format(FAN_WDT_CLEAR_PATH)
    status, output = getstatusoutput(clear_cmd)
    if status:
        print(output)

def power_off():
    cmd = "sync;sync;sync;i2cset -f -y 157 0x62 0x1f 0x3"
    status, output = getstatusoutput(cmd)
    if status:
        print(output)

def power_down_strategy():
    present_count = 0
    retry_time = 3

    while retry_time:
        present_count = 0
        for index in range(FAN_NUM_MAX):
            present = get_fan_present(index)
            if(present == False):
                present_count += 1
        if present_count >= FAN_NUM_MAX:
            SYS_LOG_INFO("The fans are all pull out, check again, the check count{}".format(3-retry_time))
            retry_time -= 1
        else:
            break
    if retry_time == 0:
        SYS_LOG_INFO("The fans are all pull out, system will power cycle")
        power_off()


def main(argv):
    global TEST_MODE, DEBUG_MODE, SHOW_MODE
    # Check test mode flag.
    if len(argv) > 1 :
        if argv[1] == 'test':
            TEST_MODE = True

    # Wait 60 seconds for system to finish the initialization.
    if not TEST_MODE:
        SYS_LOG_INFO('Waiting for system finish init...')
        time.sleep(60)
        SYS_LOG_INFO('Waiting for system finish init...done.')

    # Init
    ret = init_data()
    if ret != 0:
        SYS_LOG_CRITICAL('Fail to init program data, %d, exiting...' %(ret))
        exit(ret)

    # Main loop.
    set_fan_timeout(15)
    while True:
        # Check debug flag.
        DEBUG_MODE = check_debug_flag()
        SHOW_MODE = check_show_flag()

        thermal_pid_fan_policy()
        clear_fan_count()
        power_down_strategy()
        # Sleep
        DBG_LOG("fan monitor alive")
        time.sleep(SLEEP_TIME)


if __name__ == '__main__':
    main(sys.argv)


