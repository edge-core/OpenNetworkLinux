// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A hwmon driver for the as7926_40xfb_sys
 *
 * Copyright (C) 2019  Edgecore Networks Corporation.
 * Brandon Chuang <brandon_chuang@edge-core.com>
 *
 * Based on:
 *	pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *	pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 *	i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 *	pca9540.c from Jean Delvare <khali@linux-fr.org>.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/platform_device.h>
#include "accton_ipmi_intf.h"

#define DRVNAME "as7926_40xfb_sys"

#define IPMI_SYSEEPROM_READ_CMD 0x18
#define IPMI_READ_MAX_LEN       128
#define IPMI_RESET_CMD			0x65
#define IPMI_RESET_CMD_LENGTH	6
#define IPMI_CPLD_READ_REG_CMD 0x22

#define EEPROM_NAME				"eeprom"
#define EEPROM_SIZE				256	/*      256 byte eeprom */

static int as7926_40xfb_sys_probe(struct platform_device *pdev);
static int as7926_40xfb_sys_remove(struct platform_device *pdev);
static ssize_t get_reset(struct device *dev, struct device_attribute *da,
			char *buf);
static ssize_t set_reset(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count);
static ssize_t show_bios_flash_id(struct device *dev, struct device_attribute *da,
			char *buf);
static ssize_t show_version(struct device *dev, struct device_attribute *da,
			char *buf);

struct as7926_40xfb_sys_data {
	struct platform_device *pdev;
	struct mutex update_lock;
	char valid;		/* != 0 if registers are valid */
	unsigned long last_updated;	/* In jiffies */
	struct ipmi_data ipmi;
	unsigned char ipmi_resp_eeprom[EEPROM_SIZE];
	unsigned char ipmi_resp_cpld[2];
	unsigned char ipmi_tx_data[2];
	unsigned char ipmi_resp_rst[2];
	unsigned char ipmi_tx_data_rst[IPMI_RESET_CMD_LENGTH];
	struct bin_attribute eeprom;	/* eeprom data */
};

struct as7926_40xfb_sys_data *data = NULL;

static struct platform_driver as7926_40xfb_sys_driver = {
	.probe = as7926_40xfb_sys_probe,
	.remove = as7926_40xfb_sys_remove,
	.driver = {
		   .name = DRVNAME,
		   .owner = THIS_MODULE,
		   },
};

/* sysfs attributes */
enum as7926_40xfb_sysfs_attrs {
	RESET_MUX,
	RESET_MAC,
	RESET_JR2,
	RESET_OP2,
	RESET_GEARBOX,
	CPU_CPLD_VER,
	CPLD1_VER,
	BIOS_FLASH_ID
};

#define DECLARE_RESET_SENSOR_DEVICE_ATTR() \
	static SENSOR_DEVICE_ATTR(reset_mac, S_IWUSR | S_IRUGO, \
				get_reset, set_reset, RESET_MAC); \
	static SENSOR_DEVICE_ATTR(reset_jr2, S_IWUSR | S_IRUGO, \
				get_reset, set_reset, RESET_JR2); \
	static SENSOR_DEVICE_ATTR(reset_op2, S_IWUSR | S_IRUGO, \
				get_reset, set_reset, RESET_OP2); \
	static SENSOR_DEVICE_ATTR(reset_gb, S_IWUSR | S_IRUGO, \
				get_reset, set_reset, RESET_GEARBOX); \
	static SENSOR_DEVICE_ATTR(reset_mux, S_IWUSR | S_IRUGO, \
				get_reset, set_reset, RESET_MUX)
#define DECLARE_RESET_ATTR() \
	&sensor_dev_attr_reset_mac.dev_attr.attr, \
	&sensor_dev_attr_reset_jr2.dev_attr.attr, \
	&sensor_dev_attr_reset_op2.dev_attr.attr, \
	&sensor_dev_attr_reset_gb.dev_attr.attr, \
	&sensor_dev_attr_reset_mux.dev_attr.attr

DECLARE_RESET_SENSOR_DEVICE_ATTR();

static SENSOR_DEVICE_ATTR(cpu_cpld_version, S_IRUGO, \
			show_version, NULL, CPU_CPLD_VER);
static SENSOR_DEVICE_ATTR(cpld1_version, S_IRUGO, \
			show_version, NULL, CPLD1_VER);
static SENSOR_DEVICE_ATTR(bios_flash_id, S_IRUGO, \
			show_bios_flash_id, NULL, BIOS_FLASH_ID);

