/*
 * Copyright (C)  Roger Ho <roger530_ho@edge-core.com>
 *
 * Based on:
 *    pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *    pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 *    i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 *    pca9540.c from Jean Delvare <khali@linux-fr.org>.
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
#include <linux/string_helpers.h>
#include "accton_ipmi_intf.h"

#define DRVNAME "as9817_64_sys"

#define IPMI_READ_MAX_LEN 128

#define IPMI_CPLD_READ_CMD 0x20
#define IPMI_OTP_PROTECT_CMD 0x94
#define IPMI_GET_FAN_CONTROLLER_CMD 0x66
#define IPMI_SET_FAN_CONTROLLER_CMD 0x67
#define IPMI_SEND_THERMAL_DATA_CMD 0x13
#define IPMI_RESET_CMD 0x65
#define IPMI_RESET_CMD_LENGTH 6

static int as9817_64_sys_probe(struct platform_device *pdev);
static int as9817_64_sys_remove(struct platform_device *pdev);
static ssize_t show_version(struct device *dev,
                                struct device_attribute *da, char *buf);
static ssize_t get_bmc_fan_controller(struct device *dev,
                                          struct device_attribute *da, char *buf);
static ssize_t set_otp_protect(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count);
static ssize_t set_bmc_fan_controller(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count);
static ssize_t set_bmc_thermal_data(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count);
static ssize_t get_reset(struct device *dev, struct device_attribute *da,
            char *buf);
static ssize_t set_reset(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count);

struct as9817_64_sys_data {
    struct platform_device *pdev;
    struct mutex update_lock;
    char valid; /* != 0 if registers are valid */
    unsigned long last_updated;    /* In jiffies */
    struct ipmi_data ipmi;
    unsigned char ipmi_resp_cpld[2];
    unsigned char ipmi_resp_fan_controller[1];
    unsigned char ipmi_tx_data[3];
    unsigned char ipmi_resp_rst[2];
    unsigned char ipmi_tx_data_rst[IPMI_RESET_CMD_LENGTH];
};

struct as9817_64_sys_data *data = NULL;

static struct platform_driver as9817_64_sys_driver = {
    .probe = as9817_64_sys_probe,
    .remove = as9817_64_sys_remove,
    .driver = {
        .name = DRVNAME,
        .owner = THIS_MODULE,
    },
};

enum as9817_64_sys_sysfs_attrs {
    FPGA_VER, /* FPGA version */
    OTP_PROTECT,
    FAN_CONTROLLER,
    THERMAL_DATA,
    RESET_MAC
};

static SENSOR_DEVICE_ATTR(fpga_version, S_IRUGO, show_version, NULL, FPGA_VER);
static SENSOR_DEVICE_ATTR(otp_protect, S_IWUSR, NULL, set_otp_protect, OTP_PROTECT);
static SENSOR_DEVICE_ATTR(bmc_fan_controller, S_IRUGO|S_IWUSR, 
                          get_bmc_fan_controller, set_bmc_fan_controller, 
                          FAN_CONTROLLER);
static SENSOR_DEVICE_ATTR(bmc_thermal_data, S_IWUSR, NULL, set_bmc_thermal_data, THERMAL_DATA);
static SENSOR_DEVICE_ATTR(reset_mac, S_IWUSR | S_IRUGO, get_reset, \
                          set_reset, RESET_MAC);

static struct attribute *as9817_64_sys_attributes[] = {
    &sensor_dev_attr_fpga_version.dev_attr.attr,
    &sensor_dev_attr_otp_protect.dev_attr.attr,
    &sensor_dev_attr_bmc_fan_controller.dev_attr.attr,
    &sensor_dev_attr_bmc_thermal_data.dev_attr.attr,
    &sensor_dev_attr_reset_mac.dev_attr.attr,
    NULL
};

static const struct attribute_group as9817_64_sys_group = {
    .attrs = as9817_64_sys_attributes,
};

static struct as9817_64_sys_data *as9817_64_sys_update_fpga_ver(void)
{
    int status = 0;

    data->valid = 0;
    data->ipmi_tx_data[0] = 0x60;
    status = ipmi_send_message(&data->ipmi, IPMI_CPLD_READ_CMD,
                                data->ipmi_tx_data, 1,
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
    unsigned char major;
    unsigned char minor;
    int error = 0;

    mutex_lock(&data->update_lock);

    data = as9817_64_sys_update_fpga_ver();
    if (!data->valid) {
        error = -EIO;
        goto exit;
    }

    major = data->ipmi_resp_cpld[0];
    minor = data->ipmi_resp_cpld[1];
    mutex_unlock(&data->update_lock);
    return sprintf(buf, "%x.%x\n", major, minor);

exit:
    mutex_unlock(&data->update_lock);
    return error;
}

static struct as9817_64_sys_data *as9817_64_sys_update_fan_controller(void)
{
    int status = 0;

    data->valid = 0;
    status = ipmi_send_message(&data->ipmi, IPMI_GET_FAN_CONTROLLER_CMD,
                                NULL, 0,
                                data->ipmi_resp_fan_controller,
                                sizeof(data->ipmi_resp_fan_controller));
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

static ssize_t get_bmc_fan_controller(struct device *dev,
                                          struct device_attribute *da, char *buf)
{
    unsigned char status;
    int error = 0;

    mutex_lock(&data->update_lock);

