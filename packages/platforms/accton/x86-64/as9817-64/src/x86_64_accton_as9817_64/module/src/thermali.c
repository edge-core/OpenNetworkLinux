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
 * Thermal Sensor Platform Implementation.
 *
 ***********************************************************/
#include <onlplib/file.h>
#include <onlp/platformi/thermali.h>
#include "platform_lib.h"
#include <dirent.h>
#include <regex.h>

#define CPU_CORETEMP_DIR_PATH "/sys/devices/platform/coretemp.0/hwmon"
#define MAX_ENTRIES 128  /* Maximum number of temp*_input files */
#define MAX_NAME_LEN 256 /* Maximum file name length */

#define VALIDATE(_id)                           \
    do {                                        \
        if(!ONLP_OID_IS_THERMAL(_id)) {         \
            return ONLP_STATUS_E_INVALID;       \
        }                                       \
    } while(0)

static char* devfiles__[] = { /* must map with onlp_thermal_id */
    NULL,
    NULL,                  /* CPU_CORE files */
    "/sys/devices/platform/as9817_64_thermal*temp1_input",
    "/sys/devices/platform/as9817_64_thermal*temp2_input",
    "/sys/devices/platform/as9817_64_thermal*temp3_input",
    "/sys/devices/platform/as9817_64_thermal*temp4_input",
    "/sys/devices/platform/as9817_64_thermal*temp5_input",
    "/sys/devices/platform/as9817_64_thermal*temp6_input",
    "/sys/devices/platform/as9817_64_thermal*temp7_input",
    "/sys/devices/platform/as9817_64_thermal*temp8_input",
    "/sys/devices/platform/as9817_64_psu*psu1_temp1_input",
    "/sys/devices/platform/as9817_64_psu*psu1_temp2_input",
    "/sys/devices/platform/as9817_64_psu*psu1_temp3_input",
    "/sys/devices/platform/as9817_64_psu*psu2_temp1_input",
    "/sys/devices/platform/as9817_64_psu*psu2_temp2_input",
    "/sys/devices/platform/as9817_64_psu*psu2_temp3_input"
};

typedef struct {
    char name[MAX_NAME_LEN];
    int index;
} TempEntry;


TempEntry temp_entries[MAX_ENTRIES];
/* Global regex variables */
regex_t dir_regex, file_regex;
int sysfs_count = 0;
/* Function to safely concatenate paths and handle overflow */
int safe_snprintf(char *buffer, size_t buffer_size, const char *path1, const char *path2) {
    size_t needed_size = strlen(path1) + strlen(path2) + 2; /* 1 for '/', 1 for '\0' */
    if (needed_size > buffer_size) {
        AIM_LOG_ERROR("Path too long: %s/%s\n", path1, path2);
        return -1;
    }
    snprintf(buffer, buffer_size, "%s/%s", path1, path2);
    return 0;
}

/* Static values */
static onlp_thermal_info_t tinfo[] = {
    { }, /* Not used */
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_CPU_CORE), "CPU Core", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 90000, 100000, 100000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_MAIN_BROAD), "MB_RearCenter_temp(0x48)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 70900, 80900, 85900 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_MAIN_BROAD), "MB_RearRight_temp(0x49)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 58600, 68600, 73600 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_MAIN_BROAD), "MB_RearCenter_temp(0x4A)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 74800, 84800, 89800 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_4_ON_MAIN_BROAD), "MB_RearLeft_temp(0x4B)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 68400, 78400, 83400 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_5_ON_MAIN_BROAD), "MB_FrontLeft_temp(0x4C)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 73000, 83000, 88000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_6_ON_MAIN_BROAD), "MB_FrontRight_temp(0x4D)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 59100, 69100, 74100 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_7_ON_MAIN_BROAD), "FB_temp(0x4D)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 60800, 70800, 75800 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_8_ON_MAIN_BROAD), "FB_temp(0x4E)", 0, {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 60900, 70900, 75900 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU1), "PSU-1 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU1_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 70000, 75000, 75000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU1), "PSU-1 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU1_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 125000, 130000, 130000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU1), "PSU-1 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU1_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 110000, 115000, 115000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_1_ON_PSU2), "PSU-2 Thermal Sensor 1", ONLP_PSU_ID_CREATE(PSU2_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 70000, 75000, 75000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_2_ON_PSU2), "PSU-2 Thermal Sensor 2", ONLP_PSU_ID_CREATE(PSU2_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 125000, 130000, 130000 }
    },
    {   { ONLP_THERMAL_ID_CREATE(THERMAL_3_ON_PSU2), "PSU-2 Thermal Sensor 3", ONLP_PSU_ID_CREATE(PSU2_ID), {0} },
        ONLP_THERMAL_STATUS_PRESENT,
        ONLP_THERMAL_CAPS_ALL, 0, { 110000, 115000, 115000 }
    }
};