static struct attribute *as7926_40xfb_sys_attributes[] = {
	/* sysfs attributes */
	DECLARE_RESET_ATTR(),
	&sensor_dev_attr_cpu_cpld_version.dev_attr.attr,
	&sensor_dev_attr_cpld1_version.dev_attr.attr,
	&sensor_dev_attr_bios_flash_id.dev_attr.attr,
	NULL
};

static const struct attribute_group as7926_40xfb_sys_group = {
	.attrs = as7926_40xfb_sys_attributes,
};

static ssize_t get_reset(struct device *dev, struct device_attribute *da,
			char *buf)
{
	int status = 0;

	mutex_lock(&data->update_lock);
	status = ipmi_send_message(&data->ipmi, IPMI_RESET_CMD, NULL, 0,
				   data->ipmi_resp_rst, sizeof(data->ipmi_resp_rst));
	if (unlikely(status != 0))
		goto exit;

	if (unlikely(data->ipmi.rx_result != 0)) {
		status = -EIO;
		goto exit;
	}

	mutex_unlock(&data->update_lock);
	return sprintf(buf, "0x%x 0x%x", data->ipmi_resp_rst[0], data->ipmi_resp_rst[1]);

 exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static ssize_t set_reset(struct device *dev, struct device_attribute *da,
		       const char *buf, size_t count)
{
	u32 magic[2];
	int status;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

	if (sscanf(buf, "0x%x 0x%x", &magic[0], &magic[1]) != 2)
		return -EINVAL;

	if (magic[0] > 0xFF || magic[1] > 0xFF)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	/* Send IPMI write command */
	data->ipmi_tx_data_rst[0] = 0;
	data->ipmi_tx_data_rst[1] = 0;
	data->ipmi_tx_data_rst[2] = (attr->index == RESET_MUX) ? 0 : (attr->index);
	data->ipmi_tx_data_rst[3] = (attr->index == RESET_MUX) ? 2 : 1;
	data->ipmi_tx_data_rst[4] = magic[0];
	data->ipmi_tx_data_rst[5] = magic[1];

	status = ipmi_send_message(&data->ipmi, IPMI_RESET_CMD,
				   data->ipmi_tx_data_rst,
				   sizeof(data->ipmi_tx_data_rst), NULL, 0);
	if (unlikely(status != 0))
		goto exit;

	if (unlikely(data->ipmi.rx_result != 0)) {
		status = -EIO;
		goto exit;
	}

	status = count;

 exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static struct as7926_40xfb_sys_data *as7926_40xfb_sys_update_reg(
			unsigned char addr, unsigned char reg)
{
	int status = 0;

	data->valid = 0;

	data->ipmi_tx_data[0] = addr;
	data->ipmi_tx_data[1] = reg;
	status = ipmi_send_message(&data->ipmi, IPMI_CPLD_READ_REG_CMD,
					data->ipmi_tx_data, 2,
					data->ipmi_resp_cpld,
					sizeof(data->ipmi_resp_cpld));
	if (unlikely(status != 0))
		goto exit;

	if (unlikely(data->ipmi.rx_result != 0)) {
		status = -EIO;
		goto exit;
	}

	data->last_updated = jiffies;
	data->valid = 1;

exit:
	return data;
}

static ssize_t show_version(struct device *dev,
				struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	unsigned char addr;
	unsigned char reg = 0x1;
	unsigned char value = 0;
	int error = 0;

	mutex_lock(&data->update_lock);

	if ((attr->index == CPU_CPLD_VER))
		addr = 0x65;
	else if ((attr->index == CPLD1_VER))
		addr = 0x60;

	data = as7926_40xfb_sys_update_reg(addr, reg);

	if (!data->valid) {
		error = -EIO;
		goto exit;
	}

	value = data->ipmi_resp_cpld[0];

	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%d\n", value);

exit:
	mutex_unlock(&data->update_lock);
	return error;
}

static ssize_t show_bios_flash_id(struct device *dev, struct device_attribute *da,
					char *buf)
{
	unsigned char addr = 0x65;
	unsigned char reg = 0x3;
	unsigned char bit_offset = 0x2;
	unsigned char value = 0;
	int error = 0;

	mutex_lock(&data->update_lock);

	data = as7926_40xfb_sys_update_reg(addr, reg);

	if (!data->valid) {
		error = -EIO;
		goto exit;
	}

	value = ((data->ipmi_resp_cpld[0] >> bit_offset) == 1) ? 1 : 2; /*1: master, 2: slave*/

	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%d\n", value);

exit:
	mutex_unlock(&data->update_lock);
	return error;
}

static ssize_t sys_eeprom_read(loff_t off, char *buf, size_t count)
{
	int status = 0;
	unsigned char length = 0;

	if ((off + count) > EEPROM_SIZE) {
		return -EINVAL;
	}

	length = (count >= IPMI_READ_MAX_LEN) ? IPMI_READ_MAX_LEN : count;
	data->ipmi_tx_data[0] = (off & 0xff);
	data->ipmi_tx_data[1] = length;
	status = ipmi_send_message(&data->ipmi, IPMI_SYSEEPROM_READ_CMD,
				   data->ipmi_tx_data,
				   sizeof(data->ipmi_tx_data),
				   data->ipmi_resp_eeprom + off, length);
	if (unlikely(status != 0)) {
		goto exit;
	}

	if (unlikely(data->ipmi.rx_result != 0)) {
		status = -EIO;
		goto exit;
	}

	status = length;	/* Read length */
	memcpy(buf, data->ipmi_resp_eeprom + off, length);

 exit:
	return status;
}

static ssize_t sysfs_bin_read(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *attr,
			      char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;

	if (unlikely(!count)) {
		return count;
	}

	/*
	 * Read data from chip, protecting against concurrent updates
	 * from this host
	 */
	mutex_lock(&data->update_lock);

	while (count) {
		ssize_t status;

		status = sys_eeprom_read(off, buf, count);
		if (status <= 0) {
			if (retval == 0) {
				retval = status;
			}
			break;
		}

		buf += status;
		off += status;
		count -= status;
		retval += status;
	}

	mutex_unlock(&data->update_lock);
	return retval;

}

static int sysfs_eeprom_init(struct kobject *kobj, struct bin_attribute *eeprom)
{
	sysfs_bin_attr_init(eeprom);
	eeprom->attr.name = EEPROM_NAME;
	eeprom->attr.mode = S_IRUGO;
	eeprom->read = sysfs_bin_read;
	eeprom->write = NULL;
	eeprom->size = EEPROM_SIZE;

	/* Create eeprom file */
	return sysfs_create_bin_file(kobj, eeprom);
}

static int sysfs_eeprom_cleanup(struct kobject *kobj,
				struct bin_attribute *eeprom)
{
	sysfs_remove_bin_file(kobj, eeprom);
	return 0;
}

static int as7926_40xfb_sys_probe(struct platform_device *pdev)
{
	int status = -1;

	/* Register sysfs hooks */
	status = sysfs_eeprom_init(&pdev->dev.kobj, &data->eeprom);
	if (status) {
		goto exit_eeprom;
	}

	status = sysfs_create_group(&pdev->dev.kobj, &as7926_40xfb_sys_group);
	if (status) {
		goto exit_sysfs;
	}

	dev_info(&pdev->dev, "device created\n");

	return 0;

exit_sysfs:
    sysfs_eeprom_cleanup(&pdev->dev.kobj, &data->eeprom);
 exit_eeprom:
	return status;
}

static int as7926_40xfb_sys_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &as7926_40xfb_sys_group);
	sysfs_eeprom_cleanup(&pdev->dev.kobj, &data->eeprom);

	return 0;
}

