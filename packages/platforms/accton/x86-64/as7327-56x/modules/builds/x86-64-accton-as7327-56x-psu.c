/*
 * Hardware monitoring driver for as7327_56x_psu
 *
 * Copyright (c) 2015 Accton Technology
 * Copyright (c) 2015 Zhenling Yin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/version.h>

#define PSU_MAX_FAN_SPEED   23000
#define PMBUS_MFR_MAX_LEN   (31)
#define PMBUS_MFR_PSU_VIN_TYPE      0x85  //just for C1A-B0650
#define PMBUS_READ_LED_STATUS       0xDA  //just for C1A-B0650
#define I2C_RW_RETRY_COUNT      10
#define I2C_RW_RETRY_INTERVAL   60 /* ms */

#define IS_PRESENT(id, value)		(!(value & BIT(7 - id)))

/*
 * Registers
 */
enum pmbus_regs {
	PMBUS_VOUT_MODE			= 0x20,
	PMBUS_STATUS_WORD		= 0x79,
	PMBUS_STATUS_TEMPERATURE	= 0x7D,
	PMBUS_STATUS_FAN_12		= 0x81,
	PMBUS_READ_VIN			= 0x88,
	PMBUS_READ_IIN			= 0x89,
	PMBUS_READ_VOUT			= 0x8B,
	PMBUS_READ_IOUT			= 0x8C,
	PMBUS_READ_TEMPERATURE_1	= 0x8D,
	PMBUS_READ_TEMPERATURE_2	= 0x8E,
	PMBUS_READ_TEMPERATURE_3	= 0x8F,
	PMBUS_READ_FAN_SPEED_1		= 0x90,
	PMBUS_READ_POUT			= 0x96,
	PMBUS_READ_PIN			= 0x97,
	PMBUS_MFR_ID			= 0x99,
	PMBUS_MFR_MODEL			= 0x9A,
	PMBUS_MFR_REVISION		= 0x9B,
	PMBUS_MFR_SERIAL		= 0x9E,
};

/*
 * STATUS_BYTE, STATUS_WORD (lower)
 */
#define PB_STATUS_NONE_ABOVE	BIT(0)
#define PB_STATUS_CML			BIT(1)
#define PB_STATUS_TEMPERATURE	BIT(2)
#define PB_STATUS_VIN_UV		BIT(3)
#define PB_STATUS_IOUT_OC		BIT(4)
#define PB_STATUS_VOUT_OV		BIT(5)
#define PB_STATUS_OFF			BIT(6)
#define PB_STATUS_BUSY			BIT(7)

/*
 * STATUS_WORD (upper)
 */
#define PB_STATUS_UNKNOWN		BIT(8)
#define PB_STATUS_OTHER			BIT(9)
#define PB_STATUS_FANS			BIT(10)
#define PB_STATUS_POWER_GOOD_N	BIT(11)
#define PB_STATUS_WORD_MFR		BIT(12)
#define PB_STATUS_INPUT			BIT(13)
#define PB_STATUS_IOUT_POUT		BIT(14)
#define PB_STATUS_VOUT			BIT(15)

/* Each client has this additional data
 */
struct as7327_56x_data {
    struct device     *hwmon_dev;
    struct mutex        update_lock;
    char                valid;         /* !=0 if registers are valid */
    unsigned long      last_updated;    /* In jiffies */
    u8   chip;          /* chip id */
    u8   status;          /* Status(present/power_good) register read from CPLD */
    u16  status_word;   /* Register value */
    u8   fan_fault;   /* Register value */
    u8   over_temp;   /* Register value */
    u16  v_in;        /* Register value */
    u16  i_in;        /* Register value */
    u16  p_in;        /* Register value */
    u16  v_out;        /* Register value */
    u16  i_out;       /* Register value */
    u16  p_out;       /* Register value */
    u8   vout_mode;     /* Register value */
    u16  temp[3];         /* Register value */
    u16  fan_speed;   /* Register value */
    u16  fan_duty_cycle;  /* Register value */
    u8   mfr_id[PMBUS_MFR_MAX_LEN];     /* Register value */
    u8   mfr_model[PMBUS_MFR_MAX_LEN]; /* Register value */
    u8   mfr_revsion[PMBUS_MFR_MAX_LEN]; /* Register value */
    u8   mfr_serial[PMBUS_MFR_MAX_LEN]; /* Register value */
    u8   mfr_vin_type; /* Register value */
    u8   led_status;   /* Register value */
};