int initialize_regex() {
    if (regcomp(&dir_regex, "^hwmon[0-9]+$", REG_EXTENDED) != 0) {
        AIM_LOG_ERROR("Failed to compile directory regex\n");
        return -1;
    }
    if (regcomp(&file_regex, "^temp([0-9]+)_input$", REG_EXTENDED) != 0) {
        AIM_LOG_ERROR("Failed to compile file regex\n");
        regfree(&dir_regex);
        return -1;
    }
    return 0;
}

void cleanup_regex() {
    regfree(&dir_regex);
    regfree(&file_regex);
}

/* Function to scan hwmon directories for temp*_input files */
void scan_hwmon_files(const char *parent_path) {
    DIR *parent_dir = opendir(parent_path);
    if (!parent_dir) {
        AIM_LOG_ERROR("Failed to open parent directory");
        return;
    }

    size_t count = 0;

    struct dirent *entry;
    while ((entry = readdir(parent_dir)) != NULL) {
        if (regexec(&dir_regex, entry->d_name, 0, NULL, 0) != 0) {
            continue;
        }

        char subdir_path[MAX_NAME_LEN];
        if (safe_snprintf(subdir_path, sizeof(subdir_path), parent_path, entry->d_name) < 0) {
            continue;
        }

        DIR *subdir = opendir(subdir_path);
        if (!subdir) {
            AIM_LOG_ERROR("Failed to open subdirectory");
            continue;
        }

        struct dirent *sub_entry;
        while ((sub_entry = readdir(subdir)) != NULL) {
            regmatch_t pmatch[2];
            if (regexec(&file_regex, sub_entry->d_name, 2, pmatch, 0) == 0) {
                if (count >= MAX_ENTRIES) {
                    AIM_LOG_ERROR("Maximum entries reached. Ignoring additional files.\n");
                    break;
                }

                char full_path[MAX_NAME_LEN];
                if (safe_snprintf(full_path, sizeof(full_path), subdir_path, sub_entry->d_name) < 0) {
                    continue;
                }

                int index = atoi(&sub_entry->d_name[pmatch[1].rm_so]);
                strncpy(temp_entries[count].name, full_path, MAX_NAME_LEN - 1);
                temp_entries[count].name[MAX_NAME_LEN - 1] = '\0';
                temp_entries[count].index = index;
                count++;
            }
        }
        closedir(subdir);
    }

    closedir(parent_dir);

    sysfs_count = count;
}

/*
 * This will be called to intiialize the thermali subsystem.
 */
int
onlp_thermali_init(void)
{
    /* Initialize regex patterns */
    if (initialize_regex() != 0) {
        return ONLP_STATUS_E_INTERNAL;
    }
    scan_hwmon_files(CPU_CORETEMP_DIR_PATH);
    /* Clean up regex patterns */
    cleanup_regex();

    return ONLP_STATUS_OK;
}

/*
 * Retrieve the information structure for the given thermal OID.
 *
 * If the OID is invalid, return ONLP_E_STATUS_INVALID.
 * If an unexpected error occurs, return ONLP_E_STATUS_INTERNAL.
 * Otherwise, return ONLP_STATUS_OK with the OID's information.
 *
 * Note -- it is expected that you fill out the information
 * structure even if the sensor described by the OID is not present.
 */
int
onlp_thermali_info_get(onlp_oid_t id, onlp_thermal_info_t* info)
{
    int tid;
    int psu_id, psu_tid_start = 0;
    int val = 0;
    VALIDATE(id);
    int coretemp_max = 0, coretemp_temp = 0;

    tid = ONLP_OID_ID_GET(id);
    *info = tinfo[tid];

    if (tid == THERMAL_CPU_CORE) {
        for (size_t i = 0; i < sysfs_count; i++) {
            if (onlp_file_read_int(&coretemp_temp, temp_entries[i].name) < 0) {
                AIM_LOG_ERROR("Unable to read status from file (%s)\r\n", temp_entries[i].name);
                return ONLP_STATUS_E_INTERNAL;
            }
            if (coretemp_temp > coretemp_max)
                coretemp_max = coretemp_temp;
        }

        info->mcelsius = coretemp_max;

        return ONLP_STATUS_OK;
    }

    psu_tid_start = CHASSIS_THERMAL_COUNT + 1;

    if (tid >= psu_tid_start) {
        psu_id = ( tid < (psu_tid_start + NUM_OF_THERMAL_PER_PSU) ) ? PSU1_ID : PSU2_ID;
        /* Get power good status */
        onlp_file_read_int(&val, PSU_SYSFS_FORMAT, psu_id, "power_good");
        if(val != PSU_STATUS_POWER_GOOD) {
            info->status |= ONLP_THERMAL_STATUS_FAILED;
        }
    }

    return onlp_file_read_int(&info->mcelsius, devfiles__[tid]);
}