static int __init as7926_40xfb_sys_init(void)
{
	int ret;

	data = kzalloc(sizeof(struct as7926_40xfb_sys_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_init(&data->update_lock);

	ret = platform_driver_register(&as7926_40xfb_sys_driver);
	if (ret < 0) {
		goto dri_reg_err;
	}

	data->pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(data->pdev)) {
		ret = PTR_ERR(data->pdev);
		goto dev_reg_err;
	}

	/* Set up IPMI interface */
	ret = init_ipmi_data(&data->ipmi, 0, &data->pdev->dev);
	if (ret)
		goto ipmi_err;

	return 0;

 ipmi_err:
	platform_device_unregister(data->pdev);
 dev_reg_err:
	platform_driver_unregister(&as7926_40xfb_sys_driver);
 dri_reg_err:
	kfree(data);
 alloc_err:
	return ret;
}

static void __exit as7926_40xfb_sys_exit(void)
{
	ipmi_destroy_user(data->ipmi.user);
	platform_device_unregister(data->pdev);
	platform_driver_unregister(&as7926_40xfb_sys_driver);
	kfree(data);
}

MODULE_AUTHOR("Brandon Chuang <brandon_chuang@accton.com.tw>");
MODULE_DESCRIPTION("as7926_40xfb_sys driver");
MODULE_LICENSE("GPL");

module_init(as7926_40xfb_sys_init);
module_exit(as7926_40xfb_sys_exit);
