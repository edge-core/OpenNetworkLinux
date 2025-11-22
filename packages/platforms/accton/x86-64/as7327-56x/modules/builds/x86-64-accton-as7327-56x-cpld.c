/*
 * Copyright (C)  Brandon Cheng <brandon_cheng@edge-core.com>
 *
 * This module supports the accton cpld that hold the channel select
 * mechanism for other i2c slave devices, such as SFP.
 * This includes the:
 *	 Accton as7327_56x CPLD1/CPLD2
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
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/stat.h>
#include <linux/hwmon-sysfs.h>
#include <linux/delay.h>

#define I2C_RW_RETRY_COUNT				10
#define I2C_RW_RETRY_INTERVAL			60 /* ms */
#define FAN_MAX_DUTY_CYCLE              100
#define FAN_TECK_SPEED_CNT              150 // 1/167.67*1000/2*60 = 178.92

static LIST_HEAD(cpld_client_list);
static struct mutex     list_lock;

struct cpld_client_node {
    struct i2c_client *client;
    struct list_head   list;
};

enum cpld_type {
    as7327_56x_cpld1,
    as7327_56x_cpld2
};

enum fan_box_id {
    FAN_BOX1_ID,
    FAN_BOX2_ID,
};

/* 2 fan-tray modules with 4 pcs of 40mmx40mmx56mm 12V fans, hot-swappable */
static const u8 fan_reg[] = {
    0x52,      /* fan box's status and direction */
    0x36,      /* fan PWM(for fan1), duty cycle */
    0x37,      /* fan PWM(for fan2), duty cycle */
    0x38,      /* fan PWM(for fan3), duty cycle */
    0x39,      /* fan PWM(for fan4), duty cycle */
    0x3A,      /* front fan1 speed(rpm) */
    0x3B,      /* front fan2 speed(rpm) */
    0x3C,      /* front fan3 speed(rpm) */
    0x3D,      /* front fan4 speed(rpm) */
    0x3E,      /* rear fan1 speed(rpm) */
    0x3F,      /* rear fan2 speed(rpm) */
    0x40,      /* rear fan3 speed(rpm) */
    0x41,      /* rear fan4 speed(rpm) */
    0x2e,      /* fan watchdog en */
    0x2f,      /* fan watchdog setting */
};

#define FAN_WATCHDOG_EN_REG 0x2E

struct as7327_56x_cpld_data {
    enum cpld_type   type;
    struct device   *hwmon_dev;
    struct mutex     update_lock;
	char             valid;           /* != 0 if registers are valid */
    unsigned long    last_updated;    /* In jiffies */
	u8               reg_fan_val[ARRAY_SIZE(fan_reg)]; /* Register value */

};

static const struct i2c_device_id as7327_56x_cpld_id[] = {
    { "as7327_56x_cpld1", as7327_56x_cpld1 },
    { "as7327_56x_cpld2", as7327_56x_cpld2 },
    { }
};
MODULE_DEVICE_TABLE(i2c, as7327_56x_cpld_id);

#define TRANSCEIVER_PRESENT_ATTR_ID(index)   	MODULE_PRESENT_##index
#define TRANSCEIVER_TXDISABLE_ATTR_ID(index)   	MODULE_TXDISABLE_##index
#define TRANSCEIVER_RXLOS_ATTR_ID(index)   		MODULE_RXLOS_##index
#define TRANSCEIVER_TXFAULT_ATTR_ID(index)   	MODULE_TXFAULT_##index
#define TRANSCEIVER_RESET_ATTR_ID(index)   	    MODULE_RESET_##index
#define TRANSCEIVER_LPMODE_ATTR_ID(index)       MODULE_LPMODE_##index

#define FAN_DIRECTION_ID(index) FAN_DIRECTION_##index      // by fan box(module)
#define FAN_PRESENT_ATTR_ID(index) FAN_PRESENT_##index     // by fan box(module)
#define FAN_FRONT_SPEED_RPM_ATTR_ID(index) FAN_FRONT_SPEED_RPM_##index // by each fan
#define FAN_REAR_SPEED_RPM_ATTR_ID(index)  FAN_REAR_SPEED_RPM_##index   // by each fan

