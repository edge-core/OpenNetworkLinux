#!/bin/bash

function _get_i2c_bus_num {
    local name_file="/sys/bus/i2c/devices/i2c-0/name"
    if [ -f "$name_file" ]; then
        if grep -q "iSMT" "$name_file"; then
            echo "1"
        else
            echo "0"
        fi
    else
        echo "1"
    fi
}

cpu_bus_id_offset=$(_get_i2c_bus_num)

# CPU eeprom
cpu_eeprom_bus_id="68"
cpu_eeprom_i2c_addr="56"

cpu_eeprom_sysfs="/sys/bus/i2c/devices/${cpu_eeprom_bus_id}-00${cpu_eeprom_i2c_addr}/eeprom"

# BIOS flash
support_bios_flash=1
ipmi_fpga_device_dir="as9817_64_fpga"
bios_boot_flash_sysfs="/sys/devices/platform/${ipmi_fpga_device_dir}/bios_flash_id"

# PSU sysfs
ipmi_psu_device_dir="as9817_64_psu"
psu1_hwmon=$(eval "ls /sys/devices/platform/${ipmi_psu_device_dir}.0/hwmon | grep hwmon")
psu2_hwmon=$(eval "ls /sys/devices/platform/${ipmi_psu_device_dir}.1/hwmon | grep hwmon")
psu1_present_sysfs="/sys/devices/platform/${ipmi_psu_device_dir}.0/hwmon/${psu1_hwmon}/psu1_present"
psu2_present_sysfs="/sys/devices/platform/${ipmi_psu_device_dir}.1/hwmon/${psu2_hwmon}/psu2_present"
psu1_power_good_sysfs="/sys/devices/platform/${ipmi_psu_device_dir}.0/hwmon/${psu1_hwmon}/psu1_power_good"
psu2_power_good_sysfs="/sys/devices/platform/${ipmi_psu_device_dir}.1/hwmon/${psu2_hwmon}/psu2_power_good"
bmc_psu1_id=1
bmc_psu2_id=2

# QSFP/SFP
ipmi_sfp_device_dir="as9817_64_fpga"
support_sfp=1
support_qsfpdd=1

sfp_eeprom_bus_array=(66 67)
qsfp_eeprom_bus_array=(2  3  4  5  6  7  8  9  10 11 \
                       12 13 14 15 16 17 18 19 20 21 \
                       22 23 24 25 26 27 28 29 30 31 \
                       32 33 34 35 36 37 38 39 40 41 \
                       42 43 44 45 46 47 48 49 50 51 \
                       52 53 54 55 56 57 58 59 60 61 \
                       62 63 64 65)

sfp_port_array=(65 66)
qsfp_port_array=(1  2  3  4  5  6  7  8  9  10 \
                 11 12 13 14 15 16 17 18 19 20 \
                 21 22 23 24 25 26 27 28 29 30 \
                 31 32 33 34 35 36 37 38 39 40 \
                 41 42 43 44 45 46 47 48 49 50 \
                 51 52 53 54 55 56 57 58 59 60 \
                 61 62 63 64)

# CPU temp
cpu_temp_hwmon=$(eval "ls /sys/devices/platform/coretemp.0/hwmon | grep hwmon")
cpu_temp_bus_id_array=("1" "2" "3" "4" "5")

# System led
ipmi_psu_device_dir="as9817_64_led"
sys_led_array=("diag" "loc" "alarm" "fan" "psu1" "psu2")
sys_led_sysfs=("/sys/devices/platform/${ipmi_psu_device_dir}/led_loc" \
               "/sys/devices/platform/${ipmi_psu_device_dir}/led_diag" \
               "/sys/devices/platform/${ipmi_psu_device_dir}/led_alarm" \
               "/sys/devices/platform/${ipmi_psu_device_dir}/led_fan" \
               "/sys/devices/platform/${ipmi_psu_device_dir}/led_psu1" \
               "/sys/devices/platform/${ipmi_psu_device_dir}/led_psu2")

sys_beacon_led_sysfs="/sys/devices/platform/${ipmi_psu_device_dir}/led_loc"

# USB
usb_auth_file_array=("/sys/bus/usb/devices/usb1/authorized" \
                     "/sys/bus/usb/devices/usb1/authorized_default" \
                     "/sys/bus/usb/devices/1-0:1.0/authorized" \
                     "/sys/bus/usb/devices/1-1/authorized" \
                     "/sys/bus/usb/devices/1-1:1.0/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized_default" \
                     "/sys/bus/usb/devices/2-1/authorized" \
                     "/sys/bus/usb/devices/2-0:1.0/authorized" \
                     "/sys/bus/usb/devices/2-1:1.0/authorized" \
                     "/sys/bus/usb/devices/usb3/authorized" \
                     "/sys/bus/usb/devices/usb3/authorized_default" \
                     "/sys/bus/usb/devices/3-0:1.0/authorized")

# Function SFP
function _sfp_get_rx_los_func {
    idx=$1
    echo "/sys/devices/platform/${ipmi_sfp_device_dir}/module_rx_los_${sfp_port_array[$idx]}"
}

function _sfp_get_present_func {
    idx=$1
    echo "/sys/devices/platform/${ipmi_sfp_device_dir}/module_present_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_fault_func {
    idx=$1
    echo "/sys/devices/platform/${ipmi_sfp_device_dir}/module_tx_fault_${sfp_port_array[$idx]}"
}

function _sfp_get_tx_disable_func {
    idx=$1
    echo "/sys/devices/platform/${ipmi_sfp_device_dir}/module_tx_disable_${sfp_port_array[$idx]}"
}

function _sfp_get_eeprom_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${sfp_eeprom_bus_array[$idx]}-0050/eeprom"
}

# Function QSDP-DD
function _qsfpdd_get_present_func {
    idx=$1
    echo "/sys/devices/platform/${ipmi_sfp_device_dir}/module_present_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_lp_mode_func {
    idx=$1
    echo "/sys/devices/platform/${ipmi_sfp_device_dir}/module_lpmode_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_reset_func {
    idx=$1
    echo "/sys/devices/platform/${ipmi_sfp_device_dir}/module_reset_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_eeprom_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${qsfp_eeprom_bus_array[$idx]}-0050/eeprom"
}
