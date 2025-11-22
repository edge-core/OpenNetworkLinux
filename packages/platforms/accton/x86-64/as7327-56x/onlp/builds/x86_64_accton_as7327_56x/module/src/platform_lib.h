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
#ifndef __PLATFORM_LIB_H__
#define __PLATFORM_LIB_H__

#include <onlplib/file.h>
#include "x86_64_accton_as7327_56x_log.h"

#define CHASSIS_FAN_COUNT		8
#define CHASSIS_THERMAL_COUNT	4
#define CHASSIS_PSU_COUNT		2
#define CHASSIS_PSU_THERMAL_COUNT   2
#define CHASSIS_LED_COUNT		4

#define PSU1_ID 1
#define PSU2_ID 2

#define PSU_NODE_MAX_INT_LEN  8
#define PSU_NODE_MAX_PATH_LEN 64

#define PSU1_AC_PMBUS_PREFIX "/sys/bus/i2c/devices/1-005a/"
#define PSU2_AC_PMBUS_PREFIX "/sys/bus/i2c/devices/2-0059/"

#define PSU1_AC_PMBUS_NODE(node) PSU1_AC_PMBUS_PREFIX#node
#define PSU2_AC_PMBUS_NODE(node) PSU2_AC_PMBUS_PREFIX#node

#define CPLD_NODE_PATH	"/sys/bus/i2c/devices/i2c-157/157-0062/"
#define FAN_NODE(node)	CPLD_NODE_PATH#node
#define FAN_WDT_ENABLE       0x1
#define FAN_WDT_DISABLE      0x0
#define FAN_WDT_CLEAR        0x1


#define IDPROM_PATH "/sys/bus/i2c/devices/0-0050/eeprom"

int onlp_file_write_integer(char *filename, int value);
int onlp_file_read_binary(char *filename, char *buffer, int buf_size, int data_len);
int onlp_file_read_string(char *filename, char *buffer, int buf_size, int data_len);


int psu_pmbus_info_get(int id, char *node, int *value);
int psu_pmbus_info_set(int id, char *node, int value);

typedef enum psu_type {
    PSU_TYPE_UNKNOWN,
    PSU_TYPE_AC,
    PSU_TYPE_DC
} psu_type_t;

psu_type_t get_psu_type(int id, char* modelname, int modelname_len);
int psu_pmbus_model_name_get(int id, char *model, int model_len);
int psu_pmbus_serial_number_get(int id, char *serial, int serial_len);

//#define DEBUG_MODE 1

#if (DEBUG_MODE == 1)
    #define DEBUG_PRINT(format, ...)   printf(format, __VA_ARGS__)
#else
    #define DEBUG_PRINT(format, ...)
#endif

#endif  /* __PLATFORM_LIB_H__ */

