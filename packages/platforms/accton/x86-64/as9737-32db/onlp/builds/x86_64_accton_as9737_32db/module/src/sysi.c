/************************************************************
 * <bsn.cl fy=2014 v=onl>
 *
 *           Copyright 2014 Big Switch Networks, Inc.
 *           Copyright 2017 Accton Technology Corporation.
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

#include <onlplib/file.h>
#include <onlp/platformi/sysi.h>
#include <onlp/platformi/ledi.h>
#include <onlp/platformi/thermali.h>
#include <onlp/platformi/fani.h>
#include <onlp/platformi/psui.h>
#include "platform_lib.h"

#include "x86_64_accton_as9737_32db_int.h"
#include "x86_64_accton_as9737_32db_log.h"

#define NUM_OF_CPLD_VER 5

const char* onlp_sysi_platform_get(void)
{
	return "x86-64-accton-as9737-32db-r0";
}

int onlp_sysi_onie_data_get(uint8_t** data, int* size)
{
	uint8_t* rdata = aim_zmalloc(256);
	if(onlp_file_read(rdata, 256, size, IDPROM_PATH) == ONLP_STATUS_OK) {
		if(*size == 256) {
			*data = rdata;
			return ONLP_STATUS_OK;
		}
	}

	aim_free(rdata);
	*size = 0;
	return ONLP_STATUS_E_INTERNAL;
}

int onlp_sysi_oids_get(onlp_oid_t* table, int max)
{
	int i;
	onlp_oid_t* e = table;
	memset(table, 0, max*sizeof(onlp_oid_t));

	/* 5 Thermal sensors on the chassis */
	for (i = 1; i <= CHASSIS_THERMAL_COUNT; i++)
		*e++ = ONLP_THERMAL_ID_CREATE(i);

	/* 5 LEDs on the chassis */
	for (i = 1; i <= CHASSIS_LED_COUNT; i++)
		*e++ = ONLP_LED_ID_CREATE(i);

	/* 2 PSUs on the chassis */
	for (i = 1; i <= CHASSIS_PSU_COUNT; i++)
		*e++ = ONLP_PSU_ID_CREATE(i);

	/* 6 Fans on the chassis */
	for (i = 1; i <= CHASSIS_FAN_COUNT; i++)
		*e++ = ONLP_FAN_ID_CREATE(i);

	return 0;
}

static char* cpld_ver_path[NUM_OF_CPLD_VER] = {
	"/sys/devices/platform/as9737_32db_sys/cpu_ec_version",
	"/sys/devices/platform/as9737_32db_sys/fpga_version",
	"/sys/bus/i2c/devices/35-0061/version", /* CPLD-2 */
	"/sys/bus/i2c/devices/36-0062/version", /* CPLD-3 */
	"/sys/devices/platform/as9737_32db_fan/hwmon/hwmon*/version" /* Fan CPLD */
};

int onlp_sysi_platform_info_get(onlp_platform_info_t* pi)
{
	int i, len, ret = ONLP_STATUS_OK;
	char *v[NUM_OF_CPLD_VER] = {NULL};
	onlp_onie_info_t onie;
	char *bios_ver = NULL;
	char *bmc_buf = NULL;
	char *aux_buf = NULL;
	int bmc_major = 0, bmc_minor = 0;
	unsigned int bmc_aux[4] = {0};
	char bmc_ver[16] = "";
	const char *bios = "";
	const char *onie_ver = "";

	for (i = 0; i < AIM_ARRAYSIZE(cpld_ver_path); i++) {
		if (i == 4) {
			int hwmon_idx = onlp_get_fan_hwmon_idx();

			if (hwmon_idx < 0) {
				ret = ONLP_STATUS_E_INTERNAL;
				break;
			}

			len = onlp_file_read_str(&v[i], FAN_SYSFS_FORMAT_1, hwmon_idx, "version");
		} else {
			len = onlp_file_read_str(&v[i], cpld_ver_path[i]);
		}

		if (v[i] == NULL || len <= 0) {
			ret = ONLP_STATUS_E_INTERNAL;
			break;
		}
	}

	if (ret == ONLP_STATUS_OK) {
	pi->cpld_versions = aim_fstrdup("\r\n\t   CPU EC: %s"
					"\r\n\t   FPGA(0x60): %s"
					"\r\n\t   SMB CPLD(0x61): %s"
					"\r\n\t   SMB CPLD(0x62): %s"
					"\r\n\t   Fan CPLD(0x66): %s\r\n",
					v[0], v[1], v[2], v[3], v[4]);
	}

	for (i = 0; i < AIM_ARRAYSIZE(v); i++)
		AIM_FREE_IF_PTR(v[i]);

	if ((onlp_file_read_str(&bmc_buf, BMC_VER1_PATH) >= 0) &&
		(onlp_file_read_str(&aux_buf, BMC_VER2_PATH) >= 0))
	{
		bmc_buf[strcspn(bmc_buf, "\n")] = '\0';
		aux_buf[strcspn(aux_buf, "\n")] = '\0';

		/*
		* NOTE: The value in /sys/devices/platform/ipmi_bmc.0/firmware_revision is formatted
		* using "%u.%x" in the kernel driver (see ipmi_msghandler.c::firmware_revision_show).
		* The second field (after the dot) is output in hexadecimal format and must be parsed
		* using "%x" from user-space.
		*/
		if (sscanf(bmc_buf, "%u.%x", &bmc_major, &bmc_minor) == 2 &&
		    sscanf(aux_buf, "0x%x 0x%x 0x%x 0x%x", &bmc_aux[0], &bmc_aux[1], &bmc_aux[2], &bmc_aux[3]) == 4)
		{
			snprintf(bmc_ver, sizeof(bmc_ver), "%02X.%02X.%02X",
			bmc_major, bmc_minor, bmc_aux[3]);
		}
	}

	if (onlp_file_read_str(&bios_ver, BIOS_VER_PATH) > 0) {
		bios = bios_ver;
	}

	if (onlp_onie_decode_file(&onie, IDPROM_PATH) >= 0) {
		onie_ver = onie.onie_version;
	}

	pi->other_versions = aim_fstrdup("\r\n\t   BIOS: %s\r\n\t   ONIE: %s\r\n\t   BMC: %s",
					bios, onie_ver, bmc_ver);

	AIM_FREE_IF_PTR(bmc_buf);
	AIM_FREE_IF_PTR(aux_buf);
	AIM_FREE_IF_PTR(bios_ver);
	onlp_onie_info_free(&onie);

	return ret;
}

void onlp_sysi_platform_info_free(onlp_platform_info_t* pi)
{
	aim_free(pi->cpld_versions);
}