    data = as9817_64_sys_update_fan_controller();
    if (!data->valid) {
        error = -EIO;
        goto exit;
    }

    status = data->ipmi_resp_fan_controller[0];
    mutex_unlock(&data->update_lock);
    return sprintf(buf, "%d\n", status);

exit:
    mutex_unlock(&data->update_lock);
    return error;
}

static ssize_t set_otp_protect(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count)
{
    long otp_protect;
    int status;

    status = kstrtol(buf, 10, &otp_protect);
    if (status)
        return status;

    if (!otp_protect)
        return count;

    mutex_lock(&data->update_lock);

    data->ipmi_tx_data[0] = 3;
    status = ipmi_send_message(&data->ipmi, IPMI_OTP_PROTECT_CMD,
                                data->ipmi_tx_data, 1,
                                NULL, 0);
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

static ssize_t set_bmc_fan_controller(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count)
{
    long enable;
    int status;

    status = kstrtol(buf, 10, &enable);
    if (status)
        return status;

    mutex_lock(&data->update_lock);

    data->ipmi_tx_data[0] = 0;
    data->ipmi_tx_data[1] = 2;
    if (enable) {
        data->ipmi_tx_data[1] = 3;
    }
    status = ipmi_send_message(&data->ipmi, IPMI_SET_FAN_CONTROLLER_CMD,
                                data->ipmi_tx_data, 2,
                                NULL, 0);
    if (unlikely(status != 0))
        goto exit;

    if (unlikely(data->ipmi.rx_result != 0)) {
        status = -EINVAL;
        goto exit;
    }

    status = count;

exit:
    mutex_unlock(&data->update_lock);
    return status;
}


static ssize_t set_bmc_thermal_data(struct device *dev, struct device_attribute *da,
            const char *buf, size_t count)
{
    int status;
    int args;
    char *opt, tmp[32] = {0};
    char *tmp_p;
    size_t copy_size;
    u8 input[3] = {0};

    copy_size = (count < sizeof(tmp)) ? count : sizeof(tmp) - 1;
    #ifdef __STDC_LIB_EXT1__
    memcpy_s(tmp, copy_size, buf, copy_size);
    #else
    memcpy(tmp, buf, copy_size);
    #endif
    tmp[copy_size] = '\0';

    args = 0;
    tmp_p = strim(tmp);
    while (args < 3 && (opt = strsep(&tmp_p, " ")) != NULL) {
        if (kstrtou8(opt, 10, &input[args]) == 0) {
            args++;
        }
    }
    if (args != 3) {
        return -EINVAL;
    }

    mutex_lock(&data->update_lock);

    data->ipmi_tx_data[0] = input[0];
    data->ipmi_tx_data[1] = input[1];
    data->ipmi_tx_data[2] = input[2];
    status = ipmi_send_message(&data->ipmi, IPMI_SEND_THERMAL_DATA_CMD,
                                data->ipmi_tx_data, 3,
                                NULL, 0);
    if (unlikely(status != 0))
        goto exit;

    if (unlikely(data->ipmi.rx_result != 0)) {
        status = -EINVAL;
        goto exit;
    }

    status = count;

exit:
    mutex_unlock(&data->update_lock);
    return status;
}

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

    if (sscanf(buf, "0x%x 0x%x", &magic[0], &magic[1]) != 2)
        return -EINVAL;

    if (magic[0] > 0xFF || magic[1] > 0xFF)
        return -EINVAL;

    mutex_lock(&data->update_lock);

    /* Send IPMI write command */
    
    data->ipmi_tx_data_rst[0] = 0;
    data->ipmi_tx_data_rst[1] = 0;
    data->ipmi_tx_data_rst[2] = 1;
    data->ipmi_tx_data_rst[3] = 1;
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

static int as9817_64_sys_probe(struct platform_device *pdev)
{
    int status = -1;

    /* Register sysfs hooks */
    status = sysfs_create_group(&pdev->dev.kobj, &as9817_64_sys_group);
    if (status)
        goto exit;

    dev_info(&pdev->dev, "device created\n");

    return 0;

exit:
    return status;
}

static int as9817_64_sys_remove(struct platform_device *pdev)
{
    sysfs_remove_group(&pdev->dev.kobj, &as9817_64_sys_group);

    return 0;
}

static int __init as9817_64_sys_init(void)
{
    int ret;

    data = kzalloc(sizeof(struct as9817_64_sys_data), GFP_KERNEL);
    if (!data) {
        ret = -ENOMEM;
        goto alloc_err;
    }

    mutex_init(&data->update_lock);

    ret = platform_driver_register(&as9817_64_sys_driver);
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

    return 0;

ipmi_err:
    platform_device_unregister(data->pdev);
dev_reg_err:
    platform_driver_unregister(&as9817_64_sys_driver);
dri_reg_err:
    kfree(data);
alloc_err:
    return ret;
}

static void __exit as9817_64_sys_exit(void)
{
    ipmi_destroy_user(data->ipmi.user);
    platform_device_unregister(data->pdev);
    platform_driver_unregister(&as9817_64_sys_driver);
    kfree(data);
}

MODULE_AUTHOR("Roger Ho <roger530_ho@edge-core.com>");
MODULE_DESCRIPTION("as9817_64_sys driver");
MODULE_LICENSE("GPL");

module_init(as9817_64_sys_init);
module_exit(as9817_64_sys_exit);
