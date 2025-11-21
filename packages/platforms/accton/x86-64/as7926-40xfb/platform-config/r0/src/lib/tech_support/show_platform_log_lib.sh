#!/bin/bash

#prefix
common_prefix=/sys/devices/platform/
sys_prefix=/as7926_40xfb_sys/

# CPU eeprom
cpu_eeprom_sysfs="${common_prefix}${sys_prefix}eeprom"

# BIOS flash
support_bios_flash=1
bios_boot_flash_sysfs="${common_prefix}${sys_prefix}bios_flash_id"

# PSU sysfs
psu_prefix=/as7926_40xfb_psu/
psu1_present_sysfs="${common_prefix}${psu_prefix}psu1_present"
psu2_present_sysfs="${common_prefix}${psu_prefix}psu2_present"
psu1_power_good_sysfs="${common_prefix}${psu_prefix}psu1_power_good"
psu2_power_good_sysfs="${common_prefix}${psu_prefix}psu2_power_good"
bmc_psu1_id=1
bmc_psu2_id=2

# QSFP/SFP
support_sfp=1
support_qsfpdd=1
sfp_eeprom_bus_array=(30 31)
qsfp_eeprom_bus_array=(33 34 37 38 41 42 45 46 49 50 
                       53 54 57 58 61 62 65 66 69 70
                       35 36 39 40 43 44 47 48 51 52
                       55 56 59 60 63 64 67 68 71 72
                       85 76 75 74 73 78 77 80 79 82
                       81 84 83)

port_status_cpld_i2c_bus_addr_array=("12-0062" "13-0063" "20-0064")
sfp_port_array=(54 55)
qsfp_port_array=( 1  2  3  4  5  6  7  8  9 10 
                 11 12 13 14 15 16 17 18 19 20 
                 21 22 23 24 25 26 27 28 29 30
                 31 32 33 34 35 36 37 38 39 40
                 41 42 43 44 45 46 47 48 49 50
                 51 52 53)

# CPU temp
cpu_temp_hwmon=$(eval "ls ${common_prefix}coretemp.0/hwmon | grep hwmon")
cpu_temp_bus_id_array=("1" "2" "3" "4" "5" "6" "7" "8" "9")

# System led
led_prefix=/as7926_40xfb_led/
sys_led_array=("led_loc" "led_diag" "led_psu" "led_fan")
sys_led_sysfs=("${common_prefix}${led_prefix}led_loc" \
               "${common_prefix}${led_prefix}led_diag" \
               "${common_prefix}${led_prefix}led_psu" \
               "${common_prefix}${led_prefix}led_fan" )

sys_beacon_led_sysfs="${common_prefix}${led_prefix}led_loc"

# USB
usb_auth_file_array=("/sys/bus/usb/devices/usb1/authorized" \
                     "/sys/bus/usb/devices/usb1/authorized_default" \
                     "/sys/bus/usb/devices/1-1/authorized" \
                     "/sys/bus/usb/devices/1-0:1.0/authorized" \
                     "/sys/bus/usb/devices/1-1:1.0/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized" \
                     "/sys/bus/usb/devices/usb2/authorized_default" \
                     "/sys/bus/usb/devices/2-1/authorized" \
                     "/sys/bus/usb/devices/2-0:1.0/authorized" \
                     "/sys/bus/usb/devices/2-1:1.0/authorized" \
                     "/sys/bus/usb/devices/2-4/authorized" \
                     "/sys/bus/usb/devices/2-4.1/authorized" \
                     "/sys/bus/usb/devices/2-4:1.0/authorized" \
                     "/sys/bus/usb/devices/2-4.1:1.0/authorized" \
                     "/sys/bus/usb/devices/2-4.1:1.1/authorized" \
                     "/sys/bus/usb/devices/2-4.1:1.2/authorized" \
                     "/sys/bus/usb/devices/2-4.1:1.3/authorized" \
                     "/sys/bus/usb/devices/usb3/authorized" \
                     "/sys/bus/usb/devices/usb3/authorized_default" \
                     "/sys/bus/usb/devices/3-0:1.0/authorized")

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
    #cpld has no tx_fault register
    echo ""
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
function _qsfpdd_select_cpld_i2c_bus_addr_idx {
    port_idx=$(($1 + 1))

    if ((( port_idx >= 1 )) && (( port_idx <= 10 ))) || \
       ((( port_idx >= 21)) && (( port_idx <= 30 ))); then
        echo 0
    elif ((( port_idx >= 11 )) && (( port_idx <= 20 ))) || \
         ((( port_idx >= 31 )) && (( port_idx <= 40 ))); then
        echo 1
    elif (( port_idx >= 41 )) && (( port_idx <= 53 )); then
        echo 2
    fi
}

function _qsfpdd_get_present_func {
    idx=$1
    cpld_bus_idx=$(_qsfpdd_select_cpld_i2c_bus_addr_idx "$idx")
    port_status_cpld_i2c_bus_addr=${port_status_cpld_i2c_bus_addr_array[$cpld_bus_idx]}

    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_present_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_lp_mode_func {
    #cpld has no lp_mode register 
    echo ""
}

function _qsfpdd_get_reset_func {
    idx=$1
    cpld_bus_idx=$(_qsfpdd_select_cpld_i2c_bus_addr_idx "$idx")
    port_status_cpld_i2c_bus_addr=${port_status_cpld_i2c_bus_addr_array[$cpld_bus_idx]}

    echo "/sys/bus/i2c/devices/${port_status_cpld_i2c_bus_addr}/module_reset_${qsfp_port_array[$idx]}"
}

function _qsfpdd_get_eeprom_func {
    idx=$1
    echo "/sys/bus/i2c/devices/${qsfp_eeprom_bus_array[$idx]}-0050/eeprom"
}
