/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2014 Accton Technology Corporation.
 *
 * Licensed under the Eclipse Public License, Version 1.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *        http://www.eclipse.org/legal/epl-v10.html
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the
 * License.
 *
 * </bsn.cl>
 ************************************************************
 *
 *
 *
 ***********************************************************/
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"
#include "x86_64_accton_dcs6500_48z8c_int.h"
#include "x86_64_accton_dcs6500_48z8c_log.h"
#include <onlplib/i2c.h>

#define NUM_OF_FAN_ON_MAIN_BROAD      8

#define NUM_OF_CPLD                      3
#define CPLD_VER_MAX_STR_LEN             20
#define FAN_DUTY_CYCLE_MAX         (100)
#define FAN_DUTY_CYCLE_DEFAULT     (50)
#define FAN_DUTY_PLUS_FOR_DIR      (13)
/* Note, all chassis fans share 1 single duty setting.
 * Here use fan 1 to represent global fan duty value.*/
#define FAN_ID_FOR_SET_FAN_DUTY    (1)
#define CELSIUS_RECORD_NUMBER      (2)  /*Must >= 2*/


const char*
onlp_sysi_platform_get(void)
{
    return "x86-64-accton-dcs6500-48z8c-r0";
}

int
onlp_sysi_onie_data_get(uint8_t** data, int* size)
{
    const int len = 256;
    uint8_t* rdata = aim_zmalloc(len);
    int  ret = ONLP_STATUS_OK;

    ret = onlp_file_open(O_RDONLY, 0, IDPROM_PATH);
    if (ret >= 0) {
        close(ret);
        if(onlp_file_read(rdata, len, size, IDPROM_PATH) == ONLP_STATUS_OK) {
            if(*size == len) {
                *data = rdata;
                return ONLP_STATUS_OK;
            }
        }
    }

    aim_free(rdata);
    *size = 0;
    return ONLP_STATUS_E_INTERNAL;
}

void onlp_sysi_onie_data_free(uint8_t* data)
{
    if (data)
        aim_free(data);
}

int onlp_sysi_onie_info_get(onlp_onie_info_t* onie)
{
    int ret = ONLP_STATUS_OK;

    ret = onlp_onie_decode_file(onie, IDPROM_PATH);

    onie->_hdr_id_string = aim_fstrdup("TlvInfo");
    onie->_hdr_version = 0x1;
    return ret;
}

int
onlp_sysi_oids_get(onlp_oid_t* table, int max)
{
    int i;
    onlp_oid_t* e = table;
    memset(table, 0, max*sizeof(onlp_oid_t));

    /* 4 Thermal sensors on the chassis */
    for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++) {
        *e++ = ONLP_THERMAL_ID_CREATE(i);
    }

    /* 3 LEDs on the chassis */
    for (i = 1; i <= CHASSIS_LED_COUNT; i++) {
        *e++ = ONLP_LED_ID_CREATE(i);
    }

    /* 2 PSUs on the chassis */
    for (i = 1; i <= CHASSIS_PSU_COUNT; i++) {
        *e++ = ONLP_PSU_ID_CREATE(i);
    }

    /* 8 Fans on the chassis */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++) {
        *e++ = ONLP_FAN_ID_CREATE(i);
    }

    return 0;
}

static char* cpld_ver_path[NUM_OF_CPLD] = {
    "/sys/class/fpga_class/fpga/version", /* FPGA */
    "/sys/bus/i2c/devices/157-0062/version", /* CPLD-1 */
    "/sys/bus/i2c/devices/158-0064/version"  /* CPLD-2 */
};

int
onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{

    int i = 0;
    int len = 0;
    char *string = NULL;
    char ver[NUM_OF_CPLD][CPLD_VER_MAX_STR_LEN]={{0}};

    for (i = 0; i < NUM_OF_CPLD; i++) {
        len = onlp_file_read_str(&string, cpld_ver_path[i]);
        if (string && len) {
            strncpy(ver[i], string, len);
            aim_free(string);
        } else {
            return ONLP_STATUS_E_INTERNAL;
        }
    }

    pi->cpld_versions = aim_fstrdup("FPGA:%s CPLD-1:%s CPLD-2:%s",
                                    ver[0], ver[1], ver[2]);

    return 0;
}

void
onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
    aim_free(pi->cpld_versions);
}

/* Thermal policy
 * Both B2F and F2B direction use the same policy
 *1.	(Thermal sensor_LM75_49 + Thermal sensor_LM75_CPU) /2 =< 39C    , Keep 37.5%(0x05) Fan speed
 *2.	(Thermal sensor_LM75_49 + Thermal sensor_LM75_CPU) /2 > 39C    , Change Fan speed from 37.5%(0x05) to 75%(0x0B)
 *3.	(Thermal sensor_LM75_49 + Thermal sensor_LM75_CPU) /2 > 45C    , Change Fan speed from 75%(0x0B) to 100%(0x0F)
 *4.	(Thermal sensor_LM75_49 + Thermal sensor_LM75_CPU) /2 > 61C     , Send alarm message
 *5.	(Thermal sensor_LM75_49 + Thermal sensor_LM75_CPU) /2 > 66C     , Shut down system
 *6.	One Fan fail      , Change Fan speed to 100%(0x0F)
 */