enum as7327_56x_cpld_sysfs_attributes {
	CPLD_VERSION,
	ACCESS,
	MODULE_PRESENT_ALL,
	MODULE_RXLOS_ALL,
	/* transceiver attributes */
	TRANSCEIVER_PRESENT_ATTR_ID(1),
	TRANSCEIVER_PRESENT_ATTR_ID(2),
	TRANSCEIVER_PRESENT_ATTR_ID(3),
	TRANSCEIVER_PRESENT_ATTR_ID(4),
	TRANSCEIVER_PRESENT_ATTR_ID(5),
	TRANSCEIVER_PRESENT_ATTR_ID(6),
	TRANSCEIVER_PRESENT_ATTR_ID(7),
	TRANSCEIVER_PRESENT_ATTR_ID(8),
	TRANSCEIVER_PRESENT_ATTR_ID(9),
	TRANSCEIVER_PRESENT_ATTR_ID(10),
	TRANSCEIVER_PRESENT_ATTR_ID(11),
	TRANSCEIVER_PRESENT_ATTR_ID(12),
	TRANSCEIVER_PRESENT_ATTR_ID(13),
	TRANSCEIVER_PRESENT_ATTR_ID(14),
	TRANSCEIVER_PRESENT_ATTR_ID(15),
	TRANSCEIVER_PRESENT_ATTR_ID(16),
	TRANSCEIVER_PRESENT_ATTR_ID(17),
	TRANSCEIVER_PRESENT_ATTR_ID(18),
	TRANSCEIVER_PRESENT_ATTR_ID(19),
	TRANSCEIVER_PRESENT_ATTR_ID(20),
	TRANSCEIVER_PRESENT_ATTR_ID(21),
	TRANSCEIVER_PRESENT_ATTR_ID(22),
	TRANSCEIVER_PRESENT_ATTR_ID(23),
	TRANSCEIVER_PRESENT_ATTR_ID(24),
	TRANSCEIVER_PRESENT_ATTR_ID(25),
	TRANSCEIVER_PRESENT_ATTR_ID(26),
	TRANSCEIVER_PRESENT_ATTR_ID(27),
	TRANSCEIVER_PRESENT_ATTR_ID(28),
	TRANSCEIVER_PRESENT_ATTR_ID(29),
	TRANSCEIVER_PRESENT_ATTR_ID(30),
	TRANSCEIVER_PRESENT_ATTR_ID(31),
	TRANSCEIVER_PRESENT_ATTR_ID(32),
	TRANSCEIVER_PRESENT_ATTR_ID(33),
	TRANSCEIVER_PRESENT_ATTR_ID(34),
	TRANSCEIVER_PRESENT_ATTR_ID(35),
	TRANSCEIVER_PRESENT_ATTR_ID(36),
	TRANSCEIVER_PRESENT_ATTR_ID(37),
	TRANSCEIVER_PRESENT_ATTR_ID(38),
	TRANSCEIVER_PRESENT_ATTR_ID(39),
	TRANSCEIVER_PRESENT_ATTR_ID(40),
	TRANSCEIVER_PRESENT_ATTR_ID(41),
	TRANSCEIVER_PRESENT_ATTR_ID(42),
	TRANSCEIVER_PRESENT_ATTR_ID(43),
	TRANSCEIVER_PRESENT_ATTR_ID(44),
	TRANSCEIVER_PRESENT_ATTR_ID(45),
	TRANSCEIVER_PRESENT_ATTR_ID(46),
	TRANSCEIVER_PRESENT_ATTR_ID(47),
	TRANSCEIVER_PRESENT_ATTR_ID(48),
	TRANSCEIVER_PRESENT_ATTR_ID(49),
	TRANSCEIVER_PRESENT_ATTR_ID(50),
	TRANSCEIVER_PRESENT_ATTR_ID(51),
	TRANSCEIVER_PRESENT_ATTR_ID(52),
	TRANSCEIVER_PRESENT_ATTR_ID(53),
	TRANSCEIVER_PRESENT_ATTR_ID(54),
	TRANSCEIVER_PRESENT_ATTR_ID(55),
	TRANSCEIVER_PRESENT_ATTR_ID(56),
	TRANSCEIVER_TXDISABLE_ATTR_ID(1),
	TRANSCEIVER_TXDISABLE_ATTR_ID(2),
	TRANSCEIVER_TXDISABLE_ATTR_ID(3),
	TRANSCEIVER_TXDISABLE_ATTR_ID(4),
	TRANSCEIVER_TXDISABLE_ATTR_ID(5),
	TRANSCEIVER_TXDISABLE_ATTR_ID(6),
	TRANSCEIVER_TXDISABLE_ATTR_ID(7),
	TRANSCEIVER_TXDISABLE_ATTR_ID(8),
	TRANSCEIVER_TXDISABLE_ATTR_ID(9),
	TRANSCEIVER_TXDISABLE_ATTR_ID(10),
	TRANSCEIVER_TXDISABLE_ATTR_ID(11),
	TRANSCEIVER_TXDISABLE_ATTR_ID(12),
	TRANSCEIVER_TXDISABLE_ATTR_ID(13),
	TRANSCEIVER_TXDISABLE_ATTR_ID(14),
	TRANSCEIVER_TXDISABLE_ATTR_ID(15),
	TRANSCEIVER_TXDISABLE_ATTR_ID(16),
	TRANSCEIVER_TXDISABLE_ATTR_ID(17),
	TRANSCEIVER_TXDISABLE_ATTR_ID(18),
	TRANSCEIVER_TXDISABLE_ATTR_ID(19),
	TRANSCEIVER_TXDISABLE_ATTR_ID(20),
	TRANSCEIVER_TXDISABLE_ATTR_ID(21),
	TRANSCEIVER_TXDISABLE_ATTR_ID(22),
	TRANSCEIVER_TXDISABLE_ATTR_ID(23),
	TRANSCEIVER_TXDISABLE_ATTR_ID(24),
	TRANSCEIVER_TXDISABLE_ATTR_ID(25),
	TRANSCEIVER_TXDISABLE_ATTR_ID(26),
	TRANSCEIVER_TXDISABLE_ATTR_ID(27),
	TRANSCEIVER_TXDISABLE_ATTR_ID(28),
	TRANSCEIVER_TXDISABLE_ATTR_ID(29),
	TRANSCEIVER_TXDISABLE_ATTR_ID(30),
	TRANSCEIVER_TXDISABLE_ATTR_ID(31),
	TRANSCEIVER_TXDISABLE_ATTR_ID(32),
	TRANSCEIVER_TXDISABLE_ATTR_ID(33),
	TRANSCEIVER_TXDISABLE_ATTR_ID(34),
	TRANSCEIVER_TXDISABLE_ATTR_ID(35),
	TRANSCEIVER_TXDISABLE_ATTR_ID(36),
	TRANSCEIVER_TXDISABLE_ATTR_ID(37),
	TRANSCEIVER_TXDISABLE_ATTR_ID(38),
	TRANSCEIVER_TXDISABLE_ATTR_ID(39),
	TRANSCEIVER_TXDISABLE_ATTR_ID(40),
	TRANSCEIVER_TXDISABLE_ATTR_ID(41),
	TRANSCEIVER_TXDISABLE_ATTR_ID(42),
	TRANSCEIVER_TXDISABLE_ATTR_ID(43),
	TRANSCEIVER_TXDISABLE_ATTR_ID(44),
	TRANSCEIVER_TXDISABLE_ATTR_ID(45),
	TRANSCEIVER_TXDISABLE_ATTR_ID(46),
	TRANSCEIVER_TXDISABLE_ATTR_ID(47),
	TRANSCEIVER_TXDISABLE_ATTR_ID(48),
	TRANSCEIVER_RXLOS_ATTR_ID(1),
	TRANSCEIVER_RXLOS_ATTR_ID(2),
	TRANSCEIVER_RXLOS_ATTR_ID(3),
	TRANSCEIVER_RXLOS_ATTR_ID(4),
	TRANSCEIVER_RXLOS_ATTR_ID(5),
	TRANSCEIVER_RXLOS_ATTR_ID(6),
	TRANSCEIVER_RXLOS_ATTR_ID(7),
	TRANSCEIVER_RXLOS_ATTR_ID(8),
	TRANSCEIVER_RXLOS_ATTR_ID(9),
	TRANSCEIVER_RXLOS_ATTR_ID(10),
	TRANSCEIVER_RXLOS_ATTR_ID(11),
	TRANSCEIVER_RXLOS_ATTR_ID(12),
	TRANSCEIVER_RXLOS_ATTR_ID(13),
	TRANSCEIVER_RXLOS_ATTR_ID(14),
	TRANSCEIVER_RXLOS_ATTR_ID(15),
	TRANSCEIVER_RXLOS_ATTR_ID(16),
	TRANSCEIVER_RXLOS_ATTR_ID(17),
	TRANSCEIVER_RXLOS_ATTR_ID(18),
	TRANSCEIVER_RXLOS_ATTR_ID(19),
	TRANSCEIVER_RXLOS_ATTR_ID(20),
	TRANSCEIVER_RXLOS_ATTR_ID(21),
	TRANSCEIVER_RXLOS_ATTR_ID(22),
	TRANSCEIVER_RXLOS_ATTR_ID(23),
	TRANSCEIVER_RXLOS_ATTR_ID(24),
	TRANSCEIVER_RXLOS_ATTR_ID(25),
	TRANSCEIVER_RXLOS_ATTR_ID(26),
	TRANSCEIVER_RXLOS_ATTR_ID(27),
	TRANSCEIVER_RXLOS_ATTR_ID(28),
	TRANSCEIVER_RXLOS_ATTR_ID(29),
	TRANSCEIVER_RXLOS_ATTR_ID(30),
	TRANSCEIVER_RXLOS_ATTR_ID(31),
	TRANSCEIVER_RXLOS_ATTR_ID(32),
	TRANSCEIVER_RXLOS_ATTR_ID(33),
	TRANSCEIVER_RXLOS_ATTR_ID(34),
	TRANSCEIVER_RXLOS_ATTR_ID(35),
	TRANSCEIVER_RXLOS_ATTR_ID(36),
	TRANSCEIVER_RXLOS_ATTR_ID(37),
	TRANSCEIVER_RXLOS_ATTR_ID(38),
	TRANSCEIVER_RXLOS_ATTR_ID(39),
	TRANSCEIVER_RXLOS_ATTR_ID(40),
	TRANSCEIVER_RXLOS_ATTR_ID(41),
	TRANSCEIVER_RXLOS_ATTR_ID(42),
	TRANSCEIVER_RXLOS_ATTR_ID(43),
	TRANSCEIVER_RXLOS_ATTR_ID(44),
	TRANSCEIVER_RXLOS_ATTR_ID(45),
	TRANSCEIVER_RXLOS_ATTR_ID(46),
	TRANSCEIVER_RXLOS_ATTR_ID(47),
	TRANSCEIVER_RXLOS_ATTR_ID(48),
	TRANSCEIVER_TXFAULT_ATTR_ID(1),
	TRANSCEIVER_TXFAULT_ATTR_ID(2),
	TRANSCEIVER_TXFAULT_ATTR_ID(3),
	TRANSCEIVER_TXFAULT_ATTR_ID(4),
	TRANSCEIVER_TXFAULT_ATTR_ID(5),
	TRANSCEIVER_TXFAULT_ATTR_ID(6),
	TRANSCEIVER_TXFAULT_ATTR_ID(7),
	TRANSCEIVER_TXFAULT_ATTR_ID(8),
	TRANSCEIVER_TXFAULT_ATTR_ID(9),
	TRANSCEIVER_TXFAULT_ATTR_ID(10),
	TRANSCEIVER_TXFAULT_ATTR_ID(11),
	TRANSCEIVER_TXFAULT_ATTR_ID(12),
	TRANSCEIVER_TXFAULT_ATTR_ID(13),
	TRANSCEIVER_TXFAULT_ATTR_ID(14),
	TRANSCEIVER_TXFAULT_ATTR_ID(15),
	TRANSCEIVER_TXFAULT_ATTR_ID(16),
	TRANSCEIVER_TXFAULT_ATTR_ID(17),
	TRANSCEIVER_TXFAULT_ATTR_ID(18),
	TRANSCEIVER_TXFAULT_ATTR_ID(19),
	TRANSCEIVER_TXFAULT_ATTR_ID(20),
	TRANSCEIVER_TXFAULT_ATTR_ID(21),
	TRANSCEIVER_TXFAULT_ATTR_ID(22),
	TRANSCEIVER_TXFAULT_ATTR_ID(23),
	TRANSCEIVER_TXFAULT_ATTR_ID(24),
	TRANSCEIVER_TXFAULT_ATTR_ID(25),
	TRANSCEIVER_TXFAULT_ATTR_ID(26),
	TRANSCEIVER_TXFAULT_ATTR_ID(27),
	TRANSCEIVER_TXFAULT_ATTR_ID(28),
	TRANSCEIVER_TXFAULT_ATTR_ID(29),
	TRANSCEIVER_TXFAULT_ATTR_ID(30),
	TRANSCEIVER_TXFAULT_ATTR_ID(31),
	TRANSCEIVER_TXFAULT_ATTR_ID(32),
	TRANSCEIVER_TXFAULT_ATTR_ID(33),
	TRANSCEIVER_TXFAULT_ATTR_ID(34),
	TRANSCEIVER_TXFAULT_ATTR_ID(35),
	TRANSCEIVER_TXFAULT_ATTR_ID(36),
	TRANSCEIVER_TXFAULT_ATTR_ID(37),
	TRANSCEIVER_TXFAULT_ATTR_ID(38),
	TRANSCEIVER_TXFAULT_ATTR_ID(39),
	TRANSCEIVER_TXFAULT_ATTR_ID(40),
	TRANSCEIVER_TXFAULT_ATTR_ID(41),
	TRANSCEIVER_TXFAULT_ATTR_ID(42),
	TRANSCEIVER_TXFAULT_ATTR_ID(43),
	TRANSCEIVER_TXFAULT_ATTR_ID(44),
	TRANSCEIVER_TXFAULT_ATTR_ID(45),
	TRANSCEIVER_TXFAULT_ATTR_ID(46),
	TRANSCEIVER_TXFAULT_ATTR_ID(47),
	TRANSCEIVER_TXFAULT_ATTR_ID(48),
	TRANSCEIVER_RESET_ATTR_ID(49),
	TRANSCEIVER_RESET_ATTR_ID(50),
	TRANSCEIVER_RESET_ATTR_ID(51),
	TRANSCEIVER_RESET_ATTR_ID(52),
	TRANSCEIVER_RESET_ATTR_ID(53),
	TRANSCEIVER_RESET_ATTR_ID(54),
	TRANSCEIVER_RESET_ATTR_ID(55),
	TRANSCEIVER_RESET_ATTR_ID(56),
	TRANSCEIVER_LPMODE_ATTR_ID(49),
	TRANSCEIVER_LPMODE_ATTR_ID(50),
	TRANSCEIVER_LPMODE_ATTR_ID(51),
	TRANSCEIVER_LPMODE_ATTR_ID(52),
	TRANSCEIVER_LPMODE_ATTR_ID(53),
	TRANSCEIVER_LPMODE_ATTR_ID(54),
	TRANSCEIVER_LPMODE_ATTR_ID(55),
	TRANSCEIVER_LPMODE_ATTR_ID(56),
	FAN_PRESENT_ATTR_ID(1),
	FAN_PRESENT_ATTR_ID(2),
	FAN_DIRECTION_ID(1),
	FAN_DIRECTION_ID(2),
	FAN_FRONT_SPEED_RPM_ATTR_ID(1),
	FAN_FRONT_SPEED_RPM_ATTR_ID(2),
	FAN_FRONT_SPEED_RPM_ATTR_ID(3),
	FAN_FRONT_SPEED_RPM_ATTR_ID(4),
	FAN_REAR_SPEED_RPM_ATTR_ID(1),
	FAN_REAR_SPEED_RPM_ATTR_ID(2),
	FAN_REAR_SPEED_RPM_ATTR_ID(3),
	FAN_REAR_SPEED_RPM_ATTR_ID(4),
	FAN_DUTY_CYCLE_PERCENTAGE,
	FAN_WDT_ENABLE,
	FAN_WDT_CLEAR,
	FAN_WDT_COUNT,
	SYSLED_WDT_CLEAR
};