static ssize_t show_present(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_word(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_linear(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_vout(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_fan_fault(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_over_temp(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_ascii(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_pout_max(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_vin_type(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t show_led_status(struct device *dev, struct device_attribute *da, char *buf);
static struct as7327_56x_data *as7327_56x_update_device(struct device *dev);
extern int as7327_56x_cpld_read(unsigned short cpld_addr, u8 reg);

enum as7327_56x_sysfs_attributes {
    PSU_PRESENT = 0,
    PSU_POWER_GOOD,
    PSU_POWER_ON,
    PSU_TEMP_FAULT,
    PSU_FAN1_FAULT,
    PSU_OVER_TEMP,
    PSU_V_IN,
    PSU_I_IN,
    PSU_P_IN,
    PSU_V_OUT,
    PSU_I_OUT,
    PSU_P_OUT,
    PSU_TEMP1_INPUT,
    PSU_TEMP2_INPUT,
    PSU_TEMP3_INPUT,
    PSU_FAN1_SPEED,
    PSU_FAN1_DUTY_CYCLE,
    PSU_MFR_ID,
    PSU_MFR_MODEL,
    PSU_MFR_REVISION,
    PSU_MFR_SERIAL,
    PSU_MFR_POUT_MAX,
    PSU_MFR_VIN_TYPE,
    PSU_LED_STATUS
};

/* sysfs attributes for hwmon
 */
static SENSOR_DEVICE_ATTR(psu_present,      S_IRUGO, show_present,    NULL, PSU_PRESENT);
static SENSOR_DEVICE_ATTR(psu_power_on,     S_IRUGO, show_word,   NULL, PSU_POWER_ON);
static SENSOR_DEVICE_ATTR(psu_temp_fault,   S_IRUGO, show_word,   NULL, PSU_TEMP_FAULT);
static SENSOR_DEVICE_ATTR(psu_power_good,   S_IRUGO, show_word,   NULL, PSU_POWER_GOOD);
static SENSOR_DEVICE_ATTR(psu_fan1_fault,   S_IRUGO, show_fan_fault, NULL, PSU_FAN1_FAULT);
static SENSOR_DEVICE_ATTR(psu_over_temp,    S_IRUGO, show_over_temp, NULL, PSU_OVER_TEMP);
static SENSOR_DEVICE_ATTR(psu_v_in,     S_IRUGO, show_linear,   NULL, PSU_V_IN);
static SENSOR_DEVICE_ATTR(psu_i_in,     S_IRUGO, show_linear,   NULL, PSU_I_IN);
static SENSOR_DEVICE_ATTR(psu_p_in,     S_IRUGO, show_linear,   NULL, PSU_P_IN);
static SENSOR_DEVICE_ATTR(psu_v_out,        S_IRUGO, show_vout,     NULL, PSU_V_OUT);
static SENSOR_DEVICE_ATTR(psu_i_out,        S_IRUGO, show_linear,   NULL, PSU_I_OUT);
static SENSOR_DEVICE_ATTR(psu_p_out,        S_IRUGO, show_linear,   NULL, PSU_P_OUT);
static SENSOR_DEVICE_ATTR(psu_temp1_input,  S_IRUGO, show_linear,   NULL, PSU_TEMP1_INPUT);
static SENSOR_DEVICE_ATTR(psu_temp2_input,  S_IRUGO, show_linear,   NULL, PSU_TEMP2_INPUT);
static SENSOR_DEVICE_ATTR(psu_temp3_input,  S_IRUGO, show_linear,   NULL, PSU_TEMP3_INPUT);
static SENSOR_DEVICE_ATTR(psu_fan1_speed_rpm, S_IRUGO, show_linear, NULL, PSU_FAN1_SPEED);
static SENSOR_DEVICE_ATTR(psu_fan1_duty_cycle_percentage, S_IRUGO, show_word, NULL, PSU_FAN1_DUTY_CYCLE);
static SENSOR_DEVICE_ATTR(psu_mfr_id,       S_IRUGO, show_ascii,  NULL, PSU_MFR_ID);
static SENSOR_DEVICE_ATTR(psu_mfr_model,    S_IRUGO, show_ascii,  NULL, PSU_MFR_MODEL);
static SENSOR_DEVICE_ATTR(psu_mfr_revision, S_IRUGO, show_ascii, NULL, PSU_MFR_REVISION);
static SENSOR_DEVICE_ATTR(psu_mfr_serial,   S_IRUGO, show_ascii, NULL, PSU_MFR_SERIAL);
static SENSOR_DEVICE_ATTR(psu_mfr_pout_max, S_IRUGO, show_pout_max, NULL, PSU_MFR_POUT_MAX);
static SENSOR_DEVICE_ATTR(psu_mfr_vin_type, S_IRUGO, show_vin_type, NULL, PSU_MFR_VIN_TYPE);
static SENSOR_DEVICE_ATTR(psu_led_status, S_IRUGO, show_led_status, NULL, PSU_LED_STATUS);

static struct attribute *as7327_56x_attributes[] = {
    &sensor_dev_attr_psu_present.dev_attr.attr,
    &sensor_dev_attr_psu_power_on.dev_attr.attr,
    &sensor_dev_attr_psu_temp_fault.dev_attr.attr,
    &sensor_dev_attr_psu_power_good.dev_attr.attr,
    &sensor_dev_attr_psu_fan1_fault.dev_attr.attr,
    &sensor_dev_attr_psu_over_temp.dev_attr.attr,
    &sensor_dev_attr_psu_v_in.dev_attr.attr,
    &sensor_dev_attr_psu_i_in.dev_attr.attr,
    &sensor_dev_attr_psu_p_in.dev_attr.attr,
    &sensor_dev_attr_psu_v_out.dev_attr.attr,
    &sensor_dev_attr_psu_i_out.dev_attr.attr,
    &sensor_dev_attr_psu_p_out.dev_attr.attr,
    &sensor_dev_attr_psu_temp1_input.dev_attr.attr,
    &sensor_dev_attr_psu_temp2_input.dev_attr.attr,
    &sensor_dev_attr_psu_temp3_input.dev_attr.attr,
    &sensor_dev_attr_psu_fan1_speed_rpm.dev_attr.attr,
    &sensor_dev_attr_psu_fan1_duty_cycle_percentage.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_id.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_model.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_revision.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_serial.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_pout_max.dev_attr.attr,
    &sensor_dev_attr_psu_mfr_vin_type.dev_attr.attr,
    &sensor_dev_attr_psu_led_status.dev_attr.attr,
    NULL
};

static ssize_t show_present(struct device *dev, struct device_attribute *da, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct as7327_56x_data *data = i2c_get_clientdata(client);
    int status = 0;

    mutex_lock(&data->update_lock);

    status = as7327_56x_cpld_read(0x62, 0x1d);
    if (status < 0) {
        dev_dbg(&client->dev, "cpld reg 0x62 err %d\n", status);
    }
    else {
        data->status = status;
    }
    mutex_unlock(&data->update_lock);

    status = IS_PRESENT(data->chip, data->status);

    return sprintf(buf, "%d\n", status);
}

static ssize_t show_word(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct as7327_56x_data *data = as7327_56x_update_device(dev);
    u16 status = 0;

    if (!data->valid) {
        return 0;
    }

    switch (attr->index) {
    case PSU_POWER_ON: /* psu_power_on, low byte bit 6 of status_word, 0=>ON, 1=>OFF */
        status = (data->status_word & PB_STATUS_OFF) ? 0 : 1;
        break;
    case PSU_TEMP_FAULT: /* psu_temp_fault, low byte bit 2 of status_word, 0=>Normal, 1=>temp fault */
        status = !!(data->status_word & PB_STATUS_TEMPERATURE);
        break;
    case PSU_POWER_GOOD: /* psu_power_good, high byte bit 3 of status_word, 0=>OK, 1=>FAIL */
        status = (data->status_word & PB_STATUS_POWER_GOOD_N) ? 0 : 1;
        break;
    case PSU_FAN1_DUTY_CYCLE:
        status = (data->fan_speed * 100) / PSU_MAX_FAN_SPEED;
        status = (status > 100) ? 100 : status;
        break;
    default:
        return 0;
    }

    return sprintf(buf, "%d\n", status);
}

static int two_complement_to_int(u16 data, u8 valid_bit, int mask)
{
    u16  valid_data  = data & mask;
    bool is_negative = valid_data >> (valid_bit - 1);

    return is_negative ? (-(((~valid_data) & mask) + 1)) : valid_data;
}

static int pmbus_linear16_to_val(int data, int data_exponent, long *val)
{
    int exponent, mantissa;
    int temp_value = 0;
    int multiplier = 1000;

    if (data < 0)
    {
        return data;
    }
    if (data_exponent < 0)
    {
        return data_exponent;
    }
    exponent = two_complement_to_int(data_exponent, 5, 0x1f);
    mantissa = data;
    if (exponent >= 0)
    {
        temp_value = (mantissa << exponent) * multiplier;
    }
    else
    {
        temp_value = (mantissa * multiplier) / (1 << -exponent);
    }
    *val = temp_value;
    return 0;
}

static ssize_t show_linear(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct as7327_56x_data *data = as7327_56x_update_device(dev);
    u8 *ptr = NULL;

    u16 value = 0;
    int exponent, mantissa;
    int multiplier = 1000;
    ptr = data->mfr_model + 1; /* The first byte is the count byte of string. */

    if (!data->valid) {
        return 0;
    }

    switch (attr->index) {
    case PSU_V_IN:
        value = data->v_in;
        break;
    case PSU_I_IN:
        value = data->i_in;
        break;
    case PSU_P_IN:
        value = data->p_in;
        break;
    case PSU_I_OUT:
        value = data->i_out;
        break;
    case PSU_P_OUT:
        value = data->p_out;
        break;
    case PSU_TEMP1_INPUT:
    case PSU_TEMP2_INPUT:
    case PSU_TEMP3_INPUT:
        value = data->temp[attr->index-PSU_TEMP1_INPUT];
        break;
    case PSU_FAN1_SPEED:
        value = data->fan_speed;
        multiplier = 1;
        break;
    default:
        return 0;
    }

    exponent = two_complement_to_int(value >> 11, 5, 0x1f);
    mantissa = two_complement_to_int(value & 0x7ff, 11, 0x7ff);

    return (exponent >= 0) ? sprintf(buf, "%d\n", (mantissa << exponent) * multiplier) :
                             sprintf(buf, "%d\n", (mantissa * multiplier) / (1 << -exponent));
}

static ssize_t show_fan_fault(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct as7327_56x_data *data = as7327_56x_update_device(dev);
    u8 shift;

    if (!data->valid) {
        return 0;
    }

    shift = (attr->index == PSU_FAN1_FAULT) ? 7 : 6;

    return sprintf(buf, "%d\n", data->fan_fault >> shift);
}

static ssize_t show_over_temp(struct device *dev, struct device_attribute *da, char *buf)
{
    struct as7327_56x_data *data = as7327_56x_update_device(dev);

    if (!data->valid) {
        return 0;
    }

    return sprintf(buf, "%d\n", data->over_temp >> 7);
}

static ssize_t show_ascii(struct device *dev, struct device_attribute *da, char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct as7327_56x_data *data = as7327_56x_update_device(dev);
    u8 *ptr = NULL;

    if (!data->valid) {
        return 0;
    }

    switch (attr->index) {
    case PSU_MFR_ID: /* psu_mfr_id */
            ptr = data->mfr_id + 1; /* The first byte is the count byte of string. */;
        break;
    case PSU_MFR_MODEL: /* psu_mfr_model */
            ptr = data->mfr_model + 1; /* The first byte is the count byte of string. */;
        break;
    case PSU_MFR_REVISION: /* psu_mfr_revision */
            ptr = data->mfr_revsion + 1; /* The first byte is the count byte of string. */;
        break;
    case PSU_MFR_SERIAL: /* psu_mfr_serial */
        ptr = data->mfr_serial + 1; /* The first byte is the count byte of string. */;
        break;
    default:
        return 0;
    }

    return sprintf(buf, "%s\n", ptr);
}

static ssize_t show_vout(struct device *dev, struct device_attribute *da, char *buf)
{
    struct as7327_56x_data *data = as7327_56x_update_device(dev);
    int status = 0;
    long val = 0;

    if (!data->valid) {
        return 0;
    }

    status = pmbus_linear16_to_val(data->v_out, data->vout_mode, &val);
    if (status < 0) {
        return 0;
    }

    return sprintf(buf, "%ld\n", val);
}

static ssize_t show_pout_max(struct device *dev, struct device_attribute *da, char *buf)
{
    int val = 650 * 1000;

    return sprintf(buf, "%d\n", val);
}

static ssize_t show_vin_type(struct device *dev, struct device_attribute *da, char *buf)
{
    struct as7327_56x_data *data = as7327_56x_update_device(dev);

    if (!data->valid) {
        return 0;
    }

    return sprintf(buf, "%d\n", data->mfr_vin_type);
}

static ssize_t show_led_status(struct device *dev, struct device_attribute *da, char *buf)
{
    struct as7327_56x_data *data = as7327_56x_update_device(dev);

    if (!data->valid) {
        return 0;
    }

    return sprintf(buf, "%d\n", data->led_status);
}

static int as7327_56x_read_byte(struct i2c_client *client, u8 reg)
{
    int status = 0, retry = I2C_RW_RETRY_COUNT;

    while (retry) {
        status = i2c_smbus_read_byte_data(client, reg);
        if (unlikely(status < 0)) {
            msleep(I2C_RW_RETRY_INTERVAL);
            retry--;
            continue;
        }

        break;
    }

    return status;
}

static int as7327_56x_read_word(struct i2c_client *client, u8 reg)
{
    int status = 0, retry = I2C_RW_RETRY_COUNT;

    while (retry) {
        status = i2c_smbus_read_word_data(client, reg);
        if (unlikely(status < 0)) {
            msleep(I2C_RW_RETRY_INTERVAL);
            retry--;
            continue;
        }

        break;
    }

    return status;
}

static int as7327_56x_read_block(struct i2c_client *client, u8 command, u8 *data)
{
    int status = 0, retry = I2C_RW_RETRY_COUNT;
    int read_value = 0;

    while (retry) {
        read_value = i2c_smbus_read_byte_data(client, command);
        if ((read_value >= (PMBUS_MFR_MAX_LEN-1)) || (read_value < 0))
        {
            return -EINVAL;
        }
        status = i2c_smbus_read_i2c_block_data(client, command, read_value+1, data);
        if (unlikely(status < 0)) {
            msleep(I2C_RW_RETRY_INTERVAL);
            retry--;
            continue;
        }
        data[read_value+1] = '\0';
        break;
    }

    return status;
}

struct reg_data_byte {
    u8   reg;
    u8  *value;
};

struct reg_data_word {
    u8   reg;
    u16 *value;
};

static struct as7327_56x_data *as7327_56x_update_device(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct as7327_56x_data *data = i2c_get_clientdata(client);

    mutex_lock(&data->update_lock);

    if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
        || !data->valid) {
        int i, status;
        struct reg_data_byte regs_byte[] = { {PMBUS_VOUT_MODE, &data->vout_mode},
                                             {PMBUS_STATUS_TEMPERATURE, &data->over_temp},
                                             {PMBUS_STATUS_FAN_12, &data->fan_fault},
                                             {PMBUS_MFR_PSU_VIN_TYPE, &data->mfr_vin_type},
                                             {PMBUS_READ_LED_STATUS, &data->led_status},
                                             };
        struct reg_data_word regs_word[] = { {PMBUS_STATUS_WORD, &data->status_word},
                                             {PMBUS_READ_VIN, &data->v_in},
                                             {PMBUS_READ_VOUT, &data->v_out},
                                             {PMBUS_READ_IIN, &data->i_in},
                                             {PMBUS_READ_IOUT, &data->i_out},
                                             {PMBUS_READ_POUT, &data->p_out},
                                             {PMBUS_READ_PIN, &data->p_in},
                                             {PMBUS_READ_TEMPERATURE_1, &(data->temp[0])},
                                             {PMBUS_READ_TEMPERATURE_2, &(data->temp[1])},
                                             {PMBUS_READ_TEMPERATURE_3, &(data->temp[2])},
                                             {PMBUS_READ_FAN_SPEED_1, &(data->fan_speed)},
                                             };

        dev_dbg(&client->dev, "Starting as7327_56x_psu update\n");

        /* Read byte data */
        for (i = 0; i < ARRAY_SIZE(regs_byte); i++) {
            status = as7327_56x_read_byte(client, regs_byte[i].reg);
            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n",
                        regs_byte[i].reg, status);
            }
            else {
                *(regs_byte[i].value) = status;
            }
        }

        /* Read word data */
        for (i = 0; i < ARRAY_SIZE(regs_word); i++) {
            status = as7327_56x_read_word(client, regs_word[i].reg);
            if (status < 0) {
                dev_dbg(&client->dev, "reg %d, err %d\n",
                        regs_word[i].reg, status);
            }
            else {
                *(regs_word[i].value) = status;
            }

        }
        /* Read mfr_id */
        status = as7327_56x_read_block(client, PMBUS_MFR_ID, data->mfr_id);
        if (status < 0) {
            dev_dbg(&client->dev, "reg %d, err %d\n", PMBUS_MFR_ID, status);
            goto exit;
        }
        /* Read mfr_model */
        status = as7327_56x_read_block(client, PMBUS_MFR_MODEL, data->mfr_model);
        if (status < 0) {
            dev_dbg(&client->dev, "reg %d, err %d\n", PMBUS_MFR_MODEL, status);
            goto exit;
        }
        /* Read mfr_revsion */
        status = as7327_56x_read_block(client, PMBUS_MFR_REVISION, data->mfr_revsion);
        if (status < 0) {
            dev_dbg(&client->dev, "reg %d, err %d\n", PMBUS_MFR_REVISION, status);
            goto exit;
        }
        /* Read mfr_serial */
        status = as7327_56x_read_block(client, PMBUS_MFR_SERIAL, data->mfr_serial);
        if (status < 0) {
            dev_dbg(&client->dev, "reg %d, err %d\n", PMBUS_MFR_SERIAL, status);
            goto exit;
        }

        data->last_updated = jiffies;
        data->valid = 1;
    }

exit:
    mutex_unlock(&data->update_lock);

    return data;
}

enum psu_index {
    as7327_56x_psu1,
    as7327_56x_psu2
};

static const struct i2c_device_id as7327_56x_psu_id[] = {
    { "as7327_56x_psu1", as7327_56x_psu1 },
    { "as7327_56x_psu2", as7327_56x_psu2 },
    { }
};
MODULE_DEVICE_TABLE(i2c, as7327_56x_psu_id);

static const struct attribute_group as7327_56x_group = {
    .attrs = as7327_56x_attributes,
};

static int as7327_56x_psu_probe(struct i2c_client *client,
             const struct i2c_device_id *dev_id)
{
    struct as7327_56x_data *data;
    int status = 0;

    if (!i2c_check_functionality(client->adapter,
            I2C_FUNC_SMBUS_READ_BYTE_DATA | I2C_FUNC_SMBUS_READ_WORD_DATA | I2C_FUNC_SMBUS_READ_BLOCK_DATA))
        return -ENODEV;

    data = kzalloc(sizeof(struct as7327_56x_data), GFP_KERNEL);
    if (!data) {
        status = -ENOMEM;
        goto exit;
    }

    i2c_set_clientdata(client, data);
    mutex_init(&data->update_lock);
    data->chip = dev_id->driver_data;
    dev_info(&client->dev, "chip found\n");

    /* Register sysfs hooks */
    status = sysfs_create_group(&client->dev.kobj, &as7327_56x_group);
    if (status) {
        goto exit_free;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
    data->hwmon_dev = hwmon_device_register_with_info(&client->dev, "as7327_56x",
                                                      NULL, NULL, NULL);
#else
    data->hwmon_dev = hwmon_device_register(&client->dev);
#endif
    if (IS_ERR(data->hwmon_dev)) {
        status = PTR_ERR(data->hwmon_dev);
        goto exit_remove;
    }

    dev_info(&client->dev, "%s: psu '%s'\n",
         dev_name(data->hwmon_dev), client->name);

    return 0;

exit_remove:
    sysfs_remove_group(&client->dev.kobj, &as7327_56x_group);
exit_free:
    kfree(data);
exit:
    return status;
}

int as7327_56x_psu_remove(struct i2c_client *client)
{
    struct as7327_56x_data *data = i2c_get_clientdata(client);

    hwmon_device_unregister(data->hwmon_dev);
    sysfs_remove_group(&client->dev.kobj, &as7327_56x_group);
    kfree(data);

    return 0;
}

static struct i2c_driver as7327_56x_psu_driver = {
    .driver = {
           .name = "as7327_56x_psu",
           },
    .probe = as7327_56x_psu_probe,
    .remove = as7327_56x_psu_remove,
    .id_table = as7327_56x_psu_id,
};

module_i2c_driver(as7327_56x_psu_driver);

MODULE_AUTHOR("vincent_chiang@edge-core.com");
MODULE_DESCRIPTION("as7327_56x_psu driver");
MODULE_LICENSE("GPL");

