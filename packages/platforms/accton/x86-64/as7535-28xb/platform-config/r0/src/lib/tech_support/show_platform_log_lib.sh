#!/bin/bash

#prefix
common_prefix=/sys/devices/platform/
sys_prefix=/as7535_28xb_sys/

# CPU eeprom
cpu_eeprom_sysfs="${common_prefix}${sys_prefix}eeprom"

# BIOS flash
support_bios_flash=1
bios_boot_flash_sysfs="${common_prefix}${sys_prefix}bios_flash_id"

# PSU sysfs
psu_prefix=/as7535_28xb_psu/
psu1_present_sysfs="${common_prefix}${psu_prefix}psu1_present"
psu2_present_sysfs="${common_prefix}${psu_prefix}psu2_present"
psu1_power_good_sysfs="${common_prefix}${psu_prefix}psu1_power_good"
psu2_power_good_sysfs="${common_prefix}${psu_prefix}psu2_power_good"
bmc_psu1_id=1
bmc_psu2_id=2

# QSFP/SFP
support_sfp=1
support_qsfpdd=1
sfp_eeprom_bus_array=(25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48)
qsfp_eeprom_bus_array=(23 21 24 22)

port_status_cpld_i2c_bus_addr_array=("12-0061")
sfp_port_array=(5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28)
qsfp_port_array=(1 2 3 4)

# CPU temp
cpu_temp_hwmon=$(eval "ls ${common_prefix}coretemp.0/hwmon | grep hwmon")
cpu_temp_bus_id_array=("2" "4" "5" "6" "7" "8" "9")

# System led
led_prefix=/as7535_28xb_led/
sys_led_array=("led_loc" "led_diag" "led_psu1" "led_psu2" "led_fan" "led_alarm")
sys_led_sysfs=("${common_prefix}${led_prefix}led_loc" \
               "${common_prefix}${led_prefix}led_diag" \
               "${common_prefix}${led_prefix}led_psu1" \
               "${common_prefix}${led_prefix}led_psu2" \
               "${common_prefix}${led_prefix}led_fan" \
               "${common_prefix}${led_prefix}led_alarm" )

sys_beacon_led_sysfs=""

# USB
usb_auth_file_array=("/sys/bus/usb/devices/usb1/authorized" \
                     "/sys/bus/usb/devices/usb1/authorized_default" \
                     "/sys/bus/usb/devices/1-0:1.0/authorized" \
                     "/sys/bus/usb/devices/1-2/authorized" \
                     "/sys/bus/usb/devices/1-2:1.0/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized_default" \
                     "/sys/bus/usb/devices/2-0:1.0/authorized")

# Function SFP
cpld_bus_idx=0
port_status_cpld_i2c_bus_addr=${port_status_cpld_i2c_bus_addr_array[$cpld_bus_idx]}

function _sfp_get_rx_los_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_rx_los_${sfp_port_array[$idx]}"
}

function _sfp_get_present_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_present_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_fault_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_tx_fault_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_disable_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_tx_disable_${sfp_port_array[$idx]}"
}

function _sfp_get_eeprom_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${sfp_eeprom_bus_array[$idx]}-0050/eeprom"
}

# Function QSDP-DD
function _qsfpdd_get_present_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_present_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_lp_mode_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_lpmode_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_reset_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_reset_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_eeprom_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${qsfp_eeprom_bus_array[$idx]}-0050/eeprom"
}