/* sysfs attributes for hwmon 
 */
static ssize_t show_status(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_present_all(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t show_rxlos_all(struct device *dev, struct device_attribute *da,
             char *buf);
static ssize_t set_tx_disable(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count);
static ssize_t set_qsfp(struct device *dev, struct device_attribute *da,
                        const char *buf, size_t count);
static ssize_t access(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count);
static ssize_t show_version(struct device *dev, struct device_attribute *da,
             char *buf);
static int as7327_56x_cpld_read_internal(struct i2c_client *client, u8 reg);
static int as7327_56x_cpld_write_internal(struct i2c_client *client, u8 reg, u8 value);


/*fan sysfs*/
static struct as7327_56x_cpld_data *as7327_56x_fan_update_device(struct device *dev);
static ssize_t fan_show_value(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t set_duty_cycle(struct device *dev, struct device_attribute *da,
                              const char *buf, size_t count);
static ssize_t set_fan_wdt_status(struct device *dev, struct device_attribute *da,
                                const char *buf, size_t count);
static ssize_t set_fan_wdt_clear(struct device *dev, struct device_attribute *da,
                                const char *buf, size_t count);
static ssize_t set_fan_wdt_count(struct device *dev, struct device_attribute *da,
                                const char *buf, size_t count);

/*sysled wdt sysfs*/
static ssize_t sysled_wdt_show_value(struct device *dev, struct device_attribute *da, char *buf);
static ssize_t set_sysled_wdt_clear(struct device *dev, struct device_attribute *da,
                                    const char *buf, size_t count);

/* transceiver attributes */
#define DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(index) \
	static SENSOR_DEVICE_ATTR(module_present_##index, S_IRUGO, show_status, NULL, MODULE_PRESENT_##index); \
	static SENSOR_DEVICE_ATTR(module_tx_disable_##index, S_IRUGO | S_IWUSR, show_status, set_tx_disable, MODULE_TXDISABLE_##index); \
	static SENSOR_DEVICE_ATTR(module_rx_los_##index, S_IRUGO, show_status, NULL, MODULE_RXLOS_##index); \
	static SENSOR_DEVICE_ATTR(module_tx_fault_##index, S_IRUGO, show_status, NULL, MODULE_TXFAULT_##index)
#define DECLARE_SFP_TRANSCEIVER_ATTR(index)  \
    &sensor_dev_attr_module_present_##index.dev_attr.attr, \
	&sensor_dev_attr_module_tx_disable_##index.dev_attr.attr, \
	&sensor_dev_attr_module_rx_los_##index.dev_attr.attr, \
	&sensor_dev_attr_module_tx_fault_##index.dev_attr.attr
	
#define DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(index) \
    static SENSOR_DEVICE_ATTR(module_lpmode_##index, S_IRUGO | S_IWUSR, show_status, set_qsfp, MODULE_LPMODE_##index); \
    static SENSOR_DEVICE_ATTR(module_reset_##index, S_IRUGO | S_IWUSR, show_status, set_qsfp, MODULE_RESET_##index); \
    static SENSOR_DEVICE_ATTR(module_present_##index, S_IRUGO, show_status, NULL, MODULE_PRESENT_##index);

#define DECLARE_QSFP_TRANSCEIVER_ATTR(index)  \
    &sensor_dev_attr_module_lpmode_##index.dev_attr.attr, \
    &sensor_dev_attr_module_reset_##index.dev_attr.attr, \
    &sensor_dev_attr_module_present_##index.dev_attr.attr

#define DECLARE_FAN_SENSOR_DEV_ATTR(index) \
    static SENSOR_DEVICE_ATTR(fan_present_##index, S_IRUGO, fan_show_value, NULL, FAN_PRESENT_##index); \
	static SENSOR_DEVICE_ATTR(fan_direction_##index, S_IRUGO, fan_show_value, NULL, FAN_DIRECTION_##index);
#define DECLARE_FAN_BOX_ATTR(index)  \
    &sensor_dev_attr_fan_present_##index.dev_attr.attr, \
	&sensor_dev_attr_fan_direction_##index.dev_attr.attr

#define DECLARE_FAN_SENSOR_ATTR1(index) \
    static SENSOR_DEVICE_ATTR(fan_front_speed_rpm_##index, S_IRUGO, fan_show_value, NULL, FAN_FRONT_SPEED_RPM_##index);
#define DECLARE_FAN_FRONT_ATTR(index)  \
    &sensor_dev_attr_fan_front_speed_rpm_##index.dev_attr.attr

#define DECLARE_FAN_SENSOR_ATTR2(index) \
    static SENSOR_DEVICE_ATTR(fan_rear_speed_rpm_##index, S_IRUGO, fan_show_value, NULL, FAN_REAR_SPEED_RPM_##index);
#define DECLARE_FAN_REAR_ATTR(index)  \
    &sensor_dev_attr_fan_rear_speed_rpm_##index.dev_attr.attr

#define DECLARE_FAN_DUTY_CYCLE_SENSOR_DEV_ATTR(index) \
    static SENSOR_DEVICE_ATTR(fan_duty_cycle_percentage, S_IWUSR | S_IRUGO, fan_show_value, set_duty_cycle, FAN_DUTY_CYCLE_PERCENTAGE);
#define DECLARE_FAN_DUTY_CYCLE_ATTR(index) \
    &sensor_dev_attr_fan_duty_cycle_percentage.dev_attr.attr

#define DECLARE_FAN_WDT_SENSOR_DEV_ATTR() \
    static SENSOR_DEVICE_ATTR(fan_wdt_status, S_IWUSR | S_IRUGO, fan_show_value, set_fan_wdt_status, FAN_WDT_ENABLE);\
    static SENSOR_DEVICE_ATTR(fan_wdt_clear, S_IWUSR | S_IRUGO, fan_show_value, set_fan_wdt_clear, FAN_WDT_CLEAR);\
    static SENSOR_DEVICE_ATTR(fan_wdt_count, S_IWUSR | S_IRUGO, fan_show_value, set_fan_wdt_count, FAN_WDT_COUNT)
#define DECLARE_FAN_WDT_STATUS_ATTR() &sensor_dev_attr_fan_wdt_status.dev_attr.attr
#define DECLARE_FAN_WDT_CLEAR_ATTR() &sensor_dev_attr_fan_wdt_clear.dev_attr.attr
#define DECLARE_FAN_WDT_COUNT_ATTR() &sensor_dev_attr_fan_wdt_count.dev_attr.attr

#define DECLARE_SYSLED_WDT_SENSOR_DEV_ATTR() \
    static SENSOR_DEVICE_ATTR(sysled_wdt_clear, S_IWUSR | S_IRUGO, sysled_wdt_show_value, set_sysled_wdt_clear, SYSLED_WDT_CLEAR);
#define DECLARE_SYSLED_WDT_CLEAR_ATTR() \
    &sensor_dev_attr_sysled_wdt_clear.dev_attr.attr

static SENSOR_DEVICE_ATTR(version, S_IRUGO, show_version, NULL, CPLD_VERSION);
static SENSOR_DEVICE_ATTR(access, S_IWUSR, NULL, access, ACCESS);
/* transceiver attributes */
static SENSOR_DEVICE_ATTR(module_present_all, S_IRUGO, show_present_all, NULL, MODULE_PRESENT_ALL);
static SENSOR_DEVICE_ATTR(module_rx_los_all, S_IRUGO, show_rxlos_all, NULL, MODULE_RXLOS_ALL);

DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(1);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(2);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(3);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(4);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(5);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(6);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(7);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(8);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(9);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(10);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(11);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(12);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(13);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(14);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(15);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(16);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(17);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(18);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(19);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(20);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(21);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(22);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(23);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(24);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(25);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(26);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(27);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(28);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(29);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(30);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(31);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(32);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(33);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(34);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(35);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(36);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(37);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(38);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(39);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(40);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(41);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(42);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(43);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(44);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(45);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(46);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(47);
DECLARE_SFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(48);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(49);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(50);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(51);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(52);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(53);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(54);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(55);
DECLARE_QSFP_TRANSCEIVER_SENSOR_DEVICE_ATTR(56);

/* fan attributes */
DECLARE_FAN_SENSOR_DEV_ATTR(1);
DECLARE_FAN_SENSOR_DEV_ATTR(2);
DECLARE_FAN_SENSOR_ATTR1(1);
DECLARE_FAN_SENSOR_ATTR1(2);
DECLARE_FAN_SENSOR_ATTR1(3);
DECLARE_FAN_SENSOR_ATTR1(4);
DECLARE_FAN_SENSOR_ATTR2(1);
DECLARE_FAN_SENSOR_ATTR2(2);
DECLARE_FAN_SENSOR_ATTR2(3);
DECLARE_FAN_SENSOR_ATTR2(4);
DECLARE_FAN_DUTY_CYCLE_SENSOR_DEV_ATTR(1);
DECLARE_FAN_WDT_SENSOR_DEV_ATTR();

/* sysled wdt attribute */
DECLARE_SYSLED_WDT_SENSOR_DEV_ATTR();

static struct attribute *as7327_56x_cpld1_attributes[] = {
    &sensor_dev_attr_version.dev_attr.attr,
    &sensor_dev_attr_access.dev_attr.attr,
	/* transceiver attributes */
	&sensor_dev_attr_module_present_all.dev_attr.attr,
	&sensor_dev_attr_module_rx_los_all.dev_attr.attr,
	DECLARE_SFP_TRANSCEIVER_ATTR(1),
	DECLARE_SFP_TRANSCEIVER_ATTR(2),
	DECLARE_SFP_TRANSCEIVER_ATTR(3),
	DECLARE_SFP_TRANSCEIVER_ATTR(4),
	DECLARE_SFP_TRANSCEIVER_ATTR(5),
	DECLARE_SFP_TRANSCEIVER_ATTR(6),
	DECLARE_SFP_TRANSCEIVER_ATTR(7),
	DECLARE_SFP_TRANSCEIVER_ATTR(8),
	DECLARE_SFP_TRANSCEIVER_ATTR(9),
	DECLARE_SFP_TRANSCEIVER_ATTR(10),
	DECLARE_SFP_TRANSCEIVER_ATTR(11),
	DECLARE_SFP_TRANSCEIVER_ATTR(12),
	DECLARE_FAN_BOX_ATTR(1),
	DECLARE_FAN_BOX_ATTR(2),
	DECLARE_FAN_FRONT_ATTR(1),
	DECLARE_FAN_FRONT_ATTR(2),
	DECLARE_FAN_FRONT_ATTR(3),
	DECLARE_FAN_FRONT_ATTR(4),
	DECLARE_FAN_REAR_ATTR(1),
	DECLARE_FAN_REAR_ATTR(2),
	DECLARE_FAN_REAR_ATTR(3),
	DECLARE_FAN_REAR_ATTR(4),
	DECLARE_FAN_DUTY_CYCLE_ATTR(1),
	DECLARE_FAN_WDT_STATUS_ATTR(),
	DECLARE_FAN_WDT_CLEAR_ATTR(),
	DECLARE_FAN_WDT_COUNT_ATTR(),
	DECLARE_SYSLED_WDT_CLEAR_ATTR(),
	NULL
};

static const struct attribute_group as7327_56x_cpld1_group = {
	.attrs = as7327_56x_cpld1_attributes,
};

static struct attribute *as7327_56x_cpld2_attributes[] = {
    &sensor_dev_attr_version.dev_attr.attr,
    &sensor_dev_attr_access.dev_attr.attr,
	/* transceiver attributes */
	&sensor_dev_attr_module_present_all.dev_attr.attr,
	&sensor_dev_attr_module_rx_los_all.dev_attr.attr,
	DECLARE_SFP_TRANSCEIVER_ATTR(13),
	DECLARE_SFP_TRANSCEIVER_ATTR(14),
	DECLARE_SFP_TRANSCEIVER_ATTR(15),
	DECLARE_SFP_TRANSCEIVER_ATTR(16),
	DECLARE_SFP_TRANSCEIVER_ATTR(17),
	DECLARE_SFP_TRANSCEIVER_ATTR(18),
	DECLARE_SFP_TRANSCEIVER_ATTR(19),
	DECLARE_SFP_TRANSCEIVER_ATTR(20),
	DECLARE_SFP_TRANSCEIVER_ATTR(21),
	DECLARE_SFP_TRANSCEIVER_ATTR(22),
	DECLARE_SFP_TRANSCEIVER_ATTR(23),
	DECLARE_SFP_TRANSCEIVER_ATTR(24),
    DECLARE_SFP_TRANSCEIVER_ATTR(25),
	DECLARE_SFP_TRANSCEIVER_ATTR(26),
	DECLARE_SFP_TRANSCEIVER_ATTR(27),
	DECLARE_SFP_TRANSCEIVER_ATTR(28),
	DECLARE_SFP_TRANSCEIVER_ATTR(29),
	DECLARE_SFP_TRANSCEIVER_ATTR(30),
	DECLARE_SFP_TRANSCEIVER_ATTR(31),
	DECLARE_SFP_TRANSCEIVER_ATTR(32),
	DECLARE_SFP_TRANSCEIVER_ATTR(33),
	DECLARE_SFP_TRANSCEIVER_ATTR(34),
	DECLARE_SFP_TRANSCEIVER_ATTR(35),
	DECLARE_SFP_TRANSCEIVER_ATTR(36),
	DECLARE_SFP_TRANSCEIVER_ATTR(37),
	DECLARE_SFP_TRANSCEIVER_ATTR(38),
	DECLARE_SFP_TRANSCEIVER_ATTR(39),
	DECLARE_SFP_TRANSCEIVER_ATTR(40),
	DECLARE_SFP_TRANSCEIVER_ATTR(41),
	DECLARE_SFP_TRANSCEIVER_ATTR(42),
	DECLARE_SFP_TRANSCEIVER_ATTR(43),
	DECLARE_SFP_TRANSCEIVER_ATTR(44),
	DECLARE_SFP_TRANSCEIVER_ATTR(45),
	DECLARE_SFP_TRANSCEIVER_ATTR(46),
	DECLARE_SFP_TRANSCEIVER_ATTR(47),
	DECLARE_SFP_TRANSCEIVER_ATTR(48),
	DECLARE_QSFP_TRANSCEIVER_ATTR(49),
	DECLARE_QSFP_TRANSCEIVER_ATTR(50),
	DECLARE_QSFP_TRANSCEIVER_ATTR(51),
	DECLARE_QSFP_TRANSCEIVER_ATTR(52),
	DECLARE_QSFP_TRANSCEIVER_ATTR(53),
	DECLARE_QSFP_TRANSCEIVER_ATTR(54),
	DECLARE_QSFP_TRANSCEIVER_ATTR(55),
	DECLARE_QSFP_TRANSCEIVER_ATTR(56),
	NULL
};

static const struct attribute_group as7327_56x_cpld2_group = {
	.attrs = as7327_56x_cpld2_attributes,
};

static ssize_t show_present_all(struct device *dev, struct device_attribute *da,
             char *buf)
{
    int i, status;
    u8 values[6]  = {0};
    u8 regs_cpld1[] = {0x17, 0x18};
    u8 regs_cpld2[] = {0xf, 0x10, 0x11, 0x12, 0x13, 0x1f};
    u8 *regs[] = {regs_cpld1, regs_cpld2};
    u8  size[] = {ARRAY_SIZE(regs_cpld1), ARRAY_SIZE(regs_cpld2)};
    struct i2c_client *client = to_i2c_client(dev);
    struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);

    mutex_lock(&data->update_lock);

    for (i = 0; i < size[data->type]; i++) {
        status = as7327_56x_cpld_read_internal(client, regs[data->type][i]);
        if (status < 0) {
            goto exit;
        }

        values[i] = ~(u8)status;
    }

    mutex_unlock(&data->update_lock);

    /* Return values in order */
    if (data->type == as7327_56x_cpld1) {
        values[1] &= 0xF;
        return sprintf(buf, "%.2x %.2x\n",
                             values[0], values[1]);
    }
    else { /* as7327_56x_cpld2 */
        values[4] &= 0xf;
        return sprintf(buf, "%.2x %.2x %.2x %.2x %.2x %.2x\n",
                             values[0], values[1], values[2], values[3], values[4], values[5]);
    }

exit:
    mutex_unlock(&data->update_lock);
    return status;
}

static ssize_t show_rxlos_all(struct device *dev, struct device_attribute *da,
             char *buf)
{
    int i, status;
    u8 values[5]  = {0};
    u8 regs_cpld1[] = {0x15, 0x16};
    u8 regs_cpld2[] = {0xa, 0xb, 0xc, 0xd, 0xe};
    u8 *regs[] = {regs_cpld1, regs_cpld2};
    u8  size[] = {ARRAY_SIZE(regs_cpld1), ARRAY_SIZE(regs_cpld2)};
    struct i2c_client *client = to_i2c_client(dev);
    struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);

    mutex_lock(&data->update_lock);

    for (i = 0; i < size[data->type]; i++) {
        status = as7327_56x_cpld_read_internal(client, regs[data->type][i]);
        if (status < 0) {
            goto exit;
        }

        values[i] = (u8)status;
    }

    mutex_unlock(&data->update_lock);

    /* Return values in order */
    if (data->type == as7327_56x_cpld1) {
        values[1] &= 0xF;
        return sprintf(buf, "%.2x %.2x\n",
                             values[0], values[1]);
    }
    else { /* as7327_56x_cpld2 */
        values[4] &= 0xF;
        return sprintf(buf, "%.2x %.2x %.2x %.2x %.2x\n",
                             values[0], values[1], values[2], values[3], values[4]);
    }

exit:
    mutex_unlock(&data->update_lock);
    return status;
}

static ssize_t show_status(struct device *dev, struct device_attribute *da,
             char *buf)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);
	int status = 0;
	u8 reg = 0, mask = 0, revert = 0;

	switch (attr->index) {
	case MODULE_PRESENT_1 ... MODULE_PRESENT_12:
		reg  = 0x17 + (attr->index-MODULE_PRESENT_1)/8;
		mask = 0x1 << ((attr->index - MODULE_PRESENT_1)%8);
		break;
	case MODULE_PRESENT_13 ... MODULE_PRESENT_48:
		reg  = 0xf + (attr->index-MODULE_PRESENT_13)/8;
		mask = 0x1 << ((attr->index - MODULE_PRESENT_13)%8);
		break;
	case MODULE_PRESENT_49 ... MODULE_PRESENT_56:   /*QSFP*/
		reg  = 0x1f ;
		mask = 0x1 << ((attr->index - MODULE_PRESENT_49)%8);
		break;
	case MODULE_TXFAULT_1 ... MODULE_TXFAULT_12:
		reg  = 0x19 + (attr->index - MODULE_TXFAULT_1)/8;
		mask = 0x1 << ((attr->index - MODULE_TXFAULT_1)%8);
		break;
	case MODULE_TXFAULT_13 ... MODULE_TXFAULT_48:
		reg  = 0x14 + (attr->index-MODULE_TXFAULT_13)/8;
		mask = 0x1 << ((attr->index - MODULE_TXFAULT_13)%8);
		break;
	case MODULE_TXDISABLE_1 ... MODULE_TXDISABLE_12:
		reg  = 0x1b + (attr->index - MODULE_TXDISABLE_1)/8;
		mask = 0x1 << ((attr->index - MODULE_TXDISABLE_1)%8);
		break;
	case MODULE_TXDISABLE_13 ... MODULE_TXDISABLE_48:
		reg  = 0x19 + (attr->index-MODULE_TXDISABLE_13)/8;
		mask = 0x1 << ((attr->index - MODULE_TXDISABLE_13)%8);
		break;
	case MODULE_RXLOS_1 ... MODULE_RXLOS_12:
		reg  = 0x15 + (attr->index - MODULE_RXLOS_1)/8;
		mask = 0x1 << ((attr->index - MODULE_RXLOS_1)%8);
		break;
	case MODULE_RXLOS_13 ... MODULE_RXLOS_48:
		reg  = 0xa + (attr->index-MODULE_RXLOS_13)/8;
		mask = 0x1 << ((attr->index - MODULE_RXLOS_13)%8);
		break;
    case MODULE_RESET_49 ... MODULE_RESET_56:
        reg  = 0x20 ;
        mask = 0x1 << ((attr->index - MODULE_RESET_49)%8);
        revert = 1;
        break;
    case MODULE_LPMODE_49 ... MODULE_LPMODE_56:
        reg  = 0x21 ;
        mask = 0x1 << ((attr->index - MODULE_LPMODE_49)%8);
        revert = 0;
        break;
	default:
		return 0;
	}

    if (attr->index >= MODULE_PRESENT_1 && attr->index <= MODULE_PRESENT_56) {
        revert = 1;
    }

    mutex_lock(&data->update_lock);
	status = as7327_56x_cpld_read_internal(client, reg);
	if (unlikely(status < 0)) {
		goto exit;
	}
	mutex_unlock(&data->update_lock);

	return sprintf(buf, "%d\n", revert ? !(status & mask) : !!(status & mask));

exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static ssize_t set_qsfp(struct device *dev, struct device_attribute *da,
                        const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);
	long disable;
	int status;
    u8 reg = 0, mask = 0, revert = 0;

	status = kstrtol(buf, 10, &disable);
	if (status) {
		return status;
	}

	switch (attr->index) {
	case MODULE_RESET_49 ... MODULE_RESET_56:
		reg  = 0x20;
		mask = 0x1 << ((attr->index - MODULE_LPMODE_49)%8);
        revert = 1;
		break;
	case MODULE_LPMODE_49 ... MODULE_LPMODE_56:
		reg  = 0x21;
		mask = 0x1 << ((attr->index - MODULE_LPMODE_49)%8);
        revert = 0;
		break;
	default:
		return 0;
	}

    disable = revert ? disable : !disable;

    /* Read current status */
    mutex_lock(&data->update_lock);
	status = as7327_56x_cpld_read_internal(client, reg);
	if (unlikely(status < 0)) {
		goto exit;
	}

	/* Update status */
	if (disable) {
        status &= ~mask;
    }
    else {
        status |= mask;
	}

    status = as7327_56x_cpld_write_internal(client, reg, status);
	if (unlikely(status < 0)) {
		goto exit;
	}
    
    mutex_unlock(&data->update_lock);
    return count;

exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static ssize_t set_tx_disable(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);
	long disable;
	int status;
    u8 reg = 0, mask = 0;

	status = kstrtol(buf, 10, &disable);
	if (status) {
		return status;
	}

	switch (attr->index) {
	case MODULE_TXDISABLE_1 ... MODULE_TXDISABLE_12:
		reg  = 0x1b + (attr->index - MODULE_TXDISABLE_1)/8;
		mask = 0x1 << ((attr->index - MODULE_TXDISABLE_1)%8);
		break;
	case MODULE_TXDISABLE_13 ... MODULE_TXDISABLE_48:
		reg  = 0x19 + (attr->index - MODULE_TXDISABLE_13)/8;
		mask = 0x1 << ((attr->index - MODULE_TXDISABLE_13)%8);
		break;
	default:
		return 0;
	}

    /* Read current status */
    mutex_lock(&data->update_lock);
	status = as7327_56x_cpld_read_internal(client, reg);
	if (unlikely(status < 0)) {
		goto exit;
	}

	/* Update tx_disable status */
	if (disable) {
		status |= mask;
	}
	else {
		status &= ~mask;
	}

        status = as7327_56x_cpld_write_internal(client, reg, status);
	if (unlikely(status < 0)) {
		goto exit;
	}
    
    mutex_unlock(&data->update_lock);
    return count;

exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static ssize_t access(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	int status;
	u32 addr, val;
    struct i2c_client *client = to_i2c_client(dev);
    struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);

	if (sscanf(buf, "0x%x 0x%x", &addr, &val) != 2) {
		return -EINVAL;
	}

	if (addr > 0xFF || val > 0xFF) {
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	status = as7327_56x_cpld_write_internal(client, addr, val);
	if (unlikely(status < 0)) {
		goto exit;
	}
	mutex_unlock(&data->update_lock);
	return count;

exit:
	mutex_unlock(&data->update_lock);
	return status;
}

static void as7327_56x_cpld_add_client(struct i2c_client *client)
{
    struct cpld_client_node *node = kzalloc(sizeof(struct cpld_client_node), GFP_KERNEL);

    if (!node) {
        dev_dbg(&client->dev, "Can't allocate cpld_client_node (0x%x)\n", client->addr);
        return;
    }

    node->client = client;

	mutex_lock(&list_lock);
    list_add(&node->list, &cpld_client_list);
	mutex_unlock(&list_lock);
}

static void as7327_56x_cpld_remove_client(struct i2c_client *client)
{
    struct list_head    *list_node = NULL;
    struct cpld_client_node *cpld_node = NULL;
    int found = 0;

	mutex_lock(&list_lock);

    list_for_each(list_node, &cpld_client_list)
    {
        cpld_node = list_entry(list_node, struct cpld_client_node, list);

        if (cpld_node->client == client) {
            found = 1;
            break;
        }
    }

    if (found) {
        list_del(list_node);
        kfree(cpld_node);
    }

	mutex_unlock(&list_lock);
}

static ssize_t show_version(struct device *dev, struct device_attribute *attr, char *buf)
{
    int major = 0, minor = 0;
    struct i2c_client *client = to_i2c_client(dev);
	
	major = i2c_smbus_read_byte_data(client, 0x0);

    if (major < 0) {
        dev_dbg(&client->dev, "cpld(0x%x) reg(0x0) err %d\n", client->addr, major);
    }

	minor = i2c_smbus_read_byte_data(client, 0x1);

    if (minor < 0) {
        dev_dbg(&client->dev, "cpld(0x%x) reg(0x1) err %d\n", client->addr, minor);
    }

    return sprintf(buf, "%x.%x", major, minor);
}

static struct as7327_56x_cpld_data *as7327_56x_fan_update_device(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);

    mutex_lock(&data->update_lock);

    if (time_after(jiffies, data->last_updated + HZ + HZ / 2) ||
            !data->valid) {
        int i;

        dev_dbg(&client->dev, "Starting as7327_56x_fan update\n");
        data->valid = 0;

        /* Update fan data
         */
        for (i = 0; i < ARRAY_SIZE(data->reg_fan_val); i++) {
            int status = as7327_56x_cpld_read_internal(client, fan_reg[i]);
            if (status < 0) {
                data->valid = 0;
                mutex_unlock(&data->update_lock);
                dev_dbg(&client->dev, "reg 0x%x, err %d\n", fan_reg[i], status);
                return data;
            }
            else {
                data->reg_fan_val[i] = status & 0xff;
            }
        }

        data->last_updated = jiffies;
        data->valid = 1;
    }

    mutex_unlock(&data->update_lock);

    return data;
}

/* fan utility functions  */
static u32 reg_val_to_duty_cycle(u8 reg_val)
{
	int val;
    val = reg_val;

	return (val + 2) * 100 / 256;
}

static u8 duty_cycle_to_reg_val(u8 duty_cycle)
{
    int val;
    val = duty_cycle;
    if(val < 30)
        val = 30;
    return (val * 255 / 100);
}

static u32 reg_val_to_speed_rpm(u8 reg_val)
{
    return (u32)reg_val * FAN_TECK_SPEED_CNT;
}

static ssize_t set_duty_cycle(struct device *dev, struct device_attribute *da,
							const char *buf, size_t count)
{
    int error, value;
    struct i2c_client *client = to_i2c_client(dev);
    error = kstrtoint(buf, 10, &value);
    if (error)
        return error;
    if (value < 0 || value > FAN_MAX_DUTY_CYCLE)
        return -EINVAL;

	/* Disable FAN watch dog first.*/
    /* as7327_56x_cpld_write_internal(client, FAN_WATCHDOG_EN_REG, 0);
	msleep(2000);
	*/
    as7327_56x_cpld_write_internal(client, fan_reg[1], duty_cycle_to_reg_val(value));
    as7327_56x_cpld_write_internal(client, fan_reg[2], duty_cycle_to_reg_val(value));
    as7327_56x_cpld_write_internal(client, fan_reg[3], duty_cycle_to_reg_val(value));
    as7327_56x_cpld_write_internal(client, fan_reg[4], duty_cycle_to_reg_val(value));

	return count;
}

static ssize_t set_fan_wdt_status(struct device *dev, struct device_attribute *da,
                                  const char *buf, size_t count)
{
    int status, value;
    struct i2c_client *client = to_i2c_client(dev);
    int reg = 0x2e;

    status = kstrtoint(buf, 10, &value);
    if (status) {
        return status;
    }
    if (value < 0 || value > 1) {
        return -EINVAL;
    }

	status = as7327_56x_cpld_read_internal(client, reg);
	if (unlikely(status < 0)) {
		return status;
	}

    if (value) {
        status |= value;
    }
    else {
        status = 0;
    }

    status = as7327_56x_cpld_write_internal(client, reg, status);
	if (unlikely(status < 0)) {
        return status;
    }

    return count;
}

static ssize_t set_fan_wdt_clear(struct device *dev, struct device_attribute *da,
                                 const char *buf, size_t count)
{
    int status, value;
    struct i2c_client *client = to_i2c_client(dev);
    int reg = 0x2e;

    status = kstrtoint(buf, 10, &value);
    if (status) {
        return status;
    }
    if (value != 1) {
        return -EINVAL;
    }

	status = as7327_56x_cpld_read_internal(client, reg);
	if (unlikely(status < 0)) {
		return status;
	}

    status |= (value << 1);

    status = as7327_56x_cpld_write_internal(client, reg, status);
	if (unlikely(status < 0)) {
        return status;
    }

    return count;
}

static ssize_t set_fan_wdt_count(struct device *dev, struct device_attribute *da,
                                 const char *buf, size_t count)
{
    int status, value;
    struct i2c_client *client = to_i2c_client(dev);
    int reg = 0x2f;

    status = kstrtoint(buf, 10, &value);
    if (status) {
        return status;
    }
    if (value < 0 || value > 0x3f) {
        return -EINVAL;
    }

    status = as7327_56x_cpld_write_internal(client, reg, value);
	if (unlikely(status < 0)) {
        return status;
    }

    return count;
}

static u8 reg_val_to_direction(u8 reg_val, enum fan_box_id id)
{
    u8 mask = (1 << (1-id));
    reg_val &= mask;

    return reg_val ? 1 : 0;
}

static u8 reg_val_to_is_present(u8 reg_val, enum fan_box_id id)
{
    u8 mask = (1 << (7-id));
    reg_val &= mask;

    return reg_val ? 0 : 1;
}

static ssize_t fan_show_value(struct device *dev, struct device_attribute *da, char *buf)
{
    u32 duty_cycle;
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct as7327_56x_cpld_data *data = as7327_56x_fan_update_device(dev);
    ssize_t ret = 0;

    if (data->valid) {
        switch (attr->index)
        {
            case FAN_PRESENT_1:
            case FAN_PRESENT_2:
                ret = sprintf(buf, "%d\n", reg_val_to_is_present(data->reg_fan_val[0], attr->index - FAN_PRESENT_1));
                break;
            case FAN_DIRECTION_1:
            case FAN_DIRECTION_2:
                ret = sprintf(buf, "%d\n", reg_val_to_direction(data->reg_fan_val[0], attr->index - FAN_DIRECTION_1));
                break;
            case FAN_DUTY_CYCLE_PERCENTAGE:
                duty_cycle = reg_val_to_duty_cycle(data->reg_fan_val[1]);
                ret = sprintf(buf, "%u\n", duty_cycle);
                break;
            case FAN_FRONT_SPEED_RPM_1:
            case FAN_FRONT_SPEED_RPM_2:
            case FAN_FRONT_SPEED_RPM_3:
            case FAN_FRONT_SPEED_RPM_4:
                ret = sprintf(buf, "%u\n", reg_val_to_speed_rpm(data->reg_fan_val[attr->index - FAN_FRONT_SPEED_RPM_1 + 5]));
                break;
            case FAN_REAR_SPEED_RPM_1:
            case FAN_REAR_SPEED_RPM_2:
            case FAN_REAR_SPEED_RPM_3:
            case FAN_REAR_SPEED_RPM_4:
                ret = sprintf(buf, "%u\n", reg_val_to_speed_rpm(data->reg_fan_val[attr->index - FAN_REAR_SPEED_RPM_1 + 9]));
                break;
            case FAN_WDT_ENABLE:
                ret = sprintf(buf, "%d\n", (data->reg_fan_val[13] & 0x1));
                break;
            case FAN_WDT_CLEAR:
                ret = sprintf(buf, "%d\n", ((data->reg_fan_val[13] & 0x2) >> 1));
                break;
            case FAN_WDT_COUNT:
                ret = sprintf(buf, "%d\n", (data->reg_fan_val[14] & 0x3f));
                break;
            default:
                break;
        }
    }
    return ret;
}

static ssize_t sysled_wdt_show_value(struct device *dev, struct device_attribute *da, char *buf)
{
    return sprintf(buf, "%d\n", 0);
}

static ssize_t set_sysled_wdt_clear(struct device *dev, struct device_attribute *da,
                                 const char *buf, size_t count)
{
    int status, value;
    struct i2c_client *client = to_i2c_client(dev);
    int reg = 0x85;

    status = kstrtoint(buf, 10, &value);
    if (status) {
        return status;
    }
    if (value != 1) {
        return -EINVAL;
    }

	status = as7327_56x_cpld_read_internal(client, reg);
	if (unlikely(status < 0)) {
		return status;
	}

    status |= 0x8;

    status = as7327_56x_cpld_write_internal(client, reg, status);
	if (unlikely(status < 0)) {
        return status;
    }

    return count;
}

/*
 * I2C init/probing/exit functions
 */
static int as7327_56x_cpld_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = to_i2c_adapter(client->dev.parent);
	struct as7327_56x_cpld_data *data;
	int ret = -ENODEV;
	const struct attribute_group *group = NULL;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE))
		goto exit;

	data = kzalloc(sizeof(struct as7327_56x_cpld_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
    mutex_init(&data->update_lock);
	data->type = id->driver_data;

    /* Register sysfs hooks */
    switch (data->type) {
    case as7327_56x_cpld1:
        group = &as7327_56x_cpld1_group;
        break;
    case as7327_56x_cpld2:
        group = &as7327_56x_cpld2_group;
        break;
    default:
        break;
    }

    if (group) {
        ret = sysfs_create_group(&client->dev.kobj, group);
        if (ret) {
            goto exit_free;
        }
    }

    as7327_56x_cpld_add_client(client);
    return 0;

exit_free:
    kfree(data);
exit:
	return ret;
}

static int as7327_56x_cpld_remove(struct i2c_client *client)
{
    struct as7327_56x_cpld_data *data = i2c_get_clientdata(client);
    const struct attribute_group *group = NULL;

    as7327_56x_cpld_remove_client(client);

    /* Remove sysfs hooks */
    switch (data->type) {
    case as7327_56x_cpld1:
        group = &as7327_56x_cpld1_group;
        break;
    case as7327_56x_cpld2:
        group = &as7327_56x_cpld2_group;
        break;
    default:
        break;
    }

    if (group) {
        sysfs_remove_group(&client->dev.kobj, group);
    }

    kfree(data);

    return 0;
}

static int as7327_56x_cpld_read_internal(struct i2c_client *client, u8 reg)
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

static int as7327_56x_cpld_write_internal(struct i2c_client *client, u8 reg, u8 value)
{
	int status = 0, retry = I2C_RW_RETRY_COUNT;

	while (retry) {
		status = i2c_smbus_write_byte_data(client, reg, value);
		if (unlikely(status < 0)) {
			msleep(I2C_RW_RETRY_INTERVAL);
			retry--;
			continue;
		}

		break;
	}

    return status;
}

int as7327_56x_cpld_read(unsigned short cpld_addr, u8 reg)
{
    struct list_head   *list_node = NULL;
    struct cpld_client_node *cpld_node = NULL;
    int ret = -EPERM;

    mutex_lock(&list_lock);

    list_for_each(list_node, &cpld_client_list)
    {
        cpld_node = list_entry(list_node, struct cpld_client_node, list);

        if (cpld_node->client->addr == cpld_addr) {
            ret = as7327_56x_cpld_read_internal(cpld_node->client, reg);
    		break;
        }
    }

	mutex_unlock(&list_lock);

    return ret;
}
EXPORT_SYMBOL(as7327_56x_cpld_read);

int as7327_56x_cpld_write(unsigned short cpld_addr, u8 reg, u8 value)
{
    struct list_head   *list_node = NULL;
    struct cpld_client_node *cpld_node = NULL;
    int ret = -EIO;

	mutex_lock(&list_lock);

    list_for_each(list_node, &cpld_client_list)
    {
        cpld_node = list_entry(list_node, struct cpld_client_node, list);

        if (cpld_node->client->addr == cpld_addr) {
            ret = as7327_56x_cpld_write_internal(cpld_node->client, reg, value);
            break;
        }
    }

	mutex_unlock(&list_lock);

    return ret;
}
EXPORT_SYMBOL(as7327_56x_cpld_write);

static struct i2c_driver as7327_56x_cpld_driver = {
	.driver		= {
		.name	= "as7327_56x_cpld",
		.owner	= THIS_MODULE,
	},
	.probe		= as7327_56x_cpld_probe,
	.remove		= as7327_56x_cpld_remove,
	.id_table	= as7327_56x_cpld_id,
};

static int __init as7327_56x_cpld_init(void)
{
    mutex_init(&list_lock);
    return i2c_add_driver(&as7327_56x_cpld_driver);
}

static void __exit as7327_56x_cpld_exit(void)
{
    i2c_del_driver(&as7327_56x_cpld_driver);
}

MODULE_AUTHOR("Brandon Cheng <brandon_cheng@edge-core.com>");
MODULE_DESCRIPTION("as7327_56x I2C CPLD driver");
MODULE_LICENSE("GPL");

module_init(as7327_56x_cpld_init);
module_exit(as7327_56x_cpld_exit);

