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
#include <onlp/onlp.h>
#include <onlplib/file.h>
#include "platform_lib.h"

enum onlp_fan_dir onlp_get_fan_dir(int fid)
{
    int len = 0;
    int i = 0;
    int hwmon_idx;
    char *str = NULL;
    char *dirs[FAN_DIR_COUNT] = { "F2B", "B2F" };
    char file[32];
    enum onlp_fan_dir dir = FAN_DIR_F2B;

    hwmon_idx = onlp_get_fan_hwmon_idx();
    if (hwmon_idx >= 0) {
        /* Read attribute */
        snprintf(file, sizeof(file), "fan%d_dir", fid);
        len = onlp_file_read_str(&str, FAN_SYSFS_FORMAT_1, hwmon_idx, file);

        /* Verify Fan dir string length */
        if (!str || len < 3) {
            AIM_FREE_IF_PTR(str);
            return dir;
        }

        for (i = 0; i < AIM_ARRAYSIZE(dirs); i++) {
            if (strncmp(str, dirs[i], strlen(dirs[i])) == 0) {
                dir = (enum onlp_fan_dir)i;
                break;
            }
        }

        AIM_FREE_IF_PTR(str);
    }

    return dir;
}

int onlp_get_psu_hwmon_idx(int pid)
{
    /* find hwmon index */
    char* file = NULL;
    char path[64];
    int ret, hwmon_idx, max_hwmon_idx = 20;

    for (hwmon_idx = 0; hwmon_idx <= max_hwmon_idx; hwmon_idx++) {
        snprintf(path, sizeof(path), "/sys/devices/platform/as9817_64_psu/hwmon/hwmon%d/", hwmon_idx);

        if (pid == 1)
            ret = onlp_file_find(path, "psu1_present", &file);
        else if (pid == 2)
            ret = onlp_file_find(path, "psu2_present", &file);
        else
            return -1;
 
        AIM_FREE_IF_PTR(file);

        if (ONLP_STATUS_OK == ret)
            return hwmon_idx;
    }

    return -1;
}

int onlp_get_fan_hwmon_idx(void)
{
    /* find hwmon index */
    char* file = NULL;
    char path[64];
    int ret, hwmon_idx, max_hwmon_idx = 20;

    for (hwmon_idx = 0; hwmon_idx <= max_hwmon_idx; hwmon_idx++) {
        snprintf(path, sizeof(path), "/sys/devices/platform/as9817_64_fan/hwmon/hwmon%d/", hwmon_idx);

        ret = onlp_file_find(path, "name", &file);
        AIM_FREE_IF_PTR(file);

        if (ONLP_STATUS_OK == ret)
            return hwmon_idx;
    }

    return -1;
}

as9817_64_platform_id_t get_platform_id(void)
{
    int ret, pid = 0;

    ret = onlp_file_read_int(&pid, "/sys/devices/platform/as9817_64_fpga/platform_id");
    if (ONLP_STATUS_OK != ret) {
        return PID_UNKNOWN;
    }

    return (pid == 0) ? AS9817_64O : AS9817_64D;
}

/**
 * get_bmc_version - Get BMC version as int array
 * @ver: int[3], ver[0]=major, ver[1]=minor, ver[2]=aux last byte
 *
 * Return: ONLP_STATUS_OK on success, or ONLP error code
 */
int get_bmc_version(int *ver)
{
    char *tmp;
    char primary_fw_ver[32] = {0};
    char aux_fw_raw[64] = {0};
    const char *last_aux = NULL;
    int len;

    if (!ver) {
        return ONLP_STATUS_E_INVALID;
    }
    memset(ver, 0, sizeof(int) * 3);

    len = onlp_file_read_str(&tmp, BMC_VER1_PATH);
    if (tmp && len) {
        memcpy(primary_fw_ver, tmp, len);
        primary_fw_ver[len] = '\0';
    } else {
        return ONLP_STATUS_E_INTERNAL;
    }
    AIM_FREE_IF_PTR(tmp);

    len = onlp_file_read_str(&tmp, BMC_VER2_PATH);
    if (tmp && len) {
        memcpy(aux_fw_raw, tmp, len);
        aux_fw_raw[len] = '\0';
    } else {
        return ONLP_STATUS_E_INTERNAL;
    }
    AIM_FREE_IF_PTR(tmp);

    primary_fw_ver[strcspn(primary_fw_ver, "\n")] = '\0';
    aux_fw_raw[strcspn(aux_fw_raw, "\n")] = '\0';

    /* Parse main version: "0.3" => ver[0]=0, ver[1]=3 */
    if (sscanf(primary_fw_ver, "%d.%d", &ver[0], &ver[1]) != 2) {
        return ONLP_STATUS_E_INTERNAL;
    }

    /* Get last AUX byte (e.g., from "0x00 0x00 0x00 0x02") */
    last_aux = strrchr(aux_fw_raw, ' ');
    last_aux = last_aux ? last_aux + 1 : aux_fw_raw;

    if (sscanf(last_aux, "0x%x", &ver[2]) != 1) {
        return ONLP_STATUS_E_INTERNAL;
    }

    return ONLP_STATUS_OK;
}

/**
 * @brief warm reset for mac
 * @param unit_id The warm reset device unit id, should be 0
 * @param reset_dev The warm reset device id, should be 1 ~ (WARM_RESET_MAX-1)
 * @param ret return value.
 */
int onlp_data_path_reset(uint8_t unit_id, uint8_t reset_dev)
{
    int len = 0;
    int ret = ONLP_STATUS_OK;
    char *magic_num = NULL;
    char *device_id[] = { NULL, "mac" };

    if (unit_id != 0 || reset_dev >= WARM_RESET_MAX)
        return ONLP_STATUS_E_PARAM;

    if (reset_dev == 0)
        return ONLP_STATUS_E_UNSUPPORTED;

    /* Reset device */
    len = onlp_file_read_str(&magic_num, WARM_RESET_FORMAT, device_id[reset_dev]);
    if (magic_num && len) {
        ret = onlp_file_write_str(magic_num, WARM_RESET_FORMAT, device_id[reset_dev]);
        if (ret < 0) {
            AIM_LOG_ERROR("Reset device-%d:(%s) failed.", reset_dev, device_id[reset_dev]);
        }
    }
    else {
        ret = ONLP_STATUS_E_INTERNAL;
    }

    AIM_FREE_IF_PTR(magic_num);
    return ret;
}