typedef struct fan_ctrl_policy {
   int duty_cycle;
   int temp_down; /* The boundary temperature to down adjust fan speed */
   int temp_up;   /* The boundary temperature to up adjust fan speed */
   int state;
} fan_ctrl_policy_t;

enum
{
   LEVEL_FAN_DEF=0,
   LEVEL_FAN_MID,
   LEVEL_FAN_MAX,
   LEVEL_TEMP_HIGH,
   LEVEL_TEMP_CRITICAL
};

fan_ctrl_policy_t  fan_thermal_policy[] = {
    {30,   0,      45000, LEVEL_FAN_DEF},
    {70,   45000,  55000, LEVEL_FAN_MID},
    {100,  55000,  65000, LEVEL_FAN_MAX},
    {100,  65000,  70000, LEVEL_TEMP_HIGH},
    {100,  70000,  75000, LEVEL_TEMP_CRITICAL},
};

#define FAN_SPEED_CTRL_PATH "/sys/bus/i2c/devices/157-0062/fan_duty_cycle_percentage"

#if 0
static int fan_state=LEVEL_FAN_DEF;
static int fan_fail = 0;
static int alarm_state = 0; /* 0->default or clear, 1-->alarm detect */
#endif

int onlp_sysi_platform_manage_fans(void)
    {
    #if 0
    // Thermal policy is executed by muxi_fan_monitor.py instead

    int i=0, ori_state=LEVEL_FAN_DEF, current_state=LEVEL_FAN_DEF;
    int  fd, len;
    int cur_duty_cycle, new_duty_cycle, temp=0;
    onlp_thermal_info_t thermali[3];
    char  buf[10] = {0};

    int fan_wdt_status = 0;
    char *fan_wdt_status_path = FAN_NODE(fan_wdt_status);
    char *fan_wdt_clear_path = FAN_NODE(fan_wdt_clear);
    static int failed_cnt = 0;
    static int failed_first_log = 0;

    /*  Check fan wdt state, if wdt disable, enable wdt */
    if (onlp_file_read_int(&fan_wdt_status, fan_wdt_status_path) < 0) {
        failed_cnt++;
    }
    else
    {
        if (fan_wdt_status == FAN_WDT_DISABLE)
        {
            AIM_SYSLOG_WARN("Fan-WDT Disable", "Fan-WDT Disable","Alarm for Fan-WDT Disable is detected");
            /* Enable WDT */
            if (onlp_file_write_integer(fan_wdt_status_path, FAN_WDT_ENABLE) < 0) {
                failed_cnt++;
            }
        }
        else
        {
            /* clear */
            if (onlp_file_write_integer(fan_wdt_clear_path, FAN_WDT_CLEAR) < 0) {
                failed_cnt++;
            }
        }
    }
    /* To ensure generate log message for first failed */
    if((failed_first_log == 0) && (failed_cnt > 0))
    {
        failed_first_log = 1;
        AIM_LOG_ERROR("Unable to read status from file (%s) or (%s)\r\n", fan_wdt_status_path, fan_wdt_clear_path);
    }
    /* Print the error log if the total falied counts equal or over 360 counts.
       It can decrese number of log message.
       polling every 10 sec. Polling 360 counts for one hour if it is failed to access fan cpld watchdog.
    */
    if(failed_cnt >= 360)
    {
        failed_cnt = 0;
        AIM_LOG_ERROR("Unable to read status from file (%s) or (%s)\r\n", fan_wdt_status_path, fan_wdt_clear_path);
    }


    /* Get current temperature */
    for (i=0; i<3; i++)
    {
        if (onlp_thermali_info_get(ONLP_THERMAL_ID_CREATE(i+2), &thermali[i]) != ONLP_STATUS_OK  )
        {
            AIM_LOG_ERROR("Unable to read thermal status, set fans to 87 %% speed");
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), fan_thermal_policy[LEVEL_FAN_MAX].duty_cycle);
            return ONLP_STATUS_E_INTERNAL;
        }
        temp+=thermali[i].mcelsius;
    }
    /* average of temp */
    temp = temp/3;

    /* Get current fan's duty_cycle(pwm percent)
    */
    fd = open(FAN_SPEED_CTRL_PATH, O_RDONLY);
    if (fd == -1){
        AIM_LOG_ERROR("Unable to open fan speed control node (%s)", FAN_SPEED_CTRL_PATH);
        return ONLP_STATUS_E_INTERNAL;
    }
    len = read(fd, buf, sizeof(buf));
    close(fd);
    if (len <= 0) {
        AIM_LOG_ERROR("Unable to read fan speed from (%s)", FAN_SPEED_CTRL_PATH);
        return ONLP_STATUS_E_INTERNAL;
    }
    cur_duty_cycle = atoi(buf);
    ori_state=fan_state;

    /* Get theraml_policy state and new pwm percent by temperature. */
    for(i=0; i < sizeof(fan_thermal_policy)/sizeof(fan_ctrl_policy_t); i++)
    {
        if (fan_thermal_policy[i].temp_down < temp &&
            temp <= fan_thermal_policy[i].temp_up)
        {
            current_state = i;
        }
    }

    /* if temperatures is higher than the highest tempoture in fan_thermal_policy[] */
    if (temp > fan_thermal_policy[sizeof(fan_thermal_policy)/sizeof(fan_ctrl_policy_t)-1].temp_up)
    {
        current_state = sizeof(fan_thermal_policy)/sizeof(fan_ctrl_policy_t)-1;
    }

    if(current_state > LEVEL_TEMP_CRITICAL || current_state < LEVEL_FAN_DEF)
    {
        AIM_LOG_ERROR("onlp_sysi_platform_manage_fans get error current_state=(%d)\n", current_state);
        return 0;
    }
    /* Decision 3: Decide new fan pwm percent by temperature.
    */
    if (fan_fail == 0 && cur_duty_cycle != fan_thermal_policy[current_state].duty_cycle)
    {
        new_duty_cycle = fan_thermal_policy[current_state].duty_cycle;
        AIM_LOG_MSG("Current_state=(%d), average of fan temperatures=(%d), set duty_cycle=(%d)\r\n", current_state, temp, new_duty_cycle);
        onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), new_duty_cycle);
    }
    /* Get each fan status
    */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++)
    {
        onlp_fan_info_t fan_info;
        if (onlp_fani_info_get(ONLP_FAN_ID_CREATE(i), &fan_info) != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to get fan(%d) status, try to set the other fans as full speed\r\n", i);
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_CYCLE_MAX);
            fan_fail=1;
            break;
        }
        else
        {
            fan_fail = 0;
        }
        /* Decision 1: Set fan as full speed if any fan is failed. */
        if (fan_info.status & ONLP_FAN_STATUS_FAILED || !(fan_info.status & ONLP_FAN_STATUS_PRESENT))
        {
            AIM_LOG_ERROR("Fan(%d) is not working, set the other fans as full speed!\r\n", i);
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_CYCLE_MAX);
            fan_fail=1;
            break;
        }
        else 
        {
            fan_fail = 0;
        }
    }

    /* Get each fan status
     */
    for (i = 1; i <= CHASSIS_FAN_COUNT; i++)
    {
        onlp_fan_info_t fan_info;

        if (onlp_fani_info_get(ONLP_FAN_ID_CREATE(i), &fan_info) != ONLP_STATUS_OK) {
            AIM_LOG_ERROR("Unable to get fan(%d) status, try to set the other fans as full speed\r\n", i);
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_CYCLE_MAX);
            if (fan_state < LEVEL_FAN_MAX)
            {
                fan_state=LEVEL_FAN_MAX;
                current_state=fan_state;
            }
            if(current_state <LEVEL_FAN_MAX )
                current_state=LEVEL_FAN_MAX;
            break;
        }
        /* Decision 1: Set fan as full speed if any fan is failed.
         */
        if (fan_info.status & ONLP_FAN_STATUS_FAILED || !(fan_info.status & ONLP_FAN_STATUS_PRESENT)) {
            AIM_LOG_ERROR("Fan(%d) is not working, set the other fans as full speed\r\n", i);
            onlp_fani_percentage_set(ONLP_FAN_ID_CREATE(1), FAN_DUTY_CYCLE_MAX);
            if (fan_state < LEVEL_FAN_MAX)
            {
                fan_state=LEVEL_FAN_MAX;
                current_state=fan_state;
            }
            if(current_state <LEVEL_FAN_MAX )
                current_state=LEVEL_FAN_MAX;
            break;
        }
    }

    if(current_state != ori_state)
    {
        fan_state=current_state;

        if(current_state == LEVEL_TEMP_CRITICAL)
        {
            AIM_SYSLOG_CRIT("Temperature critical", "Temperature critical ", "Alarm for temperature critical is detected, reboot DUT");
            system("sync;sync;sync");
            system("reboot");
        }

        switch (ori_state)
        {
            case LEVEL_FAN_DEF:
            case LEVEL_FAN_MID:
            case LEVEL_FAN_MAX:
                if(current_state == LEVEL_TEMP_HIGH)
                {
                    if(alarm_state==0)
                    {
                        AIM_SYSLOG_WARN("Temperature high", "Temperature high","Alarm for temperature high is detected");
                        alarm_state=1;
                    }
                }
                break;
            default:
                AIM_SYSLOG_WARN("onlp_sysi_platform_manage_fans abnormal state", "onlp_sysi_platform_manage_fans  abnormal state", "onlp_sysi_platform_manage_fans at abnormal state\n");
                break;
        }
    }

    if(alarm_state == 1 && current_state < LEVEL_TEMP_HIGH)
    {
       if (temp < (fan_thermal_policy[3].temp_down - 5000)) /*below 65 C, clear alarm*/
       {
           AIM_SYSLOG_INFO("Temperature high is clean", "Temperature high is clear", "Alarm for temperature high is cleared");
           alarm_state=0;
       }
    }
    #endif

    return 0;
}

int
onlp_sysi_platform_manage_leds(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}
