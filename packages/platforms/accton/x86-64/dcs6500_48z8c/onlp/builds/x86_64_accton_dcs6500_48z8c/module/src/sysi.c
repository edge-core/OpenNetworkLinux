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

int onlp_sysi_platform_manage_fans(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}

int
onlp_sysi_platform_manage_leds(void)
{
    return ONLP_STATUS_E_UNSUPPORTED;
}
