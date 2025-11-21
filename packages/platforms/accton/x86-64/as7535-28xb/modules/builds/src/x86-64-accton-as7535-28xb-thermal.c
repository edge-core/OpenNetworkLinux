/*
 * Copyright (C)  Brandon Chuang <brandon_chuang@accton.com.tw>
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
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/ipmi.h>
#include <linux/ipmi_smi.h>
#include <linux/platform_device.h>
#include "accton_ipmi_intf.h"

#define DRVNAME "as7535_28xb_thermal"

#define IPMI_THERMAL_READ_CMD 0x12
#define IPMI_FPGA_READ_CMD 0x22

static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
	char *buf);
static ssize_t set_max(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count);
static int as7535_28xb_thermal_probe(struct platform_device *pdev);
static int as7535_28xb_thermal_remove(struct platform_device *pdev);

static int get_pcb_id(void);
static int g_pcb_id = 0;

enum temp_data_index {
	TEMP_ADDR,
	TEMP_FAULT,
	TEMP_INPUT,
	TEMP_DATA_COUNT
};

struct as7535_28xb_thermal_data {
	struct platform_device *pdev;
	struct mutex update_lock;
	char valid;		   /* != 0 if registers are valid */
	unsigned long last_updated;	/* In jiffies */
	char   ipmi_resp[36]; /* 3 bytes for each thermal */
	struct ipmi_data ipmi;
	unsigned char ipmi_tx_data[2];  /* 0: thermal id, 1: temp */
	char temp_max[12];
};

struct as7535_28xb_thermal_data *data = NULL;

static struct platform_driver as7535_28xb_thermal_driver = {
	.probe = as7535_28xb_thermal_probe,
	.remove = as7535_28xb_thermal_remove,
	.driver = {
		.name = DRVNAME,
		.owner = THIS_MODULE,
	},
};

enum as7535_28xb_thermal_sysfs_attrs {
	TEMP1_INPUT, // 0x4B
	TEMP2_INPUT, // TMP431_0x4C_U50
	TEMP3_INPUT, // TMP431_0x4C_MAC
	TEMP4_INPUT, // 0x4D lm75
	TEMP5_INPUT, // 0x4E lm75
	TEMP6_INPUT, // 0x4F lm75
	TEMP7_INPUT, // TMP431_0x4C_U93
	TEMP8_INPUT, // TMP431_0x4C_C10
	TEMP1_MAX,
	TEMP2_MAX,
	TEMP3_MAX,
	TEMP4_MAX,
	TEMP5_MAX,
	TEMP6_MAX,
	TEMP7_MAX,
	TEMP8_MAX
};

// Read only temp_input
#define DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(index) \
	static SENSOR_DEVICE_ATTR(temp##index##_input, S_IRUGO, show_temp, \
					NULL, TEMP##index##_INPUT); \
	static SENSOR_DEVICE_ATTR(temp##index##_max, S_IWUSR | S_IRUGO, show_temp,\
					set_max, TEMP##index##_MAX)

#define DECLARE_THERMAL_ATTR(index) \
	&sensor_dev_attr_temp##index##_input.dev_attr.attr, \
	&sensor_dev_attr_temp##index##_max.dev_attr.attr

DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(1);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(2);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(3);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(4);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(5);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(6);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(7);
DECLARE_THERMAL_SENSOR_DEVICE_ATTR_R(8);

static struct attribute *as7535_28xb_thermal_attributes[] = {
	DECLARE_THERMAL_ATTR(1),
	DECLARE_THERMAL_ATTR(2),
	DECLARE_THERMAL_ATTR(3),
	DECLARE_THERMAL_ATTR(4),
	DECLARE_THERMAL_ATTR(5),
	DECLARE_THERMAL_ATTR(6),
	DECLARE_THERMAL_ATTR(7),
	DECLARE_THERMAL_ATTR(8),
	NULL
};

static const struct attribute_group as7535_28xb_thermal_group = {
	.attrs = as7535_28xb_thermal_attributes,
};

static int get_pcb_id(void)
{
	int status = 0;
	int pcb_id = 0;
	/* Get PCB ID */
	data->ipmi_tx_data[0] = 0x60;
	data->ipmi_tx_data[1] = 0x0;
	status = ipmi_send_message(&data->ipmi, IPMI_FPGA_READ_CMD, data->ipmi_tx_data, 2,
								data->ipmi_resp, sizeof(data->ipmi_resp));
	if (unlikely(status != 0))
		goto exit;

	if (unlikely(data->ipmi.rx_result != 0)) {
		status = -EIO;
		goto exit;
	}
	/* (bit 3, bit 2) 00: No C10+IDT, 01: CY10+IDT, 10: no CY10+microchip, 11: CY10+microchip
		Support 8 thermals for CY10+IDT */
	pcb_id = (((s8)data->ipmi_resp[0]) >> 2) & 0xff;
	return pcb_id;

exit:
	return status;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *da,
							char *buf)
{
	int status = 0;
	int index  = 0;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	mutex_lock(&data->update_lock);
	if (attr->index >= TEMP1_MAX && attr->index <= TEMP8_MAX) {
		int max = data->temp_max[attr->index - TEMP1_MAX];
		mutex_unlock(&data->update_lock);
		return sprintf(buf, "%d\n", max * 1000);
	}
	/* Set 0 to Temp 7 and 8 if pcb id is not 01: CY10+IDT */
	if (g_pcb_id != 1)
	{
		if ((attr->index == TEMP7_INPUT) || (attr->index == TEMP8_INPUT))
		{
			status = 0;
			mutex_unlock(&data->update_lock);
			return sprintf(buf, "%d\n", status);
		}
	}

	if (time_after(jiffies, data->last_updated + HZ * 5) || !data->valid) {
		data->valid = 0;

		status = ipmi_send_message(&data->ipmi, IPMI_THERMAL_READ_CMD, NULL, 0,
									data->ipmi_resp, sizeof(data->ipmi_resp));
		if (unlikely(status != 0))
		{
			goto exit;
		}

		if (unlikely(data->ipmi.rx_result != 0)) {
			status = -EIO;
			goto exit;
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}
	/* Get temp fault status */
	index = attr->index * TEMP_DATA_COUNT + TEMP_FAULT;
	if (unlikely(data->ipmi_resp[index] == 0)) {
		status = -EIO;
		goto exit;
	}
	/* Get temperature in degree celsius */
	index = attr->index * TEMP_DATA_COUNT + TEMP_INPUT;
	status = ((s8)data->ipmi_resp[index]) * 1000;

	mutex_unlock(&data->update_lock);
	return sprintf(buf, "%d\n", status);

exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static ssize_t set_max(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	long temp;
	int status;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

	status = kstrtol(buf, 10, &temp);
	if (status)
		return status;

	if (temp > 127 || temp < -128)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->temp_max[attr->index-TEMP1_MAX] = temp;
	status = count;
	mutex_unlock(&data->update_lock);

	return status;
}

static int as7535_28xb_thermal_probe(struct platform_device *pdev)
{
	int status = -1;

	/* Register sysfs hooks */
	status = sysfs_create_group(&pdev->dev.kobj, &as7535_28xb_thermal_group);
	if (status)
		return status;

	dev_info(&pdev->dev, "device created\n");

	return 0;
}

static int as7535_28xb_thermal_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &as7535_28xb_thermal_group);
	return 0;
}

static int __init as7535_28xb_thermal_init(void)
{
	int ret;
	int i = 0;

	data = kzalloc(sizeof(struct as7535_28xb_thermal_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_err;
	}

	mutex_init(&data->update_lock);
	data->valid = 0;

	ret = platform_driver_register(&as7535_28xb_thermal_driver);
	if (ret < 0)
		goto dri_reg_err;

	data->pdev = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(data->pdev)) {
		ret = PTR_ERR(data->pdev);
		goto dev_reg_err;
	}

	/* Set up IPMI interface */
	ret = init_ipmi_data(&data->ipmi, 0, &data->pdev->dev);
	if (ret)
		goto ipmi_err;

	for (i = 0; i < ARRAY_SIZE(data->temp_max); i++) {
		data->temp_max[i] = 70; /* default high threshold */
	}

	g_pcb_id = get_pcb_id();

	return 0;

ipmi_err:
	platform_device_unregister(data->pdev);
dev_reg_err:
	platform_driver_unregister(&as7535_28xb_thermal_driver);
dri_reg_err:
	kfree(data);
alloc_err:
	return ret;
}

static void __exit as7535_28xb_thermal_exit(void)
{
	ipmi_destroy_user(data->ipmi.user);
	platform_device_unregister(data->pdev);
	platform_driver_unregister(&as7535_28xb_thermal_driver);
	kfree(data);
}

MODULE_AUTHOR("Brandon Chuang <brandon_chuang@accton.com.tw>");
MODULE_DESCRIPTION("as7535_28xb_thermal driver");
MODULE_LICENSE("GPL");

module_init(as7535_28xb_thermal_init);
module_exit(as7535_28xb_thermal_exit);
